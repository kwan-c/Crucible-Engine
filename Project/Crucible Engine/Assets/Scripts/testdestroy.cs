namespace @Crucible.@Scripts
{
    public sealed class @testdestroy : global::Crucible.EntityScript
    {
        public override void OnCreate()
        {
            Scene.DontDestroyOnLoad(Entity);
        }

        public override void OnUpdate(float deltaTime)
        {
        }

        public override void OnDestroy()
        {
            
        }
    }
}
