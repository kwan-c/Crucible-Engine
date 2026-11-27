/*************************************************************************
* \file:         UI.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Script access to the game UI: UIText, UIImage, UISlider,
*                UIButton, UIRect, UICanvas and the static UI class.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*        GetComponent<UIText>().Text = "Ammo: " + ammo;                    // HUD
*        fill.GetComponent<UIRect>().Size = new Vector2(300f * hp, 20f);    // health bar
*        if (start.GetComponent<UIButton>().WasClicked) Scene.Load("Scenes/Level1.scene");
*        var vol = GetComponent<UISlider>();
*        if (vol.WasChanged) Audio.MusicVolume = vol.Value;                 // settings menu
*        menu.GetComponent<UICanvas>().Enabled = paused;                     // show / hide a panel
*
*    Clicks are POLLED in OnUpdate: WasClicked / WasChanged /
*    UI.WasActionTriggered are true for exactly the frame the player did it
*    (the UI handles the mouse before scripts run, so it is the same frame).
*    Code setting UISlider.Value does not count as a change.
*
*    Colours are 0-1 sRGB with alpha. UIRect positions and sizes are in
*    reference pixels (the canvas' layout page) with +y DOWN.
*
*    C++ side: Engine/src/Scripting/Bindings/UIBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>What the mouse is doing to a button or slider.</summary>
    public enum UIState
    {
        Normal = 0,
        Hovered = 1,
        /// <summary>Held down (a slider: being dragged).</summary>
        Pressed = 2,
        /// <summary>Not interactable.</summary>
        Disabled = 3,
    }

    /// <summary>Text drawn inside a UIRect. Unity's Text / TextMeshPro.</summary>
    public sealed class UIText
    {
        private readonly EntityIdentity entity;
        internal UIText(EntityIdentity owner) { entity = owner; }

        /// <summary>The entity this component is on (Unity's .gameObject), e.g. target.Entity.Name.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }
        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The text. "\n" starts a new line; rich text tags like &lt;b&gt; work if enabled.</summary>
        public string Text
        {
            get { return GetTextInternal(I, G) ?? string.Empty; }
            set { SetTextInternal(I, G, value ?? string.Empty); }
        }

        /// <summary>Text colour (sRGB, alpha = opacity).</summary>
        public Color Color
        {
            get { Color c; return GetColorInternal(I, G, out c) != 0 ? c : Color.White; }
            set { SetColorInternal(I, G, ref value); }
        }

        /// <summary>Size in reference pixels (ignored when Best Fit is on).</summary>
        public float FontSize
        {
            get { float v; return GetFontSizeInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetFontSizeInternal(I, G, ref value); }
        }

        /// <summary>
        /// The font (a public Font field set in the Inspector); null = the default font.
        /// A .ttf / .otf must have been compiled in the Font Importer.
        /// </summary>
        public Font Font
        {
            get { int low, high; return GetFontInternal(I, G, out low, out high) != 0 ? Font.FromId(low, high) : null; }
            set { SetFontInternal(I, G, value != null ? value.IdLow : 0, value != null ? value.IdHigh : 0); }
        }

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetFontInternal(int index, int generation, out int low, out int high);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetFontInternal(int index, int generation, int low, int high);

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern string GetTextInternal(int index, int generation);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetTextInternal(int index, int generation, string value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetColorInternal(int index, int generation, out Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetColorInternal(int index, int generation, ref Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetFontSizeInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetFontSizeInternal(int index, int generation, ref float value);
    }

    /// <summary>A coloured / textured rectangle - also a button's picture. Unity's Image.</summary>
    public sealed class UIImage
    {
        private readonly EntityIdentity entity;
        internal UIImage(EntityIdentity owner) { entity = owner; }

        /// <summary>The entity this component is on (Unity's .gameObject), e.g. target.Entity.Name.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }
        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>Tint (or the colour, with no texture). Alpha 0 hides it.</summary>
        public Color Color
        {
            get { Color c; return GetColorInternal(I, G, out c) != 0 ? c : Color.White; }
            set { SetColorInternal(I, G, ref value); }
        }

        /// <summary>The picture (a public Texture field set in the Inspector); null = a solid Color.</summary>
        public Texture Texture
        {
            get { int low, high; return GetTextureInternal(I, G, out low, out high) != 0 ? Texture.FromId(low, high) : null; }
            set { SetTextureInternal(I, G, value != null ? value.IdLow : 0, value != null ? value.IdHigh : 0); }
        }

        /// <summary>Visible fraction from the left (0–1). Crops the image while preserving its texture scale and layout.</summary>
        public float HorizontalFill
        {
            get { float v; return GetHorizontalFillInternal(I, G, out v) != 0 ? v : 1f; }
            set { SetHorizontalFillInternal(I, G, ref value); }
        }

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetTextureInternal(int index, int generation, out int low, out int high);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetTextureInternal(int index, int generation, int low, int high);

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetColorInternal(int index, int generation, out Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetColorInternal(int index, int generation, ref Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetHorizontalFillInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetHorizontalFillInternal(int index, int generation, ref float value);
    }

    /// <summary>A draggable value. Unity's Slider.</summary>
    public sealed class UISlider
    {
        private readonly EntityIdentity entity;
        internal UISlider(EntityIdentity owner) { entity = owner; }

        /// <summary>The entity this component is on (Unity's .gameObject), e.g. target.Entity.Name.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }
        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The value; setting it clamps to Min..Max (and rounds if WholeNumbers). Does not set WasChanged.</summary>
        public float Value
        {
            get { float v; return GetValueInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetValueInternal(I, G, ref value); }
        }

        public float MinValue
        {
            get { float v; return GetMinValueInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetMinValueInternal(I, G, ref value); }
        }

        public float MaxValue
        {
            get { float v; return GetMaxValueInternal(I, G, out v) != 0 ? v : 1f; }
            set { SetMaxValueInternal(I, G, ref value); }
        }

        /// <summary>Snap the value to whole numbers (a 0-10 setting).</summary>
        public bool WholeNumbers
        {
            get { int v; return GetWholeNumbersInternal(I, G, out v) != 0 && v != 0; }
            set { SetWholeNumbersInternal(I, G, value ? 1 : 0); }
        }

        /// <summary>Can the player drag it?</summary>
        public bool Interactable
        {
            get { int v; return GetInteractableInternal(I, G, out v) != 0 && v != 0; }
            set { SetInteractableInternal(I, G, value ? 1 : 0); }
        }

        /// <summary>Hovered, Pressed (being dragged), Disabled or Normal.</summary>
        public UIState State
        {
            get { int v; return GetStateInternal(I, G, out v) != 0 ? (UIState)v : UIState.Normal; }
        }

        /// <summary>Did the player change it this frame (press or drag)? Read it in OnUpdate.</summary>
        public bool WasChanged
        {
            get { int v; return WasChangedInternal(I, G, out v) != 0 && v != 0; }
        }

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetValueInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetValueInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetMinValueInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetMinValueInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetMaxValueInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetMaxValueInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetWholeNumbersInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetWholeNumbersInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetInteractableInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetInteractableInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetStateInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int WasChangedInternal(int index, int generation, out int value);
    }

    /// <summary>Makes an element clickable. Unity's Button.</summary>
    public sealed class UIButton
    {
        private readonly EntityIdentity entity;
        internal UIButton(EntityIdentity owner) { entity = owner; }

        /// <summary>The entity this component is on (Unity's .gameObject), e.g. target.Entity.Name.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }
        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>Was it clicked this frame (pressed and released over it)? Read it in OnUpdate.</summary>
        public bool WasClicked
        {
            get { int v; return WasClickedInternal(I, G, out v) != 0 && v != 0; }
        }

        /// <summary>Can it be clicked? (Disabled buttons are drawn faded.)</summary>
        public bool Interactable
        {
            get { int v; return GetInteractableInternal(I, G, out v) != 0 && v != 0; }
            set { SetInteractableInternal(I, G, value ? 1 : 0); }
        }

        /// <summary>Hovered, Pressed, Disabled or Normal.</summary>
        public UIState State
        {
            get { int v; return GetStateInternal(I, G, out v) != 0 ? (UIState)v : UIState.Normal; }
        }

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int WasClickedInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetInteractableInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetInteractableInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetStateInternal(int index, int generation, out int value);
    }

    /// <summary>A UI element's place and size. Unity's RectTransform (the commonly used parts).</summary>
    public sealed class UIRect
    {
        private readonly EntityIdentity entity;
        internal UIRect(EntityIdentity owner) { entity = owner; }

        /// <summary>The entity this component is on (Unity's .gameObject), e.g. target.Entity.Name.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }
        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The pivot's offset from the anchors, in reference pixels, +y DOWN.</summary>
        public Vector2 Position
        {
            get { Vector2 v; return GetPositionInternal(I, G, out v) != 0 ? v : Vector2.Zero; }
            set { SetPositionInternal(I, G, ref value); }
        }

        /// <summary>Width and height in reference pixels (extra size when the anchors stretch).</summary>
        public Vector2 Size
        {
            get { Vector2 v; return GetSizeInternal(I, G, out v) != 0 ? v : Vector2.Zero; }
            set { SetSizeInternal(I, G, ref value); }
        }

        /// <summary>Degrees, counter-clockwise, around the pivot.</summary>
        public float Rotation
        {
            get { float v; return GetRotationInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetRotationInternal(I, G, ref value); }
        }

        /// <summary>
        /// Where it was drawn last frame, in screen pixels (top-left origin) - e.g. to compare
        /// with Camera.WorldToScreenPoint. False before it has been laid out.
        /// </summary>
        public bool GetScreenRect(out Vector2 min, out Vector2 max)
        {
            return GetScreenRectInternal(I, G, out min, out max) != 0;
        }

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetPositionInternal(int index, int generation, out Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetPositionInternal(int index, int generation, ref Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetSizeInternal(int index, int generation, out Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetSizeInternal(int index, int generation, ref Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetRotationInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetRotationInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetScreenRectInternal(int index, int generation, out Vector2 min, out Vector2 max);
    }

    /// <summary>The root of a UI tree (a HUD, a menu). Unity's Canvas.</summary>
    public sealed class UICanvas
    {
        private readonly EntityIdentity entity;
        internal UICanvas(EntityIdentity owner) { entity = owner; }

        /// <summary>The entity this component is on (Unity's .gameObject), e.g. target.Entity.Name.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }
        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>False hides the whole canvas and it ignores the mouse.</summary>
        public bool Enabled
        {
            get { int v; return GetEnabledInternal(I, G, out v) != 0 && v != 0; }
            set { SetEnabledInternal(I, G, value ? 1 : 0); }
        }

        /// <summary>ScreenSpace (over the game screen) or WorldSpace (a panel in the 3D world). Set in the Inspector.</summary>
        public CanvasRenderMode RenderMode
        {
            get { int v; return GetRenderModeInternal(I, G, out v) != 0 ? (CanvasRenderMode)v : CanvasRenderMode.ScreenSpace; }
        }

        /// <summary>With several canvases, higher draws on top.</summary>
        public int SortOrder
        {
            get { int v; return GetSortOrderInternal(I, G, out v) != 0 ? v : 0; }
            set { SetSortOrderInternal(I, G, value); }
        }

        // --- WorldSpace only (a screen-space canvas keeps them but ignores them) ---- //
        // The page is laid out as on screen (its UIText / UIImage children work as usual) and
        // placed in the world by this entity's Transform. Nothing on it reacts to the mouse.

        /// <summary>Canvas pixels per metre: a 400 x 100 page at 100 is 4 x 1 m.</summary>
        public float PixelsPerUnit
        {
            get { float v; return GetPixelsPerUnitInternal(I, G, out v) != 0 ? v : 100f; }
            set { SetPixelsPerUnitInternal(I, G, ref value); }
        }

        /// <summary>The page's point on this entity: (0, 0) top-left, (0.5, 0.5) centre.</summary>
        public Vector2 Pivot
        {
            get { Vector2 v; return GetPivotInternal(I, G, out v) != 0 ? v : new Vector2(0.5f, 0.5f); }
            set { SetPivotInternal(I, G, ref value); }
        }

        /// <summary>Off (the Transform's rotation), FaceCamera, or Upright (health bars over heads).</summary>
        public Billboard Billboard
        {
            get { int v; return GetBillboardInternal(I, G, out v) != 0 ? (Billboard)v : Billboard.Off; }
            set { SetBillboardInternal(I, G, (int)value); }
        }

        /// <summary>Drawn over everything, not hidden behind walls.</summary>
        public bool AlwaysOnTop
        {
            get { int v; return GetAlwaysOnTopInternal(I, G, out v) != 0 && v != 0; }
            set { SetAlwaysOnTopInternal(I, G, value ? 1 : 0); }
        }

        /// <summary>Also drawn (mirrored) when seen from behind.</summary>
        public bool DoubleSided
        {
            get { int v; return GetDoubleSidedInternal(I, G, out v) != 0 && v != 0; }
            set { SetDoubleSidedInternal(I, G, value ? 1 : 0); }
        }

        /// <summary>Brightness in the scene; above 1 glows with bloom.</summary>
        public float Intensity
        {
            get { float v; return GetIntensityInternal(I, G, out v) != 0 ? v : 1f; }
            set { SetIntensityInternal(I, G, ref value); }
        }

        /// <summary>Fade out with distance from the camera (FadeStart .. FadeEnd).</summary>
        public bool Fade
        {
            get { int v; return GetFadeInternal(I, G, out v) != 0 && v != 0; }
            set { SetFadeInternal(I, G, value ? 1 : 0); }
        }

        /// <summary>World units: fully visible up to here.</summary>
        public float FadeStart
        {
            get { float v; return GetFadeStartInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetFadeStartInternal(I, G, ref value); }
        }

        /// <summary>World units: gone from here.</summary>
        public float FadeEnd
        {
            get { float v; return GetFadeEndInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetFadeEndInternal(I, G, ref value); }
        }

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetEnabledInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetEnabledInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetRenderModeInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetSortOrderInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetSortOrderInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetPixelsPerUnitInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetPixelsPerUnitInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetPivotInternal(int index, int generation, out Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetPivotInternal(int index, int generation, ref Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetBillboardInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetBillboardInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetAlwaysOnTopInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetAlwaysOnTopInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetDoubleSidedInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetDoubleSidedInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetIntensityInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetIntensityInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetFadeInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetFadeInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetFadeStartInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetFadeStartInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetFadeEndInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetFadeEndInternal(int index, int generation, ref float value);
    }

    /// <summary>Where a UICanvas draws. Same values as UICanvas::RenderMode.</summary>
    public enum CanvasRenderMode
    {
        /// <summary>Over the game screen, always on top (Unity: Screen Space - Overlay).</summary>
        ScreenSpace = 0,
        /// <summary>A panel in the 3D world, placed by the canvas entity's Transform.</summary>
        WorldSpace = 1,
    }

    /// <summary>Game-wide UI: the in-game menu and named button actions.</summary>
    public static class UI
    {
        /// <summary>
        /// Is the in-game menu (canvases with Toggle With Escape) open? Setting it opens or
        /// closes it at the start of the next frame, exactly like pressing Escape.
        /// </summary>
        public static bool MenuOpen
        {
            get { int v; return GetMenuOpenInternal(out v) != 0 && v != 0; }
            set { SetMenuOpenInternal(value ? 1 : 0); }
        }

        /// <summary>
        /// Was a button whose On Click is "Custom" with this Action Id clicked this frame?
        /// Lets one script handle many buttons by name: WasActionTriggered("OpenSettings").
        /// </summary>
        public static bool WasActionTriggered(string actionId)
        {
            int v;
            return !string.IsNullOrEmpty(actionId) && WasActionTriggeredInternal(actionId, out v) != 0 && v != 0;
        }

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetMenuOpenInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetMenuOpenInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int WasActionTriggeredInternal(string actionId, out int value);
    }
}
