#pragma once

//std libs
#include <vector>
#include <limits>
#include <utility>
#include <cstdint>

//header file 
#include "ECSCore.h"

/*************************************************************************
* \file:         SparceSet.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 Septemeber 2026
* \version       1.0
* \brief
*                sparse set storage for one component type
*
* \details
*                Generic sparse set: sparse[Entity.index] -> row,
*                dense[row] -> Entity, data[row] -> component value.
*                Uses swap-and-pop on Remove so dense/data stay packed.
* \history
*    -v1.0       skeleton
*
*
**************************************************************************/

namespace Crucible
{
	template<typename T>
	class SparseSet
	{
	public:
		//add entity comp for entity
		void Add(Entity e, T value)
		{
			if (Contains(e))
			{
				uint32_t row = m_sparse[e.index];
				m_data[row] = std::move(value);
				return;
			}
			if (e.index >= m_sparse.size()) m_sparse.resize(e.index + 1, m_Invalid);
			m_sparse[e.index] = static_cast<uint32_t>(m_dense.size());
			m_dense.push_back(e);
			m_data.push_back(std::move(value));
		}
		void Remove(Entity e)
		{
			if (!Contains(e)) return;
			uint32_t row = m_sparse[e.index];
			uint32_t lastRow = static_cast<uint32_t>(m_dense.size() - 1);

			m_dense[row] = m_dense[lastRow];
			m_data[row] = std::move(m_data[lastRow]);
			m_sparse[m_dense[row].index] = row;
			
			m_dense.pop_back();
			m_data.pop_back();
			m_sparse[e.index] = m_Invalid;
		}

		bool Contains(Entity e) const 
		{
			return e.index < m_sparse.size() && m_sparse[e.index] != m_Invalid;
		}

		T* Get(Entity e) { return Contains(e) ? &m_data[m_sparse[e.index]] : nullptr; }


		void Reserve(size_t expectedComponentCount)
		{
			m_dense.reserve(expectedComponentCount);
			m_data.reserve(expectedComponentCount);
		}

		void ReserveSparse(size_t maxEntityIndex)
		{
			if (maxEntityIndex >= m_sparse.size())
			{
				m_sparse.resize(maxEntityIndex + 1, m_Invalid);
			}
		}

		// for query
		auto begin() { return m_data.begin(); }
		auto end() { return m_data.end(); }
		const std::vector<Entity>& Entities() const { return m_dense; }
	private:
		static constexpr uint32_t  m_Invalid = std::numeric_limits<uint32_t>::max();
		std::vector<uint32_t> m_sparse;
	    std::vector<Entity> m_dense; //row to entity
		std::vector<T> m_data;// row to component value

	};
}
