namespace @Crucible.@Scripts
{
    public sealed class @GraphicsOptions : global::Crucible.EntityScript
    {
        public global::Crucible.Entity previousModeButton;
        public global::Crucible.Entity nextModeButton;
        public global::Crucible.Entity modeTextEntity;

        public global::Crucible.Entity previousResolutionButton;
        public global::Crucible.Entity nextResolutionButton;
        public global::Crucible.Entity resolutionTextEntity;

        private global::Crucible.UIButton previousMode;
        private global::Crucible.UIButton nextMode;
        private global::Crucible.UIButton previousResolution;
        private global::Crucible.UIButton nextResolution;

        private global::Crucible.UIText modeText;
        private global::Crucible.UIText resolutionText;

        private global::Crucible.Resolution[] resolutions;
        private bool wasVisible;

        private readonly global::Crucible.WindowMode[] modes =
        {
            global::Crucible.WindowMode.Windowed,
            global::Crucible.WindowMode.Maximized,
            global::Crucible.WindowMode.Fullscreen
        };

        private T Component<T>(global::Crucible.Entity entity)
            where T : class
        {
            return entity == null ? null : entity.GetComponent<T>();
        }

        public override void OnCreate()
        {
            //global::Crucible.Log.Info("GraphicsOptions created for entity " + this.Entity);
             previousMode =
                Component<global::Crucible.UIButton>(previousModeButton);
            nextMode =
                Component<global::Crucible.UIButton>(nextModeButton);

            previousResolution =
                Component<global::Crucible.UIButton>(previousResolutionButton);
            nextResolution =
                Component<global::Crucible.UIButton>(nextResolutionButton);

            modeText =
                Component<global::Crucible.UIText>(modeTextEntity);
            resolutionText =
                Component<global::Crucible.UIText>(resolutionTextEntity);

            resolutions = global::Crucible.Screen.Resolutions;
            Refresh();
        }

        public override void OnUpdate(float deltaTime)
        {
            if (!Entity.IsEnabled<global::Crucible.UIRect>())
            {
                wasVisible = false;
                return;
            }

            if (!wasVisible)
            {
                resolutions = global::Crucible.Screen.Resolutions;
                Refresh();
                wasVisible = true;
            }

            if (previousMode != null && previousMode.WasClicked)
                ChangeMode(-1);
            else if (nextMode != null && nextMode.WasClicked)
                ChangeMode(1);

            if (previousResolution != null && previousResolution.WasClicked)
                ChangeResolution(-1);
            else if (nextResolution != null && nextResolution.WasClicked)
                ChangeResolution(1);
        }
         private bool CanChangeDisplay()
        {
            if (!global::Crucible.Screen.IsEditor)
                return true;

            global::Crucible.Log.Info(
                "Graphics settings must be tested in the standalone game.");
            return false;
        }

        private void ChangeMode(int direction)
        {
            if (!CanChangeDisplay())
                return;

            int index = 0;

            for (int i = 0; i < modes.Length; ++i)
            {
                if (modes[i] == global::Crucible.Screen.Mode)
                {
                    index = i;
                    break;
                }
            }

            index = (index + direction + modes.Length) % modes.Length;
            global::Crucible.Screen.Mode = modes[index];
            Refresh();
        }

        private void GetSelectedSize(out int width, out int height)
        {
            if (global::Crucible.Screen.Mode ==
                global::Crucible.WindowMode.Fullscreen)
            {
                width = global::Crucible.Screen.FullscreenWidth;
                height = global::Crucible.Screen.FullscreenHeight;

                // Borderless uses the current screen size.
                if (width <= 0 || height <= 0)
                {
                    width = global::Crucible.Screen.Width;
                    height = global::Crucible.Screen.Height;
                }
            }
            else
            {
                width = global::Crucible.Screen.WindowedWidth;
                height = global::Crucible.Screen.WindowedHeight;
            }
        }

        private void ChangeResolution(int direction)
        {
            if (!CanChangeDisplay())
                return;

            resolutions = global::Crucible.Screen.Resolutions;

            if (resolutions == null || resolutions.Length == 0)
            {
                global::Crucible.Log.Warning(
                    "No display resolutions are available.");
                return;
            }

            int width, height;
            GetSelectedSize(out width, out height);

            int index = -1;
            for (int i = 0; i < resolutions.Length; ++i)
            {
                if (resolutions[i].Width == width &&
                    resolutions[i].Height == height)
                {
                    index = i;
                    break;
                }
            }

            if (index < 0)
                index = direction > 0 ? 0 : resolutions.Length - 1;
            else
                index = (index + direction + resolutions.Length)
                    % resolutions.Length;

            var selected = resolutions[index];

            bool applied = global::Crucible.Screen.SetResolution(
                selected.Width,
                selected.Height,
                global::Crucible.Screen.Mode);

            if (!applied)
                global::Crucible.Log.Warning(
                    "Could not apply the selected resolution.");

            Refresh();
        }

        private void Refresh()
        {
            if (modeText != null)
                modeText.Text = global::Crucible.Screen.Mode.ToString();

            int width, height;
            GetSelectedSize(out width, out height);

            if (resolutionText != null)
            {
                resolutionText.Text = width + " x " + height;

                if (global::Crucible.Screen.Mode ==
                        global::Crucible.WindowMode.Fullscreen &&
                    !global::Crucible.Screen.IsExclusiveFullscreen)
                {
                    resolutionText.Text += " (Borderless)";
                }
            }
        }

        public override void OnDestroy()
        {
            global::Crucible.Log.Info("GraphicsOptions destroyed for entity " + this.Entity);
        }
    }
}
