#pragma once

//std libs
#include <cstdint>
#include <memory>
#include <cassert>
#include <cstddef>
#include <algorithm>

//header fils
#include "ECS/Core/ECSCore.h"
#include "ECS/Core/SparseSet.h"
/*************************************************************************
* \file:         ComponentRegistry.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 September 2026
* \version       1.0
* \brief
*                owns and registers component pools, one SparseSet<T> per type
*
* \details
*
* \history
*    -v1.0       skeleton
*
*
**************************************************************************/

namespace Crucible 
{

	inline size_t NextComponentTypeId()
	{
		static size_t counter = 0;
		return counter++;
	}

	template<typename T>
	size_t ComponentTypeId()
	{
		static const size_t id = NextComponentTypeId();
		return id;
	}

	//base class for componentpool
	class ISparseSet
	{
	public:
		virtual ~ISparseSet() = default;
		virtual void Remove(Entity e) = 0;

	};
    template<typename T>
    class ComponentPool : public ISparseSet
    {
    public:
        void Add(Entity e, T value)
        {
            m_storage.Add(e, std::move(value));
        }

        T* Get(Entity e)
        {
            return m_storage.Get(e);
        }

        bool Contains(Entity e) const
        {
            return m_storage.Contains(e);
        }

        void Remove(Entity e) override
        {
            m_storage.Remove(e);
        }

        void Reserve(size_t expectedComponentCount)
        {
            m_storage.Reserve(expectedComponentCount);
        }

        void ReserveSparse(size_t maxEntityIndex)
        {
            m_storage.ReserveSparse(maxEntityIndex);
        }

        SparseSet<T>& Raw()
        {
            return m_storage;
        }

    private:
        SparseSet<T> m_storage;
    };
    class ComponentRegistry
    {
    public:
        ComponentRegistry() = default;

        ComponentRegistry(const ComponentRegistry&) = delete;
        ComponentRegistry& operator=(const ComponentRegistry&) = delete;

        ComponentRegistry(ComponentRegistry&&) noexcept = default;
        ComponentRegistry& operator=(ComponentRegistry&&) noexcept = default;

        template<typename T>
        void Register(size_t expectedComponentCount = 0)
        {
            const size_t id = ComponentTypeId<T>();

            auto* slot = m_pools.Get(static_cast<uint32_t>(id));

            // already registered
            if (slot && *slot)
            {
                auto* pool =static_cast<ComponentPool<T>*>(slot->get());
                if (expectedComponentCount > 0) pool->Reserve(expectedComponentCount);
                return;
            }

            auto pool = std::make_unique<ComponentPool<T>>();

            if (expectedComponentCount > 0)pool->Reserve(expectedComponentCount);

            m_pools.Set(static_cast<uint32_t>(id),std::move(pool));

            m_poolCount = std::max(m_poolCount, id + 1);
        }

        template<typename T>
        ComponentPool<T>& Pool()
        {
            const size_t id = ComponentTypeId<T>();

            auto* slot = m_pools.Get(static_cast<uint32_t>(id));

            assert(slot != nullptr &&*slot != nullptr &&"Component type has not been registered");

            return *static_cast<ComponentPool<T>*>(slot->get());
        }

        template<typename T>
        bool IsRegistered() const
        {
            const size_t id = ComponentTypeId<T>();

            const auto* slot = m_pools.Get(static_cast<uint32_t>(id));

            return slot != nullptr && *slot != nullptr;
        }

        template<typename T>
        void ReserveSparseFor(size_t maxEntityIndex)
        {
            Pool<T>().ReserveSparse(maxEntityIndex);
        }

        void RemoveEntity(Entity e)
        {
            for (size_t i = 0; i < m_poolCount; ++i)
            {
                auto* slot = m_pools.Get(static_cast<uint32_t>(i));

                if (slot && *slot)
                {
                    (*slot)->Remove(e);
                }
            }
        }

    private:
        IndexedArray<std::unique_ptr<ISparseSet>> m_pools;
        size_t m_poolCount = 0;
    };
}