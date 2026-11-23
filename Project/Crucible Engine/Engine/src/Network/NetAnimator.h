/*************************************************************************
* \file:         NetAnimator.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief
*                Replicated Animator parameters (Networking C# Phase 7): the
*                slot layout of a network object, the Animator binding that
*                records the host's writes, and the client side that hands
*                the values to each client's own animator.
*
* \details
*                What is synced is the parameters, not poses: every machine
*                runs the same controller on the same model, so with the
*                host's parameter values a client's state machine goes
*                through the host's states (one snapshot behind, like the
*                movement it goes with).
*
*                Layout (BuildNetAnimatorLayout): the object's animated
*                models - an AnimatedModel with a controller - taken breadth
*                first from the root (World::ForEachChild order), each
*                model's parameters in its controller's order, up to
*                NetAnimator::kSlots. Both machines build the object from
*                the same prefab or scene file, so they get the same layout.
*                Parameters past the limit are not synced (and stay usable
*                locally on every machine).
*
*                Host (NetAnimatorBinding): C# Animator calls go through it
*                to the animation system. A write the animation system
*                accepts on a synced parameter is also stored in the root's
*                NetAnimator: the value, or for a Trigger one more fire.
*                ResetTrigger stays local. On a client a synced parameter
*                belongs to the host, so a script's write is refused
*                (false), as NetVars refuses it.
*
*                Client (ApplyNetAnimators, run by NetAnimatorSystem every
*                frame before the animation system): every value the host
*                has set is given to the client's animator again (cheap, and
*                right again after an animator restart); a Trigger fires
*                once each time its count changes. The counts of the first
*                record are only noted, so a joining client does not replay
*                old triggers. Two fires between snapshots play once.
*
*                Nothing here knows about gameplay: which parameters exist
*                and what they mean is the controller's business.
*
* \history
*    -v1.0       Networking C# Phase 7
*
**************************************************************************/
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_set>
#include <vector>

#include "Animation/AnimatorController.h"
#include "ECS/Core/ECSCore.h"
#include "EngineExport.h"
#include "Network/NetTypes.h"
#include "Scripting/ScriptAnimatorBinding.h"

namespace Crucible
{
	class World;

	/// A controller by asset id (ResourceManager::GetAnimatorController), or null.
	using AnimatorResolver = std::function<std::shared_ptr<const AnimatorController>(std::uint64_t controllerAsset)>;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251 4275)   // std members, and an interface base, of exported types: Engine and its users share one toolchain
#endif

	/// Which parameter of which model each NetAnimator slot is.
	struct ENGINE_API NetAnimatorLayout
	{
		struct Slot
		{
			Entity                    model;       // the entity with the AnimatedModel
			const AnimatorParameter*  parameter;   // inside a pinned controller
		};
		std::vector<Slot> slots;                                           // at most NetAnimator::kSlots
		std::uint32_t     unsynced = 0;                                    // parameters past the limit
		std::vector<std::shared_ptr<const AnimatorController>> pinned;    // keeps `parameter` valid

		void Clear() { slots.clear(); unsynced = 0; pinned.clear(); }
	};

	/// Fills `out` for the object whose root is `root` (see the file comment). Models whose
	/// controller cannot be resolved take no slots.
	ENGINE_API void BuildNetAnimatorLayout(const World& world, Entity root, const AnimatorResolver& resolver, NetAnimatorLayout& out);

	/// The slot of `model`'s parameter `name` of `type`, or -1 if it is not synced.
	ENGINE_API int FindNetAnimatorSlot(const NetAnimatorLayout& layout, Entity model, std::string_view name, AnimatorParameterType type);

	/// `entity` or its nearest ancestor with a NetworkIdentity (a network object's root), or
	/// kNullEntity.
	ENGINE_API Entity FindNetworkRoot(const World& world, Entity entity);

	/// The parameter type a script call writes (ResetTrigger: Trigger).
	ENGINE_API AnimatorParameterType ParameterTypeOf(ScriptAnimatorOperation operation) noexcept;

	/// A Bool, Integer or Float write as a slot value (Bool 0/1, Integer's bits, Float's bits).
	ENGINE_API std::uint32_t EncodeAnimatorValue(const ScriptAnimatorCommand& command) noexcept;

	/// The command that sets `parameter` to slot value `bits` (Trigger: fire it).
	ENGINE_API ScriptAnimatorCommand DecodeAnimatorValue(const AnimatorParameter& parameter, std::uint32_t bits);

	/*************************************************************************
	 \brief
		Sits between C# Animator and the animation system (both are
		IScriptAnimatorBinding): forwards every call, and on a network
		object records the host's writes in the root's NetAnimator.
	 \details
		`role` says what this machine is right now (the session's Role()).
		Offline, or an entity outside a network object that replicates
		"Animator", or a parameter past the slot limit: exactly the
		animation system's answer.
	 *************************************************************************/
	class ENGINE_API NetAnimatorBinding final : public IScriptAnimatorBinding
	{
	public:
		NetAnimatorBinding(IScriptAnimatorBinding& target, World& world, AnimatorResolver resolver,
			std::function<Net::NetRole()> role)
			: m_target(target), m_world(world), m_resolver(std::move(resolver)), m_role(std::move(role)) {}

		bool SetParameter(uint32_t index, uint32_t generation, const ScriptAnimatorCommand& command) override;

	private:
		IScriptAnimatorBinding&        m_target;
		World&                         m_world;
		AnimatorResolver               m_resolver;
		std::function<Net::NetRole()>  m_role;
		NetAnimatorLayout              m_layout;    // scratch
		std::unordered_set<std::uint64_t> m_warned;   // roots already warned about unsynced parameters
	};

	/// Client: hands every root's NetAnimator values to `target` (the animation system), see the
	/// file comment. Roots without a record yet are skipped. Returns how many writes were made.
	/// `warned` remembers the roots already warned about unsynced parameters.
	ENGINE_API std::uint32_t ApplyNetAnimators(World& world, const AnimatorResolver& resolver, IScriptAnimatorBinding& target,
		NetAnimatorLayout& scratch, std::unordered_set<std::uint64_t>& warned);

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
