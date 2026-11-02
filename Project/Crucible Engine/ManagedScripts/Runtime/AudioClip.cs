/*************************************************************************
* \file:         AudioClip.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief         A reference to an audio file (.wav, .ogg, .mp3, ...).
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Declare a public field and drag an audio file from the Asset Browser
*    onto it in the Inspector, then hand it to an AudioSource:
*
*        public AudioClip footstep;
*        ...
*        AudioSource.Clip = footstep;
*        AudioSource.PlayOneShot();
*
*    An unassigned field, or a file that was deleted, is null. Saved as the
*    file's asset guid, so renaming or moving the file keeps the link.
*
*    Created by the engine only: native code writes `assetId` into a new
*    object without running a constructor - keep that field name.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>An audio file to play through an AudioSource. Assign it in the Inspector.</summary>
    public sealed class AudioClip
    {
        // Written by native code (ScriptRuntime.cpp, WriteFields). Do not rename.
#pragma warning disable 0649 // assigned from C++, which the compiler cannot see
        private ulong assetId;
#pragma warning restore 0649

        private AudioClip() {}

        internal ulong AssetId { get { return assetId; } }

        // The guid crosses to C++ as two ints (no `long` in internal calls).
        internal int IdLow { get { return unchecked((int)(uint)assetId); } }
        internal int IdHigh { get { return unchecked((int)(uint)(assetId >> 32)); } }

        internal static AudioClip FromId(int low, int high)
        {
            ulong id = ((ulong)(uint)high << 32) | (uint)low;
            if (id == 0)
                return null;
            var clip = new AudioClip();
            clip.assetId = id;
            return clip;
        }

        /// <summary>The file name, e.g. "cannon5.wav".</summary>
        public string Name
        {
            get { return GetNameInternal(IdLow, IdHigh) ?? string.Empty; }
        }

        /// <summary>True if both refer to the same audio file.</summary>
        public override bool Equals(object other)
        {
            var clip = other as AudioClip;
            return !ReferenceEquals(clip, null) && clip.assetId == assetId;
        }

        public override int GetHashCode()
        {
            return assetId.GetHashCode();
        }

        public static bool operator ==(AudioClip a, AudioClip b)
        {
            if (ReferenceEquals(a, b))
                return true;
            if (ReferenceEquals(a, null) || ReferenceEquals(b, null))
                return false;
            return a.assetId == b.assetId;
        }

        public static bool operator !=(AudioClip a, AudioClip b)
        {
            return !(a == b);
        }

        public override string ToString()
        {
            return "AudioClip " + Name;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetNameInternal(int idLow, int idHigh);
    }
}
