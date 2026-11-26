/*************************************************************************
* \file:         AudioSource.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          3 November 2026
* \version       1.1
* \brief         Script access to an entity's AudioSource component.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Unity's AudioSource. Get it with GetComponent<AudioSource>() or
*    Entity.AudioSource; the entity needs an AudioSource component (add it
*    in the Inspector). On an entity without one, calls do nothing and
*    getters return defaults - check HasComponent<AudioSource>() if unsure.
*
*        AudioSource gun = GetComponent<AudioSource>();
*        if (Input.IsPressed(KeyCode.MouseLeft)) gun.PlayOneShot();
*
*    Play / Stop / PlayOneShot happen later in the same frame. Volume and
*    Pitch also change a sound that is already playing; Loop, Bus,
*    SpatialBlend, MinDistance, MaxDistance and Clip apply from the next Play.
*
*        music.Bus = AudioBus.Music;     // the Music slider now controls it
*    PlayOneShot(clip, volumeScale) plays another clip once without changing Clip.
*
*    C++ side: Engine/src/Scripting/Bindings/AudioBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>Plays sound from an entity. Unity's AudioSource.</summary>
    public sealed class AudioSource
    {
        private readonly EntityIdentity entity;
        internal AudioSource(EntityIdentity owner) { entity = owner; }

        /// <summary>The entity this component is on (Unity's .gameObject), e.g. target.Entity.Name.</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        // --- playing ----------------------------------------------------------- //

        /// <summary>Starts the clip from the beginning, cutting off the last Play().</summary>
        public void Play() { RequestInternal(I, G, 0); }

        /// <summary>Stops the Play() sound and every one-shot still sounding.</summary>
        public void Stop() { RequestInternal(I, G, 1); }

        /// <summary>
        /// Plays the clip once more on top of whatever is playing (gunshots, footsteps).
        /// Never loops. Each call is one shot, even several in a frame.
        /// </summary>
        public void PlayOneShot() { RequestInternal(I, G, 2); }

        /// <summary>
        /// Plays <paramref name="clip"/> once on top of whatever is playing, without changing
        /// this source's Clip (Unity's PlayOneShot(clip, volumeScale)) - e.g. a reload click from
        /// the gun's source. volumeScale multiplies Volume. Several different clips in one frame
        /// each play (up to 8 a frame). Returns false if it couldn't be queued.
        /// </summary>
        public bool PlayOneShot(AudioClip clip, float volumeScale = 1f)
        {
            if (clip == null)
                return false;
            return PlayOneShotClipInternal(I, G, clip.IdLow, clip.IdHigh, volumeScale) != 0;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int PlayOneShotClipInternal(int index, int generation, int low, int high, float volumeScale);

        /// <summary>
        /// Is the Play() sound playing? One-shots do not count. Updated once per frame,
        /// so it is still false in the same frame as Play().
        /// </summary>
        public bool IsPlaying
        {
            get { int v; return IsPlayingInternal(I, G, out v) != 0 && v != 0; }
        }

        // --- settings ---------------------------------------------------------- //

        /// <summary>The audio file to play. Applies from the next Play / PlayOneShot.</summary>
        public AudioClip Clip
        {
            get { int low, high; return GetClipInternal(I, G, out low, out high) != 0 ? AudioClip.FromId(low, high) : null; }
            set { SetClipInternal(I, G, value != null ? value.IdLow : 0, value != null ? value.IdHigh : 0); }
        }

        /// <summary>0 silent .. 1 as recorded. Also changes a sound already playing.</summary>
        public float Volume
        {
            get { float v; return GetVolumeInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetVolumeInternal(I, G, ref value); }
        }

        /// <summary>1 normal, 2 an octave up and twice as fast. Also changes a sound already playing.</summary>
        public float Pitch
        {
            get { float v; return GetPitchInternal(I, G, out v) != 0 ? v : 1f; }
            set { SetPitchInternal(I, G, ref value); }
        }

        /// <summary>Repeat the Play() sound. Applies from the next Play.</summary>
        public bool Loop
        {
            get { int v; return GetLoopInternal(I, G, out v) != 0 && v != 0; }
            set { SetLoopInternal(I, G, value ? 1 : 0); }
        }

        /// <summary>
        /// The bus this source plays through (Unity's "Output"): its clip, PlayOneShot and
        /// PlayOneShot(clip) all use it. Applies from the next Play; a sound already playing
        /// stays on its old bus.
        /// </summary>
        public AudioBus Bus
        {
            get { int v; return GetBusInternal(I, G, out v) != 0 ? (AudioBus)v : AudioBus.SFX; }
            set { SetBusInternal(I, G, (int)value); }
        }

        /// <summary>0 = 2D (same in both ears), 1 = 3D from this entity's position. Applies from the next Play.</summary>
        public float SpatialBlend
        {
            get { float v; return GetSpatialBlendInternal(I, G, out v) != 0 ? v : 0f; }
            set { SetSpatialBlendInternal(I, G, ref value); }
        }

        /// <summary>3D: full volume closer than this. Applies from the next Play.</summary>
        public float MinDistance
        {
            get { float v; return GetMinDistanceInternal(I, G, out v) != 0 ? v : 1f; }
            set { SetMinDistanceInternal(I, G, ref value); }
        }

        /// <summary>3D: stops getting quieter (Logarithmic) or is silent (Linear) here. Applies from the next Play.</summary>
        public float MaxDistance
        {
            get { float v; return GetMaxDistanceInternal(I, G, out v) != 0 ? v : 500f; }
            set { SetMaxDistanceInternal(I, G, ref value); }
        }

        // --- internal calls (AudioBindings.cpp) -------------------------------- //

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int RequestInternal(int index, int generation, int action);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsPlayingInternal(int index, int generation, out int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetVolumeInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetVolumeInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetPitchInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetPitchInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetSpatialBlendInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetSpatialBlendInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMinDistanceInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetMinDistanceInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMaxDistanceInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetMaxDistanceInternal(int index, int generation, ref float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetLoopInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetLoopInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetBusInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetBusInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetClipInternal(int index, int generation, out int low, out int high);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetClipInternal(int index, int generation, int low, int high);
    }
}
