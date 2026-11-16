/*************************************************************************
* \file:         Network.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          16 November 2026
* \version       1.0
* \brief         The multiplayer session for scripts: who am I, who else is
*                here, and how a client asks the host to do something.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Crucible multiplayer is a listen server: one player hosts and decides
*    everything (the authority); up to three clients join and are shown what
*    the host decides. Every player has a PeerId: the host is 0, clients are
*    1 to 3.
*
*    The golden rule: only the authority changes the game.
*
*        if (Network.IsAuthority) health -= damage;      // host, or single player
*
*    A client that wants something to happen ASKS, with a command:
*
*        const ushort OpenDoor = 1;                      // your own numbers, from 1
*
*        // any machine, e.g. when the player presses E near the door:
*        Network.SendCommand(OpenDoor, null, door);
*
*        // in a script ON the door (the host runs it; so does a single-player game):
*        public override void OnNetworkCommand(int sender, ushort kind, NetReader data)
*        {
*            if (kind != OpenDoor) return;
*            // never trust a command: is this player allowed, and close enough?
*            Open();
*        }
*
*    SendCommand works the same in every role. On a client it travels to the
*    host; on the host, and in a single-player (offline) game, it is
*    delivered on the same machine at the start of the next frame. So a
*    script written this way needs no "if multiplayer" branches.
*
*    Offline is "a session of one": IsAuthority is true, LocalPeerId is 0,
*    and Peers is just { 0 }.
*
*    Callbacks (override them in an EntityScript; see EntityScript.cs):
*    OnNetworkStarted, OnNetworkStopped, OnPeerJoined, OnPeerReady,
*    OnPeerLeft, OnNetworkCommand.
*
*    Starting a session: in the Editor, the Network panel (Host / Client)
*    before pressing Play. Starting one from a script comes with the lobby.
*
*    C++ side: Engine/src/Scripting/Bindings/NetworkBindings.cpp.
*
* \history
*    -v1.0       role, peers, FindEntity (Networking C# Phase 1);
*                SendCommand (Networking C# Phase 2)
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>The multiplayer session: role, players and commands.</summary>
    public static class Network
    {
        /// <summary>The host's PeerId. Clients are 1 to MaxPlayers - 1.</summary>
        public const int HostPeerId = 0;
        /// <summary>The most players a session can have, the host included.</summary>
        public const int MaxPlayers = 4;
        /// <summary>The largest payload SendCommand accepts, in bytes.</summary>
        public const int MaxCommandBytes = 1024;

        // Must match NetworkBindings.cpp.
        private const int RoleOffline = 0, RoleHost = 1, RoleClient = 2;
        private const int SendOk = 0, SendRefused = 1, SendBadArgument = 2, SendBadTarget = 3;

        // --- who am I ---------------------------------------------------------------- //

        /// <summary>No session: a single-player game. This machine decides everything.</summary>
        public static bool IsOffline { get { return Role() == RoleOffline; } }

        /// <summary>This machine hosts the session (and plays in it as peer 0).</summary>
        public static bool IsHost { get { return Role() == RoleHost; } }

        /// <summary>This machine joined (or is joining) somebody else's session.</summary>
        public static bool IsClient { get { return Role() == RoleClient; } }

        /// <summary>A session is running here, as host or client.</summary>
        public static bool IsOnline { get { return Role() != RoleOffline; } }

        /// <summary>
        /// This machine decides what happens in the game: the host, or a single-player game.
        /// Guard every change to shared game state with it (health, score, doors, spawning).
        /// </summary>
        public static bool IsAuthority { get { return Role() != RoleClient; } }

        /// <summary>
        /// This machine is in the game. Always true for the host and offline; on a client only
        /// once it has joined and the host's world has arrived (OnNetworkStarted says when).
        /// </summary>
        public static bool IsReady
        {
            get { int role, peer, ready; return GetStateInternal(out role, out peer, out ready) != 0 && ready != 0; }
        }

        /// <summary>
        /// This machine's PeerId: 0 for the host and offline, 1 to 3 for a client, and -1 for a
        /// client that has not been accepted yet.
        /// </summary>
        public static int LocalPeerId
        {
            get { int role, peer, ready; return GetStateInternal(out role, out peer, out ready) != 0 ? peer : 0; }
        }

        /// <summary>This player's name (the Network panel's Player name).</summary>
        public static string LocalPlayerName
        {
            get { int peer = LocalPeerId; return peer < 0 ? string.Empty : GetPeerName(peer); }
        }

        // --- who else is here --------------------------------------------------------- //

        /// <summary>How many players are in the session, this one included (1 when offline).</summary>
        public static int PlayerCount
        {
            get
            {
                int mask = GetPeerMaskInternal(), count = 0;
                for (int peer = 0; peer < MaxPlayers; ++peer)
                    if ((mask & (1 << peer)) != 0)
                        ++count;
                return count;
            }
        }

        /// <summary>
        /// The PeerIds of everyone in the session, lowest first, this machine included. A new
        /// array each call: keep it, or call it when a player joins or leaves, not every frame.
        /// On a client it is empty until the host has answered.
        /// </summary>
        public static int[] Peers
        {
            get
            {
                int mask = GetPeerMaskInternal();
                int[] peers = new int[PlayerCount];
                for (int peer = 0, next = 0; peer < MaxPlayers && next < peers.Length; ++peer)
                    if ((mask & (1 << peer)) != 0)
                        peers[next++] = peer;
                return peers;
            }
        }

        /// <summary>Is a player with this PeerId in the session?</summary>
        public static bool IsPeerConnected(int peer)
        {
            int inGame;
            return GetPeerInfoInternal(peer, out inGame) != 0;
        }

        /// <summary>
        /// Is this player in the game - connected AND finished loading the host's world?
        /// (A player who has just joined is connected first and ready a moment later.)
        /// </summary>
        public static bool IsPeerReady(int peer)
        {
            int inGame;
            return GetPeerInfoInternal(peer, out inGame) != 0 && inGame != 0;
        }

        /// <summary>The player's name, or "" if nobody has that PeerId.</summary>
        public static string GetPeerName(int peer)
        {
            return GetPeerNameInternal(peer) ?? string.Empty;
        }

        // --- network objects ---------------------------------------------------------- //

        /// <summary>
        /// The entity of a network object, from its NetworkId (Entity.NetworkId: the same number
        /// on every machine, unlike an Entity). Null if this machine has no such object.
        /// </summary>
        public static Entity FindEntity(uint networkId)
        {
            int index, generation;
            return networkId != 0 && FindEntityInternal(networkId, out index, out generation) != 0
                ? new Entity(unchecked((uint)index), unchecked((uint)generation))
                : null;
        }

        // --- commands ------------------------------------------------------------------ //

        /// <summary>
        /// Asks the authority to do something. It answers in OnNetworkCommand: on the host when
        /// a client sends this, and on this same machine (next frame) when the host or a
        /// single-player game does.
        /// </summary>
        /// <param name="kind">Your own number for this request, 1 to 65535.</param>
        /// <param name="data">What it needs to know (at most MaxCommandBytes), or null.</param>
        /// <param name="target">
        /// The entity it is about, or null. With a target only the scripts ON that entity get
        /// OnNetworkCommand; without one every script does. From a client the target must be a
        /// network object (Entity.IsNetworked), or the host could not tell which one is meant.
        /// </param>
        /// <returns>
        /// False if it was not sent: kind 0, too much data, a destroyed target, or a client that
        /// is not in the game yet (Network.IsReady). The reason is logged.
        /// </returns>
        public static bool SendCommand(ushort kind, NetWriter data = null, Entity target = null)
        {
            if (kind == 0)
            {
                Log.Error("Network.SendCommand: kind 0 is reserved - number your commands from 1");
                return false;
            }
            if (data != null && (data.Overflowed || data.Length > MaxCommandBytes))
            {
                Log.Error("Network.SendCommand(" + kind + "): the data is " + (data.Overflowed ? "over the NetWriter limit" : data.Length + " bytes") +
                          "; a command carries at most " + MaxCommandBytes);
                return false;
            }
            bool hasTarget = !ReferenceEquals(target, null);
            int result = SendCommandInternal(kind, data != null ? data.Buffer : null, data != null ? data.Length : 0,
                                             hasTarget ? 1 : 0, hasTarget ? (int)target.Index : 0,
                                             hasTarget ? (int)target.Generation : 0);
            switch (result)
            {
                case SendOk:
                    return true;
                case SendBadTarget:
                    Log.Error("Network.SendCommand(" + kind + "): the target is destroyed, or (on a client) is not a network object");
                    return false;
                case SendBadArgument:
                    Log.Error("Network.SendCommand(" + kind + "): refused - bad kind or data size");
                    return false;
                default:
                    Log.Warning("Network.SendCommand(" + kind + "): not sent - this machine is not in the game yet (check Network.IsReady)");
                    return false;
            }
        }

        // --- engine side ---------------------------------------------------------------- //

        // One byte[] per command, shared by every script that hears it (each gets its own
        // NetReader, which only reads).
        private static int payloadSerial;
        private static byte[] payload;

        /// <summary>The payload of the command being delivered (EntityScript.DispatchNetworkEvent).</summary>
        internal static byte[] PayloadOf(int serial)
        {
            if (payload == null || payloadSerial != serial)
            {
                payload = GetEventPayloadInternal(serial) ?? new byte[0];
                payloadSerial = serial;
            }
            return payload;
        }

        /// <summary>Is the entity a network object, and which one, owned by whom? (Entity.cs)</summary>
        internal static bool GetIdentity(uint index, uint generation, out uint networkId, out int owner)
        {
            return GetIdentityInternal((int)index, (int)generation, out networkId, out owner) != 0;
        }

        private static int Role()
        {
            int role, peer, ready;
            return GetStateInternal(out role, out peer, out ready) != 0 ? role : RoleOffline;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetStateInternal(out int role, out int localPeer, out int ready);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetPeerMaskInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetPeerInfoInternal(int peer, out int inGame);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetPeerNameInternal(int peer);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetIdentityInternal(int index, int generation, out uint networkId, out int owner);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int FindEntityInternal(uint networkId, out int index, out int generation);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SendCommandInternal(int kind, byte[] data, int length, int hasTarget, int targetIndex,
                                                      int targetGeneration);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] GetEventPayloadInternal(int serial);
    }
}
