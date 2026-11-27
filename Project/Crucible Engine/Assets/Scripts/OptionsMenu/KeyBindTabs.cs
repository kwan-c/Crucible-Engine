namespace @Crucible.@Scripts
{
    public sealed class @KeyBindTabs : global::Crucible.EntityScript
    {
        public Entity mnkTab;
        public Entity controllerTab;
        public override void OnCreate()
        {
           ShowTab(0);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (UI.WasActionTriggered("tabMNK"))
                ShowTab(0);

            if (UI.WasActionTriggered("tabController"))
                ShowTab(1);
        }

        private void ShowTab(int tab)
        {
            if (mnkTab != null)
                mnkTab.SetEnabled<UIRect>(tab == 0);

            if (controllerTab != null)
                controllerTab.SetEnabled<UIRect>(tab == 1);
        }
    }
}
