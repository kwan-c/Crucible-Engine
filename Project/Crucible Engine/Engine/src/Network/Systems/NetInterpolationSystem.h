/*************************************************************************
* \file:         NetInterpolationSystem.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          6 November 2026
* \version       1.1
* \brief
*                Client: draws every networked proxy slightly in the past,
*                between the two snapshot samples around that time, so
*                motion is smooth at any frame rate although snapshots
*                arrive every 33 ms with jitter and loss.
*
* \details
*                Networking Guide 11.4. NetReceiveSystem no longer writes a
*                snapshot's position and rotation into the Transform: it
*                adds them as a sample to the proxy's NetInterpolation
*                (PushSample). Each frame this system advances the
*                session's ServerTimeEstimate and writes, for every proxy,
*                its root Transform at
*                    renderTick = estimated server tick - 6 ticks (100 ms)
*                Six ticks are three snapshot intervals: one lost snapshot
*                still leaves a sample on each side.
*                  - between two samples: position lerp, rotation slerp
*                  - all samples newer (just spawned): the oldest
*                  - nothing newer yet (loss, an idle entity): the newest is
*                    held - never extrapolated
*                  - more than 5 m between two samples (respawn, knock-
*                    back): the buffer restarts there, a snap, not a slide
*
*                The own player: its position is interpolated like any
*                proxy (v1 has no prediction). Its rotation is left to the
*                local controller only while that controller's Face Camera
*                turns it to the local camera yaw (PlayerController::
*                m_faceCamera and PlayerInput::hasYaw - the condition
*                ECSPlayerControllerSystem applies it under; the host
*                applies the same yaw), so a camera rig parented to it
*                stays steady instead of trailing 100 ms behind the mouse
*                (the guide's interim improvement in 11.7). Face Camera
*                off, or no PlayerController: the host's rotation is
*                interpolated like any proxy's.
*
*                Also completes the key-to-motion measurement (the session's
*                latency probe) when the own player is first drawn moving.
*
*                PreRender, before HierarchyTransformSystem, so the Transform
*                written here becomes this frame's WorldTransform and is
*                rendered. Only roots carry NetInterpolation; children follow
*                through the hierarchy. Host and offline: nothing (the host
*                renders the authoritative entities directly).
*
* \history
*    -v1.0       Networking Guide 11.4, 11.7 (PR 12)
*    -v1.1       review fix: own rotation kept only under Face Camera
*
**************************************************************************/
#pragma once

#include "ECS/Systems/IECSSystems.h"
#include "EngineExport.h"
#include "Math/Math.h"
#include "Network/NetComponents.h"

namespace Crucible
{
	class NetworkSession;

	class ENGINE_API NetInterpolationSystem final : public IECSSystem
	{
	public:
		static constexpr double kInterpolationDelayTicks = 6.0;   // 100 ms at 60 Hz
		static constexpr float  kTeleportMetres = 5.f;

		explicit NetInterpolationSystem(NetworkSession& session) noexcept : m_session(session) {}

		SystemStage Stage() const override { return SystemStage::PreRender; }
		void        Describe(SystemDesc& desc) const override;
		void        Update(World& world, CommandBuffer& commands) override;

		/// Adds a newer sample (the caller rejected stale ticks): the oldest goes when the buffer
		/// is full; more than kTeleportMetres from the previous sample restarts the buffer. True
		/// if it restarted (a teleport).
		static bool PushSample(NetInterpolation& buffer, const NetInterpolation::Sample& sample) noexcept;

		/// The pose `buffer` gives at `renderTick` (see the file notes). False if it is empty.
		static bool Evaluate(const NetInterpolation& buffer, double renderTick, Math::vec3& position, Math::quat& rotation) noexcept;

	private:
		NetworkSession& m_session;
	};
}
