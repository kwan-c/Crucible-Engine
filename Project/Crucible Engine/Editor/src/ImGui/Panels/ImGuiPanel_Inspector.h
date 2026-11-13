#pragma once
/*!
* @file			ImGuiPanel_Inspector.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		The editor's inspector for viewing and editing object properties.
*
* Two independent paths live here side by side:
*   - The original generic path (SetTarget<T>/DrawInstance): draws any rttr-reflected type via
*     its registered properties. Kept for future non-ECS use; rttr registration is currently
*     entirely commented out project-wide (see Reflection/ReflectionRegistration.h), so nothing
*     exercises this path today.
*   - The entity path (SetSelectedEntity/DrawEntityInspector): what the Hierarchy panel actually
*     drives. Hand-rolled (non-rttr) widgets for Transform, MeshRenderer and Material, because
*     rttr isn't wired up to anything yet.
*
* SetSelectedEntity stores an Entity, NOT a reference into one of its components: components live
* inside archetype columns that can move (AddComponent, an archetype migration, ...), so a
* reference captured once - the way the old SetTarget<T> captures &obj - would dangle the moment
* that happens. DrawEntityInspector instead re-resolves every component fresh, every Update(), via
* World::GetComponent.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/ImGuiManager.h"
#include "Reflection/ReflectionRegistration.h"
#include "Reflection/ReflectionExternalRegistration.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "AssetPipe/Assetmanager.h"
#include "Scripting/ScriptTypes.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace Crucible {
	class SceneManager;

	class Inspector : public EditorPanel {
	public:
		Inspector(World& world, Renderer& renderer, SceneManager& scenes, AssetManager& assets)
			: m_world(world), m_renderer(renderer), m_scenes(scenes), m_assets(assets) {}

		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

		// The generic rttr-instance path (see file header comment).
		template<typename T> void SetTarget(T& obj) { CommitPendingEdit(); m_draw = [this, &obj] { DrawInstance(obj); }; m_selected = kNullEntity; m_selection.clear(); }
		void ClearTarget() { CommitPendingEdit(); m_draw = nullptr; m_selected = kNullEntity; m_selection.clear(); }

		// Any panel's own UI, drawn in the Inspector instead of an entity - e.g. the Asset
		// Browser's audio clip preview (Unity shows a selected asset here too). Replaced by the
		// next SetSelectedEntity/SetTarget/ClearTarget, like the rttr path above. Commits the
		// edit in progress first, like they do, so its undo step lands before the switch.
		void SetCustomTarget(std::function<void()> draw) { CommitPendingEdit(); m_draw = std::move(draw); m_selected = kNullEntity; m_selection.clear(); }
		
		bool HasCustomTarget() const { return m_draw != nullptr; }

		// ---- Selection: one or more entities ----
		// The editor's selection lives here. It is a list (Ctrl/Shift+click in the Hierarchy or
		// Scene view); the ACTIVE entity is the last one clicked - the one the Inspector shows
		// and the gizmo sits on. With one entity selected, active == the whole selection.

		// Selects only `entity` (kNullEntity clears the selection). A plain click.
		void SetSelectedEntity(Entity entity);
		// Replaces the selection. `active` must be in the list; kNullEntity = its last entry.
		void SetSelection(std::vector<Entity> const& entities, Entity active = kNullEntity);
		// Ctrl+click: adds `entity`, or removes it if it was already selected.
		void ToggleSelected(Entity entity);
		// Shift+click: adds `entity` (if missing) and makes it the active one.
		void AddToSelection(Entity entity);
		bool IsSelected(Entity entity) const;
		// Every selected entity that is still alive, in the order they were selected.
		std::vector<Entity> GetSelection() const;
		std::size_t SelectionCount() const { return m_selection.size(); }
		
		// Turns the edit in progress, if any, into one undo step. Runs automatically when the
		// edited widget is released, when the selection changes, and before Undo / Redo.
		void CommitPendingEdit();
		Entity GetSelectedEntity() const { return m_selected; }

		// Writes every material edited since its last save (the ones shown with a '*').
		// Called by the Menu whenever the scene is saved. Returns how many failed to save.
		std::size_t SaveUnsavedMaterials();

	private:
		bool DrawInstance(rttr::instance obj);   // true if a value changed
		// Helpers drawn straight after one particular field (e.g. "Pick from Sky" after Skybox's
		// ambientFromSky), next to the values they fill in. True if they changed something.
		bool DrawPropertyExtras(rttr::instance& obj, std::string const& name);
		bool DrawPickFromSky(Skybox& skybox);

		void DrawEntityInspector(Entity entity);
		void DrawAddComponentButton(Entity entity);

		ImGuiTextFilter m_addComponentFilter;

		template <typename T> bool DrawComponentExtras(T&) { return false; }
		bool DrawComponentExtras(MeshRenderer& renderer);   // mesh name + material
		bool DrawComponentExtras(AnimatedModel& animation);
		bool DrawComponentExtras(SocketAttachment& socket);
		// Every C# script on the entity, one section each (ScriptComponent itself is never shown):
		// header with the script's own on/off checkbox; right-click to remove or reorder. True if
		// something changed. Removing the last script removes the component (deferredRemove).
		bool DrawScripts(Entity entity, std::function<void()>& deferredRemove, std::string& removeLabel);
		bool DrawScriptFields(ScriptEntry& script, std::size_t slot);   // public fields, status, errors
		void DrawCreateScriptModal();
		void RefreshScriptClasses(const std::string& assembly);
		void RequestCreateScript(Entity entity, std::string name, std::string nameSpace);
		void AssignScript(Entity entity, const std::string& assembly, const std::string& nameSpace,
			const std::string& name);
		bool DrawComponentExtras(UICanvas& canvas);         // Screen Space / World Space settings, the unused group greyed
		bool DrawComponentExtras(UIRect& rect);             // position (+y up or down, per user), size, rotation; a warning when not under a canvas
		bool DrawComponentExtras(UIButton& button);         // scene dropdown for onClick = LoadScene
		bool DrawComponentExtras(UISlider& slider);         // direction dropdown that also turns the UIRect
		bool DrawComponentExtras(UIText& text);             // multi-line text box, 3 x 3 alignment grid, font status
		bool DrawComponentExtras(NetworkObjectAuthoring& authoring);   // the networking policy table (Editor/src/Network/NetworkAuthoringInspector.h)
		bool DrawComponentExtras(WorldText& text);          // the same as UIText, in metres, plus the distance fade
		template <typename T> bool DrawTextExtras(T& text, bool world);   // UIText / WorldText shared part (Inspector.cpp)
		bool DrawMaterialSection(MeshRenderer& renderer, bool importedDefault = false);

		std::function<void()> m_draw;

		World&    m_world;
		Renderer& m_renderer;
		SceneManager& m_scenes;

		Entity    m_selected = kNullEntity;          // the active entity (always in m_selection, or null)
		std::vector<Entity> m_selection;             // every selected entity, in selection order
		void PruneSelection();                       // drops entities that no longer exist

		// Core's shared asset index (core.GetAssets()), used to resolve a dropped texture's virtual
		// path to a filesystem path for Renderer::CreateTexture.
		AssetManager& m_assets;

		// .mat asset ids edited in the Inspector but not yet written with "Save Material".
		// Edits are live on the GPU immediately; the file is only written on the button.
		std::unordered_set<std::uint64_t> m_unsavedMaterials;

		// ---- Undo: the edit in progress ----
		// m_editBefore is re-captured every frame nothing is being edited, so it always holds
		// the entity as it was just before an edit starts. The first change opens the edit
		// (NoteEdit); it is committed once no ImGui widget is active, so a whole slider drag
		// becomes ONE undo step rather than one per frame.
		void NoteEdit(std::string label);
		std::string m_editBefore;
		std::string m_editLabel;
		Entity      m_editEntity = kNullEntity;
		bool        m_editOpen = false;

        std::string m_scriptAssembly;
        std::vector<ScriptClassInfo> m_scriptClasses;
        std::string m_scriptPickerError, m_scriptCreateError, m_scriptNotice;
        std::string m_scriptNewName, m_scriptNewNamespace;
        Entity m_scriptCreateTarget = kNullEntity, m_scriptNoticeTarget = kNullEntity;
        bool m_scriptCreateRequested = false;
        // A script class's public fields with their default values, read from the compiled
        // assembly - per class, since an entity can show several scripts at once.
        struct ScriptFieldCache {
            uint64_t reload = 0;                     // ScriptRuntime::ReloadCount() they were read at
            std::vector<ScriptFieldValue> defaults;
            std::vector<std::string> buttons;        // the class's [EditorButton] methods (Jean David)
            std::string error;
        };
        std::map<std::string, ScriptFieldCache> m_scriptFieldCache;   // "assembly|namespace.class" -> fields
        ScriptInstanceHandle m_scriptFieldReadFailure;
        std::string m_scriptFieldReadError;

		// ---- Name field ----
		// Typed into m_nameBuffer (any length, UTF-8) and written to the entity only when the
		// field is left or the edit is committed (selection change, Play, Save, Undo...), so the
		// entity never holds a blank or half-typed name. World::SetEntityName applies the naming
		// rules: a name left blank becomes the next "Entity N".
		void ApplyPendingName();
		std::string m_nameBuffer;
		Entity      m_nameEntity = kNullEntity;   // whose name m_nameBuffer holds
		bool        m_nameDirty = false;          // typed into, not written yet
	};
}