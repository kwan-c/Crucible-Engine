#pragma once

#include <cstddef>
#include <cassert>
#include <new>

#include "ComponentInfo.h"

namespace Crucible
{
	class ComponentColumn
	{

	public:
		ComponentColumn() = default;
		explicit ComponentColumn (const ComponentInfo& info):m_info(&info){}
		~ComponentColumn();

		ComponentColumn(const ComponentColumn&) = delete;
		ComponentColumn& operator=(const ComponentColumn&) = delete;

		ComponentColumn(ComponentColumn&& other) noexcept;
		ComponentColumn& operator=(ComponentColumn&& other)noexcept;

		const ComponentInfo& Info()const { return *m_info; }

		std::size_t Size()const { return m_size; }
		std::size_t Capacity()const { return m_capacity; }

		void* Data() { return m_data; } 
		void* At(std::size_t row)
		{
			assert(row < m_size);
			return m_data + row * m_info->size;
		}

		void Reserve(std::size_t newCapacity);
		void* PushBackDefault();
		void* PushBackUninitialized();
		void SwapRemove(std::size_t row);
		void MoveElementTo(std::size_t row, ComponentColumn& dst);
		void Clear();

	private:
		void Grow(std::size_t required);

		const ComponentInfo* m_info = nullptr;
		std::byte* m_data = nullptr;
		std::size_t m_size = 0;
		std::size_t m_capacity = 0;
	};
}