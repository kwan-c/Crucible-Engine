/*************************************************************************
  \file       UISystem.h
  \brief
    Screen-space UI: layout (UIRect -> a rectangle on the screen) and
    drawing (UIImage / UISlider / UIText -> quads for the renderer), shared by the game and the
    editor's UI Editor tab, plus UIRenderSystem, which draws the game's UI.
  \details
    Layout, per screen-space UICanvas (lowest sortOrder first):
      - The canvas's rect is the whole screen. Its scale turns reference
        pixels into screen pixels (CanvasScale).
      - Each child with a UIRect is placed inside its parent's rect from
        its anchors, pivot, position and size, then its own children inside
        it. Parents come before children, siblings in Hierarchy order, and
        that is also the draw order: later draws on top.
      - Canvas pixels: origin top-left, +y DOWN (UIRect::position too).
      - A child without a UIRect is skipped with its whole subtree, like
        Unity, where everything under a Canvas needs a RectTransform.
      - Rotation: each element is placed upright in its parent's space, then
        turned around its pivot (UIRect::rotation, counter-clockwise). Its
        children are placed inside it in that same turned space, so they
        turn with it. LayoutItem::toScreen is that space -> the screen.

    Input (UIInputSystem, PreUpdate, Play mode only): the mouse is turned
    into game-screen pixels (Renderer::WindowToGameScreen), the topmost
    element with raycastTarget under it is found (walking the draw order
    backwards, turned elements tested in their own space), button states are
    set, and clicks are sent as UIEvents::ButtonClicked. An element that is
    not a button still blocks: clicks never fall through a panel.

    Who calls what:
      - UIRenderSystem (PreRender, also in Edit mode): lays out against the
        game screen (Renderer::GetGameScreenSize), writes UIRect::computed,
        and submits the result with Renderer::SubmitUI (Game view / window).
      - The UI Editor tab: the same two functions against its design
        resolution, keeping the rects to itself (writeComputed = false), so
        the two never overwrite each other's numbers.
*************************************************************************/
#pragma once

// STL
#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

// Third party
#include <glm/glm.hpp>

// Crucible engine
#include "ECS/Systems/IECSSystems.h"
#include "EngineExport.h"
#include "Graphics/UITypes.h"
#include "UI/UIComponents.h"

namespace Crucible {

	class World;
	class Renderer;
	class ResourceManager;
	struct FontAsset;
	class SceneManager;
	class AssetManager;

	namespace UI {

		/// One laid-out UI element.
		struct LayoutItem {
			Entity     entity{};
			Math::vec4 rect{ 0.0f };          // min x, min y, max x, max y, upright, in the element's own space
			Math::vec2 pivot{ 0.0f };         // the pivot point, in the same space
			glm::mat3  toScreen{ 1.0f };      // the element's own space -> screen (canvas) pixels; includes its rotation
			glm::mat3  parentToScreen{ 1.0f };// its parent's space -> screen: turns a screen drag into a UIRect::position change
			float      scale = 1.0f;          // its canvas's reference -> screen scale (CanvasScale)

			/// A point in the element's own space, on the screen.
			Math::vec2 ToScreen(Math::vec2 p) const { return Math::vec2(toScreen * glm::vec3(p, 1.0f)); }
			/// The four corners on the screen: top-left, top-right, bottom-right, bottom-left (before turning).
			std::array<Math::vec2, 4> Corners() const {
				return { ToScreen({ rect.x, rect.y }), ToScreen({ rect.z, rect.y }), ToScreen({ rect.z, rect.w }), ToScreen({ rect.x, rect.w }) };
			}
			/// The upright screen box around the (possibly turned) corners: min x, min y, max x, max y.
			Math::vec4 Bounds() const {
				const auto c = Corners();
				Math::vec4 b(c[0], c[0]);
				for (const Math::vec2& p : c) { b.x = std::min(b.x, p.x); b.y = std::min(b.y, p.y); b.z = std::max(b.z, p.x); b.w = std::max(b.w, p.y); }
				return b;
			}
			/// True when a screen point is inside the (possibly turned) rect.
			bool Contains(Math::vec2 screenPoint) const {
				const Math::vec2 p = Math::vec2(glm::inverse(toScreen) * glm::vec3(screenPoint, 1.0f));
				return p.x >= rect.x && p.x < rect.z && p.y >= rect.y && p.y < rect.w;
			}
		};

		/*********************************************************************
		 \brief  Reference pixels -> screen pixels for a canvas on a screen of
				 `screenSize` pixels.
		 \return 1 when scaleWithScreenSize is off. Otherwise the larger scale
				 that still fits the whole reference page on the screen, so a
				 HUD laid out at 1920 x 1080 keeps its shape at any size and
				 nothing laid out on the page ends up off-screen.
		 *********************************************************************/
		ENGINE_API float CanvasScale(const UICanvas& canvas, Math::vec2 screenSize);

		/*********************************************************************
		 \brief  Lays out every screen-space UICanvas against a screen of
				 `screenSize` pixels.
		 \param[out] out          Cleared, then every element, in draw order.
		 \param[in] writeComputed Also store each rect in UIRect::computed
								  (only where it changed). The game does;
								  editor previews don't.
		 *********************************************************************/
		ENGINE_API void LayoutScreenCanvases(World& world, Math::vec2 screenSize, std::vector<LayoutItem>& out, bool writeComputed);

		/*********************************************************************
		 \brief  Appends what every laid-out element draws, in this order:
				 - UIImage: one quad, its colour times the UIButton state's
				   tint if it has one, with its texture.
				 - UISlider: track, fill, handle.
				 - UIText: one quad per glyph (Text/TextLayout.h), with the
				   font's atlas and draw mode. Truncate / Ellipsis cut at the
				   rect (clip rect when upright, whole glyphs when turned);
				   bitmap fonts are snapped to whole screen pixels.
				 Consecutive quads with the same texture, mode and clip share a
				 draw, so a label is one draw. Text layouts are cached between
				 calls and redone only when the text or its settings change.
		 \param[in] resources  Resolves UIImage::textureAsset into
							   UIImage::texture (as MeshRenderer does) and
							   UIText::fontAsset into a font. Null: images use
							   whatever UIImage::texture holds; no text is drawn.
		 *********************************************************************/
		ENGINE_API void AppendDrawList(World& world, const std::vector<LayoutItem>& items, ResourceManager* resources,
		                               std::vector<UIVertex>& vertices, std::vector<std::uint32_t>& indices,
		                               std::vector<UIDrawCommand>& commands);

		/*********************************************************************
		 \brief  Field units (see UITextEffects) per reference pixel, for text of
				 `fontSize` in this font. 0 for bitmap fonts (no field).
		 *********************************************************************/
		ENGINE_API float TextEffectFieldPerPixel(const FontAsset& font, float fontSize);

		/*********************************************************************
		 \brief  The widest outline + glow (+ bold) a font allows at `fontSize`,
				 in reference pixels: the distance field only reaches half its
				 pixel range past each edge. 0 for bitmap fonts. Raise the
				 font's Pixel Range in the Font Importer for more.
		 *********************************************************************/
		ENGINE_API float MaxTextEffectWidth(const FontAsset& font, float fontSize);

		/// Is the in-game menu open (UIMenuSystem)? Gameplay can use it to ignore input or pause.
		ENGINE_API bool IsMenuOpen();

		/*********************************************************************
		 \brief  Opens or closes the in-game menu from code - e.g. a "Resume"
				 button's UIEvents::ButtonClicked listener calls SetMenuOpen(false).
		 \note   Applied by UIMenuSystem at the start of the next frame, exactly
				 as if Escape had been pressed: menus shown / hidden, cursor
				 freed / locked.
		 *********************************************************************/
		ENGINE_API void SetMenuOpen(bool open);

		/// Does the game want a free cursor right now (menu open, or a shown UICanvas::showsCursor
		/// canvas)? The editor checks it before re-locking the cursor on a Game-view click.
		ENGINE_API bool IsCursorNeeded();

	}   // NAMESPACE UI

	/*************************************************************************
	 \brief  Draws the game's screen-space UI every frame, in Edit mode too
			 (so the Game view shows the HUD while you build it).
	 \details
		Lays out against Renderer::GetGameScreenSize(), writes
		UIRect::computed, and hands the quads to Renderer::SubmitUI. Does
		nothing while there is no game screen (no Game view open).
	 *************************************************************************/
	class UIRenderSystem final : public IECSSystem {
	public:
		UIRenderSystem(Renderer& renderer, ResourceManager* resources) : m_renderer(renderer), m_resources(resources) {}

		void Update(World& world, CommandBuffer& commands) override;
		void Describe(SystemDesc& desc) const override;
		SystemStage Stage() const override { return SystemStage::PreRender; }

	private:
		Renderer&        m_renderer;
		ResourceManager* m_resources = nullptr;

		// Reused every frame, so steady-state frames don't allocate.
		std::vector<UI::LayoutItem> m_layout;
		std::vector<UIVertex>       m_vertices;
		std::vector<std::uint32_t>  m_indices;
		std::vector<UIDrawCommand>  m_commands;
	};

	/*************************************************************************
	 \brief  Escape opens / closes the game's in-game menu: every canvas with
			 UICanvas::toggleWithEscape is shown or hidden (UICanvas::enabled),
			 and the cursor is freed so the menu's buttons can be clicked, then
			 locked again for mouse-look.
	 \details
		- Play mode only. The menu starts closed whenever Play (or Resume)
		  starts, so menu canvases can stay visible while you lay them out in
		  the editor - they are hidden as the game begins.
		- Escape still frees / locks the cursor when there is no menu canvas,
		  so the standalone game never traps the mouse.
		- The cursor: free while the menu is open or any shown canvas has
		  UICanvas::showsCursor (a title screen); locked again when that stops.
		  So loading a level from a menu goes back to mouse-look, and a title
		  scene starts with a usable cursor.
		- Menu canvases are kept matching the menu every frame, so a scene
		  loaded mid-Play has its menus hidden too.
		- Runs before UIInputSystem, so a menu opened this frame can already be
		  hovered.
	 *************************************************************************/
	class UIMenuSystem final : public IECSSystem {
	public:
		void Update(World& world, CommandBuffer& commands) override;
		void Describe(SystemDesc& desc) const override;
		SystemStage Stage() const override { return SystemStage::PreUpdate; }

	private:
		std::uint64_t       m_lastTick = 0;          // a gap means Play / Resume just started (same trick as ECSPlayerControllerSystem)
		bool                m_cursorNeeded = false;  // last frame's answer, to lock once when the need ends
		std::vector<Entity> m_menus;                 // reused
	};

	/*************************************************************************
	 \brief  Mouse -> the game's screen-space UI, every frame while Playing:
			 button states (hover / pressed tints), clicks
			 (UIEvents::ButtonClicked) and Input::IsPointerOverUI().
	 \details
		Rules (Unity's Selectable):
		  - The target is the topmost element with raycastTarget under the
			mouse. A button under another raycast-target element is covered.
		  - Pressed: the mouse went down on this button and is still over it.
		  - Hovered: the mouse is over this button (and not pressing it).
		  - Click: released over the same button it went down on.
		  - Disabled: interactable is off. Never hovered, pressed or clicked.
		  - Switched off (World::SetComponentEnabled on the UIButton / UISlider
			or a component it requires): its press or drag is cancelled, so
			switching it back on before release neither clicks nor drags.
		Sliders: pressing one jumps its value to the mouse, dragging follows
		it until release (even outside), each change sends
		UIEvents::SliderChanged; Pressed while dragged, Hovered under the mouse.
		A click sends UIEvents::ButtonClicked, then runs the button's own
		onClick action (load a scene, close the menu). Quit and Custom are
		only reported - UIEvents::QuitRequested / CustomAction - because what
		they mean is the app's call (GameApp quits / runs its action table;
		the editor stops Play / logs).
		While the cursor is locked (mouse-look) the UI ignores the mouse and
		every button shows Normal.
	 \note  SimulationOnly, so the editor's clicks in Edit mode never press
			buttons. Stop restores the scene, and states are not saved, so
			buttons come back Normal.
	 *************************************************************************/
	class UIInputSystem final : public IECSSystem {
	public:
		/// scenes / assets: for UIButton::ClickAction::LoadScene (null: that action only warns).
		UIInputSystem(Renderer& renderer, SceneManager* scenes, AssetManager* assets)
			: m_renderer(renderer), m_scenes(scenes), m_assets(assets) {}

		void Update(World& world, SystemContext& ctx) override;
		void Describe(SystemDesc& desc) const override;
		SystemStage Stage() const override { return SystemStage::PreUpdate; }

	private:
		void RunClickAction(World& world, Entity button);   // UIButton::onClick, after the event is sent
		// ReactiveTo<UIButton, UISlider> (Core.cpp): a control switched off drops its press / drag.
		void OnComponentDeactivated(World& world, Entity entity, ComponentId component) override;

		Renderer&                   m_renderer;
		SceneManager*               m_scenes = nullptr;
		AssetManager*               m_assets = nullptr;
		std::vector<UI::LayoutItem> m_layout;     // this frame's layout at the game screen's size (reused)
		Entity                      m_pressed{};  // the button the current press started on (none: kNullEntity)
		Entity                      m_dragSlider{};  // the slider being dragged (none: kNullEntity)
		std::vector<std::pair<Entity, UIButton::State>> m_stateChanges;   // collected, then written
	};

}   // NAMESPACE CRUCIBLE
