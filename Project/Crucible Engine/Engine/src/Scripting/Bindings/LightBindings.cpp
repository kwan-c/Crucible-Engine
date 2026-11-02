/*************************************************************************
* \file:         LightBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Internal calls behind the C# Light class
*                (ManagedScripts/Runtime/Light.cs).
*
* \details
*    Bindings only: the Light component (Components.h) is unchanged.
*    RenderExtractSystem reads every Light each frame, so a change made in
*    OnUpdate is drawn that same frame. Position and direction come from the
*    entity's Transform (it shines along local -Z).
*
*    Colour crosses as a ScriptVec3 (r, g, b); the C# Light converts it to
*    and from Crucible.Color. Bools cross as int.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "Graphics/LightTypes.h"
#if CRUCIBLE_WITH_MONO
#include <cmath>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		// C# LightType (Light.cs) passes these numbers.
		static_assert(static_cast<int>(LightType::Directional) == 0 && static_cast<int>(LightType::Point) == 1 &&
		                  static_cast<int>(LightType::Spot) == 2,
		              "LightTypes.h changed - update the LightType enum in ManagedScripts/Runtime/Light.cs");

		const Light* ReadLight(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Read<Light>(entity);
		}

		Light* WriteLight(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Write<Light>(entity);
		}

		// ======================================================================
		// Floats: intensity, range, cone angles
		// ======================================================================

		template <float Light::*Member>
		int32_t GetFloat(int32_t index, int32_t generation, float* out) noexcept
		{
			try
			{
				const Light* light = out ? ReadLight(index, generation) : nullptr;
				if (!light)
					return 0;
				*out = light->*Member;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  Setter for a float that must be finite and not negative.
		 *********************************************************************/
		template <float Light::*Member>
		int32_t SetFloat(int32_t index, int32_t generation, const float* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(*value) || *value < 0.0f)
					return 0;
				Light* light = WriteLight(index, generation);
				if (!light)
					return 0;
				light->*Member = *value;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Bools: enabled, castShadows
		// ======================================================================

		template <bool Light::*Member>
		int32_t GetFlag(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const Light* light = out ? ReadLight(index, generation) : nullptr;
				if (!light)
					return 0;
				*out = (light->*Member) ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <bool Light::*Member>
		int32_t SetFlag(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				Light* light = WriteLight(index, generation);
				if (!light)
					return 0;
				light->*Member = value != 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Colour and type
		// ======================================================================

		int32_t ManagedLightGetColor(int32_t index, int32_t generation, ScriptVec3* out) noexcept
		{
			try
			{
				const Light* light = out ? ReadLight(index, generation) : nullptr;
				if (!light)
					return 0;
				*out = {light->colour.r, light->colour.g, light->colour.b};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Light.Color (set). Channels must be finite and not
		         negative; above 1 is allowed (brighter than white).
		 *********************************************************************/
		int32_t ManagedLightSetColor(int32_t index, int32_t generation, const ScriptVec3* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(value->x) || !std::isfinite(value->y) || !std::isfinite(value->z) ||
				    value->x < 0.0f || value->y < 0.0f || value->z < 0.0f)
					return 0;
				Light* light = WriteLight(index, generation);
				if (!light)
					return 0;
				light->colour = {value->x, value->y, value->z};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedLightGetType(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const Light* light = out ? ReadLight(index, generation) : nullptr;
				if (!light)
					return 0;
				*out = static_cast<int32_t>(light->type);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedLightSetType(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				if (value < 0 || value > static_cast<int32_t>(LightType::Spot))
					return 0;
				Light* light = WriteLight(index, generation);
				if (!light)
					return 0;
				light->type = static_cast<LightType>(value);
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

	void RegisterLightBindings()
	{
		Add("Crucible.Light::GetTypeInternal", &ManagedLightGetType);
		Add("Crucible.Light::SetTypeInternal", &ManagedLightSetType);
		Add("Crucible.Light::GetColorInternal", &ManagedLightGetColor);
		Add("Crucible.Light::SetColorInternal", &ManagedLightSetColor);
		Add("Crucible.Light::GetIntensityInternal", &GetFloat<&Light::intensity>);
		Add("Crucible.Light::SetIntensityInternal", &SetFloat<&Light::intensity>);
		Add("Crucible.Light::GetRangeInternal", &GetFloat<&Light::range>);
		Add("Crucible.Light::SetRangeInternal", &SetFloat<&Light::range>);
		Add("Crucible.Light::GetSpotAngleInternal", &GetFloat<&Light::outerConeDeg>);
		Add("Crucible.Light::SetSpotAngleInternal", &SetFloat<&Light::outerConeDeg>);
		Add("Crucible.Light::GetInnerSpotAngleInternal", &GetFloat<&Light::innerConeDeg>);
		Add("Crucible.Light::SetInnerSpotAngleInternal", &SetFloat<&Light::innerConeDeg>);
		Add("Crucible.Light::GetEnabledInternal", &GetFlag<&Light::enabled>);
		Add("Crucible.Light::SetEnabledInternal", &SetFlag<&Light::enabled>);
		Add("Crucible.Light::GetCastShadowsInternal", &GetFlag<&Light::castShadows>);
		Add("Crucible.Light::SetCastShadowsInternal", &SetFlag<&Light::castShadows>);
	}
}
#endif
