namespace @Crucible.@Scripts
{
    public sealed class @Control : global::Crucible.EntityScript
    {
        public override void OnCreate()
        {
            global::Crucible.Log.Info("Control created for entity " + this.Entity);
        }

        public override void OnUpdate(float deltaTime)
        {
        }

        public override void OnDestroy()
        {
            global::Crucible.Log.Info("Control destroyed for entity " + this.Entity);
        }
    }
}
