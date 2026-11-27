/*************************************************************************
* \file:         NetLagHistory.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief
*                Lag compensation (the host): where every network object's
*                collision shapes were over the last second, so a client's
*                shot can be tested against what that client saw.
*
* \details
*                A client draws other objects about 100 ms in the past (the
*                interpolation delay) plus half its ping. When it fires, the
*                host would test the shot against where targets are now, and
*                a client aiming exactly at a moving target misses. With
*                this, the host tests it at the client's view tick instead
*                (C# Network.ViewTick, sent with the shot).
*
*                Record (once per frame, host): for each network object, the
*                world-space shapes of its root and of every descendant that
*                is not a network object itself: a CharacterController's
*                capsule (upright, unscaled, as physics builds it), else a
*                Collider's box, sphere or capsule (scaled). Triggers,
*                switched-off components and HeightFields are skipped. Kept
*                for kFrames frames (about a second at 60 frames a second).
*
*                Raycast: the two recorded frames around the asked tick,
*                each shape blended between them (position and rotation);
*                ticks older than the rewind limit are clamped to it, newer
*                ones to the newest frame. Only network objects are in the
*                history: walls are tested with the normal physics raycast
*                by the caller (C# Network.RaycastAt).
*
* \history
*    -v1.0       shape history, blended raycast against boxes, spheres and capsules
*
**************************************************************************/
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "EngineExport.h"
#include "ECS/Core/ECSCore.h"
#include "Math/Math.h"
#include "Network/NetTypes.h"

namespace Crucible
{
	class World;
	class NetworkSession;

	/// One collision shape of a network object at one recorded tick, in world space.
	struct LagShape
	{
		enum class Kind : std::uint8_t { Box, Sphere, Capsule };

		Net::NetworkId id = Net::kInvalidNetworkId;   // the network object it belongs to
		Entity         entity{};                       // the entity carrying the shape (the root or a part)
		std::uint16_t  index = 0;                      // its order within the object (matches across frames)
		Kind           kind = Kind::Box;
		Math::vec3     center{ 0.f };
		Math::quat     rotation{ 1.f, 0.f, 0.f, 0.f };
		Math::vec3     halfExtents{ 0.5f };            // Box
		float          radius = 0.5f;                  // Sphere, Capsule
		float          halfSegment = 0.5f;             // Capsule: half the distance between its two end centres (local Y)
	};

	/// What NetLagHistory::Raycast hit.
	struct LagHit
	{
		Net::NetworkId id = Net::kInvalidNetworkId;
		Entity         entity{};
		Math::vec3     point{ 0.f };
		Math::vec3     normal{ 0.f, 1.f, 0.f };
		float          distance = 0.f;
		double         tick = 0.0;   // the tick it was tested at (after clamping)
	};

	class ENGINE_API NetLagHistory
	{
	public:
		static constexpr std::size_t kFrames = 64;          // a little over a second at 60 frames a second
		static constexpr double      kDefaultMaxRewindTicks = 18.0;   // 0.3 s at 60 Hz

		/// Host, once per frame: every mapped network object's shapes at `tick`. A frame with the
		/// same tick as the newest replaces it (no simulation step ran).
		void Record(const World& world, const NetworkSession& session, Net::NetTick tick);

		/// Nearest hit of the ray (`direction` need not be normalised) within `maxDistance` against
		/// the shapes as they were at `tick` (fractional), clamped to [newest - maxRewind, newest].
		/// Shapes of network object `ignore` are skipped (the shooter). False if nothing was hit or
		/// nothing is recorded.
		bool Raycast(double tick, const Math::vec3& origin, const Math::vec3& direction, float maxDistance,
			Net::NetworkId ignore, LagHit& out) const;

		void   SetMaxRewindTicks(double ticks) noexcept;
		double MaxRewindTicks() const noexcept { return m_maxRewind; }

		/// Forgets everything (a new session, a new scene).
		void Clear() noexcept;

		std::size_t Frames() const noexcept { return m_count; }
		Net::NetTick NewestTick() const noexcept;
		Net::NetTick OldestTick() const noexcept;

		/// The shapes recorded at the frame nearest `tick` (tests, debug drawing). Empty if none.
		const std::vector<LagShape>& ShapesNear(double tick) const noexcept;

		/// Ray against one shape; true with the distance and normal of the entry point. Exposed for tests.
		static bool RayShape(const LagShape& shape, const Math::vec3& origin, const Math::vec3& unitDir, float maxDistance,
			float& distance, Math::vec3& normal) noexcept;

	private:
		struct Frame
		{
			Net::NetTick          tick = 0;
			std::vector<LagShape> shapes;
		};

		std::array<Frame, kFrames> m_frames{};
		std::size_t m_newest = 0;    // index of the newest frame
		std::size_t m_count = 0;     // frames recorded (<= kFrames)
		double      m_maxRewind = kDefaultMaxRewindTicks;
		std::vector<Entity> m_scratch;

		const Frame& At(std::size_t age) const noexcept { return m_frames[(m_newest + kFrames - age) % kFrames]; }   // 0 = newest
	};
}
