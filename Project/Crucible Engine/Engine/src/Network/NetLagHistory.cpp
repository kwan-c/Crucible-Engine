/*************************************************************************
* \file:         NetLagHistory.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief
*                Lag compensation's shape history and its raycast (see
*                NetLagHistory.h).
*
* \history
*    -v1.0       shape history, blended raycast against boxes, spheres and capsules
*
**************************************************************************/
#include "Network/NetLagHistory.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Network/NetComponents.h"
#include "Network/NetworkSession.h"

namespace Crucible
{
	namespace
	{
		// The network object an entity's shape belongs to: the nearest entity at or above it that is
		// mapped as a network object.
		Entity OwningObject(const World& world, const NetworkSession& session, Entity e)
		{
			for (Entity at = e; at.IsValid() && world.IsAlive(at); at = world.GetParent(at))
			{
				const NetworkIdentity* identity = world.Read<NetworkIdentity>(at);
				if (identity != nullptr && session.Entities().Find(identity->id) == at) return at;
			}
			return kNullEntity;
		}

		// The shape physics builds for `e`, in world space, if it has one that a shot can hit.
		bool ShapeOf(const World& world, Entity e, LagShape& out)
		{
			Math::mat4 m(1.f);
			if (!world.TryGetWorldMatrix(e, m)) return false;
			const Math::vec3 position(m[3]);

			// A character's capsule: upright, from height, radius and centre as they are (not scaled).
			if (const CharacterController* character = world.Read<CharacterController>(e))
			{
				if (!world.IsComponentEnabled(e, ComponentTypeId<CharacterController>())) return false;
				out.kind = LagShape::Kind::Capsule;
				out.center = position + character->m_center;
				out.rotation = Math::quat(1.f, 0.f, 0.f, 0.f);
				out.radius = std::max(0.f, character->m_radius);
				out.halfSegment = std::max(0.f, character->m_height * 0.5f - out.radius);
				return true;
			}
			const Collider* collider = world.Read<Collider>(e);
			if (collider == nullptr || collider->m_isTrigger || collider->m_type == ShapeType::HeightField
				|| !world.IsComponentEnabled(e, ComponentTypeId<Collider>()))
				return false;

			Math::vec3 scale(1.f);
			Math::mat3 axes(1.f);
			for (int axis = 0; axis < 3; ++axis)
			{
				const Math::vec3 column(m[axis]);
				const float length = glm::length(column);
				scale[axis] = length > 1e-6f && std::isfinite(length) ? length : 1.f;
				axes[axis] = column / scale[axis];
			}
			out.center = Math::vec3(m * Math::vec4(collider->m_center, 1.f));
			out.rotation = glm::normalize(glm::quat_cast(axes));
			switch (collider->m_type)
			{
			case ShapeType::Box:
				out.kind = LagShape::Kind::Box;
				out.halfExtents = collider->m_size * scale * 0.5f;
				return true;
			case ShapeType::Sphere:
				out.kind = LagShape::Kind::Sphere;
				out.radius = collider->m_radius * std::max({ scale.x, scale.y, scale.z });
				return true;
			case ShapeType::Capsule:
				out.kind = LagShape::Kind::Capsule;
				out.radius = collider->m_radius * std::max(scale.x, scale.z);
				out.halfSegment = std::max(0.f, collider->m_height * scale.y * 0.5f - out.radius);
				return true;
			default:
				return false;
			}
		}

		bool RaySphere(const Math::vec3& center, float radius, const Math::vec3& o, const Math::vec3& d, float maxDistance,
			float& t, Math::vec3& normal)
		{
			const Math::vec3 oc = o - center;
			const float c = glm::dot(oc, oc) - radius * radius;
			if (c <= 0.f) { t = 0.f; normal = -d; return true; }   // starts inside: hit at once, as physics does
			const float b = glm::dot(oc, d);
			const float disc = b * b - c;
			if (b > 0.f || disc < 0.f) return false;
			t = -b - std::sqrt(disc);
			if (t > maxDistance) return false;
			normal = glm::normalize(o + d * t - center);
			return true;
		}
	}

	bool NetLagHistory::RayShape(const LagShape& shape, const Math::vec3& o, const Math::vec3& d, float maxDistance,
		float& t, Math::vec3& normal) noexcept
	{
		switch (shape.kind)
		{
		case LagShape::Kind::Sphere:
			return RaySphere(shape.center, shape.radius, o, d, maxDistance, t, normal);

		case LagShape::Kind::Box:
		{
			// In the box's own frame: the slab test.
			const Math::quat inverse = glm::conjugate(shape.rotation);
			const Math::vec3 lo = inverse * (o - shape.center);
			const Math::vec3 ld = inverse * d;
			float tMin = 0.f, tMax = maxDistance;
			int hitAxis = -1;
			float hitSign = 0.f;
			bool inside = true;
			for (int axis = 0; axis < 3; ++axis)
			{
				const float e = shape.halfExtents[axis];
				if (std::abs(lo[axis]) > e) inside = false;
				if (std::abs(ld[axis]) < 1e-9f)
				{
					if (std::abs(lo[axis]) > e) return false;   // parallel and outside this slab
					continue;
				}
				float t1 = (-e - lo[axis]) / ld[axis];
				float t2 = (e - lo[axis]) / ld[axis];
				float sign = -1.f;
				if (t1 > t2) { std::swap(t1, t2); sign = 1.f; }
				if (t1 > tMin) { tMin = t1; hitAxis = axis; hitSign = sign; }
				tMax = std::min(tMax, t2);
				if (tMin > tMax) return false;
			}
			if (inside) { t = 0.f; normal = -d; return true; }
			if (hitAxis < 0) return false;
			t = tMin;
			Math::vec3 local(0.f);
			local[hitAxis] = hitSign;
			normal = shape.rotation * local;
			return true;
		}

		case LagShape::Kind::Capsule:
		{
			// Inigo Quilez's ray-capsule intersection: the cylinder, then the end spheres.
			const Math::vec3 axis = shape.rotation * Math::vec3(0.f, 1.f, 0.f);
			const Math::vec3 pa = shape.center - axis * shape.halfSegment;
			const Math::vec3 pb = shape.center + axis * shape.halfSegment;
			const float r = shape.radius;
			const Math::vec3 ba = pb - pa;
			const Math::vec3 oa = o - pa;
			const float baba = glm::dot(ba, ba);
			// Starts inside: hit at once.
			{
				const float h = baba > 0.f ? std::clamp(glm::dot(oa, ba) / baba, 0.f, 1.f) : 0.f;
				const Math::vec3 closest = pa + ba * h;
				if (glm::dot(o - closest, o - closest) <= r * r) { t = 0.f; normal = -d; return true; }
			}
			if (baba < 1e-12f) return RaySphere(shape.center, r, o, d, maxDistance, t, normal);
			const float bard = glm::dot(ba, d);
			const float baoa = glm::dot(ba, oa);
			const float rdoa = glm::dot(d, oa);
			const float oaoa = glm::dot(oa, oa);
			const float a = baba - bard * bard;
			float best = std::numeric_limits<float>::infinity();
			if (a > 1e-9f)
			{
				const float b = baba * rdoa - baoa * bard;
				const float c = baba * oaoa - baoa * baoa - r * r * baba;
				const float h = b * b - a * c;
				if (h >= 0.f)
				{
					const float tc = (-b - std::sqrt(h)) / a;
					const float y = baoa + tc * bard;
					if (tc >= 0.f && y > 0.f && y < baba) best = tc;   // the side
				}
			}
			float ts = 0.f;
			Math::vec3 ns;
			if (RaySphere(pa, r, o, d, maxDistance, ts, ns) && ts < best) best = ts;
			if (RaySphere(pb, r, o, d, maxDistance, ts, ns) && ts < best) best = ts;
			if (!(best <= maxDistance)) return false;
			t = best;
			const Math::vec3 p = o + d * t;
			const float h = std::clamp(glm::dot(p - pa, ba) / baba, 0.f, 1.f);
			const Math::vec3 n = p - (pa + ba * h);
			normal = glm::dot(n, n) > 1e-12f ? glm::normalize(n) : -d;
			return true;
		}
		}
		return false;
	}

	void NetLagHistory::Record(const World& world, const NetworkSession& session, Net::NetTick tick)
	{
		// The same tick as the newest frame (no step ran): replace it.
		if (m_count == 0 || m_frames[m_newest].tick != tick)
		{
			m_newest = m_count == 0 ? 0 : (m_newest + 1) % kFrames;
			m_count = std::min(m_count + 1, kFrames);
		}
		Frame& frame = m_frames[m_newest];
		frame.tick = tick;
		frame.shapes.clear();
		for (const auto& [id, root] : session.Entities())
		{
			if (!world.IsAlive(root)) continue;
			m_scratch.clear();
			world.CollectSubtree(root, m_scratch);
			std::uint16_t index = 0;
			for (const Entity e : m_scratch)
			{
				if (e != root && !(OwningObject(world, session, e) == root)) continue;   // another network object's part
				LagShape shape;
				if (!ShapeOf(world, e, shape)) continue;
				shape.id = id;
				shape.entity = e;
				shape.index = index++;
				frame.shapes.push_back(shape);
			}
		}
	}

	bool NetLagHistory::Raycast(double tick, const Math::vec3& origin, const Math::vec3& direction, float maxDistance,
		Net::NetworkId ignore, LagHit& out) const
	{
		if (m_count == 0 || !(maxDistance > 0.f) || !std::isfinite(tick)) return false;
		const float length = glm::length(direction);
		if (!(length > 1e-6f) || !std::isfinite(length)) return false;
		const Math::vec3 d = direction / length;

		// Clamp to what may be rewound and what is recorded.
		const double newest = static_cast<double>(At(0).tick);
		const double oldest = static_cast<double>(At(m_count - 1).tick);
		const double t = std::clamp(tick, std::max(oldest, newest - m_maxRewind), newest);

		// The frames around t: `before` (tick <= t) and `after` (tick >= t).
		std::size_t afterAge = 0;
		while (afterAge + 1 < m_count && static_cast<double>(At(afterAge + 1).tick) >= t) ++afterAge;
		const Frame& after = At(afterAge);
		const Frame& before = afterAge + 1 < m_count && static_cast<double>(after.tick) > t ? At(afterAge + 1) : after;
		const double span = static_cast<double>(after.tick) - static_cast<double>(before.tick);
		const float alpha = span > 0.0 ? static_cast<float>((t - static_cast<double>(before.tick)) / span) : 1.f;

		bool hit = false;
		float best = maxDistance;
		for (std::size_t i = 0; i < after.shapes.size(); ++i)
		{
			LagShape shape = after.shapes[i];
			if (shape.id == ignore) continue;
			if (alpha < 1.f)
			{
				// The same shape one frame older: blend towards it. Same order in both frames when
				// nothing changed; otherwise looked up.
				const LagShape* older = i < before.shapes.size() && before.shapes[i].id == shape.id && before.shapes[i].index == shape.index
					? &before.shapes[i] : nullptr;
				if (older == nullptr)
					for (const LagShape& s : before.shapes)
						if (s.id == shape.id && s.index == shape.index) { older = &s; break; }
				if (older != nullptr && older->kind == shape.kind)
				{
					shape.center = glm::mix(older->center, shape.center, alpha);
					shape.rotation = glm::slerp(older->rotation, shape.rotation, alpha);
				}
				else if (older == nullptr && before.tick != after.tick)
				{
					continue;   // it did not exist yet at t
				}
			}
			float distance = 0.f;
			Math::vec3 normal;
			if (!RayShape(shape, origin, d, best, distance, normal)) continue;
			hit = true;
			best = distance;
			out.id = shape.id;
			out.entity = shape.entity;
			out.distance = distance;
			out.point = origin + d * distance;
			out.normal = normal;
			out.tick = t;
		}
		return hit;
	}

	void NetLagHistory::SetMaxRewindTicks(double ticks) noexcept
	{
		m_maxRewind = std::clamp(std::isfinite(ticks) ? ticks : kDefaultMaxRewindTicks, 0.0, static_cast<double>(kFrames - 2));
	}

	void NetLagHistory::Clear() noexcept
	{
		for (Frame& frame : m_frames) { frame.tick = 0; frame.shapes.clear(); }
		m_newest = 0;
		m_count = 0;
	}

	Net::NetTick NetLagHistory::NewestTick() const noexcept { return m_count == 0 ? 0 : At(0).tick; }
	Net::NetTick NetLagHistory::OldestTick() const noexcept { return m_count == 0 ? 0 : At(m_count - 1).tick; }

	const std::vector<LagShape>& NetLagHistory::ShapesNear(double tick) const noexcept
	{
		static const std::vector<LagShape> none;
		if (m_count == 0) return none;
		std::size_t bestAge = 0;
		double bestGap = std::numeric_limits<double>::infinity();
		for (std::size_t age = 0; age < m_count; ++age)
		{
			const double gap = std::abs(static_cast<double>(At(age).tick) - tick);
			if (gap < bestGap) { bestGap = gap; bestAge = age; }
		}
		return At(bestAge).shapes;
	}
}
