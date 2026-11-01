namespace Crucible.Scripts
{
    public sealed class WeaponFirePreview : global::Crucible.EntityScript
    {
        public bool fire;
        private bool warned;
        public override void OnCreate() { Apply(); }
        public override void OnUpdate(float deltaTime) { Apply(); }
        private void Apply()
        {
            bool ok = Animator.SetBool("Fire", fire);
            if (!ok && !warned) global::Crucible.Log.Warning("WeaponFirePreview needs an AnimatedModel controller with a Fire bool parameter.");
            warned = !ok;
        }
    }
}
