/*************************************************************************
* \file:         Texture.cs
* \author:       <author>
* \par:          Email: <email>
* \date          2 November 2026
* \version       1.0
* \brief         References to image and font assets (Texture, Font), for
*                swapping UI pictures and fonts from scripts.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Declare a public field and drag the file from the Asset Browser onto it
*    in the Inspector, then hand it to a UI component:
*
*        public Texture fullHeart, emptyHeart;
*        public Font    titleFont;
*        ...
*        heart.GetComponent<UIImage>().Texture = hp > 0 ? fullHeart : emptyHeart;
*        title.GetComponent<UIText>().Font = titleFont;
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
    /// <summary>An image file (.png, .jpg, ...). Assign it in the Inspector.</summary>
    public sealed class Texture
    {
        // Written by native code (ScriptRuntime.cpp, WriteFields). Do not rename.
#pragma warning disable 0649 // assigned from C++, which the compiler cannot see
        private ulong assetId;
#pragma warning restore 0649

        private Texture() {}

        // The guid crosses to C++ as two ints (no `long` in internal calls).
        internal int IdLow { get { return unchecked((int)(uint)assetId); } }
        internal int IdHigh { get { return unchecked((int)(uint)(assetId >> 32)); } }

        internal static Texture FromId(int low, int high)
        {
            ulong id = ((ulong)(uint)high << 32) | (uint)low;
            if (id == 0)
                return null;
            var texture = new Texture();
            texture.assetId = id;
            return texture;
        }

        /// <summary>The file name, e.g. "heart_full.png".</summary>
        public string Name { get { return GetNameInternal(IdLow, IdHigh) ?? string.Empty; } }

        public override bool Equals(object other)
        {
            var texture = other as Texture;
            return !ReferenceEquals(texture, null) && texture.assetId == assetId;
        }
        public override int GetHashCode() { return assetId.GetHashCode(); }
        public static bool operator ==(Texture a, Texture b)
        {
            if (ReferenceEquals(a, b)) return true;
            if (ReferenceEquals(a, null) || ReferenceEquals(b, null)) return false;
            return a.assetId == b.assetId;
        }
        public static bool operator !=(Texture a, Texture b) { return !(a == b); }
        public override string ToString() { return "Texture " + Name; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetNameInternal(int idLow, int idHigh);
    }

    /// <summary>
    /// A font file (.ttf / .otf / .cfont). Assign it in the Inspector. A .ttf / .otf must
    /// have been compiled in the Font Importer, or UI text falls back to the default font.
    /// </summary>
    public sealed class Font
    {
        // Written by native code (ScriptRuntime.cpp, WriteFields). Do not rename.
#pragma warning disable 0649 // assigned from C++, which the compiler cannot see
        private ulong assetId;
#pragma warning restore 0649

        private Font() {}

        internal int IdLow { get { return unchecked((int)(uint)assetId); } }
        internal int IdHigh { get { return unchecked((int)(uint)(assetId >> 32)); } }

        internal static Font FromId(int low, int high)
        {
            ulong id = ((ulong)(uint)high << 32) | (uint)low;
            if (id == 0)
                return null;
            var font = new Font();
            font.assetId = id;
            return font;
        }

        /// <summary>The file name, e.g. "Orbitron.cfont".</summary>
        public string Name { get { return GetNameInternal(IdLow, IdHigh) ?? string.Empty; } }

        public override bool Equals(object other)
        {
            var font = other as Font;
            return !ReferenceEquals(font, null) && font.assetId == assetId;
        }
        public override int GetHashCode() { return assetId.GetHashCode(); }
        public static bool operator ==(Font a, Font b)
        {
            if (ReferenceEquals(a, b)) return true;
            if (ReferenceEquals(a, null) || ReferenceEquals(b, null)) return false;
            return a.assetId == b.assetId;
        }
        public static bool operator !=(Font a, Font b) { return !(a == b); }
        public override string ToString() { return "Font " + Name; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetNameInternal(int idLow, int idHigh);
    }
}
