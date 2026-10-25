/*************************************************************************
* \file:         ComponentColumn.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.2
* \brief
*                Declares ComponentColumn: a type-erased, contiguous
*                dynamic array holding one component type's data for
*                every entity in an Archetype.
*
* \details
*                ComponentColumn stores raw bytes (m_data) sized/aligned
*                according to its ComponentInfo (see ComponentInfo.h),
*                using that info's function pointers
*                (default/move/copy-construct, destroy) to manage
*                individual elements without the column itself being a
*                template. One ComponentColumn per component type lives
*                inside each Archetype (see Archetype.h). See
*                ComponentColumn.cpp for the implementation of
*                growth/move/remove.
*
*                Exception safety: every append reserves capacity first,
*                constructs the element, and only then counts the row, so a
*                failed allocation or constructor leaves the column exactly
*                as it was. Component moves and destructors are required to
*                be noexcept (see MakeComponentInfo), which makes removal,
*                relocation and rollback (PopBack) non-throwing.
*
* \history
*    -v1.0       initial type-erased dynamic array (grow/push/swap-remove/move)
*    -v1.1       strong exception guarantee for appends (commit-last),
*                PushBackMoved/PopBack replace PushBackUninitialized,
*                noexcept remove/move/clear, mutation restricted to the ECS
*                (friends)
*    -v1.2       per-row on/off bytes for toggleable components (IsEnabled,
*                EnabledData, DisabledCount), carried like change ticks
*
**************************************************************************/
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <new>
#include <vector>

#include "ComponentInfo.h"

namespace Crucible
{
	class Archetype;
	class World;
	template <typename... Components> class Query;

	/*************************************************************************
	 \brief
		A type-erased, contiguous dynamic array holding one component type's
		data for every entity in an Archetype.

	 \details
		Stores raw bytes (m_data), sized/aligned according to its
		ComponentInfo, and uses that info's function pointers to manage
		individual elements - the column itself never needs to be a
		template. One ComponentColumn exists per component type inside each
		Archetype.

	 \note
		Move-only. Everything that changes rows or component data is private
		and reachable only from Archetype, World and Query, so structural
		changes can't bypass World's entity bookkeeping.
	 *************************************************************************/
	class ComponentColumn
	{

	public:

		/*********************************************************************
		 \brief  Constructs an empty, type-less column (no backing
				 ComponentInfo).
		 *********************************************************************/
		ComponentColumn() = default;

		/*********************************************************************
		 \brief  Constructs an empty column for one specific component type.
		 \param[in] info The type's ComponentInfo; must outlive this column.
		 *********************************************************************/
		explicit ComponentColumn (const ComponentInfo& info):m_info(&info){}

		/*********************************************************************
		 \brief
			Destroys every live element (if the type isn't trivially
			destructible) and frees the backing allocation.
		 *********************************************************************/
		~ComponentColumn();

		ComponentColumn(const ComponentColumn&) = delete;
		ComponentColumn& operator=(const ComponentColumn&) = delete;

		/*********************************************************************
		\brief  Move-constructs by taking ownership of other's storage.
		\param[in] other The column to move from; left empty/type-less.
		*********************************************************************/
		ComponentColumn(ComponentColumn&& other) noexcept;

		/*********************************************************************
		 \brief
			Move-assigns by freeing this column's own storage (if any) and
			taking ownership of other's.
		 \param[in] other The column to move from; left empty/type-less.
		 \return  *this.
		 *********************************************************************/
		ComponentColumn& operator=(ComponentColumn&& other)noexcept;

		/*********************************************************************
		 \brief  The ComponentInfo this column was constructed with.
		 \return Reference to the backing ComponentInfo.
		 \note  Undefined behaviour on a default-constructed (type-less)
				column.
		 *********************************************************************/
		const ComponentInfo& Info()const { return *m_info; }

		/*********************************************************************
		\brief  How many elements (rows) the column currently holds.
		\return The element count.
		*********************************************************************/
		std::size_t Size()const { return m_size; }

		/*********************************************************************
		 \brief  How many elements the current allocation can hold before
				 growing.
		 \return The element capacity.
		 *********************************************************************/
		std::size_t Capacity()const { return m_capacity; }

		/*********************************************************************
		 \brief  Read-only pointer to the start of the column's storage.
		 \return Pointer to element 0, or nullptr if never allocated.
		 *********************************************************************/
		const void* Data() const { return m_data; }

		/*********************************************************************
		 \brief  Read-only pointer to one element's storage.
		 \param[in] row Index of the element to fetch.
		 \note  Asserts row < Size() in debug builds; no check in release.
		 *********************************************************************/
		const void* At(std::size_t row) const
		{
			assert(row < m_size);
			return m_data + row * m_info->size;
		}

		// The tick last passed to Touch() for `row`, or 0 if it never has been.
		std::uint64_t LastChanged(std::size_t row) const
		{
			assert(row < m_size);
			return m_changedTicks[row];
		}

		/*********************************************************************
		 \brief  Read-only pointer to the per-row change-tick array.
		 \return Pointer to row 0's tick, or nullptr if the column is empty.
		 *********************************************************************/
		const std::uint64_t* ChangedTicksData() const noexcept { return m_changedTicks.data(); }

		/*********************************************************************
		 \brief  Whether this column keeps per-row on/off state (its type is
				 a ToggleableComponent - see ComponentKind.h).
		 *********************************************************************/
		bool IsToggleable() const noexcept { return m_info && m_info->toggleable; }

		/*********************************************************************
		 \brief  The component's own on/off state in `row` (ignores
				 requirements - see World::IsComponentActive). Always true
				 for a column that is not toggleable.
		 *********************************************************************/
		bool IsEnabled(std::size_t row) const
		{
			assert(row < m_size);
			return m_enabled.empty() || m_enabled[row] != 0;
		}

		/*********************************************************************
		 \brief  Per-row on/off bytes (1 = on, 0 = off), or nullptr when the
				 column is not toggleable or empty.
		 \note   Valid until the next structural change to the archetype.
		 *********************************************************************/
		const std::uint8_t* EnabledData() const noexcept { return m_enabled.empty() ? nullptr : m_enabled.data(); }

		/*********************************************************************
		 \brief  How many rows are switched off. 0 means a query can walk the
				 column without looking at any row's state.
		 *********************************************************************/
		std::size_t DisabledCount() const noexcept { return m_disabledCount; }

		/*********************************************************************
		 \brief  Full check of the on/off bytes: one per row for a toggleable
				 column (none otherwise), DisabledCount() equal to the zero
				 bytes. O(rows) - for tests and debugging, not hot asserts.
		 *********************************************************************/
		bool ValidateEnabled() const
		{
			if (!IsToggleable()) return m_enabled.empty() && m_disabledCount == 0;
			if (m_enabled.size() != m_size) return false;
			const auto zeros = std::count(m_enabled.begin(), m_enabled.end(), std::uint8_t{ 0 });
			return static_cast<std::size_t>(zeros) == m_disabledCount;
		}

	private:
		friend class Archetype;
		friend class World;
		template <typename... Components> friend class Query;

		void* Data() { return m_data; }

		void* At(std::size_t row)
		{
			assert(row < m_size);
			return m_data + row * m_info->size;
		}

		std::uint64_t* ChangedTicksData() noexcept { return m_changedTicks.data(); }

		void Touch(std::size_t row, std::uint64_t tick)
		{
			assert(row < m_size);
			m_changedTicks[row] = tick;
		}

		/*********************************************************************
		 \brief  Sets one row's own on/off state (toggleable columns only).
		 \return true if it changed.
		 *********************************************************************/
		bool SetEnabled(std::size_t row, bool enabled) noexcept
		{
			assert(row < m_size && IsToggleable());
			const std::uint8_t value = enabled ? 1 : 0;
			if (m_enabled[row] == value) return false;
			m_enabled[row] = value;
			if (enabled) --m_disabledCount;
			else ++m_disabledCount;
			return true;
		}

		/*********************************************************************
		 \brief  Ensures capacity for at least newCapacity elements
				 (strong guarantee: on failure nothing changes).
		 *********************************************************************/
		void Reserve(std::size_t newCapacity);

		/*********************************************************************
		 \brief  Ensures one more element fits, growing by doubling
				 (strong guarantee).
		 *********************************************************************/
		void ReserveForAppend() { Grow(m_size + 1); }

		/*********************************************************************
		 \brief  Appends one default-constructed element.
		 \return Pointer to the new element.
		 \note  Strong guarantee: if allocation or the constructor throws,
				the column is unchanged.
		 *********************************************************************/
		void* PushBackDefault();

		/*********************************************************************
		 \brief  Appends an element relocated from src (move-constructed,
				 or bit-copied for trivially relocatable types).
		 \param[in] src A live element of this column's type; left in a
				moved-from state for the caller to destroy.
		 \return Pointer to the new element.
		 \note  Only growth can throw, before anything changes.
		 *********************************************************************/
		void* PushBackMoved(void* src);

		/*********************************************************************
		 \brief  Destroys and removes the last element (rollback helper).
		 *********************************************************************/
		void PopBack() noexcept;

		/*********************************************************************
		 \brief  Removes one element by moving the last element into its
				 place (the caller mirrors the swap in its own arrays).
		 *********************************************************************/
		void SwapRemove(std::size_t row) noexcept;

		/*********************************************************************
		 \brief  Appends a relocated copy of element `row` to dst (same
				 component type), carrying its change tick and on/off state.
		 \note  Does not remove the source element; only growth of dst can
				throw, before anything changes.
		 *********************************************************************/
		void MoveElementTo(std::size_t row, ComponentColumn& dst);

		/*********************************************************************
		 \brief  Destroys every element; keeps the allocation.
		 *********************************************************************/
		void Clear() noexcept;

		/*********************************************************************
		 \brief  Grows capacity (by doubling) to at least `required` elements.
		 \param[in] required The minimum capacity needed.
		 *********************************************************************/
		void Grow(std::size_t required);

		const ComponentInfo* m_info = nullptr;
		std::byte* m_data = nullptr;
		std::size_t m_size = 0;
		std::size_t m_capacity = 0;
		std::vector<std::uint64_t> m_changedTicks;   // capacity kept >= m_capacity, so appends never throw here
		// Toggleable columns only (empty otherwise): one on/off byte per row, travelling with the
		// row exactly like its change tick. Capacity kept >= m_capacity, like m_changedTicks.
		std::vector<std::uint8_t> m_enabled;
		std::size_t m_disabledCount = 0;   // zero bytes in m_enabled
	};
}
