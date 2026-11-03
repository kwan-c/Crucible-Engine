namespace Crucible.Scripts
{
    public sealed class WeaponReloadPreview : global::Crucible.EntityScript
    {
        public bool fire;
        public bool reload;
        private bool warned;
        public override void OnCreate() { Apply(); }
        public override void OnUpdate(float deltaTime) { Apply(); }
        private void Apply()
        {
            bool ok = Animator.SetBool("Fire", fire);
            ok = Animator.SetBool("Reload", reload) && ok;
            if (!ok && !warned) global::Crucible.Log.Warning("WeaponReloadPreview needs an AnimatedModel controller with Fire and Reload bool parameters.");
            warned = !ok;
        }
    }
}
