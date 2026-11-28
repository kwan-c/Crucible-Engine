using System;
using System.Collections.Generic;
using Crucible;

namespace Crucible.Scripts
{
    // Enemy, part 6: the attack sound. Unity's enemies played their weapon's attack sound once
    // per attack, at the release (the strike after its range and wall checks, or the shot as it
    // leaves), from one 3D voice per weapon (linear roll-off, 4 to 40 m). Same here, with the
    // team's existing clips and the engine's Audio.PlayClipAtPoint (a temporary 3D voice that
    // this script moves with the golem and can stop).
    //
    //   attackSound       the clip (empty: silent - the Spinner has no clip yet)
    //   attackSoundPeak   seconds into the clip where its hit / shot is heard. The voice starts
    //                     that long before the release, so the peak lands on the hit frame
    //                     (Mace_attack and ThrowingAxe_attack have 0.42 s of lead-in, the sword
    //                     clip 0.18 s, the crossbow and cannon clips none).
    //   attackSoundVolume 0..1
    //
    // A voice started before the release is "pending": a cancelled attack (interrupted, a lost
    // target, a shove, death) stops it, so a swing that never lands is never heard landing. After
    // the release it plays out (no clip loops). At most maxEnemyAttackVoices play at once across
    // all enemies, each new one a little quieter, so a crowd attacking together is not deafening.
    public sealed partial class Enemy
    {
        public AudioClip attackSound;
        public float attackSoundPeak = 0f;
        public float attackSoundVolume = 0.8f;
        public float attackSoundMinDistance = 4f;    // Unity's enemy weapon voice
        public float attackSoundMaxDistance = 40f;

        public static int maxEnemyAttackVoices = 6;
        static readonly List<Entity> liveVoices = new List<Entity>();

        Entity attackVoice;
        bool soundStarted, soundPending;
        float soundAt;

        /// <summary>Attack sounds started so far (QA).</summary>
        public int AttackSoundsPlayed { get; private set; }
        /// <summary>Attack sounds cut before their release (QA).</summary>
        public int AttackSoundsCancelled { get; private set; }
        public bool AttackSoundPending { get { return soundPending && attackVoice != null && attackVoice.IsAlive; } }

        // From TryAttack (after age = 0): when this attack's sound should start.
        void ScheduleAttackSound(float releaseTime)
        {
            soundStarted = false;
            soundPending = false;
            soundAt = Math.Max(0f, releaseTime - Math.Max(0f, attackSoundPeak));
            if (soundAt <= 0f) PlayAttackSound(true);
        }

        // Every attacking frame: start the sound on time, and keep its voice on the golem.
        void UpdateAttackSound(float attackAge, bool releasedNow)
        {
            if (!soundStarted && attackAge >= soundAt) PlayAttackSound(!releasedNow);
            if (releasedNow) soundPending = false;
            FollowVoice();
        }

        void PlayAttackSound(bool beforeRelease)
        {
            soundStarted = true;
            if (attackSound == null || attackSoundVolume <= 0f) return;
            PruneVoices();
            if (liveVoices.Count >= Math.Max(1, maxEnemyAttackVoices)) return;   // the crowd cap
            if (attackVoice != null && attackVoice.IsAlive) Destroy(attackVoice); // one voice per weapon: restart, never stack
            float volume = MechMath.Clamp01(attackSoundVolume) / (1f + 0.15f * liveVoices.Count);
            attackVoice = Audio.PlayClipAtPoint(attackSound, VoicePosition, volume);
            if (attackVoice == null) return;
            if (attackVoice.HasComponent<AudioSource>())
            {
                attackVoice.AudioSource.MinDistance = Math.Max(0.1f, attackSoundMinDistance);
                attackVoice.AudioSource.MaxDistance = Math.Max(attackSoundMinDistance + 0.1f, attackSoundMaxDistance);
            }
            liveVoices.Add(attackVoice);
            soundPending = beforeRelease;
            AttackSoundsPlayed++;
        }

        // CancelAttack: a sound still waiting for its hit is cut; one already past it plays out.
        void StopAttackSound()
        {
            if (soundPending && attackVoice != null && attackVoice.IsAlive)
            {
                Destroy(attackVoice);
                AttackSoundsCancelled++;
            }
            soundPending = false;
            soundStarted = true;   // nothing more for this attack
        }

        void FollowVoice()
        {
            if (attackVoice != null && attackVoice.IsAlive) attackVoice.Transform.WorldPosition = VoicePosition;
        }

        Vector3 VoicePosition { get { return weaponMuzzle != null ? weaponMuzzle.Transform.WorldPosition : Position; } }

        static void PruneVoices()
        {
            for (int i = liveVoices.Count - 1; i >= 0; i--)
                if (liveVoices[i] == null || !liveVoices[i].IsAlive) liveVoices.RemoveAt(i);
        }
    }
}
