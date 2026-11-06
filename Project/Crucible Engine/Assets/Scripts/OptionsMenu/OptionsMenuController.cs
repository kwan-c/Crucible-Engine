namespace @Crucible.@Scripts
{
    public sealed class @OptionsMenuController : global::Crucible.EntityScript
    {
        public global::Crucible.Entity optionsCanvasEntity;
        private global::Crucible.UICanvas optionsCanvas;

        public override void OnCreate()
        {
            //global::Crucible.Log.Info("OptionsMenuController created for entity " + this.Entity);
            if (optionsCanvasEntity == null)
            {
                global::Crucible.Log.Warning("Assign Options Canvas to OpenOptionsMenu.");
                return;
            }

            optionsCanvas =
                optionsCanvasEntity.GetComponent<global::Crucible.UICanvas>();

            if (optionsCanvas == null)
            {
                global::Crucible.Log.Warning("Assigned entity needs a UICanvas.");
                return;
            }

            optionsCanvas.Enabled = false;
        }

        public override void OnUpdate(float deltaTime)
        {
            if (optionsCanvas == null)
                return;

            if (global::Crucible.UI.WasActionTriggered("OpenOptions"))
                optionsCanvas.Enabled = true;

            if (global::Crucible.UI.WasActionTriggered("CloseOptions"))
            {
                if (!global::Crucible.InputSettings.Save())
                    global::Crucible.Log.Warning("Could not save settings.");

                optionsCanvas.Enabled = false;
            }
        }

        public override void OnDestroy()
        {
            global::Crucible.Log.Info("OptionsMenuController destroyed for entity " + this.Entity);
        }
    }
}
