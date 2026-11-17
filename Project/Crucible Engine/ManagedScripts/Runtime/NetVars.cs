/*************************************************************************
* \file:         NetVars.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          17 November 2026
* \version       1.0
* \brief         An entity's network variables: values the host sets and
*                every player sees (health, ammo, a state). Part of
*                Crucible.ScriptCore.dll.
*
* \details
*    Every entity has 16 numbered slots, 0 to 15 (Entity.NetVars). Number
*    them yourself, like command kinds, and keep the numbers in one place:
*
*        const int Health = 0, Ammo = 1;            // this object's slots
*
*        // the authority (host, or single player) changes them:
*        if (Network.IsAuthority) Entity.NetVars.SetInt(Health, 100);
*
*        // every machine reads them, and can be told when one changes:
*        public override void OnNetVarChanged(int slot)
*        {
*            if (slot == Health) healthBar.Value = Entity.NetVars.GetInt(Health);
*        }
*
*    For the values to reach the other players the object must be a network
*    object that replicates them: in its Network Object table, set the
*    NetVariables row to Replicated. Then a player who joins later gets the
*    current values with the object, and a lost packet is made up for by the
*    next one - unlike an event, a variable is state, not a moment.
*
*    A slot holds 32 bits: an int, a float, a bool or a reference to a
*    network object. A Vector3 takes three slots in a row. Read a slot the
*    way it was written: nothing remembers its type. Every slot starts at 0
*    (0, 0f, false, null), so the host sets the first values, in OnCreate.
*
*    On any other entity (not a network object, or in a single-player game)
*    the variables still work, but stay on this machine.
*
*    Cost: the whole object is resent when anything on it changes, so use
*    variables for values that change now and then, not every frame.
*
*    C++ side: Engine/src/Scripting/Bindings/NetworkBindings.cpp
*    (NetVariables in Engine/src/Network/NetComponents.h).
*
* \history
*    -v1.0       int, float, bool, Vector3 and Entity slots (Networking C#
*                Phase 4)
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>An entity's 16 network variables. Get it with Entity.NetVars.</summary>
    public sealed class NetVars
    {
        /// <summary>How many slots an entity has: 0 to SlotCount - 1.</summary>
        public const int SlotCount = 16;

        // Must match NetworkBindings.cpp.
        private const int VarOk = 0, VarNotAuthority = 2, VarNotReplicated = 3;

        private readonly EntityIdentity entity;

        internal NetVars(EntityIdentity owner) { entity = owner; }

        // --- reading (any machine) ----------------------------------------------------- //

        public int GetInt(int slot)
        {
            return unchecked((int)Get(slot));
        }

        /// <summary>A float; 0 if the slot does not hold a real number.</summary>
        public float GetFloat(int slot)
        {
            FloatBits bits = new FloatBits();
            bits.Bits = Get(slot);
            return float.IsNaN(bits.Single) || float.IsInfinity(bits.Single) ? 0f : bits.Single;
        }

        public bool GetBool(int slot)
        {
            return Get(slot) != 0;
        }

        /// <summary>A Vector3 from three slots: slot, slot + 1 and slot + 2.</summary>
        public Vector3 GetVector3(int slot)
        {
            return new Vector3(GetFloat(slot), GetFloat(slot + 1), GetFloat(slot + 2));
        }

        /// <summary>
        /// The network object written with SetEntity, or null: none was written, or this
        /// machine does not have that object (any more).
        /// </summary>
        public Entity GetEntity(int slot)
        {
            return Network.FindEntity(Get(slot));
        }

        // --- writing (the authority) ---------------------------------------------------- //
        // Each returns false, with the reason logged, if nothing was written: a client
        // writing a network object's variable, an object that does not replicate
        // NetVariables, a destroyed entity, or a slot outside 0 to 15.

        public bool SetInt(int slot, int value)
        {
            return Set(slot, unchecked((uint)value));
        }

        /// <summary>NaN and infinity are refused.</summary>
        public bool SetFloat(int slot, float value)
        {
            if (float.IsNaN(value) || float.IsInfinity(value))
            {
                Log.Error("NetVars.SetFloat(" + slot + "): the value is not a real number");
                return false;
            }
            FloatBits bits = new FloatBits();
            bits.Single = value;
            return Set(slot, bits.Bits);
        }

        public bool SetBool(int slot, bool value)
        {
            return Set(slot, value ? 1u : 0u);
        }

        /// <summary>A Vector3 into three slots: slot, slot + 1 and slot + 2 (so slot is at most 13).</summary>
        public bool SetVector3(int slot, Vector3 value)
        {
            if (slot < 0 || slot + 2 >= SlotCount)
            {
                Log.Error("NetVars.SetVector3(" + slot + "): a Vector3 needs three slots, so the first is 0 to " + (SlotCount - 3));
                return false;
            }
            return SetFloat(slot, value.X) && SetFloat(slot + 1, value.Y) && SetFloat(slot + 2, value.Z);
        }

        /// <summary>
        /// A reference to a network object (stored as its NetworkId). Null, a destroyed entity
        /// or one that is not a network object is stored as "none".
        /// </summary>
        public bool SetEntity(int slot, Entity value)
        {
            return Set(slot, ReferenceEquals(value, null) ? 0u : value.NetworkId);
        }

        // --- engine side ---------------------------------------------------------------- //

        private uint Get(int slot)
        {
            uint bits;
            return GetInternal((int)entity.Index, (int)entity.Generation, slot, out bits) != 0 ? bits : 0u;
        }

        private bool Set(int slot, uint bits)
        {
            switch (SetInternal((int)entity.Index, (int)entity.Generation, slot, bits))
            {
                case VarOk:
                    return true;
                case VarNotAuthority:
                    Log.Error("NetVars (slot " + slot + "): only the authority sets a network object's variables - " +
                              "guard the call with Network.IsAuthority, or ask with Network.SendCommand");
                    return false;
                case VarNotReplicated:
                    Log.Error("NetVars (slot " + slot + "): this network object does not replicate its variables - " +
                              "set NetVariables to Replicated in its Network Object table");
                    return false;
                default:
                    Log.Error("NetVars (slot " + slot + "): the entity is destroyed, or the slot is not 0 to " + (SlotCount - 1));
                    return false;
            }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetInternal(int index, int generation, int slot, out uint bits);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetInternal(int index, int generation, int slot, uint bits);
    }
}
