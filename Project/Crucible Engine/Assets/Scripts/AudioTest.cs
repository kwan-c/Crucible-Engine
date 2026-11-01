using System;

namespace Crucible.Scripts
{
    // Tests the AudioSource scripting API.
    //   1. Attach to an entity that has an AudioSource component with a clip set.
    //      (Turn its Play On Awake off if you want silence until you press P.)
    //   2. Optional: drag a second audio file onto "Other Clip".
    //   3. Press Play, then:
    //        P  Play / Stop            O  PlayOneShot (overlapping)
    //        I  PlayOneShot(Other Clip)
    //        1 / 2  volume down / up   3 / 4  pitch down / up
    //        L  toggle Loop (applies from the next Play)
    public sealed class AudioTest : EntityScript
    {
        public AudioClip otherClip;

        AudioSource source;
        bool wasPlaying;

        public override void OnCreate()
        {
            source = GetComponent<AudioSource>();
            if (source == null)
            {
                Log.Warning("[AudioTest] " + Entity + " has no AudioSource component - add one in the Inspector.");
                return;
            }
            Log.Info("[AudioTest] Clip: " + (source.Clip != null ? source.Clip.Name : "none") +
                     ", volume " + source.Volume + ", pitch " + source.Pitch + ", loop " + source.Loop +
                     ", other clip: " + (otherClip != null ? otherClip.Name : "none"));
        }

        public override void OnUpdate(float deltaTime)
        {
            if (source == null)
                return;

            if (Input.IsPressed(KeyCode.P))
            {
                if (source.IsPlaying) source.Stop(); else source.Play();
                Log.Info("[AudioTest] Playing Audio");
            }
            if (Input.IsPressed(KeyCode.O))
                source.PlayOneShot();
            if (Input.IsPressed(KeyCode.I))
            {
                if (otherClip == null) Log.Warning("[AudioTest] Other Clip is not set.");
                else source.PlayOneShot(otherClip);
            }

            if (Input.IsPressed(KeyCode.Num1)) { source.Volume -= 0.1f; Log.Info("[AudioTest] Volume " + source.Volume.ToString("0.0")); }
            if (Input.IsPressed(KeyCode.Num2)) { source.Volume += 0.1f; Log.Info("[AudioTest] Volume " + source.Volume.ToString("0.0")); }
            if (Input.IsPressed(KeyCode.Num3)) { source.Pitch -= 0.1f;  Log.Info("[AudioTest] Pitch " + source.Pitch.ToString("0.0")); }
            if (Input.IsPressed(KeyCode.Num4)) { source.Pitch += 0.1f;  Log.Info("[AudioTest] Pitch " + source.Pitch.ToString("0.0")); }
            if (Input.IsPressed(KeyCode.L))    { source.Loop = !source.Loop; Log.Info("[AudioTest] Loop " + source.Loop + " (from the next Play)"); }

            // IsPlaying is updated by the audio system once a frame.
            bool playing = source.IsPlaying;
            if (playing != wasPlaying)
            {
                Log.Info("[AudioTest] " + (playing ? "Started playing" : "Stopped playing"));
                wasPlaying = playing;
            }
        }
    }
}
