/*************************************************************************
* \file:         ZooSoundPlayer.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \date          8 November 2026
* \version       1.0
* \brief         Zoo sound cube: step on it and press F to play its clip.
*                Port of Unity's ZooSoundplayer.
*
* \details
*    Needs an AudioSource (it plays the clip as a one-shot through it), a
*    trigger Collider and a "Label" WorldText child. Set clip in the
*    Inspector.
*
**************************************************************************/
namespace Crucible.Scripts
{
    public sealed class ZooSoundPlayer : EntityScript
    {
        public AudioClip clip;
        public float volume = 1f;

        ZooPad pad;

        public override void OnCreate()
        {
            pad = new ZooPad(this);
            pad.SetText(clip != null ? "Step on me and press F\nto play " + clip.Name : "No Audio Clip Assigned");
        }

        public override void OnUpdate(float deltaTime)
        {
            if (!pad.WasUsed(KeyCode.F) || clip == null) return;
            Log.Info("Playing sound: " + clip.Name);
            if (AudioSource != null) AudioSource.PlayOneShot(clip, volume);
        }

        public override void OnTriggerEnter(Entity other) { pad.Enter(other); }
        public override void OnTriggerExit(Entity other) { pad.Exit(other); }
    }
}
