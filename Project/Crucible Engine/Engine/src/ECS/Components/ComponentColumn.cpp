/*************************************************************************
* \file:         ComponentColumn.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Implementation of ComponentColumn's type-erased storage
*                management (grow, push, swap-remove, move-between-columns).
*                See ComponentColumn.h for the public interface.
*
* \details
*                Every operation dispatches on
*                ComponentInfo::isTriviallyRelocatable /
*                isTriviallyDestructible (see ComponentInfo.h) to pick
*                between a fast memcpy/no-op path and a correct
*                move-construct+destroy path for non-trivial types.
*
* \history
*    -v1.0       initial grow/push/swap-remove/move-between-columns implementation
*
*
**************************************************************************/
#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

#include "ComponentColumn.h"

namespace Crucible
{
	ComponentColumn::~ComponentColumn()
	{
		if (!m_info)return;
		Clear();
		if (m_data)::operator delete(m_data, std::align_val_t{ m_info->alignment });
	}

	ComponentColumn::ComponentColumn(ComponentColumn&& other)noexcept
		:m_info(other.m_info), m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity),m_changedTicks(std::move(other.m_changedTicks))
	{
		other.m_info = nullptr;
		other.m_data = nullptr;
		other.m_size = 0;
		other.m_capacity = 0;
	}

	ComponentColumn& ComponentColumn::operator=(ComponentColumn&& other)noexcept
	{
		if (this == &other) return *this;

		if (m_data)
		{
			Clear();
			if (m_data)::operator delete(m_data, std::align_val_t{ m_info->alignment });
		}

		m_info = other.m_info;
		m_data = other.m_data;
		m_size = other.m_size;
		m_capacity = other.m_capacity;
		m_changedTicks = std::move(other.m_changedTicks);


		other.m_info = nullptr;
		other.m_data = nullptr;
		other.m_size = 0;
		other.m_capacity = 0;
		return *this;
	}

	void ComponentColumn::Grow(std::size_t required)
	{
		if (required <= m_capacity)return;
		std::size_t newCapacity = m_capacity == 0 ? 8 : m_capacity;
		while (newCapacity < required)
		{
			newCapacity *= 2;
		}
		Reserve(newCapacity);
	}

	void ComponentColumn::Reserve(std::size_t newCapacity)
	{
		if (newCapacity <= m_capacity)return;

		auto* newData = static_cast<std::byte*>(::operator new(newCapacity * m_info->size, std::align_val_t{ m_info->alignment }));

		if (m_info->isTriviallyRelocatable)
		{
			if (m_size > 0)std::memcpy(newData, m_data, m_size * m_info->size);
		}
		else
		{
			std::size_t constructed = 0;
			try
			{
				for (; constructed < m_size; ++constructed)
				{
					void* dst = newData + constructed * m_info->size;
					void* src = m_data + constructed * m_info->size;
					m_info->moveConstruct(dst, src);
				}
			}
			catch (...)
			{

				for (std::size_t i = 0; i < constructed; ++i)
				{
					m_info->destroy(newData + i * m_info->size);
				}
				::operator delete(newData, std::align_val_t{ m_info->alignment });
				throw;
			}


			for (std::size_t i = 0; i < m_size; i++)
			{
				m_info->destroy(m_data + i * m_info->size);
			}
		}
		if (m_data)::operator delete(m_data, std::align_val_t{ m_info->alignment });
		m_data = newData;
		m_capacity = newCapacity;
	}

	void* ComponentColumn::PushBackUninitialized()
	{
		Grow(m_size + 1);
		void* slot = m_data + m_size * m_info->size;
		++m_size;
		m_changedTicks.push_back(0);
		return slot;
	}

	void* ComponentColumn::PushBackDefault()
	{
		void* slot = PushBackUninitialized();
		m_info->defaultConstruct(slot);
		return slot;
	}

	void ComponentColumn::SwapRemove(std::size_t row)
	{
		assert(row < m_size);
		std::byte* target = m_data + row * m_info->size;
		std::byte* last = m_data + (m_size - 1) * m_info->size;

		if (m_info->isTriviallyRelocatable)
		{
			if (target != last)std::memcpy(target, last, m_info->size);
		}
		else
		{
			m_info->destroy(target);
			if (target != last)
			{
				m_info->moveConstruct(target, last);
				m_info->destroy(last);
			}
		}

		if (row != m_size - 1) m_changedTicks[row] = m_changedTicks[m_size - 1];
		m_changedTicks.pop_back();


		--m_size;
	}

	void ComponentColumn::MoveElementTo(std::size_t row, ComponentColumn& dst)
	{
		assert(row < m_size);
		assert(m_info == &dst.Info() && "column type mismatch");

		void* dstSlot = dst.PushBackUninitialized();
		if (m_info->isTriviallyRelocatable)
		{
			std::memcpy(dstSlot, At(row), m_info->size);
		}
		else
		{
			m_info->moveConstruct(dstSlot, At(row));
		}
		dst.m_changedTicks.back() = m_changedTicks[row];
	}

	void ComponentColumn::Clear()
	{
		if (!m_info) return;
		if (!m_info->isTriviallyDestructible)
		{
			for (std::size_t i = 0; i < m_size; i++)
			{
				m_info->destroy(m_data + i * m_info->size);
			}
		}
		m_size = 0;
		m_changedTicks.clear();
	}
}