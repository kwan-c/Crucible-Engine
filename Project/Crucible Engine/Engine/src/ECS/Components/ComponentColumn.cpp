/*************************************************************************
* \file:         ComponentColumn.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.2
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
*                Appends follow "commit last": capacity is reserved first
*                (data AND change ticks), the element is constructed, and
*                only then is the row counted. Moves and destructors are
*                noexcept by contract, so relocation cannot fail halfway.
*
* \history
*    -v1.0       initial grow/push/swap-remove/move-between-columns implementation
*    -v1.1       strong exception guarantee (commit-last appends, tick
*                capacity reserved with data), PushBackMoved/PopBack
*    -v1.2       on/off bytes (toggleable columns): reserved with the data,
*                appended on, carried by MoveElementTo/SwapRemove
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
		:m_info(other.m_info), m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity),m_changedTicks(std::move(other.m_changedTicks)),
		m_enabled(std::move(other.m_enabled)), m_disabledCount(other.m_disabledCount)
	{
		other.m_info = nullptr;
		other.m_data = nullptr;
		other.m_size = 0;
		other.m_capacity = 0;
		other.m_disabledCount = 0;
	}

	ComponentColumn& ComponentColumn::operator=(ComponentColumn&& other)noexcept
	{
		if (this == &other) return *this;

		if (m_data)
		{
			Clear();
			::operator delete(m_data, std::align_val_t{ m_info->alignment });
		}

		m_info = other.m_info;
		m_data = other.m_data;
		m_size = other.m_size;
		m_capacity = other.m_capacity;
		m_changedTicks = std::move(other.m_changedTicks);
		m_enabled = std::move(other.m_enabled);
		m_disabledCount = other.m_disabledCount;


		other.m_info = nullptr;
		other.m_data = nullptr;
		other.m_size = 0;
		other.m_capacity = 0;
		other.m_disabledCount = 0;
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

		// Everything that can throw happens before the column is touched.
		m_changedTicks.reserve(newCapacity);   // ticks first: later push_backs can then never throw
		if (m_info->toggleable) m_enabled.reserve(newCapacity);   // same for the on/off bytes
		auto* newData = static_cast<std::byte*>(::operator new(newCapacity * m_info->size, std::align_val_t{ m_info->alignment }));

		// Relocate the live elements; moves/destructors are noexcept by contract.
		if (m_info->isTriviallyRelocatable)
		{
			if (m_size > 0)std::memcpy(newData, m_data, m_size * m_info->size);
		}
		else
		{
			for (std::size_t i = 0; i < m_size; ++i)
			{
				m_info->moveConstruct(newData + i * m_info->size, m_data + i * m_info->size);
				m_info->destroy(m_data + i * m_info->size);
			}
		}
		if (m_data)::operator delete(m_data, std::align_val_t{ m_info->alignment });
		m_data = newData;
		m_capacity = newCapacity;
	}

	void* ComponentColumn::PushBackDefault()
	{
		ReserveForAppend();   // may throw; nothing changed yet

		void* slot = m_data + m_size * m_info->size;
		m_info->defaultConstruct(slot);   // may throw; the row isn't counted yet

		// Commit: tick capacity was reserved with the data, so this can't throw.
		m_changedTicks.push_back(0);
		if (m_info->toggleable) m_enabled.push_back(1);   // a new component starts switched on
		++m_size;
		return slot;
	}

	void* ComponentColumn::PushBackMoved(void* src)
	{
		ReserveForAppend();   // the only step that can throw

		void* slot = m_data + m_size * m_info->size;
		if (m_info->isTriviallyRelocatable) std::memcpy(slot, src, m_info->size);
		else m_info->moveConstruct(slot, src);

		m_changedTicks.push_back(0);
		if (m_info->toggleable) m_enabled.push_back(1);   // MoveElementTo copies the source's state over this
		++m_size;
		return slot;
	}

	void ComponentColumn::PopBack() noexcept
	{
		assert(m_size > 0);
		--m_size;
		if (!m_info->isTriviallyDestructible) m_info->destroy(m_data + m_size * m_info->size);
		m_changedTicks.pop_back();
		if (!m_enabled.empty())
		{
			if (m_enabled.back() == 0) --m_disabledCount;
			m_enabled.pop_back();
		}
	}

	void ComponentColumn::SwapRemove(std::size_t row) noexcept
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

		// The removed row's state goes with it; the last row's state follows its data into `row`.
		if (!m_enabled.empty())
		{
			if (m_enabled[row] == 0) --m_disabledCount;
			if (row != m_size - 1) m_enabled[row] = m_enabled[m_size - 1];
			m_enabled.pop_back();
		}

		--m_size;
	}

	void ComponentColumn::MoveElementTo(std::size_t row, ComponentColumn& dst)
	{
		assert(row < m_size);
		assert(m_info == &dst.Info() && "column type mismatch");

		dst.PushBackMoved(At(row));
		dst.m_changedTicks.back() = m_changedTicks[row];   // an archetype move is not a component change
		if (!m_enabled.empty() && m_enabled[row] == 0)      // nor a switch on/off
		{
			dst.m_enabled.back() = 0;
			++dst.m_disabledCount;
		}
	}

	void ComponentColumn::Clear() noexcept
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
		m_enabled.clear();
		m_disabledCount = 0;
	}
}
