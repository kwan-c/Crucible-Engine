/*************************************************************************
* \file:         NetEntityMap.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                NetEntityMap: which local Entity each NetworkId names,
*                one entity per id.
*
* \details
*                Owned by NetworkSession and written only by the
*                NetworkIdentity hooks (NetworkSession::OnIdentityAdded /
*                OnIdentityRemoved), so it can never disagree with the
*                World about which entities exist.
*
*                Two rules keep a bad entity from hurting a good one:
*                - TryAdd refuses an id that already names a different
*                  LIVE entity (the newcomer is the one rejected);
*                - RemoveIfMapped only removes the exact (id -> entity)
*                  pair, so destroying a rejected duplicate can never
*                  unmap - or despawn - the original.
*
*                No World dependency: the caller says whether an entity is
*                alive, so this header (and NetworkSession) also compile in
*                the standalone network tests.
*
* \history
*    -v1.0       Networking Guide 9.2 and 18.3 issue 8 (PR 6)
*
**************************************************************************/
#pragma once

#include <cstddef>
#include <unordered_map>

#include "ECS/Core/ECSCore.h"
#include "Network/NetTypes.h"

namespace Crucible
{
	class NetEntityMap
	{
	public:
		/// Maps id -> entity. False if id is invalid or already names a different entity that
		/// isAlive(entity) says is alive (a live conflict: the caller rejects the newcomer). A
		/// mapping to a dead entity (a stale handle) is replaced.
		template <typename IsAlive>
		bool TryAdd(Net::NetworkId id, Entity entity, IsAlive&& isAlive)
		{
			if (id == Net::kInvalidNetworkId) return false;
			auto [it, inserted] = m_byId.try_emplace(id, entity);
			if (inserted || it->second == entity) return true;
			if (isAlive(it->second)) return false;
			it->second = entity;
			return true;
		}

		/// Removes the mapping only if it is exactly (id -> entity). True if it was removed.
		bool RemoveIfMapped(Net::NetworkId id, Entity entity) noexcept
		{
			const auto it = m_byId.find(id);
			if (it == m_byId.end() || !(it->second == entity)) return false;
			m_byId.erase(it);
			return true;
		}

		/// The entity id names, or kNullEntity.
		Entity Find(Net::NetworkId id) const noexcept
		{
			const auto it = m_byId.find(id);
			return it == m_byId.end() ? kNullEntity : it->second;
		}

		bool        Contains(Net::NetworkId id) const noexcept { return m_byId.contains(id); }
		std::size_t Count() const noexcept { return m_byId.size(); }
		bool        Empty() const noexcept { return m_byId.empty(); }
		void        Clear() noexcept { m_byId.clear(); }

		/// Every mapping as (NetworkId, Entity): for checks and diagnostics.
		auto begin() const noexcept { return m_byId.begin(); }
		auto end() const noexcept { return m_byId.end(); }

	private:
		std::unordered_map<Net::NetworkId, Entity> m_byId;
	};
}
