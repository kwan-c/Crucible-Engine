namespace @Crucible.@Scripts
{
    public sealed class @GraphicsTabs : global::Crucible.EntityScript
    {
        public Entity windowsTab;
        public Entity advancedTab;
        public override void OnCreate()
        {
           ShowTab(0);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (UI.WasActionTriggered("tabWindows"))
                ShowTab(0);

            if (UI.WasActionTriggered("tabAdvanced"))
                ShowTab(1);
        }

        private void ShowTab(int tab)
        {
            if (windowsTab != null)
                windowsTab.SetEnabled<UIRect>(tab == 0);

            if (advancedTab != null)
                advancedTab.SetEnabled<UIRect>(tab == 1);
        }
    }
}
