/*************************************************************************
* \file:         SystemAccess.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          25 September 2026
* \version       1.0
* \brief
*                Declares SystemAccess (which engine components a system
*                reads/writes), SystemAffinity (which thread a system wants
*                to run on) and SystemsConflict(), the rule the scheduler
*                uses to decide which systems could run at the same time.
*
* \details
*                Kept free of World/IECSSystem/Query dependencies so that
*                Query.h can include it (Query.h is itself included by
*                World.h) and so the scheduler's data stays plain.
*                Access is expressed with ComponentSignature masks over the
*                closed, engine-owned component set.
*
* \history
*    -v1.0       initial access/affinity metadata for the system scheduler
*
**************************************************************************/
#pragma once

#include <cstdint>

#include "../Components/ComponentSignature.h"

namespace Crucible
{
	/*************************************************************************
	 \brief
		Which engine components a system reads and writes during Update().

	 \details
		Only direct component access counts (queries, GetComponent).
		CommandBuffer commands are not access - they are applied later, at
		the stage flush, one system at a time.

	 \note
		A system that never declares its access (declared == false) is
		treated as exclusive, so older systems are never considered safe to
		run alongside anything else.
	 *************************************************************************/
	struct SystemAccess
	{
		ComponentSignature reads;
		ComponentSignature writes;
		bool exclusive = false;   // conflicts with every other system
		bool declared = false;    // false => treated as exclusive
	};

	/*************************************************************************
	 \brief
		Which thread a system asks to run on.

	 \note
		Metadata only for now: every system runs serially on the thread that
		calls ECSSystemManager::Update(). A future job system reads this to
		decide where each system may be dispatched.
	 *************************************************************************/
	enum class SystemAffinity : std::uint8_t
	{
		Worker,        // any worker thread
		MainThread,    // must run on the main thread (default)
		RenderThread   // must run on the render thread
	};

	/*************************************************************************
	 \brief
		Whether two systems must not run at the same time.

	 \param[in] a First system's access.
	 \param[in] b Second system's access.
	 \return true if a writes something b reads or writes, b writes
			 something a reads, or either one is exclusive/undeclared.
			 Read/read never conflicts.
	 *************************************************************************/
	inline bool SystemsConflict(const SystemAccess& a, const SystemAccess& b) noexcept
	{
		// Exclusive or undeclared systems conflict with everything.
		if (a.exclusive || b.exclusive || !a.declared || !b.declared) return true;

		// Any overlap involving a write is a conflict; read/read is safe.
		return (a.writes & b.reads).any()
			|| (a.writes & b.writes).any()
			|| (a.reads & b.writes).any();
	}
}
