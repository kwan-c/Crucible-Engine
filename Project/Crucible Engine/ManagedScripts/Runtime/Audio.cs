/*************************************************************************
* \file:         Audio.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          3 November 2026
* \version       1.1
* \brief         Game-wide audio for scripts: the player's bus volumes and
*                one-off sounds at a point. Part of Crucible.ScriptCore.dll.
*
* \details
*    Bus volumes are the player's settings (UserSettings.json), the same
*    values an options menu edits. A change is heard on the next frame;
*    call Audio.SaveSettings() to keep it for the next launch (e.g. when the
*    menu closes). They are NOT part of the scene, so Stop does not undo them.
*
*        Audio.MusicVolume = slider;     // while dragging
*        Audio.SaveSettings();           // on "Apply" / menu close
*
*    PlayClipAtPoint plays a clip once, in 3D, at a position, without an
*    AudioSource of your own (Unity's AudioSource.PlayClipAtPoint):
*
*        Audio.PlayClipAtPoint(explosionClip, hitPosition);
*        Audio.PlayClipAtPoint(stinger, pos, 1f, AudioBus.Music); // another bus
*
*    It creates a temporary "One Shot Audio" entity (visible in the Hierarchy
*    during Play) that is destroyed when the sound ends.
*
*    C++ side: Engine/src/Scripting/Bindings/AudioBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>The mixer buses. Same order as AudioBus in Audio/AudioBuses.h.</summary>
    public enum AudioBus
    {
        /// <summary>Everything ends up here.</summary>
        Master = 0,
        /// <summary>Music tracks.</summary>
        Music = 1,
        /// <summary>Sound effects (AudioSources default to this).</summary>
        SFX = 2,
    }

    /// <summary>Game-wide audio: the player's bus volumes and PlayClipAtPoint.</summary>
    public static class Audio
    {
        // --- bus volumes (player settings) ------------------------------------- //

        /// <summary>A bus's volume, 0 silent .. 1 full.</summary>
        public static float GetBusVolume(AudioBus bus)
        {
            float volume;
            return GetBusVolumeInternal((int)bus, out volume) != 0 ? volume : 1f;
        }

        /// <summary>Sets a bus's volume (clamped to 0..1). Heard next frame; saved by SaveSettings.</summary>
        public static void SetBusVolume(AudioBus bus, float volume)
        {
            SetBusVolumeInternal((int)bus, volume);
        }

        /// <summary>The Master bus volume, 0..1. Scales every other bus.</summary>
        public static float MasterVolume
        {
            get { return GetBusVolume(AudioBus.Master); }
            set { SetBusVolume(AudioBus.Master, value); }
        }

        /// <summary>The Music bus volume, 0..1.</summary>
        public static float MusicVolume
        {
            get { return GetBusVolume(AudioBus.Music); }
            set { SetBusVolume(AudioBus.Music, value); }
        }

        /// <summary>The SFX bus volume, 0..1.</summary>
        public static float SfxVolume
        {
            get { return GetBusVolume(AudioBus.SFX); }
            set { SetBusVolume(AudioBus.SFX, value); }
        }

        /// <summary>
        /// Writes the player's settings, volumes included, to UserSettings.json so they
        /// survive a restart. Returns false if the file could not be written.
        /// </summary>
        public static bool SaveSettings()
        {
            return SaveSettingsInternal() != 0;
        }

        // --- one-off sounds ---------------------------------------------------- //

        /// <summary>
        /// Plays <paramref name="clip"/> once, in 3D at <paramref name="position"/>, on
        /// <paramref name="bus"/> (SFX unless given).
        /// Returns the temporary entity playing it (move it to make the sound follow
        /// something), or null if the clip is missing or it is not Play.
        /// </summary>
        public static Entity PlayClipAtPoint(AudioClip clip, Vector3 position, float volume = 1f,
                                             AudioBus bus = AudioBus.SFX)
        {
            if (clip == null)
                return null;
            int index, generation;
            if (PlayClipAtPointInternal(clip.IdLow, clip.IdHigh, ref position, volume, (int)bus,
                                        out index, out generation) == 0)
                return null;
            return new Entity(unchecked((uint)index), unchecked((uint)generation));
        }

        // --- internal calls (AudioBindings.cpp) -------------------------------- //

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetBusVolumeInternal(int bus, out float volume);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetBusVolumeInternal(int bus, float volume);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SaveSettingsInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int PlayClipAtPointInternal(int idLow, int idHigh, ref Vector3 position, float volume,
                                                          int bus, out int index, out int generation);
    }
}
