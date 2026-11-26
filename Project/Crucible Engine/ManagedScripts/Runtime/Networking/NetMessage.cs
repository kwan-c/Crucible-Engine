/*************************************************************************
* \file:         NetMessage.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Named network messages: a command or event kind worked out from a
*                name, the same on every machine.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Numbering commands and events by hand means every teammate has to
*    pick numbers nobody else uses. A NetMessage takes a name instead:
*
*        static readonly NetMessage EnemyDied = NetMessage.Named("enemy.died");
*
*        EnemyDied.SendEvent(writer, enemy);              // the Host tells everyone
*        HealMe.SendCommand(writer);                      // a Client asks the Host
*
*        // in any script, as before:
*        public override void OnNetworkEvent(ushort kind, NetReader data)
*        {
*            if (EnemyDied.Is(kind)) ...
*        }
*
*        // or once per machine, without a script override:
*        NetMessage.OnEvent(EnemyDied, (data, target) => ..., this);
*
*    The number comes from the name (0x8000 to 0xFEFF), so it is the same on
*    every machine with the same build. Two names that come out on the same
*    number are reported as an error naming both: rename one. Hand-numbered
*    kinds (1 to 0x7FFF) keep working next to named ones.
*
*    A handler given an owner (usually `this`) is dropped once the owner's
*    entity is gone, so handlers of an earlier Play never run.
*
* \history
*    -v1.0       Named, Fixed, Is, Send*, OnCommand, OnEvent, NameOf
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible
{
    /// <summary>A command handler: who sent it, its data, and the entity it is about (or null).</summary>
    public delegate void NetCommandHandler(int sender, NetReader data, Entity target);

    /// <summary>An event handler: its data, and the entity it is about (or null).</summary>
    public delegate void NetEventHandler(NetReader data, Entity target);

    /// <summary>A command or event kind with a name.</summary>
    public struct NetMessage
    {
        /// <summary>Named kinds: 0x8000 to 0xFEFF.</summary>
        public const ushort FirstNamedKind = 0x8000;
        public const ushort LastNamedKind = 0xFEFF;

        private static readonly Dictionary<ushort, string> names = new Dictionary<ushort, string>();

        public string Name { get; private set; }
        public ushort Kind { get; private set; }

        private NetMessage(string name, ushort kind) : this()
        {
            Name = name;
            Kind = kind;
        }

        /// <summary>The message called <paramref name="name"/>: its kind comes from the name.</summary>
        public static NetMessage Named(string name)
        {
            if (string.IsNullOrEmpty(name))
            {
                Log.Error("NetMessage.Named: a message needs a name");
                return new NetMessage(string.Empty, 0);
            }
            return Register(name, KindOf(name));
        }

        /// <summary>A name for a hand-picked kind (1 to 0x7FFF), so logs and NameOf show it.</summary>
        public static NetMessage Fixed(string name, ushort kind)
        {
            if (kind == 0 || kind >= FirstNamedKind)
            {
                Log.Error("NetMessage.Fixed(\"" + name + "\", " + kind + "): hand-picked kinds are 1 to " + (FirstNamedKind - 1));
                return new NetMessage(name ?? string.Empty, 0);
            }
            return Register(name ?? string.Empty, kind);
        }

        /// <summary>
        /// The kind a name gets: FNV-1a over its characters, folded into 0x8000 to 0xFEFF.
        /// The same on every machine.
        /// </summary>
        public static ushort KindOf(string name)
        {
            uint hash = 2166136261u;
            foreach (char c in name ?? string.Empty)
            {
                hash ^= c;
                hash = unchecked(hash * 16777619u);
            }
            return (ushort)(FirstNamedKind + hash % (uint)(LastNamedKind - FirstNamedKind + 1));
        }

        private static NetMessage Register(string name, ushort kind)
        {
            string existing;
            if (names.TryGetValue(kind, out existing))
            {
                if (existing != name)
                    Log.Error("NetMessage: \"" + existing + "\" and \"" + name + "\" both get kind " + kind.ToString("X4") +
                              " - rename one of them");
            }
            else
            {
                names[kind] = name;
            }
            return new NetMessage(name, kind);
        }

        /// <summary>The name registered for a kind, or its number ("#0x8123") if none.</summary>
        public static string NameOf(ushort kind)
        {
            string name;
            return names.TryGetValue(kind, out name) ? name : "#0x" + kind.ToString("X4");
        }

        /// <summary>Is <paramref name="kind"/> (from OnNetworkCommand / OnNetworkEvent) this message?</summary>
        public bool Is(ushort kind) { return Kind != 0 && kind == Kind; }

        // --- sending: the same rules as Network.SendCommand / SendEvent -------------------- //

        public bool SendCommand(NetWriter data = null, Entity target = null) { return Kind != 0 && Network.SendCommand(Kind, data, target); }
        public bool SendEvent(NetWriter data = null, Entity target = null, int toPeer = Network.Everyone) { return Kind != 0 && Network.SendEvent(Kind, data, target, toPeer); }
        public bool SendEventExcept(int exceptPeer, NetWriter data = null, Entity target = null) { return Kind != 0 && Network.SendEventExcept(exceptPeer, Kind, data, target); }
        public bool SendEventUnreliable(NetWriter data = null, Entity target = null) { return Kind != 0 && Network.SendEventUnreliable(Kind, data, target); }

        public override string ToString() { return Name + " (" + Kind.ToString("X4") + ")"; }

        // --- handlers, once per machine (NetRouter) ------------------------------------------ //

        private sealed class Handler
        {
            public ushort Kind;
            public NetCommandHandler OnCommand;
            public NetEventHandler OnEvent;
            public EntityScript Owner;   // null: kept for good

            public bool Stale
            {
                get { return !ReferenceEquals(Owner, null) && (ReferenceEquals(Owner.Entity, null) || !Owner.Entity.IsAlive); }
            }
        }

        private static readonly List<Handler> handlers = new List<Handler>();

        /// <summary>
        /// Calls <paramref name="handler"/> on the authority (the Host, or single player) for every
        /// <paramref name="message"/> command, once - whether or not a script overrides
        /// OnNetworkCommand. With an <paramref name="owner"/> it is dropped once the owner's entity
        /// is gone.
        /// </summary>
        public static void OnCommand(NetMessage message, NetCommandHandler handler, EntityScript owner = null)
        {
            if (message.Kind == 0 || handler == null)
                return;
            handlers.Add(new Handler { Kind = message.Kind, OnCommand = handler, Owner = owner });
        }

        /// <summary>
        /// Calls <paramref name="handler"/> on every machine for every <paramref name="message"/>
        /// event, once. With an <paramref name="owner"/> it is dropped once the owner's entity is gone.
        /// </summary>
        public static void OnEvent(NetMessage message, NetEventHandler handler, EntityScript owner = null)
        {
            if (message.Kind == 0 || handler == null)
                return;
            handlers.Add(new Handler { Kind = message.Kind, OnEvent = handler, Owner = owner });
        }

        /// <summary>Removes every handler <paramref name="owner"/> registered (e.g. in OnDestroy).</summary>
        public static void RemoveHandlers(EntityScript owner)
        {
            handlers.RemoveAll(h => ReferenceEquals(h.Owner, owner));
        }

        internal static void Dispatch(bool command, int sender, ushort kind, byte[] payload, Entity target)
        {
            handlers.RemoveAll(h => h.Stale);
            Handler[] snapshot = handlers.ToArray();   // a handler may register more
            foreach (Handler h in snapshot)
            {
                if (h.Kind != kind)
                    continue;
                try
                {
                    if (command && h.OnCommand != null)
                        h.OnCommand(sender, new NetReader(payload, payload.Length), target);
                    else if (!command && h.OnEvent != null)
                        h.OnEvent(new NetReader(payload, payload.Length), target);
                }
                catch (Exception e)
                {
                    Log.Error("NetMessage handler for " + NameOf(kind) + " failed: " + e.Message);
                }
            }
        }
    }
}
