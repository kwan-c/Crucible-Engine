/*************************************************************************
* \file:         ComponentColumn.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
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
* \history
*    -v1.0       initial type-erased dynamic array (grow/push/swap-remove/move)
*
*
**************************************************************************/
#pragma once

#include <cstddef>
#include <cassert>
#include <new>

#include "ComponentInfo.h"

namespace Crucible
{

	/*************************************************************************
	 \brief
		A type-erased, contiguous dynamic array holding one component type's
		data for every entity in an Archetype.

	 \details
		Stores raw bytes (m_data), sized/aligned according to its
		ComponentInfo, and uses that info's function pointers to manage
		individual elements - the column itself never needs to be a
		template. One ComponentColumn exists per component type inside each
		Archetype. Every growth/removal/move operation dispatches on
		ComponentInfo::isTriviallyRelocatable/isTriviallyDestructible to
		pick between a fast memcpy/no-op path and a correct
		move-construct+destroy path.

	 \note
		Move-only: copying is deleted because it owns a raw allocation whose
		element lifetimes are managed through function pointers, not T's
		own copy semantics.
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
		 \brief  Raw pointer to the start of the column's backing storage.
		 \return Pointer to element 0, or nullptr if never allocated.
		 \note  The caller must cast to the right type and respect Size()/
				element stride (Info().size).
		 *********************************************************************/
		void* Data() { return m_data; } 

		/*********************************************************************
		 \brief  Pointer to one element's storage.
		 \param[in] row Index of the element to fetch.
		 \return Pointer to that element's bytes.
		 \note  Asserts row < Size() in debug builds; no check in release.
		 *********************************************************************/
		void* At(std::size_t row)
		{
			assert(row < m_size);
			return m_data + row * m_info->size;
		}

		/*********************************************************************
		 \brief
			Ensures the column can hold at least newCapacity elements,
			reallocating and relocating existing elements if needed.

		 \param[in]
			newCapacity The minimum capacity required.

		 \details
			No-op if capacity is already sufficient. Otherwise allocates a
			new correctly-aligned buffer and relocates every existing
			element into it - via memcpy for trivially relocatable types, or
			move-construct-then-destroy otherwise - then frees the old
			buffer.
		 *********************************************************************/
		void Reserve(std::size_t newCapacity);

		/*********************************************************************
		 \brief  Appends one default-constructed element.
		 \return Pointer to the newly constructed element's storage.
		 \details Grows the column first if it is at capacity.
		 *********************************************************************/
		void* PushBackDefault();

		/*********************************************************************
		\brief
		   Reserves storage for one more element without constructing
		   anything in it.

		\return
		   Pointer to the new, uninitialized slot.

		\note
		   The caller MUST construct a valid element into the returned
		   pointer before the column is used again, since SwapRemove/
		   Clear/the destructor assume every slot up to Size() holds a live
		   object.
		*********************************************************************/
		void* PushBackUninitialized();

		/*********************************************************************
		 \brief  Removes one element by swapping the last element into its
				 place.

		 \param[in]
			row Index of the element to remove.

		 \details
			Destroys the element at row (for non-trivial types), then - if
			row wasn't already last - relocates the last element into row's
			slot. Reduces Size() by one.

		 \note
			Changes which element occupies row (unless it was already
			last) - callers tracking entities/rows in parallel (see
			Archetype::RemoveEntity) must apply the same swap to their own
			arrays.
		 *********************************************************************/
		void SwapRemove(std::size_t row);

		/*********************************************************************
		 \brief
			Relocates one element from this column into a new element at the
			end of another column of the same component type.

		 \param[in]
			row Index of the element to move, in this column.
		 \param[in,out]
			dst The destination column; must hold the same component type.
			Gains one new element (appended).

		 \details
			Used by World::MoveEntity when an entity's component set
			changes. Does not remove/destroy the source element - the
			caller removes the source row afterwards (see
			Archetype::RemoveEntity), which is what actually destroys the
			now-moved-from element.

		 \note
			Asserts (debug only) that dst shares this column's ComponentInfo.
		 *********************************************************************/
		void MoveElementTo(std::size_t row, ComponentColumn& dst);

		/*********************************************************************
		\brief
		   Destroys every live element and resets the column to empty,
		   without releasing the underlying allocation.
		*********************************************************************/
		void Clear();

	private:

		/*********************************************************************
		 \brief  Grows capacity (by doubling) to at least `required` elements.
		 \param[in] required The minimum capacity needed.
		 *********************************************************************/
		void Grow(std::size_t required);

		const ComponentInfo* m_info = nullptr;
		std::byte* m_data = nullptr;
		std::size_t m_size = 0;
		std::size_t m_capacity = 0;
	};
}