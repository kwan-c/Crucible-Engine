namespace @Crucible.@Scripts
{
    public sealed class @GraphicsTabs : global::Crucible.EntityScript
    {
        public Entity windowsTab;
        public Entity advancedTab;
        public Entity accessibilityTab;
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

            if (UI.WasActionTriggered("tabAccessibility"))
                ShowTab(2);
        }

        private void ShowTab(int tab)
        {
            if (windowsTab != null)
                windowsTab.SetEnabled<UIRect>(tab == 0);

            if (advancedTab != null)
                advancedTab.SetEnabled<UIRect>(tab == 1);

            if (accessibilityTab != null)
                accessibilityTab.SetEnabled<UIRect>(tab == 2);
        }
    }
}
