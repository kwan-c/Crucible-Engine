namespace @Crucible.@Scripts
{
    public sealed class @test1 : global::Crucible.EntityScript
    {
        public override void OnCreate()
        {
            global::Crucible.Log.Info("test1 created for entity " + this.Entity);
        }

        public override void OnUpdate(float deltaTime)
        {
        }

        public override void OnDestroy()
        {
            global::Crucible.Log.Info("test1 destroyed for entity " + this.Entity);
        }
    }
}
