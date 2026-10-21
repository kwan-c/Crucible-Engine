#include "Scripting/EntityScriptSystem.h"
#include "Scripting/ScriptRuntime.h"
#include "ECS/World.h"
#include "Scene/SceneManager.h"
#include "Systems/PlayMode.h"
#include "Diagnostics/Logger.h"
#include <algorithm>
#include <vector>

namespace Crucible {
    EntityScriptSystem::EntityScriptSystem(ScriptRuntime& runtime, World& world, PlayMode& play, SceneManager& scenes)
        : m_runtime(runtime), m_world(world), m_play(play), m_scenes(scenes) {}

    void EntityScriptSystem::Init() {
        const std::weak_ptr<int> alive = m_lifetime;
        // World hooks never run managed code or mutate component structure.
        m_world.OnComponentRemoved<ScriptComponent>([this, alive](World&, Entity entity, const ScriptComponent&) {
            if (!alive.expired()) m_removed.insert(Key(entity));
        });
        m_scenes.OnSceneChanged([this, alive] { if (!alive.expired()) m_sceneChanged = true; });
    }
    void EntityScriptSystem::Describe(SystemDesc& desc) const {
        // Always on for cleanup in Edit/Pause; callback execution is Play-only.
        desc.Exclusive().Affinity(SystemAffinity::MainThread);
    }
    void EntityScriptSystem::Release(Record& record) {
        if (record.instance && m_runtime.IsRunning() && !m_runtime.DestroyInstance(record.instance)) {
            record.error = m_runtime.LastError();
            CRU_USER_ERROR("Script {}.{} cleanup failed: {}", record.definition.nameSpace, record.definition.className, record.error);
        }
        record.instance = {};
    }
    void EntityScriptSystem::StopAll() {
        for (auto it = m_records.rbegin(); it != m_records.rend(); ++it) Release(it->second);
        m_records.clear(); m_removed.clear();
    }
    EntityScriptStatus EntityScriptSystem::Status(Entity entity) const {
        const auto found = m_records.find(Key(entity));
        if (found == m_records.end()) return {};
        return {found->second.instance, found->second.error};
    }
    void EntityScriptSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx) {
        if (m_sceneChanged || m_session != m_play.SessionRevision() || !m_play.InSession()) {
            StopAll(); m_sceneChanged = false; m_session = m_play.SessionRevision();
        }
        for (const auto key : m_removed) {
            const auto found = m_records.find(key);
            if (found != m_records.end()) { Release(found->second); m_records.erase(found); }
        }
        m_removed.clear();
        if (!m_play.InSession()) return;

        // Copy before callbacks: no managed execution while a query pins columns.
        std::vector<std::pair<Entity, ScriptComponent>> definitions;
        world.CreateQuery<const ScriptComponent>().ForEach([&](Entity entity, const ScriptComponent& script) {
            definitions.emplace_back(entity, script);
        });
        std::set<uint64_t> present;
        for (const auto& [entity, definition] : definitions) {
            const auto key = Key(entity); present.insert(key);
            auto found = m_records.find(key);
            if (found != m_records.end()) {
                auto old = found->second.definition;
                old.enabled = definition.enabled; // enable changes retain the managed instance
                old.fields = definition.fields; // public field edits are not a class change
                if (!(old == definition)) { Release(found->second); m_records.erase(found); found = m_records.end(); }
                else if (found->second.definition.fields != definition.fields) {
                    std::vector<ScriptFieldValue> changes;
                    for (const auto& field : definition.fields) {
                        const auto& previous = found->second.definition.fields;
                        const auto saved = std::find_if(previous.begin(), previous.end(), [&](const auto& value) { return SameScriptField(field, value); });
                        if (saved == previous.end() || !(*saved == field)) changes.push_back(field);
                    }
                    ScriptInstanceInfo state;
                    if (found->second.instance && m_runtime.GetInstanceInfo(found->second.instance, state) &&
                        state.state != ScriptInstanceState::Faulted) {
                        if (!m_runtime.SetEntityFields(found->second.instance, changes)) found->second.error = m_runtime.LastError();
                        else found->second.error.clear();
                    }
                    found->second.definition.fields = definition.fields;
                }
            }
            if (found == m_records.end()) {
                if (m_play.State() != PlayState::Playing || !definition.enabled || definition.className.empty()) continue;
                Record record{definition, {}, {}};
                ScriptAssemblyHandle assembly;
                if (!m_runtime.LoadAssembly(definition.assembly, assembly) ||
                    !m_runtime.CreateEntityInstance(assembly, definition.nameSpace, definition.className,
                        entity.index, entity.generation, record.instance, definition.fields)) {
                    record.error = m_runtime.LastError();
                    CRU_USER_ERROR("Script on entity {}:{} could not start: {}", entity.index, entity.generation, record.error);
                }
                found = m_records.emplace(key, std::move(record)).first;
            }
            auto& record = found->second;
            record.definition.enabled = definition.enabled;
            if (!record.instance) continue; // failed creation is retried only after a config/session change
            ScriptInstanceInfo info;
            if (!m_runtime.GetInstanceInfo(record.instance, info)) {
                record.error = m_runtime.LastError(); record.instance = {}; continue;
            }
            if (info.state == ScriptInstanceState::Faulted) { record.error = info.lastError; continue; }
            if ((info.state == ScriptInstanceState::Running) != definition.enabled)
                m_runtime.SetInstanceEnabled(record.instance, definition.enabled);
            if (m_play.State() == PlayState::Playing && definition.enabled &&
                !m_runtime.UpdateEntityInstance(record.instance, world.DeltaTime())) {
                record.error = m_runtime.LastError();
                CRU_USER_ERROR("Script on entity {}:{} stopped: {}", entity.index, entity.generation, record.error);
            }
        }
        for (auto it = m_records.begin(); it != m_records.end();) {
            if (!present.contains(it->first)) { Release(it->second); it = m_records.erase(it); }
            else ++it;
        }
    }
    void EntityScriptSystem::Shutdown() noexcept {
        try { StopAll(); } catch (...) {}
        m_lifetime.reset();
    }
}
