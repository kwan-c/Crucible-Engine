namespace @Crucible.@Scripts
{
    public sealed class @InGameOptionsMenu : global::Crucible.EntityScript
    {
        /// <summary>
        /// Loaded by a button whose On Click is Custom with Action Id "ReturnToMainMenu".
        /// Path under Assets, as Scene.Load takes it.
        /// </summary>
        public string mainMenuScene = "Scenes/Game/Main Menu Scene V2.scene";

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

            // Main Menu button: keep any changed settings, close the menu, then leave the level.
            if (open && UI.WasActionTriggered("ReturnToMainMenu") && !LoadingScreen.IsLoading)
            {
                SaveSettings();
                UI.MenuOpen = false;
                wasOpen = false;
                if (!Scene.Load(mainMenuScene))
                    Log.Error("InGameOptionsMenu: could not load " + mainMenuScene);
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
