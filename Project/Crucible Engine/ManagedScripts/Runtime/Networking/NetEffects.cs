/*************************************************************************
* \file:         NetEffects.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         One-call effects: play an effect here and on every other machine,
*                offline, on the Host or on a Client, with the same line.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Register each effect once (a static field is easiest), then play it:
*
*        static readonly NetEffect HitFlash = NetEffect.Register("fx.hitflash",
*            (target, position, extra) => Flash(target, position));
*
*        HitFlash.Play(enemy, hitPoint);
*
*    Offline it just plays. On the Host it plays now, and every Client plays
*    it too. On a Client it plays now, and the Host passes it on to everyone
*    else (at most 30 a second per Client; more are dropped). The machine
*    that played it never plays it twice.
*
*    Effects are looks and sounds: they must not change the game, since a
*    Client may start them. Something that matters goes through a command,
*    an event or a network variable. A late joiner does not see old effects.
*
*    `unreliable: true` sends it without a delivery guarantee: cheaper, for
*    frequent effects where a lost one does not matter (sparks, footsteps).
*    The target must be a network object to mean the same entity elsewhere
*    (others get null); extra data goes in a NetWriter.
*
* \history
*    -v1.0       Register, Play; the Host relays a Client's effects
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible
{
    /// <summary>What an effect does on each machine: the entity it is on, where, and any extra data.</summary>
    public delegate void NetEffectHandler(Entity target, Vector3 position, NetReader extra);

    /// <summary>An effect that plays on every machine (see NetEffects.cs).</summary>
    public sealed class NetEffect
    {
        /// <summary>A Client's effects the Host passes on per second (a burst of this many too).</summary>
        public const int RelayPerSecond = 30;

        private static readonly Dictionary<ushort, NetEffect> effects = new Dictionary<ushort, NetEffect>();
        // A set kept as Dictionary<..., bool>, NOT HashSet: HashSet lives in System.Core.dll, which
        // the engine's Mono cannot load (see AI/NodeHost.cs).
        private static readonly Dictionary<ushort, bool> warnedUnknown = new Dictionary<ushort, bool>();
        private static readonly double[] tokens = new double[Network.MaxPlayers];
        private static readonly double[] refilled = new double[Network.MaxPlayers];
        private static bool warnedRate;

        private NetEffectHandler handler;

        public string Name { get; private set; }
        public ushort Id { get; private set; }
        public bool Unreliable { get; private set; }

        /// <summary>Times this machine played it (any origin).</summary>
        public int PlayedHere { get; private set; }

        private NetEffect(string name, ushort id) { Name = name; Id = id; }

        /// <summary>
        /// Registers (or replaces) the effect called <paramref name="name"/>. Every machine must
        /// register it, with the same name, to play it.
        /// </summary>
        public static NetEffect Register(string name, NetEffectHandler handler, bool unreliable = false)
        {
            if (string.IsNullOrEmpty(name) || handler == null)
            {
                Log.Error("NetEffect.Register: an effect needs a name and a handler");
                return null;
            }
            ushort id = NetMessage.KindOf(name);
            NetEffect effect;
            if (effects.TryGetValue(id, out effect) && effect.Name != name)
            {
                Log.Error("NetEffect: \"" + effect.Name + "\" and \"" + name + "\" get the same id - rename one of them");
                return effect;
            }
            if (effect == null)
            {
                effect = new NetEffect(name, id);
                effects[id] = effect;
            }
            effect.handler = handler;
            effect.Unreliable = unreliable;
            return effect;
        }

        /// <summary>The registered effect called <paramref name="name"/>, or null.</summary>
        public static NetEffect Find(string name)
        {
            NetEffect effect;
            return effects.TryGetValue(NetMessage.KindOf(name), out effect) && effect.Name == name ? effect : null;
        }

        /// <summary>
        /// Plays it here now and on every other machine. False (logged) if it could not be sent;
        /// it still played here.
        /// </summary>
        public bool Play(Entity target = null, Vector3 position = default(Vector3), NetWriter extra = null)
        {
            PlayHere(target, position, extra != null ? new NetReader(extra) : new NetReader(new NetWriter()));
            if (!Network.IsOnline || !Network.IsReady)
                return true;   // single player, or not in the game yet: here only
            int origin = Network.LocalPeerId;
            NetWriter message = new NetWriter()
                .WriteUShort(Id)
                .WriteByte((byte)origin)
                .WriteBool(Unreliable)
                .WriteEntity(target)
                .WriteVector3(position)
                .WriteRaw(extra);
            if (Network.IsHost)
                return Unreliable
                    ? Network.SendHelperEvent(NetRouter.EffectEvent, message, true, -1)
                    : Network.SendHelperEvent(NetRouter.EffectEvent, message, false, Network.HostPeerId);
            return Network.SendHelperCommand(NetRouter.EffectRequest, message);
        }

        private void PlayHere(Entity target, Vector3 position, NetReader extra)
        {
            ++PlayedHere;
            try
            {
                handler(target, position, extra);
            }
            catch (Exception e)
            {
                Log.Error("NetEffect \"" + Name + "\" failed: " + e.Message);
            }
        }

        // An effect event (NetRouter): play it unless this machine started it.
        internal static void Receive(NetReader data)
        {
            ushort id = data.ReadUShort();
            int origin = data.ReadByte();
            data.ReadBool();
            Entity target = data.ReadEntity();
            Vector3 position = data.ReadVector3();
            if (!data.Ok || origin == Network.LocalPeerId)
                return;
            NetEffect effect;
            if (!effects.TryGetValue(id, out effect))
            {
                if (!warnedUnknown.ContainsKey(id) && (warnedUnknown[id] = true))
                    Log.Warning("NetEffect: an effect this machine has not registered (id " + id.ToString("X4") + ") - register it on every machine");
                return;
            }
            effect.PlayHere(target, position, data);
        }

        // The Host (NetRouter): a Client played an effect; pass it on to everyone else.
        internal static void Relay(int sender, byte[] payload)
        {
            if (!Network.IsHost || sender <= 0 || sender >= Network.MaxPlayers || payload.Length < 4)
                return;
            double now = Network.Time;
            if (now < refilled[sender])   // a new session: the shared clock started again
                tokens[sender] = RelayPerSecond;
            else
                tokens[sender] = Math.Min(RelayPerSecond, tokens[sender] + (now - refilled[sender]) * RelayPerSecond);
            refilled[sender] = now;
            if (tokens[sender] < 1.0)
            {
                if (!warnedRate)
                {
                    warnedRate = true;
                    Log.Warning("NetEffect: player " + sender + " plays more than " + RelayPerSecond + " effects a second; the extra ones are dropped");
                }
                return;
            }
            tokens[sender] -= 1.0;
            payload[2] = (byte)sender;   // the origin is who sent it, whatever the payload says
            bool unreliable = payload[3] != 0;
            NetWriter message = new NetWriter();
            foreach (byte b in payload)
                message.WriteByte(b);
            if (unreliable)
                Network.SendHelperEvent(NetRouter.EffectEvent, message, true, -1);
            else
                Network.SendHelperEvent(NetRouter.EffectEvent, message, false, sender);
        }

    }
}
