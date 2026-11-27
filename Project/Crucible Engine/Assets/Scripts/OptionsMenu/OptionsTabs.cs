namespace @Crucible.@Scripts
{
    public sealed class @OptionsTabs : global::Crucible.EntityScript
    {
        public global::Crucible.Entity graphicsContent;
        public global::Crucible.Entity audioContent;
        public global::Crucible.Entity keyBindsContent;

        public override void OnCreate()
        {
           ShowTab(0);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (global::Crucible.UI.WasActionTriggered("TabGraphics"))
                ShowTab(0);

            if (global::Crucible.UI.WasActionTriggered("TabAudio"))
                ShowTab(1);

            if (global::Crucible.UI.WasActionTriggered("TabKeyBinds"))
                ShowTab(2);
        }

        private void ShowTab(int tab)
        {
            if (graphicsContent != null)
                graphicsContent.SetEnabled<global::Crucible.UIRect>(tab == 0);

            if (audioContent != null)
                audioContent.SetEnabled<global::Crucible.UIRect>(tab == 1);

            if (keyBindsContent != null)
                keyBindsContent.SetEnabled<global::Crucible.UIRect>(tab == 2);
        }
    }
}
