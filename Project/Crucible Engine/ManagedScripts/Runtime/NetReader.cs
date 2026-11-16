/*************************************************************************
* \file:         NetReader.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          16 November 2026
* \version       1.0
* \brief         Reads the values a NetWriter packed, in the same order
*                (EntityScript.OnNetworkCommand). Part of
*                Crucible.ScriptCore.dll.
*
* \details
*    The bytes came from another machine, so a reader NEVER throws and never
*    trusts them. A read that cannot be done - not enough bytes left, a bool
*    that is neither 0 nor 1, a float that is not a real number, text that
*    is not UTF-8 - returns a harmless value (0, false, "", null), and from
*    then on Ok is false and every later read returns a harmless value too.
*    So read everything first, then check once:
*
*        public override void OnNetworkCommand(int sender, ushort kind, NetReader data)
*        {
*            int slot = data.ReadInt();
*            Vector3 aim = data.ReadVector3();
*            if (!data.Ok) return;            // damaged or dishonest: ignore it
*            ...
*        }
*
*    Ok only says the bytes could be read. Whether the VALUES make sense
*    (is slot a real slot? is aim within reach?) is still yours to check.
*
* \history
*    -v1.0       the readers matching NetWriter (Networking C# Phase 2)
*
**************************************************************************/
using System;
using System.Text;

namespace Crucible
{
    /// <summary>Reads values packed by a NetWriter. Never throws; check Ok after reading.</summary>
    public sealed class NetReader
    {
        private static readonly UTF8Encoding StrictUtf8 = new UTF8Encoding(false, true);   // throws on bad bytes

        private readonly byte[] data;
        private readonly int end;
        private int position;
        private bool failed;

        /// <summary>Reads the first <paramref name="length"/> bytes of <paramref name="bytes"/>.</summary>
        internal NetReader(byte[] bytes, int length)
        {
            data = bytes ?? new byte[0];
            end = Math.Max(0, Math.Min(length, data.Length));
        }

        /// <summary>Reads back what a writer holds - for a quick local test of your layout.</summary>
        public NetReader(NetWriter writer)
        {
            data = new byte[writer != null ? writer.Length : 0];
            end = data.Length;
            if (end > 0)
                Array.Copy(writer.Buffer, data, end);
        }

        /// <summary>False once any read could not be done. See the file comment.</summary>
        public bool Ok { get { return !failed; } }

        /// <summary>Bytes not read yet.</summary>
        public int Remaining { get { return end - position; } }

        /// <summary>Everything was read (and nothing went wrong).</summary>
        public bool AtEnd { get { return !failed && position == end; } }

        public byte ReadByte()
        {
            return Take(1) ? data[position++] : (byte)0;
        }

        /// <summary>One byte that must be 0 or 1.</summary>
        public bool ReadBool()
        {
            byte value = ReadByte();
            if (value > 1)
                failed = true;
            return !failed && value == 1;
        }

        public ushort ReadUShort()
        {
            if (!Take(2))
                return 0;
            ushort value = (ushort)(data[position] | (data[position + 1] << 8));
            position += 2;
            return value;
        }

        public short ReadShort()
        {
            return unchecked((short)ReadUShort());
        }

        public uint ReadUInt()
        {
            if (!Take(4))
                return 0;
            uint value = (uint)(data[position] | (data[position + 1] << 8) | (data[position + 2] << 16)) |
                         ((uint)data[position + 3] << 24);
            position += 4;
            return value;
        }

        public int ReadInt()
        {
            return unchecked((int)ReadUInt());
        }

        /// <summary>A float that is a real number; NaN or infinity counts as damaged (Ok false, 0).</summary>
        public float ReadFloat()
        {
            FloatBits bits = new FloatBits();
            bits.Bits = ReadUInt();
            if (failed)
                return 0f;
            if (float.IsNaN(bits.Single) || float.IsInfinity(bits.Single))
            {
                failed = true;
                return 0f;
            }
            return bits.Single;
        }

        /// <summary>Text written by WriteString ("" if it could not be read).</summary>
        public string ReadString()
        {
            int bytes = ReadUShort();
            if (failed || bytes > NetWriter.MaxStringBytes || !Take(bytes))
            {
                failed = true;
                return string.Empty;
            }
            try
            {
                string text = StrictUtf8.GetString(data, position, bytes);
                position += bytes;
                return text;
            }
            catch (ArgumentException)   // not UTF-8
            {
                failed = true;
                return string.Empty;
            }
        }

        public Vector3 ReadVector3()
        {
            float x = ReadFloat(), y = ReadFloat(), z = ReadFloat();
            return failed ? Vector3.Zero : new Vector3(x, y, z);
        }

        public Quaternion ReadQuaternion()
        {
            float x = ReadFloat(), y = ReadFloat(), z = ReadFloat(), w = ReadFloat();
            return failed ? Quaternion.Identity : new Quaternion(x, y, z, w);
        }

        /// <summary>
        /// The entity written by WriteEntity, or null: "none" was written, or this machine has
        /// no such network object (it may have been destroyed on the way). A null is not a read
        /// error, so Ok stays true - check the result yourself.
        /// </summary>
        public Entity ReadEntity()
        {
            uint networkId = ReadUInt();
            return failed || networkId == 0 ? null : Network.FindEntity(networkId);
        }

        // Are `count` more bytes there? If not, the reader has failed for good.
        private bool Take(int count)
        {
            if (failed || count < 0 || count > end - position)
            {
                failed = true;
                return false;
            }
            return true;
        }
    }
}
