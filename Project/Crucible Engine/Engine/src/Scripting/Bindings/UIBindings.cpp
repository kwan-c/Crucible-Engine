/*************************************************************************
* \file:         UIBindings.cpp
* \author:       <author>
* \par:          Email: <email>
* \date          2 November 2026
* \version       1.0
* \brief         Internal calls behind the C# UI classes (UIText, UIImage,
*                UISlider, UIButton, UIRect, UICanvas and the static UI) -
*                ManagedScripts/Runtime/UI.cs.
*
* \details
*    Bindings only: UI/UIComponents.h and UISystem are unchanged.
*
*    Component fields are read / written like the other bindings
*    (ResolveEntity, then Read / Write the component). UISystem lays out
*    and draws every frame in PreRender, after scripts, so changes show the
*    same frame. Colours are the components' sRGB 0-1 vec4s, crossing as
*    ScriptColor (= Crucible.Color). Positions and sizes are reference
*    pixels with +y DOWN, as in UIRect.
*
*    Clicks: UISystem sends UIEvents::ButtonClicked / SliderChanged /
*    CustomAction on Core's engine bus, delivered at the end of PreUpdate -
*    before scripts run (Update). SetEvents subscribes to them and records
*    this frame's clicked buttons, changed sliders and action ids;
*    EntityScriptSystem calls EntityContext::ClearUIEvents at the end of its
*    Update, so WasClicked / WasChanged / WasActionTriggered are true for
*    exactly the frame the player did it.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Scripting/ScriptInterop.h"
#include "Scripting/Bindings/EntityContext.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "ECS/Systems/EventBus/EventBuses.h"
#include "UI/UIComponents.h"
#include "UI/UIEvents.h"
#include "UI/UISystem.h"
#include "AssetPipe/Assetmanager.h"
#if CRUCIBLE_WITH_MONO
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		using namespace ScriptingDetail;

		// ======================================================================
		// This frame's UI events (main thread)
		// ======================================================================

		using UIEvent = EntityContext::UIEvent;

		// Every UI event this frame, in order (EntityContext::UIEvents).
		std::vector<UIEvent> uiEvents;

		// The listeners. A Subscription only holds a weak link to its bus, so
		// releasing these after the bus is gone is safe.
		Subscription onClick, onSlider, onAction;

		/*********************************************************************
		 \brief  true if this frame has an event of `kind` from `source`.
		 *********************************************************************/
		bool Happened(UIEvent::Kind kind, Entity source)
		{
			return std::any_of(uiEvents.begin(), uiEvents.end(),
			                   [&](const UIEvent& e) { return e.kind == kind && e.source == source; });
		}

		// ======================================================================
		// Asset ids (two int halves from C#, like AudioClip)
		// ======================================================================

		uint64_t JoinId(int32_t low, int32_t high)
		{
			return (static_cast<uint64_t>(static_cast<uint32_t>(high)) << 32) | static_cast<uint32_t>(low);
		}

		void SplitId(uint64_t id, int32_t* low, int32_t* high)
		{
			*low = static_cast<int32_t>(static_cast<uint32_t>(id));
			*high = static_cast<int32_t>(static_cast<uint32_t>(id >> 32));
		}

		// ======================================================================
		// Generic component field access
		// ======================================================================

		template <typename C>
		const C* ReadUI(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Read<C>(entity);
		}

		template <typename C>
		C* WriteUI(int32_t index, int32_t generation)
		{
			Entity entity;
			if (!EntityContext::ResolveEntity(index, generation, entity))
				return nullptr;
			return EntityContext::GetWorld()->Write<C>(entity);
		}

		template <typename C, float C::*Member>
		int32_t GetFloat(int32_t index, int32_t generation, float* out) noexcept
		{
			try
			{
				const C* component = out ? ReadUI<C>(index, generation) : nullptr;
				if (!component)
					return 0;
				*out = component->*Member;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <typename C, float C::*Member, bool AllowNegative>
		int32_t SetFloat(int32_t index, int32_t generation, const float* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(*value) || (!AllowNegative && *value < 0.0f))
					return 0;
				C* component = WriteUI<C>(index, generation);
				if (!component)
					return 0;
				component->*Member = *value;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <typename C, bool C::*Member>
		int32_t GetFlag(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const C* component = out ? ReadUI<C>(index, generation) : nullptr;
				if (!component)
					return 0;
				*out = (component->*Member) ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <typename C, bool C::*Member>
		int32_t SetFlag(int32_t index, int32_t generation, int32_t value) noexcept
		{
			try
			{
				C* component = WriteUI<C>(index, generation);
				if (!component)
					return 0;
				component->*Member = value != 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <typename C, Math::vec4 C::*Member>
		int32_t GetColor(int32_t index, int32_t generation, ScriptColor* out) noexcept
		{
			try
			{
				const C* component = out ? ReadUI<C>(index, generation) : nullptr;
				if (!component)
					return 0;
				const Math::vec4& c = component->*Member;
				*out = {c.r, c.g, c.b, c.a};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  Colour setter: each channel clamped to 0..1 (UI colours are
		         sRGB 0-1); NaN rejected.
		 *********************************************************************/
		template <typename C, Math::vec4 C::*Member>
		int32_t SetColor(int32_t index, int32_t generation, const ScriptColor* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(value->r) || !std::isfinite(value->g) || !std::isfinite(value->b) ||
				    !std::isfinite(value->a))
					return 0;
				C* component = WriteUI<C>(index, generation);
				if (!component)
					return 0;
				component->*Member = {std::clamp(value->r, 0.0f, 1.0f), std::clamp(value->g, 0.0f, 1.0f),
				                      std::clamp(value->b, 0.0f, 1.0f), std::clamp(value->a, 0.0f, 1.0f)};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <typename C, Math::vec2 C::*Member>
		int32_t GetVec2(int32_t index, int32_t generation, ScriptVec2* out) noexcept
		{
			try
			{
				const C* component = out ? ReadUI<C>(index, generation) : nullptr;
				if (!component)
					return 0;
				const Math::vec2& v = component->*Member;
				*out = {v.x, v.y};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <typename C, Math::vec2 C::*Member>
		int32_t SetVec2(int32_t index, int32_t generation, const ScriptVec2* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(value->x) || !std::isfinite(value->y))
					return 0;
				C* component = WriteUI<C>(index, generation);
				if (!component)
					return 0;
				component->*Member = {value->x, value->y};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// UIText.Text
		// ======================================================================

		MonoString* ManagedUITextGetText(int32_t index, int32_t generation) noexcept
		{
			try
			{
				const UIText* text = ReadUI<UIText>(index, generation);
				std::wstring wide;
				if (!text || !ToUtf16(text->text, wide))
					return nullptr;
				return mono_string_new_utf16(mono_domain_get(), reinterpret_cast<const mono_unichar2*>(wide.data()),
				                             static_cast<int32_t>(wide.size()));
			}
			catch (...)
			{
				return nullptr;
			}
		}

		int32_t ManagedUITextSetText(int32_t index, int32_t generation, MonoString* value) noexcept
		{
			try
			{
				GcRoot root(reinterpret_cast<MonoObject*>(value));
				std::string text;
				if (root.Get() && !FromManagedString(reinterpret_cast<MonoString*>(root.Get()), text))
					return 0;
				UIText* component = WriteUI<UIText>(index, generation);
				if (!component)
					return 0;
				if (component->text != text) // most HUDs set the same text every frame
					component->text = std::move(text);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// UISlider.Value (clamped and rounded like the slider itself)
		// ======================================================================

		int32_t ManagedUISliderSetValue(int32_t index, int32_t generation, const float* value) noexcept
		{
			try
			{
				if (!value || !std::isfinite(*value))
					return 0;
				UISlider* slider = WriteUI<UISlider>(index, generation);
				if (!slider)
					return 0;
				const float low = std::min(slider->minValue, slider->maxValue);
				const float high = std::max(slider->minValue, slider->maxValue);
				float v = std::clamp(*value, low, high);
				if (slider->wholeNumbers)
					v = std::round(v);
				slider->value = v;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// State and this frame's events
		// ======================================================================

		/*********************************************************************
		 \brief  UIButton.State / UISlider state: 0 Normal, 1 Hovered,
		         2 Pressed, 3 Disabled (UIButton::State).
		 *********************************************************************/
		template <typename C>
		int32_t GetState(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			try
			{
				const C* component = out ? ReadUI<C>(index, generation) : nullptr;
				if (!component)
					return 0;
				*out = static_cast<int32_t>(component->state);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedUIButtonWasClicked(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			if (!out)
				return 0;
			*out = Happened(UIEvent::Kind::ButtonClicked,
			                Entity{static_cast<uint32_t>(index), static_cast<uint32_t>(generation)})
			           ? 1
			           : 0;
			return 1;
		}

		int32_t ManagedUISliderWasChanged(int32_t index, int32_t generation, int32_t* out) noexcept
		{
			if (!out)
				return 0;
			*out = Happened(UIEvent::Kind::SliderChanged,
			                Entity{static_cast<uint32_t>(index), static_cast<uint32_t>(generation)})
			           ? 1
			           : 0;
			return 1;
		}

		int32_t ManagedUIWasActionTriggered(MonoString* actionId, int32_t* out) noexcept
		{
			try
			{
				if (!out)
					return 0;
				GcRoot root(reinterpret_cast<MonoObject*>(actionId));
				std::string id;
				if (!root.Get() || !FromManagedString(reinterpret_cast<MonoString*>(root.Get()), id))
					return 0;
				*out = std::any_of(uiEvents.begin(), uiEvents.end(),
				                   [&](const UIEvent& e)
				                   { return e.kind == UIEvent::Kind::CustomAction && e.actionId == id; })
				           ? 1
				           : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  UIRect.ScreenRect: last frame's laid-out box (rotation
		         included) in screen pixels, top-left origin.
		 *********************************************************************/
		int32_t ManagedUIRectGetScreenRect(int32_t index, int32_t generation, ScriptVec2* outMin,
		                                   ScriptVec2* outMax) noexcept
		{
			try
			{
				const UIRect* rect = (outMin && outMax) ? ReadUI<UIRect>(index, generation) : nullptr;
				if (!rect)
					return 0;
				*outMin = {rect->computed.x, rect->computed.y};
				*outMax = {rect->computed.z, rect->computed.w};
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// ======================================================================
		// UIImage.Texture / UIText.Font (asset guids; UISystem resolves them
		// every frame, so a change shows the same frame)
		// ======================================================================

		template <typename C, uint64_t C::*Member>
		int32_t GetAsset(int32_t index, int32_t generation, int32_t* low, int32_t* high) noexcept
		{
			try
			{
				const C* component = (low && high) ? ReadUI<C>(index, generation) : nullptr;
				if (!component)
					return 0;
				SplitId(component->*Member, low, high);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  Sets a texture / font id. 0 clears it (solid colour / the
		         default font); anything else must be an asset of the right
		         type (IsValid).
		 *********************************************************************/
		template <typename C, uint64_t C::*Member, bool (*IsValid)(uint64_t)>
		int32_t SetAsset(int32_t index, int32_t generation, int32_t low, int32_t high) noexcept
		{
			try
			{
				const uint64_t id = JoinId(low, high);
				if (id != 0 && !IsValid(id))
					return 0;
				C* component = WriteUI<C>(index, generation);
				if (!component)
					return 0;
				component->*Member = id;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Texture.Name / Font.Name: the file's name, or null.
		 *********************************************************************/
		MonoString* ManagedAssetGetName(int32_t low, int32_t high) noexcept
		{
			try
			{
				AssetManager* assets = EntityContext::GetAssets();
				const AssetEntry* entry =
				    assets ? assets->FindByGuid(xresource::instance_guid{JoinId(low, high)}) : nullptr;
				std::wstring wide;
				if (!entry || !ToUtf16(std::filesystem::path(entry->virtualPath).filename().string(), wide))
					return nullptr;
				return mono_string_new_utf16(mono_domain_get(), reinterpret_cast<const mono_unichar2*>(wide.data()),
				                             static_cast<int32_t>(wide.size()));
			}
			catch (...)
			{
				return nullptr;
			}
		}

		// ======================================================================
		// Static UI: the in-game menu
		// ======================================================================

		int32_t ManagedUIGetMenuOpen(int32_t* out) noexcept
		{
			try
			{
				if (!out)
					return 0;
				*out = UI::IsMenuOpen() ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedUISetMenuOpen(int32_t open) noexcept
		{
			try
			{
				if (!EntityContext::InPlaySession())
					return 0;
				UI::SetMenuOpen(open != 0); // applied at the start of next frame, like Escape
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <typename Fn>
		void Add(const char* name, Fn* function)
		{
			mono_add_internal_call(name, reinterpret_cast<const void*>(function));
		}
	}

	void SetEvents(EventBuses* events)
	{
		onClick = {};
		onSlider = {};
		onAction = {};
		uiEvents.clear();
		if (!events)
			return;
		onClick = events->engine.Subscribe<UIEvents::ButtonClicked>(
		    [](const UIEvents::ButtonClicked& click)
		    { uiEvents.push_back({UIEvent::Kind::ButtonClicked, click.button, 0.0f, {}}); });
		onSlider = events->engine.Subscribe<UIEvents::SliderChanged>(
		    [](const UIEvents::SliderChanged& change)
		    {
			    // One per slider per frame (UISystem sends at most one, but be safe): keep the latest value.
			    for (UIEvent& e : uiEvents)
				    if (e.kind == UIEvent::Kind::SliderChanged && e.source == change.slider)
				    {
					    e.value = change.value;
					    return;
				    }
			    uiEvents.push_back({UIEvent::Kind::SliderChanged, change.slider, change.value, {}});
		    });
		onAction = events->engine.Subscribe<UIEvents::CustomAction>(
		    [](const UIEvents::CustomAction& action)
		    { uiEvents.push_back({UIEvent::Kind::CustomAction, action.button, 0.0f, action.actionId}); });
	}

	namespace EntityContext
	{
		const std::vector<UIEvent>& UIEvents()
		{
			return uiEvents;
		}

		void ClearUIEvents()
		{
			uiEvents.clear();
		}
	}

	void RegisterUIBindings()
	{
		// UIText
		Add("Crucible.UIText::GetTextInternal", &ManagedUITextGetText);
		Add("Crucible.UIText::SetTextInternal", &ManagedUITextSetText);
		Add("Crucible.UIText::GetColorInternal", &GetColor<UIText, &UIText::colour>);
		Add("Crucible.UIText::SetColorInternal", &SetColor<UIText, &UIText::colour>);
		Add("Crucible.UIText::GetFontSizeInternal", &GetFloat<UIText, &UIText::fontSize>);
		Add("Crucible.UIText::SetFontSizeInternal", &SetFloat<UIText, &UIText::fontSize, false>);
		Add("Crucible.UIText::GetFontInternal", &GetAsset<UIText, &UIText::fontAsset>);
		Add("Crucible.UIText::SetFontInternal", &SetAsset<UIText, &UIText::fontAsset, &EntityContext::IsFontAsset>);

		// UIImage
		Add("Crucible.UIImage::GetColorInternal", &GetColor<UIImage, &UIImage::colour>);
		Add("Crucible.UIImage::SetColorInternal", &SetColor<UIImage, &UIImage::colour>);
		Add("Crucible.UIImage::GetTextureInternal", &GetAsset<UIImage, &UIImage::textureAsset>);
		Add("Crucible.UIImage::SetTextureInternal",
		    &SetAsset<UIImage, &UIImage::textureAsset, &EntityContext::IsTextureAsset>);

		// Texture / Font assets
		Add("Crucible.Texture::GetNameInternal", &ManagedAssetGetName);
		Add("Crucible.Font::GetNameInternal", &ManagedAssetGetName);

		// UISlider
		Add("Crucible.UISlider::GetValueInternal", &GetFloat<UISlider, &UISlider::value>);
		Add("Crucible.UISlider::SetValueInternal", &ManagedUISliderSetValue);
		Add("Crucible.UISlider::GetMinValueInternal", &GetFloat<UISlider, &UISlider::minValue>);
		Add("Crucible.UISlider::SetMinValueInternal", &SetFloat<UISlider, &UISlider::minValue, true>);
		Add("Crucible.UISlider::GetMaxValueInternal", &GetFloat<UISlider, &UISlider::maxValue>);
		Add("Crucible.UISlider::SetMaxValueInternal", &SetFloat<UISlider, &UISlider::maxValue, true>);
		Add("Crucible.UISlider::GetWholeNumbersInternal", &GetFlag<UISlider, &UISlider::wholeNumbers>);
		Add("Crucible.UISlider::SetWholeNumbersInternal", &SetFlag<UISlider, &UISlider::wholeNumbers>);
		Add("Crucible.UISlider::GetInteractableInternal", &GetFlag<UISlider, &UISlider::interactable>);
		Add("Crucible.UISlider::SetInteractableInternal", &SetFlag<UISlider, &UISlider::interactable>);
		Add("Crucible.UISlider::GetStateInternal", &GetState<UISlider>);
		Add("Crucible.UISlider::WasChangedInternal", &ManagedUISliderWasChanged);

		// UIButton
		Add("Crucible.UIButton::GetInteractableInternal", &GetFlag<UIButton, &UIButton::interactable>);
		Add("Crucible.UIButton::SetInteractableInternal", &SetFlag<UIButton, &UIButton::interactable>);
		Add("Crucible.UIButton::GetStateInternal", &GetState<UIButton>);
		Add("Crucible.UIButton::WasClickedInternal", &ManagedUIButtonWasClicked);

		// UIRect
		Add("Crucible.UIRect::GetPositionInternal", &GetVec2<UIRect, &UIRect::position>);
		Add("Crucible.UIRect::SetPositionInternal", &SetVec2<UIRect, &UIRect::position>);
		Add("Crucible.UIRect::GetSizeInternal", &GetVec2<UIRect, &UIRect::size>);
		Add("Crucible.UIRect::SetSizeInternal", &SetVec2<UIRect, &UIRect::size>);
		Add("Crucible.UIRect::GetRotationInternal", &GetFloat<UIRect, &UIRect::rotation>);
		Add("Crucible.UIRect::SetRotationInternal", &SetFloat<UIRect, &UIRect::rotation, true>);
		Add("Crucible.UIRect::GetScreenRectInternal", &ManagedUIRectGetScreenRect);

		// UICanvas
		Add("Crucible.UICanvas::GetEnabledInternal", &GetFlag<UICanvas, &UICanvas::enabled>);
		Add("Crucible.UICanvas::SetEnabledInternal", &SetFlag<UICanvas, &UICanvas::enabled>);

		// UI (static)
		Add("Crucible.UI::GetMenuOpenInternal", &ManagedUIGetMenuOpen);
		Add("Crucible.UI::SetMenuOpenInternal", &ManagedUISetMenuOpen);
		Add("Crucible.UI::WasActionTriggeredInternal", &ManagedUIWasActionTriggered);
	}
}
#endif
