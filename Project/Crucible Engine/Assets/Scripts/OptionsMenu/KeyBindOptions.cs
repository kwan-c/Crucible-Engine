using System;
namespace @Crucible.@Scripts
{
    public sealed class @KeyBindOptions : global::Crucible.EntityScript
    {
        public Entity optionsCanvasEntity;
        public Entity forwardButton;
        public Entity backwardButton;
        public Entity leftButton;
        public Entity rightButton;
        public Entity jumpButton;
        public Entity gamepadJumpButton;
        public Entity mouseSensitivitySlider;
        public Entity stickSensitivitySlider;
        public Entity invertYButton;

        private UICanvas canvas;
        private UIButton[] buttons;
        private UIText[] labels;
        private UISlider mouseSlider, stickSlider;
        private UIButton invertButton;
        private UIText invertLabel;
        private int waiting = -1;
        private bool wasVisible;
        private readonly KeyCode[] keys = (KeyCode[])Enum.GetValues(typeof(KeyCode));

        private T Component<T>(Entity entity) where T : class
        {
            return entity == null ? null : entity.GetComponent<T>();
        }

        public override void OnCreate()
        {
            //global::Crucible.Log.Info("KeyBindOptions created for entity " + this.Entity);
            canvas = Component<UICanvas>(optionsCanvasEntity);
            Entity[] entities = { forwardButton, backwardButton, leftButton,
                                  rightButton, jumpButton, gamepadJumpButton };
            buttons = new UIButton[6];
            labels = new UIText[6];
            for (int i = 0; i < entities.Length; ++i)
            {
                buttons[i] = Component<UIButton>(entities[i]);
                labels[i] = Component<UIText>(entities[i]);
                if (buttons[i] == null || labels[i] == null)
                    Log.Warning("KeybindOptions: binding " + i + " needs an assigned button with UIText.");
            }
            mouseSlider = Component<UISlider>(mouseSensitivitySlider);
            stickSlider = Component<UISlider>(stickSensitivitySlider);
            invertButton = Component<UIButton>(invertYButton);
            invertLabel = Component<UIText>(invertYButton);
            Configure(mouseSlider);
            Configure(stickSlider);
            if (canvas == null)
                Log.Warning("KeybindOptions: assign Options Canvas.");
            Refresh();
        }

        private void Configure(UISlider slider)
        {
            if (slider == null) return;
            slider.MinValue = 0.1f;
            slider.MaxValue = 10f;
            slider.WholeNumbers = false;
        }

        public override void OnUpdate(float deltaTime)
        {
            bool visible = canvas != null && canvas.Enabled
                && optionsCanvasEntity.IsEnabled<UICanvas>() && Entity.IsEnabled<UIRect>();
            // Scripts still run when their UI is hidden. Cancel capture on leaving the page.
            if (!visible || UI.WasActionTriggered("CloseOptions")
                || UI.WasActionTriggered("TabGraphics") || UI.WasActionTriggered("TabAudio"))
            {
                if (waiting >= 0) { waiting = -1; Refresh(); }
                wasVisible = false;
                return;
            }
            if (!wasVisible) { Refresh(); wasVisible = true; }

            for (int i = 0; i < buttons.Length; ++i)
            {
                if (buttons[i] == null || !buttons[i].WasClicked) continue;
                waiting = i;
                Refresh();
                if (labels[i] != null) labels[i].Text = "Press key...";
                return; // Never capture the click that starts rebinding.
            }

            if (waiting >= 0)
            {
                if (Input.IsPressed(KeyCode.Escape))
                {
                    waiting = -1;
                    Refresh();
                    return;
                }
                foreach (KeyCode key in keys)
                {
                    // Reserve Escape for menus; mouse clicks remain UI interactions.
                    if (key == KeyCode.Unknown || key == KeyCode.Count || key == KeyCode.Escape
                        || (key >= KeyCode.MouseLeft && key <= KeyCode.MouseButton8)) continue;
                    bool gamepad = key >= KeyCode.GamepadA;
                    if ((waiting == 5) != gamepad || !Input.IsPressed(key)) continue;
                    SetKey(waiting, key);
                    waiting = -1;
                    Refresh();
                    break;
                }
                return;
            }

            if (mouseSlider != null && mouseSlider.WasChanged)
                InputSettings.MouseSensitivity = mouseSlider.Value;
            if (stickSlider != null && stickSlider.WasChanged)
                InputSettings.StickSensitivity = stickSlider.Value;
            if (invertButton != null && invertButton.WasClicked)
            {
                InputSettings.InvertY = !InputSettings.InvertY;
                Refresh();
            }
        }
        private void Refresh()
        {
            KeyCode[] current = { InputSettings.ForwardKey, InputSettings.BackKey,
                InputSettings.LeftKey, InputSettings.RightKey, InputSettings.JumpKey,
                InputSettings.JumpButton };
            for (int i = 0; i < current.Length; ++i)
                if (labels[i] != null) labels[i].Text = current[i].ToString();
            if (mouseSlider != null) mouseSlider.Value = InputSettings.MouseSensitivity;
            if (stickSlider != null) stickSlider.Value = InputSettings.StickSensitivity;
            if (invertLabel != null) invertLabel.Text = InputSettings.InvertY ? "On" : "Off";
        }

        private void SetKey(int index, KeyCode key)
        {
            switch (index)
            {
                case 0: InputSettings.ForwardKey = key; break;
                case 1: InputSettings.BackKey = key; break;
                case 2: InputSettings.LeftKey = key; break;
                case 3: InputSettings.RightKey = key; break;
                case 4: InputSettings.JumpKey = key; break;
                case 5: InputSettings.JumpButton = key; break;
            }
        }

        public override void OnDestroy()
        {
            global::Crucible.Log.Info("KeyBindOptions destroyed for entity " + this.Entity);
        }
    }
}

