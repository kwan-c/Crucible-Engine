#pragma once
/*!
* @file			ImGuiPanel_Inspector.h
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
*/

#include "ImGui/ImGuiManager.h"
#include "Reflection/ReflectionRegistration.h"
#include "Reflection/ReflectionExternalRegistration.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "AssetPipe/Assetmanager.h"

#include <cstdint>
#include <functional>
#include <unordered_set>

namespace Crucible {
	class SceneManager;

	class Inspector : public EditorPanel {
	public:
		Inspector(World& world, Renderer& renderer, SceneManager& scenes) : m_world(world), m_renderer(renderer), m_scenes(scenes) {}

		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

		// The generic rttr-instance path (see file header comment).
		template<typename T> void SetTarget(T& obj) { m_draw = [&obj] { DrawInstance(obj); }; m_selected = kNullEntity; }
		void ClearTarget() { m_draw = nullptr; m_selected = kNullEntity; }

		// The Hierarchy panel calls this when the user clicks an entity.
		void SetSelectedEntity(Entity entity) { m_selected = entity; m_draw = nullptr; }
		Entity GetSelectedEntity() const { return m_selected; }

		// Writes every material edited since its last save (the ones shown with a '*').
		// Called by the Menu whenever the scene is saved. Returns how many failed to save.
		std::size_t SaveUnsavedMaterials();

	private:
		static bool DrawInstance(rttr::instance obj);   // true if a value changed

		void DrawEntityInspector(Entity entity);
		void DrawAddComponentButton(Entity entity);

		ImGuiTextFilter m_addComponentFilter;

		template <typename T> bool DrawComponentExtras(T&) { return false; }
		bool DrawComponentExtras(MeshRenderer& renderer);   // mesh name + material
		bool DrawMaterialSection(MeshRenderer& renderer);

		std::function<void()> m_draw;

		World&    m_world;
		Renderer& m_renderer;
		SceneManager& m_scenes;

		Entity    m_selected = kNullEntity;

		// This panel's own asset index, purely to resolve a dropped texture's virtual path to a
		// filesystem path for Renderer::CreateTexture - mirrors AssetBrowser::m_assets (see that
		// panel's Init() for why each panel keeps its own for now instead of sharing one).
		AssetManager m_assets;

		// .mat asset ids edited in the Inspector but not yet written with "Save Material".
		// Edits are live on the GPU immediately; the file is only written on the button.
		std::unordered_set<std::uint64_t> m_unsavedMaterials;
	};
}
