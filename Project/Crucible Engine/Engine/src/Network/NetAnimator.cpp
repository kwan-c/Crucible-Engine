/*************************************************************************
* \file:         NetAnimator.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief
*                Replicated Animator parameters (see the header).
*
* \history
*    -v1.0       Networking C# Phase 7
*
**************************************************************************/
#include "Network/NetAnimator.h"

#include <bit>

#include "Diagnostics/Logger.h"
#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Network/NetComponents.h"

namespace Crucible
{
	namespace
	{
		std::uint64_t KeyOf(Entity e) noexcept { return (std::uint64_t(e.generation) << 32) | e.index; }

		void WarnUnsynced(const World& world, Entity root, const NetAnimatorLayout& layout, std::unordered_set<std::uint64_t>& warned)
		{
			if (layout.unsynced == 0 || !warned.insert(KeyOf(root)).second) return;
			const NetworkIdentity* identity = world.Read<NetworkIdentity>(root);
			CRU_WARN("[Net] network object {}: {} Animator parameter(s) past the first {} are not synced",
				identity ? identity->id : Net::kInvalidNetworkId, layout.unsynced, NetAnimator::kSlots);
		}
	}

	void BuildNetAnimatorLayout(const World& world, Entity root, const AnimatorResolver& resolver, NetAnimatorLayout& out)
	{
		out.Clear();
		if (!world.IsAlive(root) || !resolver) return;
		std::vector<Entity> order{ root };   // breadth first, the same walk as the authoring checks
		for (std::size_t i = 0; i < order.size(); ++i)
			world.ForEachChild(order[i], [&](Entity child) { order.push_back(child); });
		for (const Entity e : order)
		{
			const AnimatedModel* model = world.Read<AnimatedModel>(e);
			if (model == nullptr || model->controllerAsset == 0) continue;
			std::shared_ptr<const AnimatorController> controller = resolver(model->controllerAsset);
			if (!controller) continue;
			for (const AnimatorParameter& parameter : controller->parameters)
			{
				if (out.slots.size() < NetAnimator::kSlots) out.slots.push_back({ e, &parameter });
				else ++out.unsynced;
			}
			out.pinned.push_back(std::move(controller));
		}
	}

	int FindNetAnimatorSlot(const NetAnimatorLayout& layout, Entity model, std::string_view name, AnimatorParameterType type)
	{
		for (std::size_t i = 0; i < layout.slots.size(); ++i)
		{
			const auto& slot = layout.slots[i];
			if (slot.model == model && slot.parameter->type == type && slot.parameter->name == name) return static_cast<int>(i);
		}
		return -1;
	}

	Entity FindNetworkRoot(const World& world, Entity entity)
	{
		for (Entity e = entity; e.IsValid() && world.IsAlive(e); e = world.GetParent(e))
			if (world.HasComponent<NetworkIdentity>(e)) return e;
		return kNullEntity;
	}

	AnimatorParameterType ParameterTypeOf(ScriptAnimatorOperation operation) noexcept
	{
		switch (operation)
		{
		case ScriptAnimatorOperation::Bool:    return AnimatorParameterType::Bool;
		case ScriptAnimatorOperation::Float:   return AnimatorParameterType::Float;
		case ScriptAnimatorOperation::Integer: return AnimatorParameterType::Integer;
		default:                               return AnimatorParameterType::Trigger;
		}
	}

	std::uint32_t EncodeAnimatorValue(const ScriptAnimatorCommand& command) noexcept
	{
		switch (command.operation)
		{
		case ScriptAnimatorOperation::Bool:    return command.integer != 0 ? 1u : 0u;
		case ScriptAnimatorOperation::Float:   return std::bit_cast<std::uint32_t>(command.number);
		case ScriptAnimatorOperation::Integer: return static_cast<std::uint32_t>(command.integer);
		default:                               return 0u;
		}
	}

	ScriptAnimatorCommand DecodeAnimatorValue(const AnimatorParameter& parameter, std::uint32_t bits)
	{
		ScriptAnimatorCommand command;
		command.name = parameter.name;
		switch (parameter.type)
		{
		case AnimatorParameterType::Bool:    command.operation = ScriptAnimatorOperation::Bool;    command.integer = bits != 0 ? 1 : 0; break;
		case AnimatorParameterType::Float:   command.operation = ScriptAnimatorOperation::Float;   command.number = std::bit_cast<float>(bits); break;
		case AnimatorParameterType::Integer: command.operation = ScriptAnimatorOperation::Integer; command.integer = static_cast<std::int32_t>(bits); break;
		case AnimatorParameterType::Trigger: command.operation = ScriptAnimatorOperation::Trigger; break;
		}
		return command;
	}

	bool NetAnimatorBinding::SetParameter(uint32_t index, uint32_t generation, const ScriptAnimatorCommand& command)
	{
		const Net::NetRole role = m_role ? m_role() : Net::NetRole::Offline;
		const Entity model{ index, generation };
		if (role == Net::NetRole::Offline || !m_world.IsAlive(model)) return m_target.SetParameter(index, generation, command);

		const Entity root = FindNetworkRoot(m_world, model);
		if (!root.IsValid() || !m_world.HasComponent<NetAnimator>(root))   // not a network object, or it does not sync "Animator"
			return m_target.SetParameter(index, generation, command);
		BuildNetAnimatorLayout(m_world, root, m_resolver, m_layout);
		WarnUnsynced(m_world, root, m_layout, m_warned);
		const int slot = FindNetAnimatorSlot(m_layout, model, command.name, ParameterTypeOf(command.operation));
		if (slot < 0) return m_target.SetParameter(index, generation, command);   // past the limit: local
		if (role == Net::NetRole::Client) return false;                            // the host's to set

		if (!m_target.SetParameter(index, generation, command)) return false;
		if (command.operation == ScriptAnimatorOperation::ResetTrigger) return true;   // local: clients may have played it
		NetAnimator* sync = m_world.Write<NetAnimator>(root);
		if (sync == nullptr) return true;
		const std::uint32_t bit = 1u << static_cast<std::uint32_t>(slot);
		std::uint32_t& value = sync->slots[static_cast<std::size_t>(slot)];
		value = command.operation == ScriptAnimatorOperation::Trigger ? value + 1u : EncodeAnimatorValue(command);
		sync->written |= bit;
		return true;
	}

	std::uint32_t ApplyNetAnimators(World& world, const AnimatorResolver& resolver, IScriptAnimatorBinding& target,
		NetAnimatorLayout& scratch, std::unordered_set<std::uint64_t>& warned)
	{
		std::uint32_t writes = 0;
		world.CreateQuery<NetAnimator, const NetworkIdentity>().ForEach([&](Entity root, NetAnimator& sync, const NetworkIdentity&) {
			if (!sync.received) return;   // no record yet: the host's values are not known
			BuildNetAnimatorLayout(world, root, resolver, scratch);
			WarnUnsynced(world, root, scratch, warned);
			for (std::size_t i = 0; i < scratch.slots.size(); ++i)
			{
				const auto& slot = scratch.slots[i];
				if ((sync.written & (1u << i)) == 0) continue;   // the controller's initial value
				const std::uint32_t bits = sync.slots[i];
				if (slot.parameter->type == AnimatorParameterType::Trigger)
				{
					if (!sync.primed || sync.firedSeen[i] == bits) continue;
					sync.firedSeen[i] = bits;
				}
				if (target.SetParameter(slot.model.index, slot.model.generation, DecodeAnimatorValue(*slot.parameter, bits))) ++writes;
			}
			if (!sync.primed)   // the first record: its trigger counts are the past
			{
				sync.firedSeen = sync.slots;
				sync.primed = true;
			}
		});
		return writes;
	}
}
