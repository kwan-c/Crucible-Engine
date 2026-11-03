namespace Crucible.Scripts
{
    // Inspector booleans drive only the graph. No transform/input/gameplay code.
    public sealed class LegAnimationPreview : global::Crucible.EntityScript
    {
        public bool walk;
        public bool boost;
        public bool jump;
        private bool warned;
        public override void OnCreate() { Apply(); }
        public override void OnUpdate(float deltaTime) { Apply(); }
        private void Apply()
        {
            bool ok = Animator.SetBool("Walk", walk);
            ok = Animator.SetBool("Boost", boost) && ok;
            ok = Animator.SetBool("Jump", jump) && ok;
            if (!ok && !warned) global::Crucible.Log.Warning("LegAnimationPreview needs an AnimatedModel controller with Walk, Boost and Jump bool parameters.");
            warned = !ok;
        }
    }
}
