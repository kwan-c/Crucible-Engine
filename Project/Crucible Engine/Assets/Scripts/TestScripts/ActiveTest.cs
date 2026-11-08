using System;

namespace Crucible.Scripts
{
    // Tests switching things on and off from a script.
    //   1. Attach to an entity. Drag another entity onto "Target" - ideally one with a
    //      mesh and children, a Light or a ParticleEmitter, and/or a script of its own.
    //   2. Press Play, then:
    //        F6  Target.SetActive(toggle)   - the whole entity and its children vanish / return
    //        F7  Target Light on / off      (SetEnabled<Light>)
    //        F8  Target ParticleEmitter on / off
    //        F9  Target's script on / off   (SetEnabled<EntityScript> - its OnUpdate stops)
    //   Try F7 (light off), then F6 twice: the light must STAY off after SetActive(true).
    public sealed class ActiveTest : EntityScript
    {
        public Entity target;

        public override void OnCreate()
        {
            if (target == null) { Log.Warning("[ActiveTest] Drag an entity onto Target."); return; }
            Report("Start");
        }

        public override void OnUpdate(float deltaTime)
        {
            if (target == null)
                return;

            if (Input.IsPressed(KeyCode.F6))
            {
                target.SetActive(!target.IsActiveSelf);
                Report("SetActive(" + target.IsActiveSelf + ")");
            }
            if (Input.IsPressed(KeyCode.F7))
                Toggle<Light>("Light");
            if (Input.IsPressed(KeyCode.F8))
                Toggle<ParticleEmitter>("ParticleEmitter");
            if (Input.IsPressed(KeyCode.F9))
                Toggle<EntityScript>("script");
        }

        void Toggle<T>(string what) where T : class
        {
            if (!target.HasComponent<T>()) { Log.Warning("[ActiveTest] Target has no " + what + "."); return; }
            bool now = !target.IsEnabled<T>();
            bool ok = target.SetEnabled<T>(now);
            Log.Info("[ActiveTest] " + what + " -> " + (now ? "on" : "off") + (ok ? "" : " (refused)"));
        }

        void Report(string label)
        {
            string child = target.Transform.ChildCount > 0
                ? ", first child active " + target.Transform.GetChild(0).IsActive
                : "";
            Log.Info("[ActiveTest] " + label + ": " + target.Name + " active " + target.IsActive + " (self " +
                     target.IsActiveSelf + ")" + child +
                     (target.HasComponent<Light>() ? ", light on " + target.IsEnabled<Light>() : ""));
        }
    }
}
