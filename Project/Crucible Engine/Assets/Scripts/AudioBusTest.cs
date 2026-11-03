using System;

namespace Crucible.Scripts
{
    // Tests the static Audio class: bus volumes and PlayClipAtPoint.
    //   1. Attach to any entity (no AudioSource needed). Drag an audio file onto "Clip".
    //   2. Press Play, then:
    //        J      PlayClipAtPoint(Clip) a few units to this entity's right (watch the
    //               Hierarchy: a "One Shot Audio" entity appears and goes away when it ends)
    //        H      the same on the Music bus: 7/8 should change its volume, 9/0 should not
    //        5 / 6  Master volume down / up
    //        7 / 8  Music volume down / up
    //        9 / 0  SFX volume down / up
    //        V      save the volumes to UserSettings.json
    //   Volumes are player settings, so they stay changed after Stop - press 6/8/0 to restore.
    public sealed class AudioBusTest : EntityScript
    {
        public AudioClip clip;
        public float offset = 3f;

        public override void OnCreate()
        {
            Log.Info("[AudioBusTest] Master " + Audio.MasterVolume.ToString("0.0") + ", Music " +
                     Audio.MusicVolume.ToString("0.0") + ", SFX " + Audio.SfxVolume.ToString("0.0") +
                     ". Clip: " + (clip != null ? clip.Name : "none (drag one onto Clip)"));
        }

        public override void OnUpdate(float deltaTime)
        {
            bool music = Input.IsPressed(KeyCode.H);
            if (Input.IsPressed(KeyCode.J) || music)
            {
                Vector3 where = Transform.Position + new Vector3(offset, 0f, 0f);
                AudioBus bus = music ? AudioBus.Music : AudioBus.SFX;
                Entity oneShot = Audio.PlayClipAtPoint(clip, where, 1f, bus);
                Log.Info(oneShot != null ? "[AudioBusTest] Playing " + clip.Name + " at " + where + " on " + oneShot + " (" + bus + " bus)"
                                         : "[AudioBusTest] PlayClipAtPoint returned null - is Clip set?");
            }

            Step(KeyCode.Num5, KeyCode.Num6, AudioBus.Master);
            Step(KeyCode.Num7, KeyCode.Num8, AudioBus.Music);
            Step(KeyCode.Num9, KeyCode.Num0, AudioBus.SFX);

            if (Input.IsPressed(KeyCode.V))
                Log.Info(Audio.SaveSettings() ? "[AudioBusTest] Saved volumes to UserSettings.json"
                                              : "[AudioBusTest] Could not save UserSettings.json");
        }

        void Step(KeyCode down, KeyCode up, AudioBus bus)
        {
            float change = Input.IsPressed(down) ? -0.1f : Input.IsPressed(up) ? 0.1f : 0f;
            if (change == 0f)
                return;
            Audio.SetBusVolume(bus, Audio.GetBusVolume(bus) + change);
            Log.Info("[AudioBusTest] " + bus + " volume " + Audio.GetBusVolume(bus).ToString("0.0"));
        }
    }
}
