namespace @Crucible.@Scripts
{
    /// <summary>
    /// The options menu's Graphics page.
    ///  - Window mode and resolution (Screen): applied and saved at once; ignored in the editor.
    ///  - Quality preset, anti-aliasing, shadows (&lt; / &gt; buttons) and ambient occlusion,
    ///    bloom, volumetric fog (On / Off buttons) (QualitySettings): applied at once. Apply
    ///    saves them, Revert goes back to what was last saved. Closing the menu saves too
    ///    (OptionsMenuController / InGameOptionsMenu call InputSettings.Save, which writes every
    ///    player setting). In the editor they work in Play and are undone at Stop.
    /// Every entity field is optional: a row whose entities are not assigned is skipped.
    /// </summary>
    public sealed class @GraphicsOptions : global::Crucible.EntityScript
    {
        public global::Crucible.Entity previousModeButton;
        public global::Crucible.Entity nextModeButton;
        public global::Crucible.Entity modeTextEntity;

        public global::Crucible.Entity previousResolutionButton;
        public global::Crucible.Entity nextResolutionButton;
        public global::Crucible.Entity resolutionTextEntity;

        public global::Crucible.Entity previousQualityButton;
        public global::Crucible.Entity nextQualityButton;
        public global::Crucible.Entity qualityTextEntity;

        public global::Crucible.Entity previousAntiAliasingButton;
        public global::Crucible.Entity nextAntiAliasingButton;
        public global::Crucible.Entity antiAliasingTextEntity;

        public global::Crucible.Entity previousShadowsButton;
        public global::Crucible.Entity nextShadowsButton;
        public global::Crucible.Entity shadowsTextEntity;

        // On / Off buttons: each shows its state in its own UIText.
        public global::Crucible.Entity ambientOcclusionButton;
        public global::Crucible.Entity bloomButton;
        public global::Crucible.Entity volumetricFogButton;

        public global::Crucible.Entity applyButton;
        public global::Crucible.Entity revertButton;

        private global::Crucible.UIButton previousMode;
        private global::Crucible.UIButton nextMode;
        private global::Crucible.UIButton previousResolution;
        private global::Crucible.UIButton nextResolution;
        private global::Crucible.UIButton previousQuality;
        private global::Crucible.UIButton nextQuality;
        private global::Crucible.UIButton previousAntiAliasing;
        private global::Crucible.UIButton nextAntiAliasing;
        private global::Crucible.UIButton previousShadows;
        private global::Crucible.UIButton nextShadows;
        private global::Crucible.UIButton ambientOcclusion;
        private global::Crucible.UIButton bloom;
        private global::Crucible.UIButton volumetricFog;
        private global::Crucible.UIButton apply;
        private global::Crucible.UIButton revert;

        private global::Crucible.UIText modeText;
        private global::Crucible.UIText resolutionText;
        private global::Crucible.UIText qualityText;
        private global::Crucible.UIText antiAliasingText;
        private global::Crucible.UIText shadowsText;
        private global::Crucible.UIText ambientOcclusionText;
        private global::Crucible.UIText bloomText;
        private global::Crucible.UIText volumetricFogText;

        private global::Crucible.Resolution[] resolutions;
        private bool wasVisible;

        private readonly global::Crucible.WindowMode[] modes =
        {
            global::Crucible.WindowMode.Windowed,
            global::Crucible.WindowMode.Maximized,
            global::Crucible.WindowMode.Fullscreen
        };

        // The presets the < / > buttons step through (Custom is never picked, only shown).
        private readonly global::Crucible.QualityLevel[] qualityLevels =
        {
            global::Crucible.QualityLevel.Low,
            global::Crucible.QualityLevel.Medium,
            global::Crucible.QualityLevel.High,
            global::Crucible.QualityLevel.Ultra
        };

        private readonly global::Crucible.AntiAliasing[] antiAliasingModes =
        {
            global::Crucible.AntiAliasing.Off,
            global::Crucible.AntiAliasing.FXAA,
            global::Crucible.AntiAliasing.MSAA2x,
            global::Crucible.AntiAliasing.MSAA4x
        };

        private readonly global::Crucible.ShadowQuality[] shadowQualities =
        {
            global::Crucible.ShadowQuality.Off,
            global::Crucible.ShadowQuality.Low,
            global::Crucible.ShadowQuality.Medium,
            global::Crucible.ShadowQuality.High,
            global::Crucible.ShadowQuality.Ultra
        };

        private T Component<T>(global::Crucible.Entity entity)
            where T : class
        {
            return entity == null ? null : entity.GetComponent<T>();
        }

        private static bool Clicked(global::Crucible.UIButton button)
        {
            return button != null && button.WasClicked;
        }

        public override void OnCreate()
        {
            previousMode = Component<global::Crucible.UIButton>(previousModeButton);
            nextMode = Component<global::Crucible.UIButton>(nextModeButton);
            previousResolution = Component<global::Crucible.UIButton>(previousResolutionButton);
            nextResolution = Component<global::Crucible.UIButton>(nextResolutionButton);
            previousQuality = Component<global::Crucible.UIButton>(previousQualityButton);
            nextQuality = Component<global::Crucible.UIButton>(nextQualityButton);
            previousAntiAliasing = Component<global::Crucible.UIButton>(previousAntiAliasingButton);
            nextAntiAliasing = Component<global::Crucible.UIButton>(nextAntiAliasingButton);
            previousShadows = Component<global::Crucible.UIButton>(previousShadowsButton);
            nextShadows = Component<global::Crucible.UIButton>(nextShadowsButton);
            ambientOcclusion = Component<global::Crucible.UIButton>(ambientOcclusionButton);
            bloom = Component<global::Crucible.UIButton>(bloomButton);
            volumetricFog = Component<global::Crucible.UIButton>(volumetricFogButton);
            apply = Component<global::Crucible.UIButton>(applyButton);
            revert = Component<global::Crucible.UIButton>(revertButton);

            modeText = Component<global::Crucible.UIText>(modeTextEntity);
            resolutionText = Component<global::Crucible.UIText>(resolutionTextEntity);
            qualityText = Component<global::Crucible.UIText>(qualityTextEntity);
            antiAliasingText = Component<global::Crucible.UIText>(antiAliasingTextEntity);
            shadowsText = Component<global::Crucible.UIText>(shadowsTextEntity);
            ambientOcclusionText = Component<global::Crucible.UIText>(ambientOcclusionButton);
            bloomText = Component<global::Crucible.UIText>(bloomButton);
            volumetricFogText = Component<global::Crucible.UIText>(volumetricFogButton);

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

            // --- display ------------------------------------------------------------ //
            if (Clicked(previousMode))
                ChangeMode(-1);
            else if (Clicked(nextMode))
                ChangeMode(1);

            if (Clicked(previousResolution))
                ChangeResolution(-1);
            else if (Clicked(nextResolution))
                ChangeResolution(1);

            // --- quality ------------------------------------------------------------ //
            bool changed = false;

            if (Clicked(previousQuality))      { ChangeQuality(-1); changed = true; }
            else if (Clicked(nextQuality))     { ChangeQuality(1); changed = true; }

            if (Clicked(previousAntiAliasing)) { ChangeAntiAliasing(-1); changed = true; }
            else if (Clicked(nextAntiAliasing)) { ChangeAntiAliasing(1); changed = true; }

            if (Clicked(previousShadows))      { ChangeShadows(-1); changed = true; }
            else if (Clicked(nextShadows))     { ChangeShadows(1); changed = true; }

            if (Clicked(ambientOcclusion))
            {
                global::Crucible.QualitySettings.AmbientOcclusion = !global::Crucible.QualitySettings.AmbientOcclusion;
                changed = true;
            }
            if (Clicked(bloom))
            {
                global::Crucible.QualitySettings.Bloom = !global::Crucible.QualitySettings.Bloom;
                changed = true;
            }
            if (Clicked(volumetricFog))
            {
                global::Crucible.QualitySettings.VolumetricFog = !global::Crucible.QualitySettings.VolumetricFog;
                changed = true;
            }

            if (Clicked(apply))
            {
                if (!global::Crucible.QualitySettings.Save())
                    global::Crucible.Log.Warning("GraphicsOptions: could not save the graphics settings.");
            }
            else if (Clicked(revert))
            {
                global::Crucible.QualitySettings.Revert();
                changed = true;
            }

            if (changed)
                RefreshQuality();
        }

        // ================================================================== //
        //  Display (Screen)
        // ================================================================== //

        private bool CanChangeDisplay()
        {
            if (!global::Crucible.Screen.IsEditor)
                return true;

            global::Crucible.Log.Info(
                "Window mode and resolution must be tested in the standalone game.");
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

            index = Step(index, direction, modes.Length);
            global::Crucible.Screen.Mode = modes[index];
            RefreshDisplay();
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
                index = Step(index, direction, resolutions.Length);

            var selected = resolutions[index];

            bool applied = global::Crucible.Screen.SetResolution(
                selected.Width,
                selected.Height,
                global::Crucible.Screen.Mode);

            if (!applied)
                global::Crucible.Log.Warning(
                    "Could not apply the selected resolution.");

            RefreshDisplay();
        }

        // ================================================================== //
        //  Quality (QualitySettings)
        // ================================================================== //

        private static int Step(int index, int direction, int count)
        {
            return (index + direction + count) % count;
        }

        private void ChangeQuality(int direction)
        {
            var current = global::Crucible.QualitySettings.Level;
            int index = System.Array.IndexOf(qualityLevels, current);

            // From Custom: > goes to the lowest preset, < to the highest.
            if (index < 0)
                index = direction > 0 ? 0 : qualityLevels.Length - 1;
            else
                index = Step(index, direction, qualityLevels.Length);

            global::Crucible.QualitySettings.Level = qualityLevels[index];
        }

        private void ChangeAntiAliasing(int direction)
        {
            int index = System.Array.IndexOf(antiAliasingModes, global::Crucible.QualitySettings.AntiAliasing);
            index = Step(index < 0 ? 0 : index, direction, antiAliasingModes.Length);
            global::Crucible.QualitySettings.AntiAliasing = antiAliasingModes[index];
        }

        private void ChangeShadows(int direction)
        {
            int index = System.Array.IndexOf(shadowQualities, global::Crucible.QualitySettings.Shadows);
            index = Step(index < 0 ? 0 : index, direction, shadowQualities.Length);
            global::Crucible.QualitySettings.Shadows = shadowQualities[index];
        }

        private static string AntiAliasingName(global::Crucible.AntiAliasing mode)
        {
            switch (mode)
            {
                case global::Crucible.AntiAliasing.MSAA2x: return "MSAA 2x";
                case global::Crucible.AntiAliasing.MSAA4x: return "MSAA 4x";
                default: return mode.ToString();
            }
        }

        private static string OnOff(bool value)
        {
            return value ? "On" : "Off";
        }

        // ================================================================== //
        //  Text
        // ================================================================== //

        private void Refresh()
        {
            RefreshDisplay();
            RefreshQuality();
        }

        private void RefreshDisplay()
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

        private void RefreshQuality()
        {
            if (qualityText != null)
                qualityText.Text = global::Crucible.QualitySettings.Level.ToString();
            if (antiAliasingText != null)
                antiAliasingText.Text = AntiAliasingName(global::Crucible.QualitySettings.AntiAliasing);
            if (shadowsText != null)
                shadowsText.Text = global::Crucible.QualitySettings.Shadows.ToString();
            if (ambientOcclusionText != null)
                ambientOcclusionText.Text = OnOff(global::Crucible.QualitySettings.AmbientOcclusion);
            if (bloomText != null)
                bloomText.Text = OnOff(global::Crucible.QualitySettings.Bloom);
            if (volumetricFogText != null)
                volumetricFogText.Text = OnOff(global::Crucible.QualitySettings.VolumetricFog);
        }

        public override void OnDestroy()
        {
            global::Crucible.Log.Info("GraphicsOptions destroyed for entity " + this.Entity);
        }
    }
}
