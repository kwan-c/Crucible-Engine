/*************************************************************************
* \file:         NetInterpolationSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          6 November 2026
* \version       1.4
* \brief
*                NetInterpolationSystem: proxies drawn 100 ms in the past,
*                between snapshot samples; the key-to-motion measurement.
*
* \history
*    -v1.0       snapshot interpolation of remote proxies
*    -v1.1       review fix: the own rotation stays local only while Face
*                Camera applies the local yaw
*    -v1.2       NetStats::interpolateMicros
*    -v1.3       synced children drawn at the roots' render tick (ApplyChildTransforms)
*    -v1.4       checks that its delay is the one Network.ViewTick subtracts (lag compensation)
*
**************************************************************************/
#include "Network/Systems/NetInterpolationSystem.h"

#include <algorithm>
#include <chrono>

#include "ECS/Components/Components.h"
#include "ECS/HierarchyTransformSystem.h"
#include "ECS/World.h"
#include "Network/NetSyncedChildren.h"
#include "Network/NetworkSession.h"
#include "Player/PlayerComponents.h"

namespace Crucible
{
	void NetInterpolationSystem::Describe(SystemDesc& desc) const
	{
		// Before transform propagation, so this frame's WorldTransform (and the render) uses it.
		desc.RunBeforeIfPresent<HierarchyTransformSystem>()
			.Affinity(SystemAffinity::MainThread);   // reads the session
	}

	bool NetInterpolationSystem::PushSample(NetInterpolation& buffer, const NetInterpolation::Sample& sample) noexcept
	{
		bool teleported = false;
		if (buffer.count > 0)
		{
			const Math::vec3 d = sample.position - buffer.samples[buffer.count - 1].position;
			if (glm::dot(d, d) > kTeleportMetres * kTeleportMetres) { buffer.count = 0; teleported = true; }
		}
		if (buffer.count == NetInterpolation::kCapacity)   // full: drop the oldest
		{
			std::move(buffer.samples.begin() + 1, buffer.samples.end(), buffer.samples.begin());
			--buffer.count;
		}
		buffer.samples[buffer.count++] = sample;   // ticks increase: stale ones were rejected
		return teleported;
	}

	bool NetInterpolationSystem::Evaluate(const NetInterpolation& buffer, double renderTick, Math::vec3& position, Math::quat& rotation) noexcept
	{
		if (buffer.count == 0) return false;
		const auto& s = buffer.samples;
		std::uint32_t after = 0;   // the first sample later than renderTick
		while (after < buffer.count && static_cast<double>(s[after].tick) <= renderTick) ++after;

		if (after == 0) { position = s[0].position; rotation = s[0].rotation; return true; }   // all ahead: the oldest
		if (after == buffer.count)                                                            // none ahead: hold
		{
			position = s[after - 1].position;
			rotation = s[after - 1].rotation;
			return true;
		}
		const auto& a = s[after - 1];
		const auto& b = s[after];   // b.tick > a.tick: samples strictly increase
		const float alpha = static_cast<float>((renderTick - static_cast<double>(a.tick)) / static_cast<double>(b.tick - a.tick));
		position = glm::mix(a.position, b.position, alpha);
		rotation = glm::slerp(a.rotation, b.rotation, alpha);
		return true;
	}

	void NetInterpolationSystem::Update(World& world, [[maybe_unused]] CommandBuffer& commands)
	{
		if (!m_session.IsClient() || m_session.State() != SessionState::Running) return;
		const auto started = std::chrono::steady_clock::now();
		struct Timer { NetworkSession& s; std::chrono::steady_clock::time_point t;
			~Timer() { s.MutableStats().interpolateMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t).count(); } }
			timer{ m_session, started };
		ServerTimeEstimate& time = m_session.ServerTime();
		time.Advance(world.DeltaTime());
		if (!time.Valid()) return;   // no snapshot yet: proxies stay where their spawn put them
		static_assert(kInterpolationDelayTicks == NetworkSession::kViewDelayTicks, "Network.ViewTick must name what is drawn");
		const double renderTick = time.Estimate() - kInterpolationDelayTicks;
		const Net::PeerId self = m_session.LocalPeer();

		world.CreateQuery<const NetInterpolation, const NetworkIdentity>().ForEach(
			[&](Entity e, const NetInterpolation& buffer, const NetworkIdentity& identity) {
				Math::vec3 position;
				Math::quat rotation;
				if (!Evaluate(buffer, renderTick, position, rotation)) return;
				const Transform* current = world.Read<Transform>(e);
				if (current == nullptr) return;

				// The own player keeps its rotation only while the controller's Face Camera turns it to
				// the local camera yaw - exactly the condition ECSPlayerControllerSystem applies it
				// under (m_faceCamera and a yaw from the sampler; that facing runs for a switched-off
				// PlayerController too, as on a client, where it is server-only). Otherwise the host's
				// rotation is interpolated like any proxy's.
				const bool own = identity.owner == self;
				const PlayerInput* input = own ? world.Read<PlayerInput>(e) : nullptr;
				const PlayerController* controller = input != nullptr ? world.Read<PlayerController>(e) : nullptr;
				const bool keepRotation = controller != nullptr && controller->m_faceCamera && input->hasYaw;

				// Written only when it changed: a mutable access stamps the Transform as changed.
				if (current->positions != position || (!keepRotation && current->rotation != rotation))
				{
					Transform* t = world.GetComponent<Transform>(e);
					t->positions = position;
					if (!keepRotation) t->rotation = rotation;
				}
				if (own) m_session.NoteRenderedPosition(identity.id, position);
			});

		// The synced children, at the same render tick as the roots.
		ApplyChildTransforms(world, renderTick);
	}
}
