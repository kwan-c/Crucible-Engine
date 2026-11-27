/*************************************************************************
* \file:         NetData.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         A network object's byte block (Entity.NetData): up to 256
*                bytes the host writes and every player reads.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    For state that does not fit numbered values (NetVars): lists, a
*    loadout, ammo per weapon. Write it with a NetWriter, read it with a
*    NetReader, in the same order:
*
*        // the host (or single player):
*        NetWriter w = new NetWriter();
*        w.WriteByte((byte)ammo.Length);
*        foreach (int a in ammo) w.WriteUShort((ushort)a);
*        Entity.NetData.Set(w);
*
*        // every machine, when it changes:
*        public override void OnNetDataChanged()
*        {
*            NetReader r = Entity.NetData.Read();
*            int count = r.ReadByte();
*            for (int i = 0; i < count; ++i) ammo[i] = r.ReadUShort();
*        }
*
*    The object must replicate it: in its Network Object table, set the
*    NetData row to Replicated. A player who joins later gets the current
*    bytes with the object. On any other entity (not a network object, or
*    single player) it still works, but stays on this machine.
*
*    Cost: the whole block is resent whenever the object's record is (any
*    change to the object), so keep it on objects that do not move every
*    frame, or keep it small.
*
*    C++ side: NetData in Engine/src/Network/NetComponents.h.
*
* \history
*    -v1.0       Read, Bytes, Length, Set, Clear (protocol 11)
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>An entity's network byte block. Get it with Entity.NetData.</summary>
    public sealed class NetData
    {
        /// <summary>The most bytes a block holds.</summary>
        public const int MaxBytes = 256;

        // Must match NetworkBindings.cpp.
        private const int DataOk = 0, DataNotAuthority = 2, DataNotReplicated = 3, DataTooLong = 4;

        private readonly EntityIdentity entity;

        internal NetData(EntityIdentity owner) { entity = owner; }

        // --- reading (any machine) ----------------------------------------------------- //

        /// <summary>A copy of the bytes now; empty if nothing was set yet (or the entity is gone).</summary>
        public byte[] Bytes
        {
            get { return GetInternal((int)entity.Index, (int)entity.Generation) ?? new byte[0]; }
        }

        /// <summary>How many bytes the block holds now.</summary>
        public int Length { get { return Bytes.Length; } }

        /// <summary>A reader over the bytes now, from the start.</summary>
        public NetReader Read()
        {
            byte[] bytes = Bytes;
            return new NetReader(bytes, bytes.Length);
        }

        // --- writing (the authority) ---------------------------------------------------- //
        // Each returns false, with the reason logged, if nothing was written: a client writing a
        // network object's data, an object that does not replicate NetData, a destroyed entity,
        // or more than MaxBytes.

        /// <summary>Replaces the block with what <paramref name="writer"/> holds.</summary>
        public bool Set(NetWriter writer)
        {
            if (ReferenceEquals(writer, null) || writer.Overflowed)
            {
                Log.Error("NetData.Set: the writer is null or overflowed");
                return false;
            }
            return Set(writer.Buffer, writer.Length);
        }

        /// <summary>Replaces the block with these bytes.</summary>
        public bool Set(byte[] bytes)
        {
            return bytes == null ? Clear() : Set(bytes, bytes.Length);
        }

        /// <summary>Empties the block.</summary>
        public bool Clear()
        {
            return Set(new byte[0], 0);
        }

        // --- engine side ---------------------------------------------------------------- //

        private bool Set(byte[] bytes, int length)
        {
            switch (SetInternal((int)entity.Index, (int)entity.Generation, bytes, length))
            {
                case DataOk:
                    return true;
                case DataNotAuthority:
                    Log.Error("NetData: only the authority sets a network object's data - guard the call with " +
                              "Network.IsAuthority, or ask with Network.SendCommand");
                    return false;
                case DataNotReplicated:
                    Log.Error("NetData: this network object does not replicate its data - set NetData to Replicated " +
                              "in its Network Object table");
                    return false;
                case DataTooLong:
                    Log.Error("NetData: " + length + " bytes is more than " + MaxBytes);
                    return false;
                default:
                    Log.Error("NetData: the entity is destroyed");
                    return false;
            }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] GetInternal(int index, int generation);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetInternal(int index, int generation, byte[] bytes, int length);
    }
}
