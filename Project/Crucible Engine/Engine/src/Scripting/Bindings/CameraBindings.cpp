/*************************************************************************
* \file:         CameraBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Internal calls behind the C# Camera and CameraOrbit classes
*                (ManagedScripts/Runtime/Camera.cs, CameraOrbit.cs).
*
* \details
*    Bindings only: CameraComponent (Components.h), CameraOrbit
*    (Player/PlayerComponents.h) and the Renderer are unchanged.
*
*    Camera.Main picks a camera the way RenderExtractSystem does: the
*    enabled CameraComponent with the highest priority, the first found on
*    a tie.
*
*    Screen maths uses the camera's own world matrix (World::TryGetWorldMatrix,
*    so it is current this frame) and field of view, plus the size of the
*    game screen (Renderer::GetGameScreenSize): the Game view's image in the
*    editor, the window in the game. Screen pixels have their origin at the
*    top-left, the same space as the UI. ScreenPointToRay takes WINDOW pixels
*    (Input.MousePosition) and maps them with Renderer::WindowToGameScreen.
*    The camera looks down its local -Z with +Y up, as the renderer draws it.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "Graphics/RenderSystem.h"
#include "Player/PlayerComponents.h"
#if CRUCIBLE_WITH_MONO
#include <algorithm>
#include <cmath>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		// The renderer (SetRenderer, from Core) - only for the game screen's size
		// and the window -> game screen mapping. Main thread only.
		thread_local const Renderer* renderer = nullptr;

		World& TheWorld()
		{
			return *EntityContext::GetWorld(); // only after ResolveEntity succeeded
		}

		const CameraComponent* ReadCamera(int32_t index, int32_t generation, Entity& entity)
		{
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return TheWorld().Read<CameraComponent>(entity);
		}

		CameraComponent* WriteCamera(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return TheWorld().Write<CameraComponent>(entity);
		}

		/*********************************************************************
		 \brief  What the screen maths needs about one camera: its world matrix,
		         vertical field of view (radians, clamped like the renderer),
		         clip planes and the game screen's size.
		 *********************************************************************/
		struct CameraView
		{
			Math::mat4 world;
			float fovY = 0.0f;
			float nearZ = 0.1f, farZ = 1000.0f;
			float width = 0.0f, height = 0.0f;
		};

		bool GetView(int32_t index, int32_t generation, CameraView& view)
		{
			Entity entity;
			const CameraComponent* camera = ReadCamera(index, generation, entity);
			if (!camera || !renderer || !TheWorld().TryGetWorldMatrix(entity, view.world))
				return false;
			const glm::uvec2 size = renderer->GetGameScreenSize();
			if (size.x == 0 || size.y == 0)
				return false; // no game screen (Game view closed)
			view.width = static_cast<float>(size.x);
			view.height = static_cast<float>(size.y);
			view.fovY = glm::radians(std::clamp(camera->fovY, 1.0f, 179.0f));
			view.nearZ = std::max(camera->nearZ, 0.001f);
			view.farZ = std::max(camera->farZ, view.nearZ + 0.01f);
			return true;
		}

		/*********************************************************************
		 \brief  A ray from the camera through a point given as 0..1 across and
		         down the game screen (0, 0 = top-left).
		 *********************************************************************/
		void ViewportRay(const CameraView& view, float u, float v, ScriptVec3& origin, ScriptVec3& direction)
		{
			const float tanHalf = std::tan(view.fovY * 0.5f);
			const float aspect = view.width / view.height;
			const float x = (u * 2.0f - 1.0f) * tanHalf * aspect;
			const float y = (1.0f - v * 2.0f) * tanHalf;
			const Math::vec3 local(x, y, -1.0f); // camera space: -Z ahead, +Y up
			const Math::vec3 world = glm::normalize(Math::vec3(view.world * Math::vec4(local, 0.0f)));
			const Math::vec3 position(view.world[3]);
			origin = {position.x, position.y, position.z};
			direction = {world.x, world.y, world.z};
		}

		// ======================================================================
		// Camera.Main and settings
		// ======================================================================

		/*********************************************************************
		 \brief  C# Camera.Main: the camera the game is seen through.
		 \return 1 with its entity, 0 if there is none (or not Play).
		 *********************************************************************/
		int32_t ManagedCameraGetMain(int32_t* outIndex, int32_t* outGeneration) noexcept
		{
			try
			{
				World* world = EntityContext::GetWorld();
				if (!outIndex || !outGeneration || !world || !EntityContext::InPlaySession())
					return 0;
				bool found = false;
				int bestPriority = 0;
				Entity best{};
				world->BuildQuery<const CameraComponent>().Build().ForEach(
				    [&](Entity entity, const CameraComponent& camera)
				    {
					    if (!camera.enabled || (found && camera.priority <= bestPriority))
						    return;
					    found = true;
					    bestPriority = camera.priority;
					    best = entity;
				    });
				if (!found)
					return 0;
				*outIndex = static_cast<int32_t>(best.index);
				*outGeneration = static_cast<int32_t>(best.generation);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <float CameraComponent::*Member>
		int32_t GetCameraFloat(int32_t index, int32_t generation, float* out) noexcept
		{
			try
			{
				Entity entity;
				const CameraComponent* camera = out ? ReadCamera(index, generation, entity) : nullptr;
				if (!camera)
					return 0;
				*out = camera->*Member;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  Setter for FieldOfView / NearClip / FarClip. Rejects NaN and
		         values at or below zero; the renderer clamps the rest.
		 *********************************************************************/
		template <float CameraComponent::*Member>
		int32_t SetCameraFloat(int32_t index, int32_t generation, const float* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(*value) || !(*value > 0.0f))
					return 0;
				CameraComponent* camera = WriteCamera(index, generation);
				if (!camera)
					return 0;
				camera->*Member = *value;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedCameraGetPriority(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				Entity entity;
				const CameraComponent* camera = out ? ReadCamera(index, generation, entity) : nullptr;
				if (!camera)
					return 0;
				*out = camera->priority;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedCameraSetPriority(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				CameraComponent* camera = WriteCamera(index, generation);
				if (!camera)
					return 0;
				camera->priority = value;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedCameraGetEnabled(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				Entity entity;
				const CameraComponent* camera = out ? ReadCamera(index, generation, entity) : nullptr;
				if (!camera)
					return 0;
				*out = camera->enabled ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedCameraSetEnabled(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				CameraComponent* camera = WriteCamera(index, generation);
				if (!camera)
					return 0;
				camera->enabled = value != 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Screen maths
		// ======================================================================

		/*********************************************************************
		 \brief  C# Camera.ScreenSize: the game screen in pixels.
		 *********************************************************************/
		int32_t ManagedCameraGetScreenSize(ScriptVec2* out) noexcept
		{
			try
			{
				if (!out || !renderer)
					return 0;
				const glm::uvec2 size = renderer->GetGameScreenSize();
				*out = {static_cast<float>(size.x), static_cast<float>(size.y)};
				return size.x != 0 && size.y != 0 ? 1 : 0;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Camera.ViewportPointToRay: (0..1, 0..1) from the top-left;
		         (0.5, 0.5) is the centre - the aim point when the cursor is
		         locked.
		 *********************************************************************/
		int32_t ManagedCameraViewportPointToRay(int32_t index, int32_t generation, const ScriptVec2* point,
		                                        ScriptVec3* origin, ScriptVec3* direction) noexcept
		{
			try
			{
				CameraView view;
				if (!point || !origin || !direction || !std::isfinite(point->x) || !std::isfinite(point->y) ||
				    !GetView(index, generation, view))
					return 0;
				ViewportRay(view, point->x, point->y, *origin, *direction);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Camera.ScreenPointToRay: a WINDOW position (Input.MousePosition)
		         -> a ray. 0 when the point is not on the game screen (in the
		         editor: the mouse is not over the Game view).
		 *********************************************************************/
		int32_t ManagedCameraScreenPointToRay(int32_t index, int32_t generation, const ScriptVec2* windowPoint,
		                                      ScriptVec3* origin, ScriptVec3* direction) noexcept
		{
			try
			{
				CameraView view;
				glm::vec2 pixels;
				if (!windowPoint || !origin || !direction || !GetView(index, generation, view) ||
				    !renderer->WindowToGameScreen(glm::vec2(windowPoint->x, windowPoint->y), pixels))
					return 0;
				ViewportRay(view, pixels.x / view.width, pixels.y / view.height, *origin, *direction);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Camera.WorldToScreenPoint: where a world position appears on
		         the game screen through this camera, in game-screen pixels
		         (top-left origin, the UI's space), plus its distance in front
		         of the camera.
		 \return 0 if it is behind the camera (or no game screen).
		 *********************************************************************/
		int32_t ManagedCameraWorldToScreenPoint(int32_t index, int32_t generation, const ScriptVec3* worldPoint,
		                                        ScriptVec2* outPixels, float* outDepth) noexcept
		{
			try
			{
				CameraView view;
				if (!worldPoint || !outPixels || !outDepth || !GetView(index, generation, view))
					return 0;
				const Math::vec4 local =
				    glm::inverse(view.world) * Math::vec4(worldPoint->x, worldPoint->y, worldPoint->z, 1.0f);
				const float depth = -local.z; // metres in front of the camera
				if (!(depth > 1e-4f))
					return 0;
				const float tanHalf = std::tan(view.fovY * 0.5f);
				const float aspect = view.width / view.height;
				const float ndcX = local.x / (depth * tanHalf * aspect);
				const float ndcY = local.y / (depth * tanHalf);
				*outPixels = {(ndcX + 1.0f) * 0.5f * view.width, (1.0f - ndcY) * 0.5f * view.height};
				*outDepth = depth;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// CameraOrbit (the player's third-person camera pivot)
		// ======================================================================

		template <float CameraOrbit::*Member>
		int32_t GetOrbitFloat(int32_t index, int32_t generation, float* out) noexcept
		{
			try
			{
				Entity entity;
				if (!out || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				const CameraOrbit* orbit = TheWorld().Read<CameraOrbit>(entity);
				if (!orbit)
					return 0;
				*out = orbit->*Member;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <float CameraOrbit::*Member>
		int32_t SetOrbitFloat(int32_t index, int32_t generation, const float* value) noexcept
		{
			try
			{
				Entity entity;
				if (!value || !std::isfinite(*value) || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				CameraOrbit* orbit = TheWorld().Write<CameraOrbit>(entity);
				if (!orbit)
					return 0;
				orbit->*Member = *value;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <typename Fn>
		void Add(const char* name, Fn* function)
		{
			mono_add_internal_call(name, reinterpret_cast<const void*>(function));
		}
	}

	void SetRenderer(const Renderer* newRenderer)
	{
		renderer = newRenderer;
	}

	const Renderer* CurrentRenderer()
	{
		return renderer;
	}

	void RegisterCameraBindings()
	{
		Add("Crucible.Camera::GetMainInternal", &ManagedCameraGetMain);
		Add("Crucible.Camera::GetFieldOfViewInternal", &GetCameraFloat<&CameraComponent::fovY>);
		Add("Crucible.Camera::SetFieldOfViewInternal", &SetCameraFloat<&CameraComponent::fovY>);
		Add("Crucible.Camera::GetNearClipInternal", &GetCameraFloat<&CameraComponent::nearZ>);
		Add("Crucible.Camera::SetNearClipInternal", &SetCameraFloat<&CameraComponent::nearZ>);
		Add("Crucible.Camera::GetFarClipInternal", &GetCameraFloat<&CameraComponent::farZ>);
		Add("Crucible.Camera::SetFarClipInternal", &SetCameraFloat<&CameraComponent::farZ>);
		Add("Crucible.Camera::GetPriorityInternal", &ManagedCameraGetPriority);
		Add("Crucible.Camera::SetPriorityInternal", &ManagedCameraSetPriority);
		Add("Crucible.Camera::GetEnabledInternal", &ManagedCameraGetEnabled);
		Add("Crucible.Camera::SetEnabledInternal", &ManagedCameraSetEnabled);
		Add("Crucible.Camera::GetScreenSizeInternal", &ManagedCameraGetScreenSize);
		Add("Crucible.Camera::ViewportPointToRayInternal", &ManagedCameraViewportPointToRay);
		Add("Crucible.Camera::ScreenPointToRayInternal", &ManagedCameraScreenPointToRay);
		Add("Crucible.Camera::WorldToScreenPointInternal", &ManagedCameraWorldToScreenPoint);

		Add("Crucible.CameraOrbit::GetSensitivityInternal", &GetOrbitFloat<&CameraOrbit::m_sensitivity>);
		Add("Crucible.CameraOrbit::SetSensitivityInternal", &SetOrbitFloat<&CameraOrbit::m_sensitivity>);
		Add("Crucible.CameraOrbit::GetStickSpeedInternal", &GetOrbitFloat<&CameraOrbit::m_stickSpeed>);
		Add("Crucible.CameraOrbit::SetStickSpeedInternal", &SetOrbitFloat<&CameraOrbit::m_stickSpeed>);
		Add("Crucible.CameraOrbit::GetMinPitchInternal", &GetOrbitFloat<&CameraOrbit::m_minPitch>);
		Add("Crucible.CameraOrbit::SetMinPitchInternal", &SetOrbitFloat<&CameraOrbit::m_minPitch>);
		Add("Crucible.CameraOrbit::GetMaxPitchInternal", &GetOrbitFloat<&CameraOrbit::m_maxPitch>);
		Add("Crucible.CameraOrbit::SetMaxPitchInternal", &SetOrbitFloat<&CameraOrbit::m_maxPitch>);
		Add("Crucible.CameraOrbit::GetYawInternal", &GetOrbitFloat<&CameraOrbit::m_yaw>);
		Add("Crucible.CameraOrbit::SetYawInternal", &SetOrbitFloat<&CameraOrbit::m_yaw>);
		Add("Crucible.CameraOrbit::GetPitchInternal", &GetOrbitFloat<&CameraOrbit::m_pitch>);
		Add("Crucible.CameraOrbit::SetPitchInternal", &SetOrbitFloat<&CameraOrbit::m_pitch>);
	}
}
#endif
