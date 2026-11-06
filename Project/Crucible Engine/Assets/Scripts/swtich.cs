namespace Crucible.Scripts
{
    public sealed class swtich : EntityScript
    {
        public override void OnTriggerEnter(Entity other)
        {
            // Only the player should switch levels, not crates etc.
            if (!other.IsAlive || !other.HasComponent<CharacterController>())
                return;

            Log.Info("Player hit the switch");
            if (!Scene.Load("Scenes/Game/Game Scene.scene"))
                Log.Warning("Switch: could not load the scene");
        }
    }
}