/*************************************************************************
* \file:         PlayerControllerBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Internal calls behind the C# PlayerController class
*                (ManagedScripts/Runtime/PlayerController.cs).
*
* \details
*    Reads and writes PlayerController (Player/PlayerComponents.h, Hugo's):
*    move speed, jump height and face-camera. (The keys are the player's
*    settings: InputSettingsBindings.cpp.)
*    ECSPlayerControllerSystem reads the component every frame, so a change
*    applies from that frame's movement (scripts run before it).
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "Player/PlayerComponents.h"
#if CRUCIBLE_WITH_MONO
#include <cmath>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		const PlayerController* ReadPlayer(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Read<PlayerController>(entity);
		}

		PlayerController* WritePlayer(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Write<PlayerController>(entity);
		}

		// ======================================================================
		// Floats: move speed, jump height (finite, not negative; 0 jump = no jumping)
		// ======================================================================

		template <float PlayerController::*Member>
		int32_t GetFloat(int32_t index, int32_t generation, float* out) noexcept
		{
			try
			{
				const PlayerController* player = out ? ReadPlayer(index, generation) : nullptr;
				if (!player)
					return 0;
				*out = player->*Member;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <float PlayerController::*Member>
		int32_t SetFloat(int32_t index, int32_t generation, const float* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(*value) || *value < 0.0f)
					return 0;
				PlayerController* player = WritePlayer(index, generation);
				if (!player)
					return 0;
				player->*Member = *value;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Face camera
		// ======================================================================

		int32_t ManagedPlayerGetFaceCamera(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const PlayerController* player = out ? ReadPlayer(index, generation) : nullptr;
				if (!player)
					return 0;
				*out = player->m_faceCamera ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedPlayerSetFaceCamera(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				PlayerController* player = WritePlayer(index, generation);
				if (!player)
					return 0;
				player->m_faceCamera = value != 0;
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

	void RegisterPlayerControllerBindings()
	{
		Add("Crucible.PlayerController::GetMoveSpeedInternal", &GetFloat<&PlayerController::m_moveSpeed>);
		Add("Crucible.PlayerController::SetMoveSpeedInternal", &SetFloat<&PlayerController::m_moveSpeed>);
		Add("Crucible.PlayerController::GetJumpHeightInternal", &GetFloat<&PlayerController::m_jumpHeight>);
		Add("Crucible.PlayerController::SetJumpHeightInternal", &SetFloat<&PlayerController::m_jumpHeight>);
		Add("Crucible.PlayerController::GetFaceCameraInternal", &ManagedPlayerGetFaceCamera);
		Add("Crucible.PlayerController::SetFaceCameraInternal", &ManagedPlayerSetFaceCamera);

	}
}
#endif
