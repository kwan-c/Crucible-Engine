/*************************************************************************
* \file:         ParticleBindings.cpp
* \author:       <author>
* \par:          Email: <email>
* \date          2 November 2026
* \version       1.0
* \brief         Internal calls behind the C# ParticleEmitter class
*                (ManagedScripts/Runtime/ParticleEmitter.cs).
*
* \details
*    Settings go through the ParticleEmitter component (Components.h).
*    ParticleSystem (Graphics/ParticleSystem.h) reads every emitter each
*    frame in PreRender, after scripts, so changes apply the same frame.
*
*    Play    = playing on. A stopped emitter starts a fresh run (with its burst).
*    Stop    = playing off: no new particles; live ones finish their lives.
*    Restart = playing on + ParticleSystem::Restart: a fresh run this frame,
*              even if it was already playing.
*    Emit    = ParticleSystem::Emit: extra particles this frame, playing or not.
*    ParticleCount = ParticleSystem::GetParticleCount (as of the last frame).
*
*    Colours cross as ScriptColor (r, g, b, a = Crucible.Color).
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "Graphics/ParticleSystem.h"
#if CRUCIBLE_WITH_MONO
#include <algorithm>
#include <cmath>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		// The particle system (SetParticleSystem, from Core). Main thread only.
		thread_local ParticleSystem* particles = nullptr;

		const ParticleEmitter* ReadEmitter(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Read<ParticleEmitter>(entity);
		}

		ParticleEmitter* WriteEmitter(int32_t index, int32_t generation, Entity* resolved = nullptr)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			if (resolved)
				*resolved = entity;
			return EntityContext::GetWorld()->Write<ParticleEmitter>(entity);
		}

		bool Finite(const ScriptColor& c)
		{
			return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a);
		}

		// ======================================================================
		// Play / Stop / Restart
		// ======================================================================

		/*********************************************************************
		 \brief  C# Play (0), Stop (1), Restart (2).
		 *********************************************************************/
		int32_t ManagedParticleControl(int32_t index, int32_t generation, int32_t action) noexcept
		{
			try
			{
				Entity entity;
				ParticleEmitter* emitter = WriteEmitter(index, generation, &entity);
				if (!emitter)
					return 0;
				switch (action)
				{
				case 0:
					emitter->playing = true;
					return 1;
				case 1:
					emitter->playing = false;
					return 1;
				case 2:
					emitter->playing = true;
					if (particles)
						particles->Restart(entity); // a fresh run this frame, even if already playing
					return 1;
				default:
					return 0;
				}
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# ParticleEmitter.Emit(count): `count` extra particles this
		         frame, whether or not it is playing (maxParticles still caps).
		 *********************************************************************/
		int32_t ManagedParticleEmit(int32_t index, int32_t generation, int32_t count) noexcept
		{
			try
			{
				Entity entity;
				if (!particles || count <= 0 || !EntityContext::ResolveEntity(index, generation, entity) ||
				    !EntityContext::GetWorld()->HasComponent<ParticleEmitter>(entity))
					return 0;
				particles->Emit(entity, count);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# ParticleEmitter.ParticleCount: live particles as of the
		         last frame.
		 *********************************************************************/
		int32_t ManagedParticleGetCount(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				Entity entity;
				if (!out || !particles || !EntityContext::ResolveEntity(index, generation, entity))
					return 0;
				*out = static_cast<int32_t>(particles->GetParticleCount(entity));
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Generic field access
		// ======================================================================

		/*********************************************************************
		 \brief  Getter / setter for a float field. AllowNegative = false
		         rejects values below zero (most fields); NaN is always rejected.
		 *********************************************************************/
		template <float ParticleEmitter::*Member>
		int32_t GetFloat(int32_t index, int32_t generation, float* out) noexcept
		{
			try
			{
				const ParticleEmitter* emitter = out ? ReadEmitter(index, generation) : nullptr;
				if (!emitter)
					return 0;
				*out = emitter->*Member;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <float ParticleEmitter::*Member, bool AllowNegative>
		int32_t SetFloat(int32_t index, int32_t generation, const float* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(*value) || (!AllowNegative && *value < 0.0f))
					return 0;
				ParticleEmitter* emitter = WriteEmitter(index, generation);
				if (!emitter)
					return 0;
				emitter->*Member = *value;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <int ParticleEmitter::*Member>
		int32_t GetInt(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const ParticleEmitter* emitter = out ? ReadEmitter(index, generation) : nullptr;
				if (!emitter)
					return 0;
				*out = emitter->*Member;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <int ParticleEmitter::*Member>
		int32_t SetInt(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				if (value < 0)
					return 0;
				ParticleEmitter* emitter = WriteEmitter(index, generation);
				if (!emitter)
					return 0;
				emitter->*Member = value; // ParticleSystem caps maxParticles itself
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <bool ParticleEmitter::*Member>
		int32_t GetFlag(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const ParticleEmitter* emitter = out ? ReadEmitter(index, generation) : nullptr;
				if (!emitter)
					return 0;
				*out = (emitter->*Member) ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <bool ParticleEmitter::*Member>
		int32_t SetFlag(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				ParticleEmitter* emitter = WriteEmitter(index, generation);
				if (!emitter)
					return 0;
				emitter->*Member = value != 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// Colours: start / end (with alpha) and the emitted light's colour
		// ======================================================================

		/*********************************************************************
		 \brief  Start (which = 0) or end (1) colour + alpha.
		 *********************************************************************/
		int32_t ManagedParticleGetColor(int32_t index, int32_t generation, int32_t which, ScriptColor* out) noexcept
		{
			try
			{
				const ParticleEmitter* emitter = out ? ReadEmitter(index, generation) : nullptr;
				if (!emitter)
					return 0;
				const Math::vec3& rgb = which == 0 ? emitter->colourStart : emitter->colourEnd;
				*out = {rgb.r, rgb.g, rgb.b, which == 0 ? emitter->alphaStart : emitter->alphaEnd};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedParticleSetColor(int32_t index, int32_t generation, int32_t which,
		                                const ScriptColor* value) noexcept
		{
			try
			{
				if (!value || !Finite(*value))
					return 0;
				ParticleEmitter* emitter = WriteEmitter(index, generation);
				if (!emitter)
					return 0;
				const Math::vec3 rgb(std::max(value->r, 0.0f), std::max(value->g, 0.0f), std::max(value->b, 0.0f));
				const float alpha = std::clamp(value->a, 0.0f, 1.0f);
				if (which == 0)
				{
					emitter->colourStart = rgb;
					emitter->alphaStart = alpha;
				}
				else
				{
					emitter->colourEnd = rgb;
					emitter->alphaEnd = alpha;
				}
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedParticleGetLightColor(int32_t index, int32_t generation, ScriptColor* out) noexcept
		{
			try
			{
				const ParticleEmitter* emitter = out ? ReadEmitter(index, generation) : nullptr;
				if (!emitter)
					return 0;
				*out = {emitter->lightColour.r, emitter->lightColour.g, emitter->lightColour.b, 1.0f};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedParticleSetLightColor(int32_t index, int32_t generation, const ScriptColor* value) noexcept
		{
			try
			{
				if (!value || !Finite(*value))
					return 0;
				ParticleEmitter* emitter = WriteEmitter(index, generation);
				if (!emitter)
					return 0;
				emitter->lightColour = {std::max(value->r, 0.0f), std::max(value->g, 0.0f), std::max(value->b, 0.0f)};
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

	void SetParticleSystem(ParticleSystem* system)
	{
		particles = system;
	}

	void RegisterParticleBindings()
	{
		using P = ParticleEmitter;
		Add("Crucible.ParticleEmitter::ControlInternal", &ManagedParticleControl);
		Add("Crucible.ParticleEmitter::EmitInternal", &ManagedParticleEmit);
		Add("Crucible.ParticleEmitter::GetParticleCountInternal", &ManagedParticleGetCount);

		Add("Crucible.ParticleEmitter::GetPlayingInternal", &GetFlag<&P::playing>);
		Add("Crucible.ParticleEmitter::GetLoopInternal", &GetFlag<&P::loop>);
		Add("Crucible.ParticleEmitter::SetLoopInternal", &SetFlag<&P::loop>);
		Add("Crucible.ParticleEmitter::GetWorldSpaceInternal", &GetFlag<&P::worldSpace>);
		Add("Crucible.ParticleEmitter::SetWorldSpaceInternal", &SetFlag<&P::worldSpace>);
		Add("Crucible.ParticleEmitter::GetEmitLightInternal", &GetFlag<&P::emitLight>);
		Add("Crucible.ParticleEmitter::SetEmitLightInternal", &SetFlag<&P::emitLight>);

		Add("Crucible.ParticleEmitter::GetBurstCountInternal", &GetInt<&P::burstCount>);
		Add("Crucible.ParticleEmitter::SetBurstCountInternal", &SetInt<&P::burstCount>);
		Add("Crucible.ParticleEmitter::GetMaxParticlesInternal", &GetInt<&P::maxParticles>);
		Add("Crucible.ParticleEmitter::SetMaxParticlesInternal", &SetInt<&P::maxParticles>);

		Add("Crucible.ParticleEmitter::GetDurationInternal", &GetFloat<&P::duration>);
		Add("Crucible.ParticleEmitter::SetDurationInternal", &SetFloat<&P::duration, false>);
		Add("Crucible.ParticleEmitter::GetRateInternal", &GetFloat<&P::rate>);
		Add("Crucible.ParticleEmitter::SetRateInternal", &SetFloat<&P::rate, false>);
		Add("Crucible.ParticleEmitter::GetSpreadInternal", &GetFloat<&P::spreadDeg>);
		Add("Crucible.ParticleEmitter::SetSpreadInternal", &SetFloat<&P::spreadDeg, false>);
		Add("Crucible.ParticleEmitter::GetLifetimeMinInternal", &GetFloat<&P::lifetimeMin>);
		Add("Crucible.ParticleEmitter::SetLifetimeMinInternal", &SetFloat<&P::lifetimeMin, false>);
		Add("Crucible.ParticleEmitter::GetLifetimeMaxInternal", &GetFloat<&P::lifetimeMax>);
		Add("Crucible.ParticleEmitter::SetLifetimeMaxInternal", &SetFloat<&P::lifetimeMax, false>);
		Add("Crucible.ParticleEmitter::GetSpeedMinInternal", &GetFloat<&P::speedMin>);
		Add("Crucible.ParticleEmitter::SetSpeedMinInternal", &SetFloat<&P::speedMin, false>);
		Add("Crucible.ParticleEmitter::GetSpeedMaxInternal", &GetFloat<&P::speedMax>);
		Add("Crucible.ParticleEmitter::SetSpeedMaxInternal", &SetFloat<&P::speedMax, false>);
		Add("Crucible.ParticleEmitter::GetSizeMinInternal", &GetFloat<&P::sizeMin>);
		Add("Crucible.ParticleEmitter::SetSizeMinInternal", &SetFloat<&P::sizeMin, false>);
		Add("Crucible.ParticleEmitter::GetSizeMaxInternal", &GetFloat<&P::sizeMax>);
		Add("Crucible.ParticleEmitter::SetSizeMaxInternal", &SetFloat<&P::sizeMax, false>);
		Add("Crucible.ParticleEmitter::GetEndSizeScaleInternal", &GetFloat<&P::endSizeScale>);
		Add("Crucible.ParticleEmitter::SetEndSizeScaleInternal", &SetFloat<&P::endSizeScale, false>);
		Add("Crucible.ParticleEmitter::GetGravityScaleInternal", &GetFloat<&P::gravityScale>);
		Add("Crucible.ParticleEmitter::SetGravityScaleInternal", &SetFloat<&P::gravityScale, true>);
		Add("Crucible.ParticleEmitter::GetDragInternal", &GetFloat<&P::drag>);
		Add("Crucible.ParticleEmitter::SetDragInternal", &SetFloat<&P::drag, false>);
		Add("Crucible.ParticleEmitter::GetBrightnessInternal", &GetFloat<&P::brightness>);
		Add("Crucible.ParticleEmitter::SetBrightnessInternal", &SetFloat<&P::brightness, false>);
		Add("Crucible.ParticleEmitter::GetLightIntensityInternal", &GetFloat<&P::lightIntensity>);
		Add("Crucible.ParticleEmitter::SetLightIntensityInternal", &SetFloat<&P::lightIntensity, false>);
		Add("Crucible.ParticleEmitter::GetLightRangeInternal", &GetFloat<&P::lightRange>);
		Add("Crucible.ParticleEmitter::SetLightRangeInternal", &SetFloat<&P::lightRange, false>);

		Add("Crucible.ParticleEmitter::GetColorInternal", &ManagedParticleGetColor);
		Add("Crucible.ParticleEmitter::SetColorInternal", &ManagedParticleSetColor);
		Add("Crucible.ParticleEmitter::GetLightColorInternal", &ManagedParticleGetLightColor);
		Add("Crucible.ParticleEmitter::SetLightColorInternal", &ManagedParticleSetLightColor);
	}
}
#endif
