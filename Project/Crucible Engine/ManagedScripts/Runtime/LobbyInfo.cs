/*************************************************************************
* \file:         LobbyInfo.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          21 November 2026
* \version       1.0
* \brief         One session found on the local network (Network.Lobbies),
*                and the numbers a session can end with (EndCode).
*                Part of Crucible.ScriptCore.dll.
*
* \details
*        Network.StartBrowsing();
*        foreach (LobbyInfo lobby in Network.Lobbies)
*            Log.Info(lobby.HostName + "  " + lobby.Players + "/" + lobby.MaxPlayers + "  " + lobby.Availability);
*        ...
*        Network.Join(chosen);     // refused if it is not joinable any more
*
*    What a host says about itself is only shown, never trusted: the host
*    refuses a join itself when it is full or closed.
*
*    C++ side: Engine/src/Network/LanDiscovery.h.
*
* \history
*    -v1.0       LobbyInfo, EndCode (Networking C# lobby browser)
*
**************************************************************************/
namespace Crucible
{
    /// <summary>A session hosted on the local network, as its host last described it.</summary>
    public sealed class LobbyInfo
    {
        /// <summary>Seconds without an answer after which an entry is treated as gone.</summary>
        public const float StaleAfter = 2.5f;

        /// <summary>Tells two sessions apart (random per session; stays the same while it runs).</summary>
        public string Id { get; private set; }
        /// <summary>The host's IP address ("127.0.0.1" if it runs on this PC).</summary>
        public string Address { get; private set; }
        /// <summary>The port to join it on.</summary>
        public int Port { get; private set; }
        /// <summary>The host's player name.</summary>
        public string HostName { get; private set; }
        /// <summary>What the host's game says it is doing ("In lobby", "In match"), or "".</summary>
        public string Status { get; private set; }
        public int Players { get; private set; }
        public int MaxPlayers { get; private set; }
        /// <summary>The host is taking players (Network.SetLobby).</summary>
        public bool IsOpen { get; private set; }
        /// <summary>The host runs a version of the game this one can join.</summary>
        public bool IsCompatible { get; private set; }
        /// <summary>Seconds since the host last answered.</summary>
        public float Age { get; private set; }

        public bool IsFull { get { return MaxPlayers > 0 && Players >= MaxPlayers; } }
        /// <summary>Heard from too long ago: it may have closed.</summary>
        public bool IsStale { get { return Age > StaleAfter; } }
        /// <summary>Worth trying to join now.</summary>
        public bool IsJoinable { get { return IsOpen && IsCompatible && !IsFull && !IsStale; } }

        /// <summary>Why it can or cannot be joined, for a list: "Open", "Full", "In progress"...</summary>
        public string Availability
        {
            get
            {
                if (!IsCompatible) return "Different version";
                if (IsStale) return "Not responding";
                if (!IsOpen) return "In progress";
                if (IsFull) return "Full";
                return "Open";
            }
        }

        internal LobbyInfo(string id, string address, int port, string hostName, string status, int players, int maxPlayers,
                           bool open, bool compatible, float age)
        {
            Id = id;
            Address = address;
            Port = port;
            HostName = hostName;
            Status = status;
            Players = players;
            MaxPlayers = maxPlayers;
            IsOpen = open;
            IsCompatible = compatible;
            Age = age;
        }

        public override string ToString()
        {
            return HostName + "'s lobby (" + Players + "/" + MaxPlayers + ", " + Availability + ") at " + Address + ":" + Port;
        }
    }

    /// <summary>Why a session ended (Network.LastEndCode). Network.DescribeEnd turns it into a sentence.</summary>
    public static class EndCode
    {
        /// <summary>This machine ended it (Leave, Play stopped), or a start failed.</summary>
        public const int Local = 0;
        public const int Generic = 1000;
        /// <summary>The host runs another version of the game.</summary>
        public const int ProtocolMismatch = 1001;
        /// <summary>No free player slot.</summary>
        public const int ServerFull = 1002;
        public const int Malformed = 1003;
        /// <summary>The host ended the session (it left, stopped, or closed the game).</summary>
        public const int SessionEnded = 1004;
        public const int SendFailed = 1006;
        /// <summary>A step of joining or loading took too long.</summary>
        public const int Timeout = 1007;
        /// <summary>This machine's scene or content is not the host's.</summary>
        public const int SceneMismatch = 1008;
        /// <summary>The host is not taking players now (the game has started).</summary>
        public const int LobbyClosed = 1009;
        /// <summary>The host stopped answering (crashed, or the network went away).</summary>
        public const int ConnectionLostRemote = 4001;
        public const int ConnectionLostMisc = 5003;
    }
}
