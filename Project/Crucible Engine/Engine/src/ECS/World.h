#pragma once

#include "EngineExport.h"

//std libs
#include<cstdint>

//header files
#include "ECS/Core/ECSCore.h"          // Entity, EntityManager, IndexedArray
#include "ECS/Components/ComponentRegistry.h" // ComponentRegistry
#include "ECS/Systems/Query.h"           // Query<Components...>



/*************************************************************************
* \file:         World.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 September 2026
* \version       1.0
* \brief
*                composes EntityManager + ComponentRegistry into the single
*                API systems and gameplay code actually call
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
	class ENGINE_API World
	{
	public:
		World() = default;
		~World() = default;

		// World owns ECS storage, so copying it is not allowed
		World(const World&) = delete;
		World& operator=(const World&) = delete;

		Entity CreateEntity() {  return m_entities.Create(); }

		void DestroyEntity(Entity e) 
		{
			//invalid ignore
			if (!m_entities.IsAlive(e)) return;

			m_registry.RemoveEntity(e);
			m_entities.Destroy(e);
		}

		bool IsAlive(Entity e)const { return m_entities.IsAlive(e); }


		//templates for add components
		template<typename T> void AddComponent(Entity e, T value) 
		{ 
			//safe guard
			if (!m_entities.IsAlive(e)) return; 
			
			//add to pool
			m_registry .Pool<T>() .Add(e, std::move(value)); 
		}

		template<typename T> T* GetComponent(Entity e)
		{
			if (!m_entities.IsAlive(e)) return nullptr;
			
			return m_registry .Pool<T>() .Get(e); 
		}

		template<typename T> bool HasComponent(Entity e)
		{
			if (!m_entities.IsAlive(e)) return false;
			
			return m_registry.Pool<T>().Contains(e);
		}

		template<typename T> void RemoveComponent(Entity e)
		{
			if (!m_entities.IsAlive(e)) return;
			
			m_registry .Pool<T>() .Remove(e);
		}

		template<typename... Components>
		Query<Components...> CreateQuery() 
		{
			return Query<Components...>(m_registry);
		}

		//access for registring components
		//to be improved 
		ComponentRegistry& Registry() 
		{
			return m_registry;
		}

		const ComponentRegistry& Registry() const 
		{
			return m_registry;
		}

	private:
		EntityManager     m_entities;
		ComponentRegistry m_registry;

	};
}