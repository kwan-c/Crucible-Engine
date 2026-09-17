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
		:m_info(other.m_info), m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity)
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
			for (std::size_t i = 0; i < m_size; i++)
			{
				void* dst = newData + i * m_info->size;
				void* src = m_data + i * m_info->size;
				m_info->moveConstruct(dst, src);
				m_info->destroy(src);
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
	}
}