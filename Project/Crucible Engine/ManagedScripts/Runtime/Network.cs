/*************************************************************************
* \file:         Network.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          16 November 2026
* \version       1.7
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
*    The other direction is an event: the authority tells EVERY machine that
*    something happened, so each one can show it.
*
*        const ushort DoorOpened = 1;
*        // authority, after it decided:
*        Network.SendEvent(DoorOpened, null, door);
*        // in a script on the door, on every machine (the host too):
*        public override void OnNetworkEvent(ushort kind, NetReader data)
*        {
*            if (kind == DoorOpened) PlayCreak();
*        }
*
*    Use an event for a moment (a sound, an effect, a message). Lasting state
*    still has to be something the engine keeps in step (Transform, Sync
*    on/off): a player who joins later never hears an old event.
*
*    Spawning: only the authority creates and destroys network objects.
*
*        if (Network.IsAuthority)
*        {
*            Entity bolt = Network.Spawn(boltPrefab, muzzle, aim);   // appears on every machine
*            ...
*            Network.Despawn(bolt);                                  // gone on every machine
*        }
*
*    The prefab must be a network prefab (a Network Object component on its
*    root). Prefab.Instantiate still makes a local-only copy.
*
*    Callbacks (override them in an EntityScript; see EntityScript.cs):
*    OnNetworkStarted, OnNetworkStopped, OnPeerJoined, OnPeerReady,
*    OnPeerLeft, OnNetworkCommand, OnNetworkEvent.
*
*    Starting a session: in the Editor, the Network panel (Host / Client)
*    before pressing Play. Starting one from a script comes with the lobby.
*
*    C++ side: Engine/src/Scripting/Bindings/NetworkBindings.cpp.
*
* \history
*    -v1.0       role, peers, FindEntity (Networking C# Phase 1);
*                SendCommand (Networking C# Phase 2)
*    -v1.1       SendEvent (Networking C# Phase 5A); Spawn and Despawn
*                (Networking C# Phase 3)
*    -v1.2       SendEventExcept, SendEventUnreliable (Networking C# Phase 5B)
*    -v1.3       GetPlayerEntity, LocalPlayerEntity (Networking C# Phase 6)
*    -v1.4       LoadScene (Networking C# Phase 9); StartHost, Join, Leave
*                (Networking C# Phase 10)
*    -v1.5       the LAN lobby list (StartBrowsing, Lobbies, Join(LobbyInfo),
*                SetLobby); LastEndCode, LastEndReason, DescribeEnd
*    -v1.6       the LAN connection server: LobbyServerState, LobbyServerAddress, LobbyDiscovery,
*                LobbyListFromServer, LobbyListTotal, LobbyListTruncated, IsLobbyRegistered;
*                LastBrowseError, LobbyListingProblem; LobbyServerFailedAttempts,
*                LobbyServerMaxAttempts, UseLanDiscovery, CanUseLanDiscovery, IsUsingLanDiscovery
*    -v1.7       ObjectOf (Networking C# Phase 8: client physics queries)
*
**************************************************************************/
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>The multiplayer session: role, players and commands.</summary>
    public static class Network
    {
        // --- starting and leaving (a menu or lobby script) ------------------------------ //

        /// <summary>
        /// Hosts a session from this machine, starting at the next frame. Port 0 (and a null
        /// name) = this instance's settings (EngineConfig.json, or the Editor's Network panel).
        /// The result arrives as a callback: OnNetworkStarted, or OnNetworkStopped with the
        /// reason (the port is in use...). False, with the reason logged, if it cannot even be
        /// asked: a session already runs, or the settings are invalid.
        /// </summary>
        public static bool StartHost(int port = 0, string playerName = null)
        {
            return Start(1, null, port, playerName);
        }

        /// <summary>
        /// Joins the host at <paramref name="address"/> (an IP address, e.g. "192.168.1.20" or
        /// "127.0.0.1"), starting at the next frame. Null address / port 0 / null name = this
        /// instance's settings. OnNetworkStarted follows once joined and the world has arrived;
        /// OnNetworkStopped with the reason if the join fails (refused, wrong version, timeout).
        /// </summary>
        public static bool Join(string address, int port = 0, string playerName = null)
        {
            return Start(2, address, port, playerName);
        }

        /// <summary>
        /// Leaves the session at the next update. On the host it ends the session for everyone.
        /// OnNetworkStopped follows. False if no session runs.
        /// </summary>
        public static bool Leave(string reason = null)
        {
            return LeaveInternal(reason) != 0;
        }

        // --- the LAN lobby list ----------------------------------------------------------- //

        /// <summary>
        /// Starts listing the sessions hosted on the local network (Lobbies). Keep it on while a
        /// lobby browser is shown; StopBrowsing when it closes. False (reason logged) if nothing
        /// can be listened to (LAN mode and the port is taken; Server mode and no server address).
        /// Where the list comes from is the Network settings' lobby discovery (LobbyDiscovery):
        /// the connection server, LAN broadcast, or (Auto) the connection server first - if it
        /// does not answer LobbyServerMaxAttempts requests, LobbyServerState is Unreachable and
        /// the game may offer LAN discovery (CanUseLanDiscovery, UseLanDiscovery).
        /// </summary>
        public static bool StartBrowsing()
        {
            string error = BrowseInternal(1);
            LastBrowseError = error ?? "";
            if (error == null)
                return true;
            Log.Error("Network.StartBrowsing: " + error);
            return false;
        }

        /// <summary>
        /// Why the last StartBrowsing failed, for showing in place of an empty list ("" when it
        /// succeeded, and after StopBrowsing). For example: "Lobby discovery is set to the
        /// connection server, but no server address is set."
        /// </summary>
        public static string LastBrowseError { get; private set; } = "";

        /// <summary>Stops listing sessions (Lobbies becomes empty).</summary>
        public static void StopBrowsing()
        {
            BrowseInternal(0);
            LastBrowseError = "";
        }

        /// <summary>
        /// The sessions found, one entry per session (LobbyInfo.FromServer / FromLan say how):
        /// the connection server's list (open ones first, then in the order they were created), or
        /// the LAN list (in the order found). Either way a session keeps its place. A session that
        /// closes drops off within a few seconds. A new array each call.
        /// </summary>
        public static LobbyInfo[] Lobbies
        {
            get
            {
                int count = LobbyCountInternal();
                var lobbies = new List<LobbyInfo>(count);
                for (int i = 0; i < count; ++i)
                {
                    int port, players, maxPlayers, flags;
                    float age;
                    if (GetLobbyInternal(i, out port, out players, out maxPlayers, out flags, out age) == 0)
                        continue;
                    lobbies.Add(new LobbyInfo(GetLobbyTextInternal(i, 3) ?? "", GetLobbyTextInternal(i, 0) ?? "", port,
                                              GetLobbyTextInternal(i, 1) ?? "", GetLobbyTextInternal(i, 2) ?? "", players,
                                              maxPlayers, (flags & 1) != 0, (flags & 2) != 0, age,
                                              (flags & 4) != 0, (flags & 8) != 0));
                }
                return lobbies.ToArray();
            }
        }

        /// <summary>
        /// Joins a listed session. False (reason logged) if it is not joinable right now: full,
        /// closed, another version, or not heard from recently (it may have closed).
        /// </summary>
        public static bool Join(LobbyInfo lobby, string playerName = null)
        {
            if (lobby == null)
                return false;
            if (!lobby.IsJoinable)
            {
                Log.Warning("Network.Join: " + lobby.HostName + "'s session cannot be joined (" + lobby.Availability + ")");
                return false;
            }
            return Join(lobby.Address, lobby.Port, playerName);
        }

        // --- the LAN connection server ------------------------------------------------------ //

        /// <summary>
        /// The connection server (ConnectionServer/) as this machine sees it: Off when none is
        /// set or nothing is asked of it (not browsing, not hosting a listed session), Connecting,
        /// Online or Unreachable. A running session never depends on it.
        /// </summary>
        public static LobbyServerStatus LobbyServerState
        {
            get
            {
                int status, mode, total, flags;
                return LobbyServerInfoInternal(out status, out mode, out total, out flags) != 0 ? (LobbyServerStatus)status : LobbyServerStatus.Off;
            }
        }

        /// <summary>"192.168.0.5:27018": the connection server in use (or set), "" if none.</summary>
        public static string LobbyServerAddress { get { return LobbyServerAddressInternal() ?? ""; } }

        /// <summary>How lobbies are found: the browser's mode while browsing, else the setting.</summary>
        public static LobbyDiscoveryMode LobbyDiscovery
        {
            get
            {
                int status, mode, total, flags;
                return LobbyServerInfoInternal(out status, out mode, out total, out flags) != 0 ? (LobbyDiscoveryMode)mode : LobbyDiscoveryMode.Auto;
            }
        }

        /// <summary>True while Lobbies is the connection server's list (false: the LAN list).</summary>
        public static bool LobbyListFromServer { get { return (LobbyServerFlags() & 1) != 0; } }

        /// <summary>
        /// How many sessions the connection server knows of (0 while the LAN list is shown). More
        /// than Lobbies.Length when the list was truncated (one reply holds about 25).
        /// </summary>
        public static int LobbyListTotal
        {
            get
            {
                int status, mode, total, flags;
                return LobbyServerInfoInternal(out status, out mode, out total, out flags) != 0 ? total : 0;
            }
        }

        /// <summary>The connection server sent fewer sessions than it knows of (LobbyListTotal).</summary>
        public static bool LobbyListTruncated { get { return (LobbyServerFlags() & 2) != 0; } }

        /// <summary>Host: the connection server has this session in its list.</summary>
        public static bool IsLobbyRegistered { get { return (LobbyServerFlags() & 4) != 0; } }

        /// <summary>
        /// Host: why lobby browsers cannot find this session, or "" (listed, still trying, or not
        /// hosting). For example lobby discovery is Server but no server address is set. Join by
        /// IP still works either way.
        /// </summary>
        public static string LobbyListingProblem { get { return LobbyListingProblemInternal() ?? ""; } }

        /// <summary>
        /// The lobby browser's list requests to the connection server that went unanswered in a
        /// row (0 after a reply). While connecting, attempt LobbyServerFailedAttempts + 1 of
        /// LobbyServerMaxAttempts is in flight; at LobbyServerMaxAttempts the state is Unreachable.
        /// </summary>
        public static int LobbyServerFailedAttempts
        {
            get
            {
                int attempts, max;
                return LobbyBrowseInfoInternal(out attempts, out max) != 0 ? attempts : 0;
            }
        }

        /// <summary>How many unanswered list requests make the connection server Unreachable (5).</summary>
        public static int LobbyServerMaxAttempts
        {
            get
            {
                int attempts, max;
                LobbyBrowseInfoInternal(out attempts, out max);
                return max > 0 ? max : 5;
            }
        }

        /// <summary>The player chose LAN discovery for this browse (UseLanDiscovery).</summary>
        public static bool IsUsingLanDiscovery
        {
            get
            {
                int attempts, max;
                return (LobbyBrowseInfoInternal(out attempts, out max) & 1) != 0;
            }
        }

        /// <summary>
        /// UseLanDiscovery would switch the list now: Auto mode, a connection server set, LAN
        /// discovery running, not chosen yet. Offer it when LobbyServerState is Unreachable.
        /// </summary>
        public static bool CanUseLanDiscovery
        {
            get
            {
                int attempts, max;
                return (LobbyBrowseInfoInternal(out attempts, out max) & 2) != 0;
            }
        }

        /// <summary>
        /// Lists the sessions found by LAN discovery instead of the connection server, until the
        /// next StartBrowsing (which tries the server again). For when the player agreed after
        /// the server was not accessible. False (reason logged) if not browsing, in Server mode,
        /// or LAN discovery could not start on this PC.
        /// </summary>
        public static bool UseLanDiscovery()
        {
            string error = UseLanDiscoveryInternal();
            if (error == null)
                return true;
            Log.Warning("Network.UseLanDiscovery: " + error);
            return false;
        }

        private static int LobbyServerFlags()
        {
            int status, mode, total, flags;
            return LobbyServerInfoInternal(out status, out mode, out total, out flags) != 0 ? flags : 0;
        }

        /// <summary>
        /// Host: whether new players may join (a closed session refuses them, and lobby lists
        /// show it as not joinable) and the status lists show ("In lobby", "In match").
        /// A new session starts open. False unless this machine hosts. Closing it (the match
        /// started) also takes it off the connection server's list, freeing its slot there - the
        /// session and its players are not affected; opening it again lists it again. LAN lobby
        /// lists keep showing it, as not joinable.
        /// </summary>
        public static bool SetLobby(bool open, string status = null)
        {
            return SetLobbyInternal(open ? 1 : 0, status) != 0;
        }

        // --- why a session ended -------------------------------------------------------- //

        /// <summary>
        /// Why the latest session ended, as a number (EndCode): on a client, mostly what the host
        /// said - EndCode.SessionEnded when the host left, EndCode.ServerFull, EndCode.LobbyClosed -
        /// or EndCode.ConnectionLost* when the host stopped answering; 0 when this machine ended it.
        /// Read it in OnNetworkStopped.
        /// </summary>
        public static int LastEndCode { get { return LastEndCodeInternal(); } }

        /// <summary>The engine's own text for the latest end (technical; for logs).</summary>
        public static string LastEndReason { get { return LastEndReasonInternal() ?? string.Empty; } }

        /// <summary>
        /// A sentence for players about the latest end. <paramref name="wasJoined"/>: the session
        /// had started for this machine (OnNetworkStarted ran) - a failed join reads differently
        /// from a lost game.
        /// </summary>
        public static string DescribeEnd(bool wasJoined)
        {
            switch (LastEndCode)
            {
                case EndCode.ServerFull:
                    return "That lobby is full.";
                case EndCode.LobbyClosed:
                    return "That game has already started.";
                case EndCode.ProtocolMismatch:
                    return "That host runs a different version of the game.";
                case EndCode.SceneMismatch:
                    return "Your game files do not match the host's.";
                case EndCode.SessionEnded:
                case EndCode.Generic:
                    return wasJoined ? "The host left. The session has ended." : "The host closed that lobby.";
                case EndCode.Timeout:
                case EndCode.ConnectionLostRemote:
                case EndCode.ConnectionLostMisc:
                    return wasJoined ? "Lost the connection to the host. The session has ended."
                                     : "Could not reach that lobby. It may have closed.";
                case 0:
                    return wasJoined ? "You left the session." : "Could not start: " + LastEndReason;
                default:
                    return (wasJoined ? "The session ended" : "Could not join") + " (" + LastEndReason + ").";
            }
        }

        private static bool Start(int mode, string address, int port, string playerName)
        {
            string error = StartInternal(mode, address, port, playerName);
            if (error == null)
                return true;
            Log.Error("Network." + (mode == 1 ? "StartHost" : "Join") + ": " + error);
            return false;
        }

        /// <summary>
        /// Moves the whole session to another scene (a path inside Assets, e.g.
        /// "Scenes/Arena.scene"). Host: everyone loads it, the session goes on, the players are
        /// spawned again, and OnAllPeersReady says when every client is in. Offline: an ordinary
        /// Scene.Load. A client cannot: false. Every machine must have the scene.
        /// (Scene.Load during a session still ends it.)
        /// </summary>
        public static bool LoadScene(string path)
        {
            switch (LoadSceneInternal(path))
            {
                case 0:
                    return true;
                case 2:
                    Log.Error("Network.LoadScene: '" + path + "' is not a scene in the project");
                    return false;
                default:
                    Log.Error("Network.LoadScene('" + path + "'): only the host changes the session's scene");
                    return false;
            }
        }

        /// <summary>The host's PeerId. Clients are 1 to MaxPlayers - 1.</summary>
        public const int HostPeerId = 0;
        /// <summary>The most players a session can have, the host included.</summary>
        public const int MaxPlayers = 4;
        /// <summary>The largest payload SendCommand accepts, in bytes.</summary>
        public const int MaxCommandBytes = 1024;
        /// <summary>The largest payload SendEvent accepts, in bytes.</summary>
        public const int MaxEventBytes = NetWriter.MaxBytes;
        /// <summary>SendEvent's toPeer for "every machine".</summary>
        public const int Everyone = -1;
        /// <summary>The largest payload SendEventUnreliable accepts, in bytes (one packet).</summary>
        public const int MaxUnreliableEventBytes = 1000;

        // Must match NetworkBindings.cpp.
        private const int RoleOffline = 0, RoleHost = 1, RoleClient = 2;
        private const int SendOk = 0, SendRefused = 1, SendBadArgument = 2, SendBadTarget = 3, SendNotAuthority = 4;
        private const int SpawnOk = 0, SpawnNotAuthority = 1, SpawnNotNetworkPrefab = 2, SpawnBadOwner = 3;

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

        /// <summary>
        /// The character the session spawned for a player (the player prefab: the one with
        /// "Spawn per player" in its Network Object), or null: that player is not in the game
        /// yet, the project has no player prefab, or no session is running. It appears a frame
        /// or two after OnPeerReady, so look it up when you need it instead of keeping it.
        /// </summary>
        public static Entity GetPlayerEntity(int peer)
        {
            int index, generation;
            return GetPlayerEntityInternal(peer, out index, out generation) != 0
                ? new Entity(unchecked((uint)index), unchecked((uint)generation))
                : null;
        }

        /// <summary>This machine's own character: GetPlayerEntity(LocalPeerId).</summary>
        public static Entity LocalPlayerEntity
        {
            get { int peer = LocalPeerId; return peer < 0 ? null : GetPlayerEntity(peer); }
        }

        /// <summary>
        /// The network object an entity belongs to: the entity itself or its nearest parent
        /// that is one, or null (not networked, no session, or gone). Use it on raycast hits:
        /// on the Host a ray hits the object (or one of its parts), on a Client it hits a
        /// "Network Query Body" the engine added under the object, because a Client has no
        /// physics of its own for network objects. Network.ObjectOf(hit.Entity) is the object
        /// on both.
        /// </summary>
        public static Entity ObjectOf(Entity entity)
        {
            if (entity == null)
                return null;
            int index, generation;
            return ObjectOfInternal((int)entity.Index, (int)entity.Generation, out index, out generation) != 0
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

        // --- events -------------------------------------------------------------------- //

        /// <summary>
        /// The authority tells every machine that something happened; each one gets
        /// OnNetworkEvent at the start of its next frame (this machine too). In a
        /// single-player game it is simply delivered here.
        /// </summary>
        /// <param name="kind">Your own number for this event, 1 to 65535.</param>
        /// <param name="data">What it needs to know (at most MaxEventBytes), or null.</param>
        /// <param name="target">
        /// The network object it is about, or null. With a target only the scripts ON that
        /// entity get OnNetworkEvent; without one every script does. An object spawned with
        /// Network.Spawn in this same frame is fine.
        /// </param>
        /// <param name="toPeer">Everyone (the default), or one player's PeerId.</param>
        /// <returns>
        /// False if it was not sent: called on a client, kind 0, too much data, or a target
        /// that is destroyed or (in a session) not a network object. The reason is logged.
        /// </returns>
        public static bool SendEvent(ushort kind, NetWriter data = null, Entity target = null, int toPeer = Everyone)
        {
            return SendEventCore("SendEvent", kind, data, target, toPeer, -1, false);
        }

        /// <summary>
        /// SendEvent to every machine except one player's - usually the player who caused it
        /// and has already shown the effect itself. <paramref name="exceptPeer"/> may be 0:
        /// then the host's own scripts are the ones left out.
        /// </summary>
        public static bool SendEventExcept(int exceptPeer, ushort kind, NetWriter data = null, Entity target = null)
        {
            if (exceptPeer < 0 || exceptPeer >= MaxPlayers)
            {
                Log.Error("Network.SendEventExcept(" + kind + "): " + exceptPeer + " is not a PeerId");
                return false;
            }
            return SendEventCore("SendEventExcept", kind, data, target, Everyone, exceptPeer, false);
        }

        /// <summary>
        /// SendEvent to every machine, but sent once with no guarantee: it may be lost, and it
        /// may arrive before a reliable event or a spawn sent earlier. Cheaper and never
        /// delays anything else - for frequent effects where a missed one does not matter
        /// (footsteps, sparks). Never for anything the game depends on, and not about an
        /// object spawned this frame. At most MaxUnreliableEventBytes of data.
        /// </summary>
        public static bool SendEventUnreliable(ushort kind, NetWriter data = null, Entity target = null)
        {
            if (data != null && data.Length > MaxUnreliableEventBytes)
            {
                Log.Error("Network.SendEventUnreliable(" + kind + "): " + data.Length + " bytes; an unreliable event carries at most " +
                          MaxUnreliableEventBytes);
                return false;
            }
            return SendEventCore("SendEventUnreliable", kind, data, target, Everyone, -1, true);
        }

        private static bool SendEventCore(string call, ushort kind, NetWriter data, Entity target, int toPeer, int exceptPeer,
                                          bool unreliable)
        {
            if (kind == 0)
            {
                Log.Error("Network.SendEvent: kind 0 is reserved - number your events from 1");
                return false;
            }
            if (data != null && data.Overflowed)
            {
                Log.Error("Network.SendEvent(" + kind + "): the data is over the NetWriter limit of " + NetWriter.MaxBytes + " bytes");
                return false;
            }
            bool hasTarget = !ReferenceEquals(target, null);
            int result = SendEventInternal(kind, data != null ? data.Buffer : null, data != null ? data.Length : 0, toPeer,
                                           exceptPeer, unreliable ? 1 : 0, hasTarget ? 1 : 0,
                                           hasTarget ? (int)target.Index : 0, hasTarget ? (int)target.Generation : 0);
            switch (result)
            {
                case SendOk:
                    return true;
                case SendNotAuthority:
                    Log.Error("Network.SendEvent(" + kind + "): only the authority sends events - a client asks with SendCommand");
                    return false;
                case SendBadTarget:
                    Log.Error("Network.SendEvent(" + kind + "): the target is destroyed or is not a network object");
                    return false;
                case SendBadArgument:
                    Log.Error("Network.SendEvent(" + kind + "): refused - bad kind, data size or toPeer");
                    return false;
                default:
                    Log.Warning("Network.SendEvent(" + kind + "): not sent (see the log)");
                    return false;
            }
        }

        // --- spawning ------------------------------------------------------------------ //

        /// <summary>
        /// The authority creates a network object: a copy of a network prefab that appears on
        /// every machine, now and for players who join later. It exists here at once (its
        /// scripts get OnCreate next frame, like Prefab.Instantiate). In a single-player game
        /// it is an ordinary Instantiate.
        /// </summary>
        /// <param name="prefab">A prefab with a Network Object component on its root.</param>
        /// <param name="owner">
        /// The PeerId it belongs to (Entity.Owner / IsMine on every machine): the host by
        /// default; a player's id for that player's character or belongings.
        /// </param>
        /// <returns>
        /// The new root entity, or null: called on a client, not a network prefab, no such
        /// player, or the spawn failed. The reason is logged.
        /// </returns>
        public static Entity Spawn(Prefab prefab, Vector3 position, Quaternion rotation, int owner = HostPeerId)
        {
            if (prefab == null)
            {
                Log.Error("Network.Spawn: the prefab is null (is the field assigned in the Inspector?)");
                return null;
            }
            int index, generation;
            int result = SpawnInternal(prefab.IdLow, prefab.IdHigh, ref position, ref rotation, owner, out index, out generation);
            switch (result)
            {
                case SpawnOk:
                    return new Entity(unchecked((uint)index), unchecked((uint)generation));
                case SpawnNotAuthority:
                    Log.Error("Network.Spawn(" + prefab.Name + "): only the authority spawns - a client asks with SendCommand");
                    return null;
                case SpawnNotNetworkPrefab:
                    Log.Error("Network.Spawn(" + prefab.Name + "): not a network prefab - add a Network Object component to its root and save it");
                    return null;
                case SpawnBadOwner:
                    Log.Error("Network.Spawn(" + prefab.Name + "): no player has PeerId " + owner);
                    return null;
                default:
                    Log.Error("Network.Spawn(" + prefab.Name + "): failed (see the log)");
                    return null;
            }
        }

        /// <summary>Spawns at the origin, unrotated.</summary>
        public static Entity Spawn(Prefab prefab, int owner = HostPeerId)
        {
            return Spawn(prefab, Vector3.Zero, Quaternion.Identity, owner);
        }

        /// <summary>
        /// The authority removes an entity at the end of the frame (or after
        /// <paramref name="delay"/> seconds); a network object disappears on every machine.
        /// The same as Entity.Destroy, plus the check that a client never does it: a client
        /// that destroys its copy of a network object only breaks its own picture of the game.
        /// </summary>
        public static bool Despawn(Entity entity, float delay = 0f)
        {
            if (ReferenceEquals(entity, null) || !entity.IsAlive)
                return false;
            if (!IsAuthority && entity.IsNetworked)
            {
                Log.Error("Network.Despawn(" + entity + "): only the authority removes network objects - a client asks with SendCommand");
                return false;
            }
            Entity.Destroy(entity, delay);
            return true;
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
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetPlayerEntityInternal(int peer, out int index, out int generation);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int ObjectOfInternal(int index, int generation, out int outIndex, out int outGeneration);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string StartInternal(int mode, string address, int port, string playerName);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int LeaveInternal(string reason);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int LoadSceneInternal(string path);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string BrowseInternal(int on);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int LobbyCountInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetLobbyInternal(int index, out int port, out int players, out int maxPlayers, out int flags,
                                                   out float age);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetLobbyTextInternal(int index, int which);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetLobbyInternal(int open, string status);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int LobbyServerInfoInternal(out int status, out int mode, out int total, out int flags);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string LobbyServerAddressInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string LobbyListingProblemInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int LobbyBrowseInfoInternal(out int attempts, out int maxAttempts);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string UseLanDiscoveryInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int LastEndCodeInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string LastEndReasonInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SendEventInternal(int kind, byte[] data, int length, int toPeer, int exceptPeer,
                                                    int unreliable, int hasTarget, int targetIndex, int targetGeneration);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SpawnInternal(int idLow, int idHigh, ref Vector3 position, ref Quaternion rotation, int owner,
                                                out int index, out int generation);
    }
}
