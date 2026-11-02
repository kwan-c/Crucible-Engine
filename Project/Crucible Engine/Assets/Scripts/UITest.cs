using System;

namespace Crucible.Scripts
{
    // Tests the UI scripting API. Attach to any entity and set whichever fields you have
    // (each part is skipped if its field is empty):
    //   Text    - an entity with a UIText: shows the time and frame count every frame
    //   Button  - an entity with a UIButton: logs clicks, and each click tints its UIImage
    //   Slider  - an entity with a UISlider: logs player changes and drives Bar's width
    //   Bar     - an entity with a UIRect (and UIImage): its width follows the slider (a health bar)
    //   Canvas  - an entity with a UICanvas: V shows / hides it
    //   Action Id - a Custom button's Action Id: logs when that action fires
    //   Swap Texture - an image: J swaps the Button's UIImage texture to it and back
    //   Swap Font    - a font:   F swaps the Text's font to it and back
    // Keys:  V toggle Canvas   N toggle UI.MenuOpen   K make Button (un)clickable
    // The OnButtonClicked / OnSliderChanged / OnUIAction callbacks log every UI event too.
    public sealed class UITest : EntityScript
    {
        public Entity text;
        public Entity button;
        public Entity slider;
        public Entity bar;
        public Entity canvas;
        public string actionId = "";
        public float barMaxWidth = 300f;
        public Texture swapTexture;
        public Font swapFont;

        Texture originalTexture;
        Font originalFont;

        UIText label;
        UIButton btn;
        UIImage btnImage;
        UISlider sld;
        UIRect barRect;
        UICanvas cnv;
        int clicks;
        readonly Color[] tints = { Color.White, Color.Red, Color.Green, Color.Cyan, Color.Yellow };

        public override void OnCreate()
        {
            label    = text   != null ? text.GetComponent<UIText>()     : null;
            btn      = button != null ? button.GetComponent<UIButton>() : null;
            btnImage = button != null ? button.GetComponent<UIImage>()  : null;
            sld      = slider != null ? slider.GetComponent<UISlider>() : null;
            barRect  = bar    != null ? bar.GetComponent<UIRect>()      : null;
            cnv      = canvas != null ? canvas.GetComponent<UICanvas>() : null;

            Log.Info("[UITest] text " + (label != null) + ", button " + (btn != null) + ", slider " + (sld != null) +
                     ", bar " + (barRect != null) + ", canvas " + (cnv != null) + ", menu open " + UI.MenuOpen);
            if (sld != null)
                Log.Info("[UITest] Slider " + sld.MinValue + ".." + sld.MaxValue + " = " + sld.Value + " (whole numbers " + sld.WholeNumbers + ")");
            if (barRect != null)
                Log.Info("[UITest] Bar position " + barRect.Position + ", size " + barRect.Size);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (label != null)
                label.Text = "Time " + Time.ElapsedTime.ToString("0.0") + " s\nFrame " + Time.FrameCount +
                             (clicks > 0 ? "\nClicks " + clicks : "");

            if (btn != null && btn.WasClicked)
            {
                ++clicks;
                if (btnImage != null) btnImage.Color = tints[clicks % tints.Length];
                Log.Info("[UITest] Button clicked (" + clicks + "), state " + btn.State);
            }

            if (sld != null)
            {
                if (sld.WasChanged)
                    Log.Info("[UITest] Slider changed by the player: " + sld.Value.ToString("0.00"));
                if (barRect != null)
                {
                    float t = (sld.Value - sld.MinValue) / Math.Max(1e-6f, sld.MaxValue - sld.MinValue);
                    barRect.Size = new Vector2(barMaxWidth * t, barRect.Size.Y);
                }
            }

            if (actionId.Length > 0 && UI.WasActionTriggered(actionId))
                Log.Info("[UITest] Custom action '" + actionId + "' fired");

            if (Input.IsPressed(KeyCode.V) && cnv != null)
            {
                cnv.Enabled = !cnv.Enabled;
                Log.Info("[UITest] Canvas " + (cnv.Enabled ? "shown" : "hidden"));
            }
            if (Input.IsPressed(KeyCode.N))
            {
                UI.MenuOpen = !UI.MenuOpen;
                Log.Info("[UITest] Menu open -> " + !UI.MenuOpen + " (applied next frame)");
            }
            if (Input.IsPressed(KeyCode.K) && btn != null)
            {
                btn.Interactable = !btn.Interactable;
                Log.Info("[UITest] Button interactable " + btn.Interactable);
            }

            if (Input.IsPressed(KeyCode.J) && btnImage != null && swapTexture != null)
            {
                bool swapped = btnImage.Texture == swapTexture;
                if (!swapped) originalTexture = btnImage.Texture;
                btnImage.Texture = swapped ? originalTexture : swapTexture;
                Log.Info("[UITest] Button texture -> " + (btnImage.Texture != null ? btnImage.Texture.Name : "none (solid colour)"));
            }
            if (Input.IsPressed(KeyCode.F) && label != null && swapFont != null)
            {
                bool swapped = label.Font == swapFont;
                if (!swapped) originalFont = label.Font;
                label.Font = swapped ? originalFont : swapFont;
                Log.Info("[UITest] Text font -> " + (label.Font != null ? label.Font.Name : "default"));
            }
        }

        // --- UI callbacks: every UI event, once, before this frame's OnUpdate ---------

        public override void OnButtonClicked(Entity clicked)
        {
            Log.Info("[UITest] OnButtonClicked(" + clicked.Name + ")" + (clicked == button ? " - that's my Button" : ""));
        }

        public override void OnSliderChanged(Entity changed, float value)
        {
            Log.Info("[UITest] OnSliderChanged(" + changed.Name + ", " + value.ToString("0.00") + ")");
        }

        public override void OnUIAction(string action)
        {
            Log.Info("[UITest] OnUIAction(\"" + action + "\")");
        }
    }
}
