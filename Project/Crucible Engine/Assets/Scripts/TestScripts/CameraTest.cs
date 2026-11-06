using System;

namespace Crucible.Scripts
{
    // Tests the Camera / CameraOrbit API.
    //   1. Attach to any entity in a scene with a Camera. Optionally set "Target" (an entity
    //      to track on screen) and "Other Camera" (a second entity with a Camera component).
    //   2. Press Play, then:
    //        Left click  ray from the mouse (or the screen centre if the cursor is locked) and log the hit
    //        Z (hold)    zoom: field of view 30
    //        C           switch Camera.Main to Other Camera and back (by Priority)
    //        V           recoil: kick the CameraOrbit pitch up 5 degrees (if the camera has one above it)
    //   Every second it logs where Target is on screen.
    public sealed class CameraTest : EntityScript
    {
        public Entity target;
        public Entity otherCamera;
        public float zoomFov = 30f;

        float normalFov;
        float nextLog = 1f;

        public override void OnCreate()
        {
            Camera main = Camera.Main;
            if (main == null) { Log.Warning("[CameraTest] No enabled camera in the scene."); return; }
            normalFov = main.FieldOfView;
            Log.Info("[CameraTest] Main: " + main + ", FOV " + main.FieldOfView + ", clip " + main.NearClip + "-" +
                     main.FarClip + ", priority " + main.Priority + ", screen " + Camera.ScreenSize);
            Log.Info("[CameraTest] Orbit above the camera: " + (FindOrbit(main) != null ? "yes" : "none"));
        }

        public override void OnUpdate(float deltaTime)
        {
            Camera main = Camera.Main;
            if (main == null)
                return;

            if (Input.IsPressed(KeyCode.MouseLeft))
            {
                Ray ray = new Ray();
                bool fromMouse = !Input.IsMouseLocked && main.ScreenPointToRay(Input.MousePosition, out ray);
                if (!fromMouse && !main.ViewportPointToRay(new Vector2(0.5f, 0.5f), out ray))
                {
                    Log.Warning("[CameraTest] No game screen to cast from.");
                    return;
                }
                RaycastHit hit;
                Log.Info("[CameraTest] " + (fromMouse ? "Mouse" : "Centre") + " " + ray + " -> " +
                         (Physics.Raycast(ray, out hit, 500f) ? hit.ToString() : "nothing"));
            }

            float wantFov = Input.IsDown(KeyCode.Z) ? zoomFov : normalFov;
            if (normalFov > 0f && Math.Abs(main.FieldOfView - wantFov) > 0.01f)
                main.FieldOfView = wantFov;

            if (Input.IsPressed(KeyCode.C) && otherCamera != null)
            {
                Camera other = otherCamera.GetComponent<Camera>();
                if (other == null) Log.Warning("[CameraTest] Other Camera has no Camera component.");
                else
                {
                    other.Priority = main.Priority + 1;   // highest priority wins
                    Log.Info("[CameraTest] Switched to " + other + "; Camera.Main is now " + Camera.Main);
                }
            }

            if (Input.IsPressed(KeyCode.V))
            {
                CameraOrbit orbit = FindOrbit(main);
                if (orbit != null) { orbit.Pitch += 5f; Log.Info("[CameraTest] Recoil: pitch " + orbit.Pitch); }
            }

            if (target != null && Time.ElapsedTime >= nextLog)
            {
                Vector2 screen;
                float depth;
                Log.Info("[CameraTest] Target " + (main.WorldToScreenPoint(target.Transform.WorldPosition, out screen, out depth)
                    ? "on screen at " + screen + " (" + depth.ToString("0.0") + " m away, screen " + Camera.ScreenSize + ")"
                    : "behind the camera"));
                nextLog += 1f;
            }
        }

        // The CameraOrbit pivot is usually the camera's parent (or further up).
        static CameraOrbit FindOrbit(Camera camera)
        {
            for (Entity e = camera.Entity; e != null; e = e.Transform.Parent)
            {
                CameraOrbit orbit = e.GetComponent<CameraOrbit>();
                if (orbit != null) return orbit;
            }
            return null;
        }
    }
}
