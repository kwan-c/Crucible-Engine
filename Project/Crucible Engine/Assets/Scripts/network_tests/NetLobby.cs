/*************************************************************************
* \file:         NetLobby.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          20 November 2026
* \version       2.3
* \brief         Networking sample: a multiplayer lobby with a session
*                browser. Create a lobby (you host) or pick one from the
*                list of sessions on your network (you join); get ready;
*                the host starts the match for everyone. One per scene, on
*                a network scene object.
*
* \details
*    Screens (NetLobbyUi prefab, all one canvas; the buttons send
*    OnUIAction ids "lobby.*"):
*
*      Menu      Create Lobby  -> Network.StartHost: this player hosts
*                Find Lobbies  -> Browser
*      Browser   the sessions on the local network (Network.StartBrowsing,
*                Network.Lobbies), refreshed every frame: host, players,
*                and whether it can be joined (open, full, in progress,
*                another version, not responding). Pick one, Join Lobby ->
*                Network.Join: this player is a client. No IP address to
*                type. Back -> Menu. The header says where the list comes
*                from: the connection server, or LAN broadcast (also when
*                the server is unreachable), and "showing N of M" when
*                there are more than fit.
*      Busy      "Creating your lobby..." / "Joining X's lobby...", Cancel
*      Room      everyone in the session and who is ready (network
*                variables, slot = PeerId); Ready (a command: the host
*                decides); Start Match (host, once everyone is ready and
*                there are enough players): the lobby closes to new players
*                and Network.LoadScene takes everyone to arenaScene;
*                Leave Lobby
*
*    Endings: whenever the session ends - the host left or closed the game,
*    the connection was lost, a join was refused, this player left - the
*    screen goes back to the menu (or to the list, after a failed join)
*    with a sentence saying why (Network.DescribeEnd). The arena sends a
*    player here with its own sentence (ArrivalMessage).
*
*    A session never outlives its host: when the host goes, the engine ends
*    it for every client (EndCode.SessionEnded, or a lost connection).
*
*    Setup (network_lobby.scene): this entity has a Network Object with
*    NetVariables Replicated and this script Everywhere; UI Prefab =
*    Prefabs/network_tests/NetLobbyUi. The Network panel stays Offline.
*
* \history
*    -v1.0       keyboard lobby (Networking C# Phase 10)
*    -v2.0       lobby screens and the LAN session browser
*    -v2.1       the LAN connection server: where the list comes from, and
*                whether the host is listed there; why the browser or the
*                host's lobby cannot work (no server address) instead of
*                "Searching..." / "Players on your network can see it"; Auto
*                tries the connection server 5 times, then says it is not
*                accessible and offers Search LAN (Network.UseLanDiscovery)
*    -v2.2       the default name is the Network panel's ("Player"), no longer
*                the Windows user name
*    -v2.3       the network scenes moved to Scenes/network_tests (arenaScene's default)
**************************************************************************/
using System;

namespace Crucible.@Scripts
{
    public sealed class NetLobby : EntityScript
    {
        private const ushort ReadyCommand = 1;   // payload: bool
        private const int ReadySlot = 0;         // + PeerId
        private const int Rows = 6;

        /// <summary>The scene the host starts (a path inside Assets).</summary>
        public string arenaScene = "Scenes/network_tests/network_gamplay.scene";
        /// <summary>This player's name; empty = the Network panel's ("Player" unless changed there).</summary>
        public string playerName = "";
        /// <summary>Port to host on; 0 = this instance's setting (Network panel, EngineConfig.json).</summary>
        public int port = 0;
        /// <summary>Players needed before the host can start (the host included).</summary>
        public int minPlayers = 2;
        /// <summary>The lobby screens (NetLobbyUi prefab).</summary>
        public Prefab uiPrefab;

        /// <summary>A sentence to show when this scene opens (set by the arena before it sends a player back).</summary>
        public static string ArrivalMessage;

        // This lobby started or joined the running session (so a session already running when the
        // scene opens is ours, back from the arena - not one the Editor's Network panel started).
        private static bool sessionFromLobby;

        private enum Screen { Menu, Browser, Busy, Room }
        private Screen screen;
        private bool joining;        // Busy: true joining, false creating
        private bool cancelled;      // the player pressed Cancel
        private bool wasJoined;      // OnNetworkStarted ran in this session
        private bool starting;       // the host pressed Start Match
        private string selectedId;   // the lobby picked in the list
        private string joiningName = "";

        // The UI
        private Entity menu, browser, busy, room;
        private UIText title, subtitle, status, busyText, roomTitle, playersText, roomHint, emptyText, header;
        private readonly Entity[] rows = new Entity[Rows];
        private readonly UIText[] rowLabels = new UIText[Rows];
        private readonly string[] rowIds = new string[Rows];
        private UIButton joinButton, readyButton, startButton;
        private Entity lanEntity;   // Browser > "Use LAN": shown when the connection server is not accessible
        private UIText readyLabel;
        private Entity startEntity;

        // --- set up ------------------------------------------------------------------------- //

        public override void OnCreate()
        {
            Input.SetMouseLocked(false);   // a menu: the arena locks it again
            Entity ui = uiPrefab != null ? uiPrefab.Instantiate() : null;
            if (ui == null)
            {
                Log.Error("NetLobby: the UI Prefab field is empty (Prefabs/network_tests/NetLobbyUi)");
                return;
            }
            title = TextOf(ui, "Title");
            subtitle = TextOf(ui, "Subtitle");
            status = TextOf(ui, "Status");
            menu = ui.FindChild("Menu");
            browser = ui.FindChild("Browser");
            busy = ui.FindChild("Busy");
            room = ui.FindChild("Room");
            busyText = TextOf(busy, "Busy Text");
            roomTitle = TextOf(room, "Room Title");
            playersText = TextOf(room, "Players");
            roomHint = TextOf(room, "Room Hint");
            emptyText = TextOf(browser, "Empty");
            header = TextOf(browser, "Header");
            for (int i = 0; i < Rows; ++i)
            {
                rows[i] = browser != null ? browser.FindChild("Row " + i) : null;
                rowLabels[i] = TextOf(rows[i], "Label");
            }
            joinButton = ButtonOf(browser, "Join");
            lanEntity = browser != null ? browser.FindChild("Use LAN") : null;
            if (lanEntity != null)
                lanEntity.SetActive(false);
            readyButton = ButtonOf(room, "Ready");
            readyLabel = TextOf(room != null ? room.FindChild("Ready") : null, "Label");
            startEntity = room != null ? room.FindChild("Start") : null;
            startButton = startEntity != null ? startEntity.GetComponent<UIButton>() : null;

            // Back from a match (the session goes on), or a fresh start.
            if (Network.IsOnline)
            {
                if (!sessionFromLobby)
                    ArrivalMessage = "This session was started by the Network panel (" + (Network.IsHost ? "Host" : "Client") +
                                     "). Set the panel to Offline to use Create Lobby and Find Lobbies.";
                // Back from the arena with the session: a client may still be receiving this
                // scene's world (OnNetworkStarted then moves it to the room).
                wasJoined = true;
                joining = Network.IsClient;
                joiningName = Network.GetPeerName(Network.HostPeerId);
                if (Network.IsHost)
                    Network.SetLobby(true, "In lobby");
                Show(Network.IsReady ? Screen.Room : Screen.Busy);
            }
            else
            {
                sessionFromLobby = false;
                Show(Screen.Menu);
            }
            Say(ArrivalMessage ?? "");
            ArrivalMessage = null;
        }

        public override void OnDestroy()
        {
            Network.StopBrowsing();
        }

        private static UIText TextOf(Entity parent, string child)
        {
            Entity e = parent != null ? parent.FindChild(child) : null;
            return e != null ? e.GetComponent<UIText>() : null;
        }

        private static UIButton ButtonOf(Entity parent, string child)
        {
            Entity e = parent != null ? parent.FindChild(child) : null;
            return e != null ? e.GetComponent<UIButton>() : null;
        }

        private string LocalName()
        {
            if (!string.IsNullOrEmpty(playerName))
                return playerName;
            string configured = Network.GetPeerName(Network.HostPeerId);   // offline: this instance's setting
            return !string.IsNullOrEmpty(configured) ? configured : "Player";
        }

        // --- screens ------------------------------------------------------------------------- //

        private void Show(Screen next)
        {
            screen = next;
            if (menu != null) menu.SetActive(next == Screen.Menu);
            if (browser != null) browser.SetActive(next == Screen.Browser);
            if (busy != null) busy.SetActive(next == Screen.Busy);
            if (room != null) room.SetActive(next == Screen.Room);
            if (next == Screen.Browser)
                Network.StartBrowsing();
            else
                Network.StopBrowsing();
            if (next != Screen.Browser)
                selectedId = null;
        }

        private void Say(string text)
        {
            if (status != null)
                status.Text = text ?? "";
        }

        public override void OnUpdate(float deltaTime)
        {
            if (title != null)
                title.Text = screen == Screen.Room ? "Lobby" : "Crucible Arena";
            if (subtitle != null)
                subtitle.Text = "Playing as " + (Network.IsOnline && Network.IsReady ? Network.LocalPlayerName : LocalName()) + HostListing();
            if (screen == Screen.Browser)
                UpdateBrowser();
            else if (screen == Screen.Room)
                UpdateRoom();
            else if (screen == Screen.Busy && busyText != null)
                busyText.Text = joining ? "Joining " + joiningName + "'s lobby..." : "Creating your lobby...";
        }

        // --- the browser -------------------------------------------------------------------- //

        private void UpdateBrowser()
        {
            LobbyInfo[] lobbies = Network.Lobbies;
            bool selectedSeen = false;
            for (int i = 0; i < Rows; ++i)
            {
                LobbyInfo lobby = i < lobbies.Length ? lobbies[i] : null;
                rowIds[i] = lobby != null ? lobby.Id : null;
                if (rows[i] == null)
                    continue;
                rows[i].SetActive(lobby != null);
                if (lobby == null)
                    continue;
                bool selected = lobby.Id == selectedId;
                selectedSeen |= selected;
                if (rowLabels[i] != null)
                {
                    rowLabels[i].Text = Pad(lobby.HostName + "'s lobby", 34) + Pad(lobby.Players + " / " + lobby.MaxPlayers, 14) +
                                        lobby.Availability + (lobby.IsOpen && lobby.Status.Length > 0 ? " - " + lobby.Status : "");
                    rowLabels[i].Color = lobby.IsJoinable ? Color.White : Color.Gray;
                }
                UIImage background = rows[i].GetComponent<UIImage>();
                if (background != null)
                    background.Color = selected ? new Color(0.2f, 0.42f, 0.78f) : new Color(0.17f, 0.19f, 0.25f);
            }
            if (selectedId != null && !selectedSeen)
            {
                selectedId = null;
                Say("That lobby is no longer available.");
            }
            UpdateSource(lobbies.Length);
            LobbyInfo chosen = Find(lobbies, selectedId);
            if (joinButton != null)
                joinButton.Interactable = chosen != null && chosen.IsJoinable;
        }

        // Where the list comes from (Network settings: lobby discovery, connection server address).
        private void UpdateSource(int shown)
        {
            LobbyServerStatus server = Network.LobbyServerState;
            bool fromServer = Network.LobbyListFromServer;
            string where = Network.LobbyServerAddress;
            int total = Network.LobbyListTotal;
            int maxAttempts = Network.LobbyServerMaxAttempts;
            bool notAccessible = fromServer && server == LobbyServerStatus.Unreachable;
            bool offerLan = notAccessible && Network.CanUseLanDiscovery;
            if (lanEntity != null)
                lanEntity.SetActive(offerLan);

            string browseError = Network.LastBrowseError;
            if (browseError.Length > 0)
            {
                // StartBrowsing failed: nothing is being searched, so say why instead of "Searching...".
                if (header != null)
                    header.Text = "Cannot list lobbies";
                if (emptyText != null)
                    emptyText.Text = browseError + " (Network window > Lobby discovery)";
                return;
            }

            // Auto with a server: the server only, until the player picks Search LAN.
            string head;
            if (fromServer && server == LobbyServerStatus.Online)
                head = Network.LobbyListTruncated || total > Rows ? "Lobbies - showing " + Math.Min(shown, Rows) + " of " + total : "Lobbies - connection server";
            else if (notAccessible)
                head = "Connection server not accessible";
            else if (fromServer)
                head = "Connecting to the connection server...";
            else if (Network.IsUsingLanDiscovery)
                head = shown > Rows ? "LAN lobbies - showing " + Rows + " of " + shown : "Lobbies on your network (LAN)";
            else
                head = shown > Rows ? "Lobbies - showing " + Rows + " of " + shown : "Lobbies on your network";
            if (header != null)
                header.Text = head;

            if (emptyText == null)
                return;
            if (shown > 0)
                emptyText.Text = "";
            else if (offerLan)
                emptyText.Text = "No answer from " + where + " after " + maxAttempts + " attempts. Search your local network (LAN) instead?";
            else if (notAccessible)
                emptyText.Text = "No answer from the connection server at " + where + " after " + maxAttempts + " attempts.";
            else if (fromServer && server != LobbyServerStatus.Online)
                emptyText.Text = "Connecting to " + where + " (attempt " + Math.Min(Network.LobbyServerFailedAttempts + 1, maxAttempts) + " of " + maxAttempts + ")...";
            else if (fromServer)
                emptyText.Text = "No lobbies yet (connection server " + where + ")...";
            else
                emptyText.Text = "Searching for lobbies on your network...";
        }

        // The host: whether browsers can find this lobby through the connection server.
        private static string HostListing()
        {
            if (!Network.IsHost)
                return "";
            if (Network.LobbyListingProblem.Length > 0)
                return "   -   not in lobby lists";
            if (Network.LobbyDiscovery == LobbyDiscoveryMode.Lan)
                return "";
            switch (Network.LobbyServerState)
            {
                case LobbyServerStatus.Online:
                    return Network.IsLobbyRegistered ? "   -   listed on the connection server" : "";
                case LobbyServerStatus.Unreachable:
                    return Network.LobbyDiscovery == LobbyDiscoveryMode.Server ? "   -   connection server unreachable: not listed"
                                                                             : "   -   connection server unreachable: LAN only";
                default:
                    return "";
            }
        }

        private static string Pad(string text, int width)
        {
            return text.Length >= width ? text + "  " : text + new string(' ', width - text.Length);
        }

        private static LobbyInfo Find(LobbyInfo[] lobbies, string id)
        {
            if (id == null)
                return null;
            foreach (LobbyInfo lobby in lobbies)
                if (lobby.Id == id)
                    return lobby;
            return null;
        }

        private void JoinSelected()
        {
            // Asked again now, not from what the list showed: it may have closed or filled since.
            LobbyInfo lobby = Find(Network.Lobbies, selectedId);
            if (lobby == null)
            {
                selectedId = null;
                Say("That lobby is no longer available.");
                return;
            }
            if (!lobby.IsJoinable)
            {
                Say(lobby.HostName + "'s lobby cannot be joined: " + lobby.Availability.ToLowerInvariant() + ".");
                return;
            }
            if (!Network.Join(lobby, LocalName()))
            {
                Say("Could not join (see the log).");
                return;
            }
            joining = true;
            cancelled = false;
            sessionFromLobby = true;
            joiningName = lobby.HostName;
            Say("");
            Show(Screen.Busy);
        }

        // --- the room ------------------------------------------------------------------------ //

        private bool IsReady(int peer)
        {
            return peer >= 0 && peer < Network.MaxPlayers && Entity.NetVars.GetBool(ReadySlot + peer);
        }

        private void UpdateRoom()
        {
            int count = 0, ready = 0;
            string list = "";
            foreach (int peer in Network.Peers)
            {
                ++count;
                bool isReady = Network.IsPeerReady(peer) && IsReady(peer);
                if (isReady)
                    ++ready;
                list += (isReady ? "[READY]     " : "[  ...  ]     ") + Network.GetPeerName(peer) +
                        (peer == Network.HostPeerId ? "   (host)" : "") + (peer == Network.LocalPeerId ? "   (you)" : "") + "\n";
            }
            if (roomTitle != null)
                roomTitle.Text = Network.GetPeerName(Network.HostPeerId) + "'s lobby   -   " + count + " / " + Network.MaxPlayers + " players";
            if (playersText != null)
                playersText.Text = list;

            bool canStart = count >= minPlayers && ready == count && !starting;
            if (roomHint != null)
            {
                if (!Network.IsHost)
                    roomHint.Text = IsReady(Network.LocalPeerId) ? "Waiting for the host to start the match" : "Press Ready when you are";
                else if (count < minPlayers)
                    roomHint.Text = "Waiting for players (at least " + minPlayers + ")";
                else if (ready < count)
                    roomHint.Text = "Waiting for everyone to be ready (" + ready + " / " + count + ")";
                else
                    roomHint.Text = "Everyone is ready - start the match";
            }
            if (readyLabel != null)
                readyLabel.Text = IsReady(Network.LocalPeerId) ? "Not Ready" : "Ready";
            if (readyButton != null)
                readyButton.Interactable = Network.IsReady && !starting;
            if (startEntity != null)
                startEntity.SetActive(Network.IsHost);
            if (startButton != null)
                startButton.Interactable = canStart;
        }

        private void StartMatch()
        {
            if (!Network.IsHost || starting)
                return;
            // Closed first: nobody can join while everyone is loading the arena.
            Network.SetLobby(false, "In match");
            if (Network.LoadScene(arenaScene))
            {
                starting = true;
                Say("Starting the match...");
            }
            else
            {
                Network.SetLobby(true, "In lobby");
                Say("Could not start: the arena scene is missing.");
            }
        }

        // --- the buttons -------------------------------------------------------------------- //

        public override void OnUIAction(string actionId)
        {
            if (actionId.StartsWith("lobby.row.", StringComparison.Ordinal))
            {
                int row;
                if (int.TryParse(actionId.Substring("lobby.row.".Length), out row) && row >= 0 && row < Rows && rowIds[row] != null)
                {
                    selectedId = rowIds[row];
                    Say("");
                }
                return;
            }
            switch (actionId)
            {
                case "lobby.create":
                    if (Network.StartHost(port, LocalName()))
                    {
                        sessionFromLobby = true;
                        joining = false;
                        cancelled = false;
                        Say("");
                        Show(Screen.Busy);
                    }
                    else
                        Say("Could not create a lobby (see the log).");
                    break;
                case "lobby.browse":
                    Say("");
                    Show(Screen.Browser);
                    break;
                case "lobby.back":
                    Say("");
                    Show(Screen.Menu);
                    break;
                case "lobby.join":
                    JoinSelected();
                    break;
                case "lobby.lan":
                    // The player agreed: the connection server is not accessible, list LAN lobbies.
                    if (Network.UseLanDiscovery())
                        Say("Searching your local network instead of the connection server.");
                    break;
                case "lobby.cancel":
                    cancelled = true;
                    if (!Network.Leave())
                        Show(joining ? Screen.Browser : Screen.Menu);
                    break;
                case "lobby.ready":
                    Network.SendCommand(ReadyCommand, new NetWriter().WriteBool(!IsReady(Network.LocalPeerId)), Entity);
                    break;
                case "lobby.start":
                    StartMatch();
                    break;
                case "lobby.leave":
                    Network.Leave();
                    break;
            }
        }

        // --- the session ---------------------------------------------------------------------- //

        public override void OnNetworkStarted()
        {
            wasJoined = true;
            if (Network.IsHost)
                Network.SetLobby(true, "In lobby");
            string unlisted = Network.IsHost ? Network.LobbyListingProblem : "";
            if (!Network.IsHost)
                Say("Joined " + joiningName + "'s lobby.");
            else if (unlisted.Length > 0)
                Say("Your lobby is open, but lobby lists cannot see it. " + unlisted);
            else
                Say("Your lobby is open. Players on your network can see it.");
            Show(Screen.Room);
        }

        public override void OnNetworkStopped(string reason)
        {
            string message = cancelled ? "Cancelled." : Network.DescribeEnd(wasJoined);
            bool failedJoin = joining && !wasJoined;
            wasJoined = false;
            sessionFromLobby = false;
            starting = false;
            joining = false;
            cancelled = false;
            Show(failedJoin ? Screen.Browser : Screen.Menu);
            Say(message);
        }

        // The host keeps the ready list (every machine shows it from the network variables).
        public override void OnNetworkCommand(int sender, ushort kind, NetReader data)
        {
            bool ready = data.ReadBool();
            if (kind != ReadyCommand || !data.Ok || sender < 0 || sender >= Network.MaxPlayers || starting)
                return;
            Entity.NetVars.SetBool(ReadySlot + sender, ready);
        }

        public override void OnPeerReady(int peer)
        {
            if (Network.IsAuthority && peer >= 0 && peer < Network.MaxPlayers)
                Entity.NetVars.SetBool(ReadySlot + peer, false);
        }

        public override void OnPeerJoined(int peer)
        {
            if (screen == Screen.Room)
                Say(Network.GetPeerName(peer) + " joined.");
        }

        public override void OnPeerLeft(int peer)
        {
            if (Network.IsAuthority && peer >= 0 && peer < Network.MaxPlayers)
                Entity.NetVars.SetBool(ReadySlot + peer, false);
            if (screen == Screen.Room)
                Say("A player left.");
        }
    }
}
