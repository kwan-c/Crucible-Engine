using System;

namespace Crucible.Scripts
{
    // Tests the Transform world-space / hierarchy API and Scene loading.
    //   1. Attach to an entity. Optionally drag another entity onto "Target",
    //      and type a scene path (e.g. "Scenes/Fog_Lighting_Effects.scene") into "Next Scene".
    //   2. Press Play, then:
    //        T  LookAt(Target)                    Y  turn 45 degrees (world)
    //        U  step 1 m along Forward            O  log position, rotation, directions, parent, children
    //        P  parent to Target / back to root (stays where it is in the world)
    //        N  Scene.Load(Next Scene)            M  Scene.Reload()
    public sealed class TransformSceneTest : EntityScript
    {
        public Entity target;
        public string nextScene = "";

        public override void OnCreate()
        {
            Log.Info("[TransformSceneTest] Scene '" + Scene.Name + "', Time.ElapsedTime " +
                     Time.ElapsedTime.ToString("0.00") + " (keeps counting across Scene.Load)");
            Report();
        }

        public override void OnUpdate(float deltaTime)
        {
            if (Input.IsPressed(KeyCode.T))
            {
                if (target == null) Log.Warning("[TransformSceneTest] Target is not set.");
                else { Transform.LookAt(target); Log.Info("[TransformSceneTest] Looking at " + target + ", forward " + Transform.Forward); }
            }
            if (Input.IsPressed(KeyCode.Y))
                Transform.WorldRotation = Quaternion.Euler(0f, 45f, 0f) * Transform.WorldRotation;
            if (Input.IsPressed(KeyCode.U))
                Transform.WorldPosition = Transform.WorldPosition + Transform.Forward;
            if (Input.IsPressed(KeyCode.O))
                Report();

            if (Input.IsPressed(KeyCode.P) && target != null)
            {
                bool toTarget = Transform.Parent != target;
                Vector3 before = Transform.WorldPosition;
                bool ok = Transform.SetParent(toTarget ? target : null);
                Log.Info("[TransformSceneTest] " + (toTarget ? "Parented to " + target : "Unparented") + (ok ? "" : " - REFUSED") +
                         ". World position " + before + " -> " + Transform.WorldPosition + ", local " + Transform.Position);
            }

            if (Input.IsPressed(KeyCode.N))
                Log.Info("[TransformSceneTest] Scene.Load(\"" + nextScene + "\") -> " + Scene.Load(nextScene));
            if (Input.IsPressed(KeyCode.M))
                Log.Info("[TransformSceneTest] Scene.Reload() -> " + Scene.Reload());
        }

        public override void OnDestroy()
        {
            Log.Info("[TransformSceneTest] OnDestroy (scene change or Stop)");
        }

        void Report()
        {
            Log.Info("[TransformSceneTest] Local " + Transform.Position + " | world " + Transform.WorldPosition +
                     " | world rotation " + Transform.WorldRotation.EulerAngles);
            Log.Info("[TransformSceneTest] Forward " + Transform.Forward + " Right " + Transform.Right + " Up " + Transform.Up);
            string children = "";
            for (int i = 0; i < Transform.ChildCount; ++i)
                children += (i > 0 ? ", " : "") + Transform.GetChild(i).Name;
            Log.Info("[TransformSceneTest] Parent " + (Transform.Parent != null ? Transform.Parent.ToString() : "none") +
                     " | children (" + Transform.ChildCount + "): " + children);
        }
    }
}
