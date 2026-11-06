namespace @Crucible.@Scripts
{
    public sealed class @AudioOptions : global::Crucible.EntityScript
    {
        public global::Crucible.Entity masterSliderEntity;
        public global::Crucible.Entity musicSliderEntity;
        public global::Crucible.Entity sfxSliderEntity;

        private global::Crucible.UISlider master;
        private global::Crucible.UISlider music;
        private global::Crucible.UISlider sfx;
        private bool wasVisible;

        public override void OnCreate()
        {
            master = Setup(masterSliderEntity);
            music = Setup(musicSliderEntity);
            sfx = Setup(sfxSliderEntity);
            Refresh();

           // global::Crucible.Log.Info("AudioOptions created for entity " + this.Entity);
        }

        private global::Crucible.UISlider Setup(
            global::Crucible.Entity entity)
        {
            var slider = entity == null
                ? null
                : entity.GetComponent<global::Crucible.UISlider>();

            if (slider == null)
            {
                global::Crucible.Log.Warning(
                    "AudioOptions: assign an entity with UISlider.");
                return null;
            }

            slider.MinValue = 0f;
            slider.MaxValue = 1f;
            slider.WholeNumbers = false;
            return slider;
        }

        public override void OnUpdate(float deltaTime)
        {
            bool visible = Entity.IsEnabled<global::Crucible.UIRect>();

            if (!visible)
            {
                wasVisible = false;
                return;
            }

            if (!wasVisible)
            {
                Refresh();
                wasVisible = true;
            }

            if (master != null && master.WasChanged)
                global::Crucible.Audio.MasterVolume = master.Value;

            if (music != null && music.WasChanged)
                global::Crucible.Audio.MusicVolume = music.Value;

            if (sfx != null && sfx.WasChanged)
                global::Crucible.Audio.SfxVolume = sfx.Value;
        }

        private void Refresh()
        {
            if (master != null)
                master.Value = global::Crucible.Audio.MasterVolume;

            if (music != null)
                music.Value = global::Crucible.Audio.MusicVolume;

            if (sfx != null)
                sfx.Value = global::Crucible.Audio.SfxVolume;
        }
        
        public override void OnDestroy()
        {
            global::Crucible.Log.Info("AudioOptions destroyed for entity " + this.Entity);
        }
    }
}
