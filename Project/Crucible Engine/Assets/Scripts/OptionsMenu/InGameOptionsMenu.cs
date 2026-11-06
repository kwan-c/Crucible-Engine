namespace @Crucible.@Scripts
{
    public sealed class @InGameOptionsMenu : global::Crucible.EntityScript
    {
        private bool wasOpen;

        public override void OnCreate()
        {
            //global::Crucible.Log.Info("InGameOptionsMenu created for entity " + this.Entity);
             wasOpen = UI.MenuOpen;
        }

        public override void OnUpdate(float deltaTime)
        {
            bool open = UI.MenuOpen;

            if (open && UI.WasActionTriggered("CloseOptions"))
            {
                SaveSettings();
                UI.MenuOpen = false;
                wasOpen = false;
                return;
            }

            // Escape closes the menu before scripts update.
            if (wasOpen && !open)
                SaveSettings();

            wasOpen = open;
        }
        private void SaveSettings()
        {
            if (!InputSettings.Save())
                Log.Warning("InGameOptionsMenu: could not save settings.");
        }

        public override void OnDestroy()
        {
            global::Crucible.Log.Info("InGameOptionsMenu destroyed for entity " + this.Entity);
        }
    }
}
