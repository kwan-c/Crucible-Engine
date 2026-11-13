/*************************************************************************
* \file:         EntityScriptSystem.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          13 November 2026
* \version       1.1
* \brief         Implementation - see EntityScriptSystem.h.
*
**************************************************************************/
#include "Scripting/EntityScriptSystem.h"
#include "Scripting/ScriptRuntime.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/World.h"
#include "Scene/SceneManager.h"
#include "Systems/PlayMode.h"
#include "Diagnostics/Logger.h"
#include "AssetPipe/Assetmanager.h"
#include <algorithm>
#include <vector>

namespace Crucible
{
	EntityScriptSystem::EntityScriptSystem(ScriptRuntime& runtime, World& world, PlayMode& play,
	                                       SceneManager& scenes)
	    : m_runtime(runtime)
	    , m_world(world)
	    , m_play(play)
	    , m_scenes(scenes)
	{
	}

	void EntityScriptSystem::Init()
	{
		// The callbacks below may outlive this system; `alive` expires when
		// Shutdown() resets m_lifetime, so they stop touching `this`.
		const std::weak_ptr<int> alive = m_lifetime;
		// World hooks never run managed code or mutate component structure.
		// They only record what happened; Update() does the actual cleanup.
		m_world.OnComponentRemoved<ScriptComponent>(
		    [this, alive](World&, Entity entity, const ScriptComponent&)
		    {
			    if (!alive.expired())
				    m_removed.insert(Key(entity));
		    });
		m_scenes.OnSceneChanged(
		    [this, alive]
		    {
			    if (!alive.expired())
				    m_sceneChanged = true;
		    });
#if CRUCIBLE_WITH_MONO
		// Lets C# GetComponent<SomeScript>() reach the scripts this system owns.
		ScriptBindings::EntityContext::SetScriptLookup(&EntityScriptSystem::LookupScripts, this);
#endif
	}

	void EntityScriptSystem::Describe(SystemDesc& desc) const
	{
		// Always on for cleanup in Edit/Pause; callback execution is Play-only.
		desc.Exclusive().Affinity(SystemAffinity::MainThread);
	}

	void EntityScriptSystem::Release(Record& record)
	{
		if (record.instance && m_runtime.IsRunning() && !m_runtime.DestroyInstance(record.instance))
		{
			record.error = m_runtime.LastError();
			CRU_USER_ERROR("Script {}.{} cleanup failed: {}", record.definition.nameSpace,
			               record.definition.className, record.error);
		}
		record.instance = {};
	}

	void EntityScriptSystem::ReleaseAll(std::vector<Record>& records)
	{
		for (auto it = records.rbegin(); it != records.rend(); ++it)
			Release(*it);
	}

	void EntityScriptSystem::StopAll()
	{
		for (auto it = m_records.rbegin(); it != m_records.rend(); ++it)
			ReleaseAll(it->second);
		m_records.clear();
		m_removed.clear();
	}

	EntityScriptStatus EntityScriptSystem::Status(Entity entity, std::size_t slot) const
	{
		const auto found = m_records.find(Key(entity));
		if (found == m_records.end() || slot >= found->second.size())
			return {};
		const Record& record = found->second[slot];
		return {record.instance, record.error};
	}

	void EntityScriptSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx)
	{
		// --- Step 0: after ScriptRuntime::ReloadScripts every old instance handle is
		// stale (the runtime already destroyed the objects), so just forget them.
		if (m_reloadSeen != m_runtime.ReloadCount())
		{
			m_records.clear();
			m_removed.clear();
			m_reloadSeen = m_runtime.ReloadCount();
		}

		// --- Step 1: reset everything on Stop or a new Play session.
		const bool sessionChanged = m_session != m_play.SessionRevision() || !m_play.InSession();
		if (sessionChanged)
		{
			StopAll();
#if CRUCIBLE_WITH_MONO
			ScriptBindings::EntityContext::ClearDestroyQueue(); // destroys belong to the session that asked
			ScriptBindings::EntityContext::ClearOneShotAudio();
			ScriptBindings::EntityContext::ClearUIEvents();
			ScriptBindings::EntityContext::ClearCollisionEvents();
			ScriptBindings::EntityContext::ClearActiveStates();
			ScriptBindings::EntityContext::ResetPlayClock();
#endif
		}
		// A level switch (Scene.Load) during play: only the old scene's scripts end.
		// DontDestroyOnLoad entities survived the load with the same handles, so their
		// scripts keep running, state and all. Time.ElapsedTime keeps counting, like
		// Unity's Time.time.
		else if (m_sceneChanged)
		{
			for (auto it = m_records.begin(); it != m_records.end();)
			{
				const Entity entity{ static_cast<uint32_t>(it->first & 0xFFFFFFFFu),
				                     static_cast<uint32_t>(it->first >> 32) };
				if (world.IsAlive(entity))
				{
					++it;
					continue;
				}
				ReleaseAll(it->second);
				it = m_records.erase(it);
			}
#if CRUCIBLE_WITH_MONO
			ScriptBindings::EntityContext::PruneDestroyQueue(world);
			ScriptBindings::EntityContext::ClearOneShotAudio();
			ScriptBindings::EntityContext::ClearUIEvents();
			ScriptBindings::EntityContext::ClearCollisionEvents();
			ScriptBindings::EntityContext::PruneActiveStates(world);
#endif
		}
		m_sceneChanged = false;
		m_session = m_play.SessionRevision();

#if CRUCIBLE_WITH_MONO
		// A new play session: the engine's loading screen joins it before any script runs,
		// so its script starts this frame with the rest (step 4).
		if (sessionChanged && m_play.InSession())
			SpawnLoadingScreen();
#endif

		// --- Step 2: destroy scripts whose ScriptComponent was removed.
		for (const auto key : m_removed)
		{
			const auto found = m_records.find(key);
			if (found != m_records.end())
			{
				ReleaseAll(found->second);
				m_records.erase(found);
			}
		}
		m_removed.clear();

		// Nothing runs in Edit mode.
		if (!m_play.InSession())
			return;

#if CRUCIBLE_WITH_MONO
		// C# Time: count this frame before any OnCreate / OnUpdate sees the clock.
		if (m_play.State() == PlayState::Playing)
			ScriptBindings::EntityContext::AdvancePlayClock(world.DeltaTime());
#endif

		// --- Step 3: copy every ScriptComponent out of the ECS.
		// Copy before callbacks: no managed execution while a query pins columns.
		// Every script on the entity, in order. Switched off (World::SetComponentEnabled)
		// pauses all of them exactly like each one's own `enabled` field: the instances
		// and their state are kept, not destroyed and recreated.
		std::vector<std::pair<Entity, std::vector<ScriptEntry>>> definitions;
		world.BuildQuery<const ScriptComponent>().IncludeDisabled().Build().ForEach(
		    [&](Entity entity, const ScriptComponent& component)
		    {
			    definitions.emplace_back(entity, component.scripts);
			    if (!world.IsComponentActive<ScriptComponent>(entity))
				    for (ScriptEntry& script : definitions.back().second)
					    script.enabled = false;
		    });

		// --- Step 4: bring each entity's live scripts in line with its component.
		std::set<uint64_t> present;                           // entities that still have a ScriptComponent
		std::vector<std::pair<Entity, std::size_t>> created; // (entity, slot) still waiting for OnCreate (step 5)
		for (const auto& [entity, scripts] : definitions)
		{
			const auto key = Key(entity);
			present.insert(key);
			std::vector<Record> old;
			if (const auto found = m_records.find(key); found != m_records.end())
				old = std::move(found->second);

			// 4a. Match each entry to the record of the same script, preferring the same
			// place in the list, so reordering or removing one script keeps the others'
			// instances (and their state). Duplicates of one class pair up in order.
			std::vector<Record> next(scripts.size());
			std::vector<bool> claimed(old.size(), false);
			for (std::size_t slot = 0; slot < scripts.size(); ++slot)
			{
				const ScriptEntry& definition = scripts[slot];
				std::size_t match = old.size();
				if (slot < old.size() && !claimed[slot] && SameScript(old[slot].definition, definition))
					match = slot;
				for (std::size_t i = 0; match == old.size() && i < old.size(); ++i)
					if (!claimed[i] && SameScript(old[i].definition, definition))
						match = i;

				if (match == old.size())
				{
					next[slot].definition = definition; // new (or changed class): created below
					continue;
				}
				claimed[match] = true;
				Record& record = next[slot] = std::move(old[match]);

				// Same class, but the Inspector changed some field values: collect only the
				// fields that differ and write them into the live C# object.
				if (record.definition.fields != definition.fields)
				{
					std::vector<ScriptFieldValue> changes;
					const auto& previous = record.definition.fields;
					for (const auto& field : definition.fields)
					{
						const auto saved = std::find_if(previous.begin(), previous.end(),
						                                [&](const auto& value) { return SameScriptField(field, value); });
						if (saved == previous.end() || !(*saved == field))
							changes.push_back(field);
					}
					ScriptInstanceInfo state;
					if (record.instance && m_runtime.GetInstanceInfo(record.instance, state) &&
					    state.state != ScriptInstanceState::Faulted)
					{
						if (!m_runtime.SetEntityFields(record.instance, changes))
							record.error = m_runtime.LastError();
						else
							record.error.clear();
					}
					record.definition.fields = definition.fields;
				}
			}

			// 4b. Scripts that were removed or changed class: OnDestroy, last first.
			for (std::size_t i = old.size(); i-- > 0;)
				if (!claimed[i])
					Release(old[i]);

			// 4c. Scripts not made yet: create them while Playing - switched off ones too, as
			// in Unity, so GetComponent finds them and another script can switch them on (a
			// failed one is not retried every frame). OnCreate waits for step 5, after every
			// new script exists, and for a script that is off, until it is first switched on.
			for (std::size_t slot = 0; slot < next.size(); ++slot)
			{
				Record& record = next[slot];
				const ScriptEntry& definition = scripts[slot];
				if (!record.attempted && m_play.State() == PlayState::Playing && !definition.className.empty())
				{
					record.definition = definition;
					record.attempted = true;
					ScriptAssemblyHandle assembly;
					if (!m_runtime.LoadAssembly(definition.assembly, assembly) ||
					    !m_runtime.CreateEntityInstance(assembly, definition.nameSpace, definition.className,
					                                    entity.index, entity.generation, record.instance,
					                                    definition.fields, true))
					{
						// The record stays, so the error is shown and creation is not retried.
						record.error = m_runtime.LastError();
						CRU_USER_ERROR("Script {} on entity {}:{} could not start: {}", definition.className, entity.index,
						               entity.generation, record.error);
					}
				}
				// Made but never started, and on now (just created, or switched on since):
				// OnCreate in step 5, then OnUpdate this same frame.
				if (record.instance && !record.started && definition.enabled && m_play.State() == PlayState::Playing)
					created.emplace_back(entity, slot);
			}

			if (next.empty())
				m_records.erase(key);
			else
				m_records[key] = std::move(next);
		}

		// --- Step 5: OnCreate for every script that starts this frame (created above while
		// on, or switched on for the first time), in entity then list order. Every script
		// exists by now, so one script's OnCreate can find another's (GetComponent), on its
		// own entity or any other.
		for (const auto& [entity, slot] : created)
		{
			const auto found = m_records.find(Key(entity));
			if (found == m_records.end() || slot >= found->second.size())
				continue;
			Record& record = found->second[slot];
			if (!record.instance || record.started)
				continue;
			// OnCreate runs C#, which never changes m_records directly (removals and scene
			// changes are only noted by the hooks in Init), so `record` stays valid.
			if (!m_runtime.StartEntityInstance(record.instance))
			{
				// The runtime already destroyed the instance; keep the error.
				record.error = m_runtime.LastError();
				record.instance = {};
				CRU_USER_ERROR("Script {} on entity {}:{} could not start: {}", record.definition.className, entity.index,
				               entity.generation, record.error);
			}
			else
				record.started = true;
		}

#if CRUCIBLE_WITH_MONO
		// --- Step 5b: last frame's physics events (to the two entities' scripts), then this frame's UI events (clicks, slider drags, custom actions) to every
		// running script's OnButtonClicked / OnSliderChanged / OnUIAction, before OnUpdate.
		// UISystem sent them at the end of PreUpdate; only while Playing.
		if (m_play.State() == PlayState::Playing)
		{
			// Physics events from last frame's step (ECSPhysicsSystem runs after scripts): to the
			// scripts on BOTH entities, Unity's rule. The normal is flipped for `b` so it always
			// points from the receiving entity toward the other.
			for (const auto& contact : ScriptBindings::EntityContext::CollisionEvents())
			{
				const float flipped[3] = {-contact.normal[0], -contact.normal[1], -contact.normal[2]};
				const std::pair<Entity, Entity> receivers[2] = {{contact.a, contact.b}, {contact.b, contact.a}};
				for (int side = 0; side < 2; ++side)
				{
					const auto [self, other] = receivers[side];
					const auto found = m_records.find(Key(self));
					if (found == m_records.end())
						continue;
					// Every script on the entity, in order (Unity sends to all of them).
					for (Record& record : found->second)
					{
						if (!record.instance || !record.started || !record.definition.enabled)
							continue;
						if (!m_runtime.DispatchPhysicsEvent(record.instance, static_cast<int32_t>(contact.kind), other.index,
						                                    other.generation, contact.point,
						                                    side == 0 ? contact.normal : flipped, contact.impactSpeed))
						{
							record.error = m_runtime.LastError();
							CRU_USER_ERROR("Script {}.{} stopped in a physics callback: {}", record.definition.nameSpace,
							               record.definition.className, record.error);
						}
					}
				}
			}
			ScriptBindings::EntityContext::ClearCollisionEvents();

			for (const auto& event : ScriptBindings::EntityContext::UIEvents())
			{
				for ([[maybe_unused]] auto& [key, records] : m_records)
				{
					for (Record& record : records)
					{
						if (!record.instance || !record.started || !record.definition.enabled)
							continue;
						if (!m_runtime.DispatchUIEvent(record.instance, static_cast<int32_t>(event.kind), event.source.index,
						                               event.source.generation, event.value, event.actionId))
						{
							record.error = m_runtime.LastError();
							CRU_USER_ERROR("Script {}.{} stopped in a UI callback: {}", record.definition.nameSpace,
							               record.definition.className, record.error);
						}
					}
				}
			}
		}
#endif

		// --- Step 6: apply enable/disable and call OnUpdate, entity by entity, each
		// entity's scripts in list order (Unity's component order).
		for (const auto& [entity, scripts] : definitions)
		{
			const auto found = m_records.find(Key(entity));
			if (found == m_records.end())
				continue;
			for (std::size_t slot = 0; slot < found->second.size() && slot < scripts.size(); ++slot)
			{
				Record& record = found->second[slot];
				const bool enabled = scripts[slot].enabled;
				record.definition.enabled = enabled;
				if (!record.instance || !record.started)
					continue; // not made yet, failed (retried only after a script/session change), or off since Play
				ScriptInstanceInfo info;
				if (!m_runtime.GetInstanceInfo(record.instance, info))
				{
					record.error = m_runtime.LastError();
					record.instance = {};
					continue;
				}
				if (info.state == ScriptInstanceState::Faulted)
				{
					// A callback threw earlier; the runtime already cleaned it up.
					record.error = info.lastError;
					continue;
				}
				if ((info.state == ScriptInstanceState::Running) != enabled)
					m_runtime.SetInstanceEnabled(record.instance, enabled);
				if (m_play.State() == PlayState::Playing && enabled &&
				    !m_runtime.UpdateEntityInstance(record.instance, world.DeltaTime()))
				{
					record.error = m_runtime.LastError();
					CRU_USER_ERROR("Script {} on entity {}:{} stopped: {}", record.definition.className, entity.index,
					               entity.generation, record.error);
				}
			}
		}

		// --- Step 6b: Entity.Destroy requests that are due (end of frame, like Unity),
		// including Audio.PlayClipAtPoint entities whose sound has finished.
		if (m_play.State() == PlayState::Playing)
		{
#if CRUCIBLE_WITH_MONO
			ScriptBindings::EntityContext::UpdateOneShotAudio(world.DeltaTime());
#endif
			ApplyDestroys(world);
		}

		// --- Step 7: destroy scripts whose entity no longer has a ScriptComponent
		// (e.g. the entity itself was destroyed).
		for (auto it = m_records.begin(); it != m_records.end();)
		{
			if (!present.contains(it->first))
			{
				ReleaseAll(it->second);
				it = m_records.erase(it);
			}
			else
				++it;
		}

#if CRUCIBLE_WITH_MONO
		// --- Step 8: this frame's UI clicks have been seen by every script; forget
		// them so UIButton.WasClicked etc. are true for one frame only.
		ScriptBindings::EntityContext::ClearUIEvents();
#endif
	}

	void EntityScriptSystem::SpawnLoadingScreen()
	{
#if CRUCIBLE_WITH_MONO
		// Found by path, so no scene or script has to reference it. Cook copies every prefab
		// into Resources with its .meta, so the packaged game finds it the same way.
		AssetManager* assets = ScriptBindings::EntityContext::GetAssets();
		const AssetEntry* entry = assets ? assets->Find(SceneManager::kLoadingScreenPrefab) : nullptr;
		if (!entry)
		{
			CRU_USER_WARN("No loading screen: '{}' is not in the project - scene changes will switch instantly",
			              SceneManager::kLoadingScreenPrefab);
			return;
		}
		const Entity root = ScriptBindings::EntityContext::SpawnPrefab(entry->guid.m_Value);
		if (!root.IsValid())
		{
			CRU_USER_WARN("No loading screen: '{}' could not be spawned - scene changes will switch instantly",
			              SceneManager::kLoadingScreenPrefab);
			return;
		}
		// Kept through every scene change. Its script claims the scene changes (SetLoadGate)
		// once it starts; until then a scene change is instant.
		m_scenes.DontDestroyOnLoad(root);
#endif
	}

	void EntityScriptSystem::Shutdown() noexcept
	{
		try
		{
			StopAll();
		}
		catch (...)
		{
		}
		m_lifetime.reset(); // expires `alive` in the Init() callbacks
#if CRUCIBLE_WITH_MONO
		ScriptBindings::EntityContext::SetScriptLookup(nullptr, nullptr);
#endif
	}

	void EntityScriptSystem::ApplyDestroys(World& world)
	{
#if CRUCIBLE_WITH_MONO
		std::vector<Entity> due;
		ScriptBindings::EntityContext::TakeDueDestroys(world.DeltaTime(), due);
		// A few rounds, for OnDestroy calls that destroy something else; anything
		// still queued after that waits for the next frame.
		for (int round = 0; round < 8 && !due.empty(); ++round)
		{
			for (const Entity root : due)
			{
				if (!world.IsAlive(root))
					continue; // already gone, e.g. destroyed with its parent
				// The subtree, parents before children.
				std::vector<Entity> subtree{root};
				for (std::size_t i = 0; i < subtree.size(); ++i)
				{
					const HierarchyNode* node = world.Read<HierarchyNode>(subtree[i]);
					for (Entity child = node ? node->firstChild : Entity{}; child.IsValid();)
					{
						subtree.push_back(child);
						const HierarchyNode* childNode = world.Read<HierarchyNode>(child);
						child = childNode ? childNode->nextSibling : Entity{};
					}
				}
				// OnDestroy children first, while every entity still exists.
				for (auto it = subtree.rbegin(); it != subtree.rend(); ++it)
				{
					const auto found = m_records.find(Key(*it));
					if (found != m_records.end())
					{
						// Moved out first: OnDestroy runs C#, and the entry is gone either way.
						std::vector<Record> scripts = std::move(found->second);
						m_records.erase(found);
						ReleaseAll(scripts);
					}
				}
				if (world.IsAlive(root)) // an OnDestroy could have destroyed it already
					world.DestroyEntity(root);
			}
			due.clear();
			ScriptBindings::EntityContext::TakeDueDestroys(0.0f, due); // queued by those OnDestroys
		}
#else
		(void)world;
#endif
	}

	void EntityScriptSystem::LookupScripts(void* self, Entity entity,
	                                       std::vector<ScriptBindings::EntityContext::RunningScript>& out)
	{
		// Every running script on the entity, in list order, with its place in the list
		// (records line up with ScriptComponent::scripts). C# GetComponent<T> takes the first
		// that is a T, GetComponents<T> all of them; EntityScript.Enabled finds its own slot.
		auto& system = *static_cast<EntityScriptSystem*>(self);
		const auto found = system.m_records.find(Key(entity));
		if (found == system.m_records.end())
			return;
		for (std::size_t slot = 0; slot < found->second.size(); ++slot)
		{
			const Record& record = found->second[slot];
			uint32_t handle = 0;
			if (record.instance && system.m_runtime.GetInstanceGcHandle(record.instance, handle) && handle)
				out.push_back({handle, static_cast<uint32_t>(slot)});
		}
	}
}
