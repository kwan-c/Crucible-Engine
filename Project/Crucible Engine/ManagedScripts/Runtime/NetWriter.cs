/*************************************************************************
* \file:         NetWriter.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          16 November 2026
* \version       1.0
* \brief         Packs values into the bytes a network message carries
*                (Network.SendCommand). Part of Crucible.ScriptCore.dll.
*
* \details
*    Write the values in an order of your choosing; the receiver reads them
*    back with a NetReader in the SAME order:
*
*        var data = new NetWriter();
*        data.WriteInt(slot).WriteVector3(aimPoint);
*        Network.SendCommand(UseItem, data);
*
*        // in OnNetworkCommand:
*        int slot = data.ReadInt();
*        Vector3 aimPoint = data.ReadVector3();
*        if (!data.Ok) return;        // too short or damaged: ignore it
*
*    Nothing describes the layout but your two pieces of code, so keep them
*    next to each other. Numbers are little-endian; a string is UTF-8 with a
*    two-byte length in front; an entity travels as its NetworkId.
*
*    A writer can be reused: Clear() empties it and keeps its memory, so a
*    script that sends often should keep one in a field.
*
*    Pure C#: no engine calls except WriteEntity (the entity's NetworkId).
*
* \history
*    -v1.0       bool, byte, short, ushort, int, uint, float, string,
*                Vector3, Quaternion, Entity (Networking C# Phase 2)
*
**************************************************************************/
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Crucible
{
    /// <summary>Packs values into bytes for a network message. Read them back with NetReader.</summary>
    public sealed class NetWriter
    {
        /// <summary>
        /// The most a writer holds, in bytes. A write that would pass it writes nothing and sets
        /// Overflowed. (A command carries less: Network.MaxCommandBytes.)
        /// </summary>
        public const int MaxBytes = 16000;

        /// <summary>The longest string WriteString accepts, in UTF-8 bytes.</summary>
        public const int MaxStringBytes = 1000;

        private byte[] buffer = new byte[64];
        private int length;
        private bool overflowed;

        /// <summary>Bytes written so far.</summary>
        public int Length { get { return length; } }

        /// <summary>
        /// A write did not fit (MaxBytes) or a string was too long. Everything after it was
        /// skipped, so the message is incomplete: Network refuses to send it.
        /// </summary>
        public bool Overflowed { get { return overflowed; } }

        /// <summary>The bytes; only the first Length are meaningful. For Network.cs.</summary>
        internal byte[] Buffer { get { return buffer; } }

        /// <summary>Empties the writer for reuse (keeps its memory).</summary>
        public NetWriter Clear()
        {
            length = 0;
            overflowed = false;
            return this;
        }

        public NetWriter WriteByte(byte value)
        {
            if (Reserve(1))
                buffer[length++] = value;
            return this;
        }

        /// <summary>One byte: 1 or 0.</summary>
        public NetWriter WriteBool(bool value)
        {
            return WriteByte(value ? (byte)1 : (byte)0);
        }

        public NetWriter WriteUShort(ushort value)
        {
            if (Reserve(2))
            {
                buffer[length++] = (byte)value;
                buffer[length++] = (byte)(value >> 8);
            }
            return this;
        }

        public NetWriter WriteShort(short value)
        {
            return WriteUShort(unchecked((ushort)value));
        }

        public NetWriter WriteUInt(uint value)
        {
            if (Reserve(4))
            {
                buffer[length++] = (byte)value;
                buffer[length++] = (byte)(value >> 8);
                buffer[length++] = (byte)(value >> 16);
                buffer[length++] = (byte)(value >> 24);
            }
            return this;
        }

        public NetWriter WriteInt(int value)
        {
            return WriteUInt(unchecked((uint)value));
        }

        public NetWriter WriteFloat(float value)
        {
            FloatBits bits = new FloatBits();
            bits.Single = value;
            return WriteUInt(bits.Bits);
        }

        /// <summary>
        /// Text as UTF-8 with its byte count in front. Null is written as "". Text longer than
        /// MaxStringBytes is not written at all (Overflowed).
        /// </summary>
        public NetWriter WriteString(string value)
        {
            if (value == null)
                value = string.Empty;
            int bytes = Encoding.UTF8.GetByteCount(value);
            if (bytes > MaxStringBytes)
            {
                overflowed = true;
                return this;
            }
            if (Reserve(2 + bytes))
            {
                buffer[length++] = (byte)bytes;
                buffer[length++] = (byte)(bytes >> 8);
                length += Encoding.UTF8.GetBytes(value, 0, value.Length, buffer, length);
            }
            return this;
        }

        /// <summary>Three floats: X, Y, Z.</summary>
        public NetWriter WriteVector3(Vector3 value)
        {
            if (Reserve(12))
                WriteFloat(value.X).WriteFloat(value.Y).WriteFloat(value.Z);
            return this;
        }

        /// <summary>Four floats: X, Y, Z, W.</summary>
        public NetWriter WriteQuaternion(Quaternion value)
        {
            if (Reserve(16))
                WriteFloat(value.X).WriteFloat(value.Y).WriteFloat(value.Z).WriteFloat(value.W);
            return this;
        }

        /// <summary>
        /// A reference to a network object, as its NetworkId (four bytes). Null, a destroyed
        /// entity, or one that is not a network object is written as "none", and ReadEntity
        /// then returns null - an Entity itself means nothing on another machine.
        /// </summary>
        public NetWriter WriteEntity(Entity value)
        {
            return WriteUInt(ReferenceEquals(value, null) ? 0u : value.NetworkId);
        }

        // Room for `count` more bytes? Grows the buffer; past MaxBytes it sets Overflowed.
        private bool Reserve(int count)
        {
            if (overflowed)
                return false;
            if (length + count > MaxBytes)
            {
                overflowed = true;
                return false;
            }
            if (length + count > buffer.Length)
                Array.Resize(ref buffer, Math.Min(MaxBytes, Math.Max(buffer.Length * 2, length + count)));
            return true;
        }
    }

    /// <summary>The four bytes of a float, without unsafe code or a temporary array.</summary>
    [StructLayout(LayoutKind.Explicit)]
    internal struct FloatBits
    {
        [FieldOffset(0)] public float Single;
        [FieldOffset(0)] public uint Bits;
    }
}
