/*************************************************************************
* \file:         EntityScriptSystem.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          30 October 2026
* \version       1.0
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
		ScriptBindings::EntityContext::SetScriptLookup(&EntityScriptSystem::LookupScript, this);
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

	void EntityScriptSystem::StopAll()
	{
		for (auto it = m_records.rbegin(); it != m_records.rend(); ++it)
			Release(it->second);
		m_records.clear();
		m_removed.clear();
	}

	EntityScriptStatus EntityScriptSystem::Status(Entity entity) const
	{
		const auto found = m_records.find(Key(entity));
		if (found == m_records.end())
			return {};
		return {found->second.instance, found->second.error};
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
				Release(it->second);
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
				Release(found->second);
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
		std::vector<std::pair<Entity, ScriptComponent>> definitions;
		// Switched off (World::SetComponentEnabled) pauses the script exactly like its own
		// `enabled` field: the instance and its state are kept, not destroyed and recreated.
		world.BuildQuery<const ScriptComponent>().IncludeDisabled().Build().ForEach(
		    [&](Entity entity, const ScriptComponent& script)
		    {
			    definitions.emplace_back(entity, script);
			    if (!world.IsComponentActive<ScriptComponent>(entity))
				    definitions.back().second.enabled = false;
		    });

		// --- Step 4: bring each entity's live script in line with its component.
		std::set<uint64_t> present; // entities that still have a ScriptComponent
		std::vector<Entity> created; // new scripts still waiting for OnCreate (step 5)
		for (const auto& [entity, definition] : definitions)
		{
			const auto key = Key(entity);
			present.insert(key);
			auto found = m_records.find(key);

			// 4a. Existing script: has the class changed, or only field values?
			if (found != m_records.end())
			{
				auto old = found->second.definition;
				old.enabled = definition.enabled; // enable changes retain the managed instance
				old.fields = definition.fields;   // public field edits are not a class change
				if (!(old == definition))
				{
					// Different class/assembly: throw the old instance away; it is
					// recreated below.
					Release(found->second);
					m_records.erase(found);
					found = m_records.end();
				}
				else if (found->second.definition.fields != definition.fields)
				{
					// Same class, but the Inspector changed some field values:
					// collect only the fields that differ and write them into the
					// live C# object.
					std::vector<ScriptFieldValue> changes;
					for (const auto& field : definition.fields)
					{
						const auto& previous = found->second.definition.fields;
						const auto saved = std::find_if(previous.begin(), previous.end(),
						                                [&](const auto& value)
						                                {
							                                return SameScriptField(field, value);
						                                });
						if (saved == previous.end() || !(*saved == field))
							changes.push_back(field);
					}
					ScriptInstanceInfo state;
					if (found->second.instance && m_runtime.GetInstanceInfo(found->second.instance, state) &&
					    state.state != ScriptInstanceState::Faulted)
					{
						if (!m_runtime.SetEntityFields(found->second.instance, changes))
							found->second.error = m_runtime.LastError();
						else
							found->second.error.clear();
					}
					found->second.definition.fields = definition.fields;
				}
			}

			// 4b. No script yet: create one (only while Playing, enabled and assigned).
			// OnCreate is deferred to step 5, after every new script exists.
			if (found == m_records.end())
			{
				if (m_play.State() != PlayState::Playing || !definition.enabled ||
				    definition.className.empty())
					continue;
				Record record{definition, {}, {}};
				ScriptAssemblyHandle assembly;
				if (!m_runtime.LoadAssembly(definition.assembly, assembly) ||
				    !m_runtime.CreateEntityInstance(assembly, definition.nameSpace, definition.className,
				                                    entity.index, entity.generation, record.instance,
				                                    definition.fields, true))
				{
					// Keep a record anyway, so the error is shown and creation is
					// not retried every frame.
					record.error = m_runtime.LastError();
					CRU_USER_ERROR("Script on entity {}:{} could not start: {}", entity.index,
					               entity.generation, record.error);
				}
				else
					created.push_back(entity);
				m_records.emplace(key, std::move(record));
			}
		}

		// --- Step 5: OnCreate for every script created above. All of them exist
		// by now, so one script's OnCreate can find another's (GetComponent).
		for (const Entity entity : created)
		{
			const auto found = m_records.find(Key(entity));
			if (found == m_records.end() || !found->second.instance)
				continue;
			if (!m_runtime.StartEntityInstance(found->second.instance))
			{
				// The runtime already destroyed the instance; keep the error.
				found->second.error = m_runtime.LastError();
				found->second.instance = {};
				CRU_USER_ERROR("Script on entity {}:{} could not start: {}", entity.index, entity.generation,
				               found->second.error);
			}
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
					if (found == m_records.end() || !found->second.instance || !found->second.definition.enabled)
						continue;
					auto& record = found->second;
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
			ScriptBindings::EntityContext::ClearCollisionEvents();

			for (const auto& event : ScriptBindings::EntityContext::UIEvents())
			{
				for ([[maybe_unused]] auto& [key, record] : m_records)
				{
					if (!record.instance || !record.definition.enabled)
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
#endif

		// --- Step 6: apply enable/disable and call OnUpdate.
		for (const auto& [entity, definition] : definitions)
		{
			const auto found = m_records.find(Key(entity));
			if (found == m_records.end())
				continue;
			auto& record = found->second;
			record.definition.enabled = definition.enabled;
			if (!record.instance)
				continue; // failed creation is retried only after a config/session change
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
			if ((info.state == ScriptInstanceState::Running) != definition.enabled)
				m_runtime.SetInstanceEnabled(record.instance, definition.enabled);
			if (m_play.State() == PlayState::Playing && definition.enabled &&
			    !m_runtime.UpdateEntityInstance(record.instance, world.DeltaTime()))
			{
				record.error = m_runtime.LastError();
				CRU_USER_ERROR("Script on entity {}:{} stopped: {}", entity.index, entity.generation,
				               record.error);
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
				Release(it->second);
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
						Release(found->second);
						m_records.erase(found);
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

	uint32_t EntityScriptSystem::LookupScript(void* self, Entity entity)
	{
		auto& system = *static_cast<EntityScriptSystem*>(self);
		const auto found = system.m_records.find(Key(entity));
		uint32_t handle = 0;
		if (found != system.m_records.end() && found->second.instance)
			system.m_runtime.GetInstanceGcHandle(found->second.instance, handle);
		return handle;
	}
}
