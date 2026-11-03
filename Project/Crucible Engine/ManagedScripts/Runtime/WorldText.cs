/*************************************************************************
* \file:         WorldText.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Script access to an entity's WorldText component: text in
*                the 3D world (signs, nameplates, damage numbers). Also the
*                Billboard / alignment enums shared with world-space UI.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Unity's TextMeshPro (3D). Get it with GetComponent<WorldText>() or
*    Entity.WorldText:
*
*        var label = GetComponent<WorldText>();
*        label.Text = "HP " + hp;
*        label.Color = hp < 20 ? Color.Red : Color.White;
*        label.Billboard = Billboard.Upright;     // a nameplate
*
*    Units are WORLD UNITS (metres) before the Transform's scale: FontSize is
*    the em (a capital is ~0.7 of it), Size is the text box, effect widths too.
*    The text sits on the entity's position (Pivot picks the box's point:
*    (0, 0) top-left, (1, 1) bottom-right) and turns with its Transform.
*
*    Changes show the same frame. The text is only laid out again when the
*    text or a layout setting changes - setting the same Text every frame is
*    free; a different one every frame (a timer) is fine for a few labels.
*    Colours are sRGB 0..1 (clamped); Intensity above 1 glows with bloom.
*
*    C++ side: Engine/src/Scripting/Bindings/UIBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>How a flat thing in the world turns toward the camera (WorldText, world-space UICanvas).</summary>
    public enum Billboard
    {
        /// <summary>Faces the way its Transform says.</summary>
        Off = 0,
        /// <summary>Always square on to the camera.</summary>
        FaceCamera = 1,
        /// <summary>Turns around Y only, so it never tips back when seen from above (nameplates).</summary>
        Upright = 2,
    }

    /// <summary>Where lines sit across the text box.</summary>
    public enum HorizontalAlign { Left = 0, Center = 1, Right = 2 }

    /// <summary>Where the block of lines sits down the text box.</summary>
    public enum VerticalAlign { Top = 0, Middle = 1, Bottom = 2 }

    /// <summary>What happens to text that does not fit the box.</summary>
    public enum TextOverflow
    {
        /// <summary>Drawn past the box's edges.</summary>
        Overflow = 0,
        /// <summary>Cut at the box's edges.</summary>
        Truncate = 1,
        /// <summary>Cut, with "..." at the end.</summary>
        Ellipsis = 2,
    }

    /// <summary>Text in the 3D world. Unity's TextMeshPro (3D).</summary>
    public sealed class WorldText
    {
        private readonly EntityIdentity entity;
        internal WorldText(EntityIdentity owner) { entity = owner; }
        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The entity this text is on (move it through Entity.Transform).</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }

        // --- text ------------------------------------------------------------------ //

        /// <summary>The text. "\n" starts a new line; &lt;b&gt; &lt;u&gt; &lt;color=#f00&gt; tags with RichText.</summary>
        public string Text
        {
            get { return GetTextInternal(I, G) ?? string.Empty; }
            set { SetTextInternal(I, G, value ?? string.Empty); }
        }

        /// <summary>The letters' colour (sRGB, 0..1).</summary>
        public Color Color
        {
            get { Color c; return GetColorInternal(I, G, out c) != 0 ? c : Color.White; }
            set { SetColorInternal(I, G, ref value); }
        }

        /// <summary>The em size in world units (0.5 = letters about 35 cm tall). Ignored with BestFit.</summary>
        public float FontSize
        {
            get { float v; return GetFontSizeInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetFontSizeInternal(I, G, ref value); }
        }

        /// <summary>The font (a public Font field set in the Inspector); null = the default font. SDF / MTSDF fonts look best.</summary>
        public Font Font
        {
            get { int low, high; return GetFontInternal(I, G, out low, out high) != 0 ? Font.FromId(low, high) : null; }
            set { SetFontInternal(I, G, value != null ? value.IdLow : 0, value != null ? value.IdHigh : 0); }
        }

        // --- placement --------------------------------------------------------------- //

        /// <summary>The text box: width, height in world units.</summary>
        public Vector2 Size
        {
            get { Vector2 v; return GetSizeInternal(I, G, out v) != 0 ? v : Vector2.Zero; }
            set { SetSizeInternal(I, G, ref value); }
        }

        /// <summary>The box's point on the entity's position: (0, 0) top-left, (0.5, 0.5) centre, (1, 1) bottom-right.</summary>
        public Vector2 Pivot
        {
            get { Vector2 v; return GetPivotInternal(I, G, out v) != 0 ? v : new Vector2(0.5f, 0.5f); }
            set { SetPivotInternal(I, G, ref value); }
        }

        /// <summary>Off (the Transform's rotation), FaceCamera, or Upright.</summary>
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

        /// <summary>Brightness in the scene; above 1 glows with bloom (neon signs).</summary>
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

        // --- layout ----------------------------------------------------------------- //

        public HorizontalAlign HorizontalAlign
        {
            get { int v; return GetHorizontalAlignInternal(I, G, out v) != 0 ? (HorizontalAlign)v : HorizontalAlign.Center; }
            set { SetHorizontalAlignInternal(I, G, (int)value); }
        }

        public VerticalAlign VerticalAlign
        {
            get { int v; return GetVerticalAlignInternal(I, G, out v) != 0 ? (VerticalAlign)v : VerticalAlign.Middle; }
            set { SetVerticalAlignInternal(I, G, (int)value); }
        }

        public TextOverflow Overflow
        {
            get { int v; return GetOverflowInternal(I, G, out v) != 0 ? (TextOverflow)v : TextOverflow.Overflow; }
            set { SetOverflowInternal(I, G, (int)value); }
        }

        /// <summary>Break lines at spaces to stay inside the box's width.</summary>
        public bool Wrap
        {
            get { int v; return GetWrapInternal(I, G, out v) != 0 && v != 0; }
            set { SetWrapInternal(I, G, value ? 1 : 0); }
        }

        /// <summary>Times the font's line height.</summary>
        public float LineSpacing
        {
            get { float v; return GetLineSpacingInternal(I, G, out v) != 0 ? v : 1f; }
            set { SetLineSpacingInternal(I, G, ref value); }
        }

        /// <summary>Extra space after each character, in ems (may be negative).</summary>
        public float CharacterSpacing
        {
            get { float v; return GetCharacterSpacingInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetCharacterSpacingInternal(I, G, ref value); }
        }

        /// <summary>Read &lt;b&gt; &lt;u&gt; &lt;color&gt; tags.</summary>
        public bool RichText
        {
            get { int v; return GetRichTextInternal(I, G, out v) != 0 && v != 0; }
            set { SetRichTextInternal(I, G, value ? 1 : 0); }
        }

        public bool Bold
        {
            get { int v; return GetBoldInternal(I, G, out v) != 0 && v != 0; }
            set { SetBoldInternal(I, G, value ? 1 : 0); }
        }

        public bool Underline
        {
            get { int v; return GetUnderlineInternal(I, G, out v) != 0 && v != 0; }
            set { SetUnderlineInternal(I, G, value ? 1 : 0); }
        }

        /// <summary>Size the text to fit the box, between MinFontSize and MaxFontSize.</summary>
        public bool BestFit
        {
            get { int v; return GetBestFitInternal(I, G, out v) != 0 && v != 0; }
            set { SetBestFitInternal(I, G, value ? 1 : 0); }
        }

        public float MinFontSize
        {
            get { float v; return GetMinFontSizeInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetMinFontSizeInternal(I, G, ref value); }
        }

        public float MaxFontSize
        {
            get { float v; return GetMaxFontSizeInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetMaxFontSizeInternal(I, G, ref value); }
        }

        // --- effects (SDF / MTSDF fonts; widths in world units) --------------------- //

        public bool Outline
        {
            get { int v; return GetOutlineInternal(I, G, out v) != 0 && v != 0; }
            set { SetOutlineInternal(I, G, value ? 1 : 0); }
        }

        public Color OutlineColor
        {
            get { Color c; return GetOutlineColorInternal(I, G, out c) != 0 ? c : Color.Black; }
            set { SetOutlineColorInternal(I, G, ref value); }
        }

        public float OutlineWidth
        {
            get { float v; return GetOutlineWidthInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetOutlineWidthInternal(I, G, ref value); }
        }

        public bool Glow
        {
            get { int v; return GetGlowInternal(I, G, out v) != 0 && v != 0; }
            set { SetGlowInternal(I, G, value ? 1 : 0); }
        }

        public Color GlowColor
        {
            get { Color c; return GetGlowColorInternal(I, G, out c) != 0 ? c : Color.White; }
            set { SetGlowColorInternal(I, G, ref value); }
        }

        public float GlowWidth
        {
            get { float v; return GetGlowWidthInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetGlowWidthInternal(I, G, ref value); }
        }

        /// <summary>0 = an even band, 1 = fades quickly.</summary>
        public float GlowSoftness
        {
            get { float v; return GetGlowSoftnessInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetGlowSoftnessInternal(I, G, ref value); }
        }

        public bool Shadow
        {
            get { int v; return GetShadowInternal(I, G, out v) != 0 && v != 0; }
            set { SetShadowInternal(I, G, value ? 1 : 0); }
        }

        public Color ShadowColor
        {
            get { Color c; return GetShadowColorInternal(I, G, out c) != 0 ? c : Color.Black; }
            set { SetShadowColorInternal(I, G, ref value); }
        }

        /// <summary>In the text's plane: +x right, +y DOWN (as UIText).</summary>
        public Vector2 ShadowOffset
        {
            get { Vector2 v; return GetShadowOffsetInternal(I, G, out v) != 0 ? v : Vector2.Zero; }
            set { SetShadowOffsetInternal(I, G, ref value); }
        }

        /// <summary>0 = crisp, 1 = as blurred as the font allows.</summary>
        public float ShadowSoftness
        {
            get { float v; return GetShadowSoftnessInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetShadowSoftnessInternal(I, G, ref value); }
        }

        public override string ToString() { return "WorldText \"" + Text + "\" on " + Entity; }

        // --- internal calls (UIBindings.cpp) ----------------------------------------- //

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern string GetTextInternal(int index, int generation);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetTextInternal(int index, int generation, string value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetColorInternal(int index, int generation, out Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetColorInternal(int index, int generation, ref Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetFontSizeInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetFontSizeInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetFontInternal(int index, int generation, out int low, out int high);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetFontInternal(int index, int generation, int low, int high);

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetSizeInternal(int index, int generation, out Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetSizeInternal(int index, int generation, ref Vector2 value);
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

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetHorizontalAlignInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetHorizontalAlignInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetVerticalAlignInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetVerticalAlignInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetOverflowInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetOverflowInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetWrapInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetWrapInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetLineSpacingInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetLineSpacingInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetCharacterSpacingInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetCharacterSpacingInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetRichTextInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetRichTextInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetBoldInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetBoldInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetUnderlineInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetUnderlineInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetBestFitInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetBestFitInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetMinFontSizeInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetMinFontSizeInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetMaxFontSizeInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetMaxFontSizeInternal(int index, int generation, ref float value);

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetOutlineInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetOutlineInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetOutlineColorInternal(int index, int generation, out Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetOutlineColorInternal(int index, int generation, ref Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetOutlineWidthInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetOutlineWidthInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetGlowInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetGlowInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetGlowColorInternal(int index, int generation, out Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetGlowColorInternal(int index, int generation, ref Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetGlowWidthInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetGlowWidthInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetGlowSoftnessInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetGlowSoftnessInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetShadowInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetShadowInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetShadowColorInternal(int index, int generation, out Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetShadowColorInternal(int index, int generation, ref Color value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetShadowOffsetInternal(int index, int generation, out Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetShadowOffsetInternal(int index, int generation, ref Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetShadowSoftnessInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetShadowSoftnessInternal(int index, int generation, ref float value);
    }
}
