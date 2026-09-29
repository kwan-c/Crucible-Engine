/*************************************************************************
* \file:         SystemAccess.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          25 September 2026
* \version       1.1
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
*    -v1.1       thread-local running-system context and the
*                CheckDeclaredAccess/CheckStructuralAccess enforcement used
*                by World and Query
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#include "EngineExport.h"
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

	/*************************************************************************
	 \brief
		The system currently running on this thread and the access it
		declared. Set by ECSSystemManager around each system's Update().
	 *************************************************************************/
	struct ActiveSystemAccess
	{
		const SystemAccess* access = nullptr;
		const char* systemName = "";
	};

	/*************************************************************************
	 \brief  The system running on the calling thread, or nullptr outside
			 any system's Update().
	 \note   Thread-local, so it stays correct once systems run on worker
			 threads.
	 *************************************************************************/
	ENGINE_API const ActiveSystemAccess* CurrentSystemAccess() noexcept;

	namespace Detail
	{
		// Only declared, non-exclusive systems are held to their declaration;
		// undeclared (legacy) and exclusive systems may touch anything.
		inline const ActiveSystemAccess* RestrictedSystem() noexcept
		{
			const ActiveSystemAccess* active = CurrentSystemAccess();
			if (!active || !active->access) return nullptr;
			if (!active->access->declared || active->access->exclusive) return nullptr;
			return active;
		}

		inline std::string ComponentIdList(const ComponentSignature& signature)
		{
			std::string ids;
			for (std::size_t id = 0; id < signature.size(); ++id)
			{
				if (signature.test(id)) ids += (ids.empty() ? "" : ",") + std::to_string(id);
			}
			return ids;
		}
	}

	/*************************************************************************
	 \brief  Throws std::logic_error if the running system touches
			 components it did not declare in Describe().
	 \param[in] reads  Components about to be read.
	 \param[in] writes Components about to be written.
	 \param[in] operation What is being done, for the message.
	 *************************************************************************/
	inline void CheckDeclaredAccess(const ComponentSignature& reads, const ComponentSignature& writes, const char* operation)
	{
		const ActiveSystemAccess* active = Detail::RestrictedSystem();
		if (!active) return;

		// A write needs a declared write; a read is covered by a declared read or write.
		const ComponentSignature missingWrites = writes & ~active->access->writes;
		const ComponentSignature missingReads = reads & ~(active->access->reads | active->access->writes);
		if (missingWrites.none() && missingReads.none()) return;

		std::string message = std::string("ECS access violation: system '") + active->systemName + "' " + operation;
		if (missingWrites.any()) message += " writes undeclared component(s) " + Detail::ComponentIdList(missingWrites);
		if (missingReads.any()) message += " reads undeclared component(s) " + Detail::ComponentIdList(missingReads);
		throw std::logic_error(message + " - declare them in Describe()");
	}

	/*************************************************************************
	 \brief  Throws std::logic_error if a declared (parallel-schedulable)
			 system makes a direct structural change instead of using its
			 CommandBuffer.
	 \param[in] operation The World function being called.
	 *************************************************************************/
	inline void CheckStructuralAccess(const char* operation)
	{
		const ActiveSystemAccess* active = Detail::RestrictedSystem();
		if (!active) return;

		throw std::logic_error(std::string("ECS access violation: system '") + active->systemName + "' called " + operation +
			" directly - systems that declare their access must make structural changes through their CommandBuffer");
	}
}
