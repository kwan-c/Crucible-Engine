/*************************************************************************
* \file:         EntityScriptSystem.cpp
* \author:       <author>
* \par:          Email: <email>
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

		// --- Step 1: reset everything on scene change, Stop, or a new Play session.
		if (m_sceneChanged || m_session != m_play.SessionRevision() || !m_play.InSession())
		{
			StopAll();
			m_sceneChanged = false;
			m_session = m_play.SessionRevision();
		}

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
