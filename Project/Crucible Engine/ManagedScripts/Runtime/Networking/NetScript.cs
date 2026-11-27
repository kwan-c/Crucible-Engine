/*************************************************************************
* \file:         NetScript.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         A script whose fields are synced: NetInt, NetFloat, NetBool,
*                NetVector3, NetEntityRef and NetTimer fields bound to the
*                entity's network variables.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Change the base class and the field types; the logic stays:
*
*        public sealed class Mech : NetScript
*        {
*            readonly NetInt health = new NetInt(slot: 0, initial: 100);
*            readonly NetTimer dash = new NetTimer(slot: 1);
*
*            public override void OnCreate()
*            {
*                health.Changed += (old, now) => Log.Info("health " + now);
*            }
*            void TakeDamage(int n) { health.Value -= n; }   // the Host
*        }
*
*    The fields are found when the script is created (before OnCreate). On
*    the authority (the Host, or single player) each field with an initial
*    value sets it then. Changed fires on every machine when the value
*    changes, the authority's own changes included (the frame after).
*
*    Rules: each field needs its own slots (two on one slot are reported);
*    scripts on the same entity share the entity's 64 slots, so give them
*    different numbers; a NetScript that overrides OnNetVarChanged must call
*    base.OnNetVarChanged(slot). The object needs NetVariables replicated
*    (its Network Object table), as for Entity.NetVars.
*
* \history
*    -v1.0       NetScript: binds its NetField fields, initial values, Changed
*
**************************************************************************/
using System;
using System.Collections.Generic;
using System.Reflection;

namespace Crucible
{
    /// <summary>An EntityScript whose NetField fields are kept in step on every machine.</summary>
    public abstract class NetScript : EntityScript
    {
        private readonly List<NetField> fields = new List<NetField>();

        /// <summary>The synced fields this script declares.</summary>
        protected IList<NetField> NetFields { get { return fields; } }

        internal override void OnEntityBound()
        {
            ulong used = 0;
            for (Type type = GetType(); type != null && type != typeof(NetScript); type = type.BaseType)
            {
                foreach (FieldInfo info in type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic |
                                                          BindingFlags.DeclaredOnly))
                {
                    if (!typeof(NetField).IsAssignableFrom(info.FieldType))
                        continue;
                    NetField field = info.GetValue(this) as NetField;
                    if (field == null || !field.Valid)
                        continue;
                    ulong mask = field.Mask;
                    if ((used & mask) != 0)
                        Log.Error(GetType().Name + "." + info.Name + ": slot " + field.Slot + " is already used by another field - give it its own");
                    used |= mask;
                    field.Bind(this, info.Name);
                    fields.Add(field);
                }
            }
            if (Network.IsAuthority)
                foreach (NetField field in fields)
                    field.ApplyInitial();
            foreach (NetField field in fields)
                field.Capture();
        }

        /// <summary>Raises the fields' Changed events. An override must call base.OnNetVarChanged(slot).</summary>
        public override void OnNetVarChanged(int slot)
        {
            foreach (NetField field in fields)
                if (field.Covers(slot))
                    field.Refresh();
        }
    }
}
