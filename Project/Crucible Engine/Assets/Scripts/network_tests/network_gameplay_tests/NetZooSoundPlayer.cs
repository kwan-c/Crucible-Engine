/*************************************************************************
* \file:         NetZooSoundPlayer.cs
* \author:       Tok Duan Jie Nelson (ZooSoundPlayer, the original);
*                Abhishek Akilandeswari Patchamuthu (this networked copy)
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Networked gameplay test: a copy of ZooSoundPlayer. Any
*                player steps on it and presses F; the clip plays on every
*                machine.
*
* \details
*    The host sees the press (NetZooPad) and plays an effect at the pad's
*    position; each machine plays the clip of the sound pad there. The
*    pads are ordinary scene entities (not network objects), found by
*    position. ZooSoundPlayer itself is unchanged.
*
* \history
*    -v1.0       copy of ZooSoundPlayer: host decides, plays everywhere
*
* The original's notes follow.
*
* \brief         Zoo sound cube: step on it and press F to play its clip.
*                Port of Unity's ZooSoundplayer.
*
* \details
*    Needs an AudioSource (it plays the clip as a one-shot through it), a
*    trigger Collider and a "Label" WorldText child. Set clip in the
*    Inspector.
*
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.Scripts.NetGameplay
{
    public sealed class NetZooSoundPlayer : EntityScript
    {
        public AudioClip clip;
        public float volume = 1f;

        NetZooPad pad;

        static readonly List<NetZooSoundPlayer> all = new List<NetZooSoundPlayer>();
        static readonly NetEffect PlayEffect = NetEffect.Register("netgameplay.zoo.sound", PlayAt);

        public override void OnCreate()
        {
            all.Add(this);
            pad = new NetZooPad(this);
            pad.SetText(clip != null ? "Step on me and press F\nto play " + clip.Name : "No Audio Clip Assigned");
        }

        public override void OnDestroy() { all.Remove(this); }

        public override void OnUpdate(float deltaTime)
        {
            if (!pad.WasUsed() || clip == null) return;
            Log.Info("Playing sound: " + clip.Name);
            PlayEffect.Play(null, Transform.WorldPosition);
        }

        // Every machine: the sound pad at that position plays its clip.
        static void PlayAt(Entity target, Vector3 position, NetReader extra)
        {
            NetZooSoundPlayer best = null;
            float bestDistance = 1f;   // metres: the pads are 6 m apart
            foreach (NetZooSoundPlayer pad in all)
            {
                float d = (pad.Transform.WorldPosition - position).Length;
                if (d < bestDistance) { bestDistance = d; best = pad; }
            }
            if (best != null && best.clip != null && best.AudioSource != null) best.AudioSource.PlayOneShot(best.clip, best.volume);
        }

        public override void OnTriggerEnter(Entity other) { pad.Enter(other); }
        public override void OnTriggerExit(Entity other) { pad.Exit(other); }
    }
}
