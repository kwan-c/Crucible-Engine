/*************************************************************************
* \file:         NetGameState.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         The match-wide state every game mode keeps the same way: phase,
*                wave, a match timer and each player's score.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Make one small script from it and put it on one scene object that
*    replicates NetVariables (its Network Object table):
*
*        public sealed class MatchState : NetGameState { }
*
*        // the Host (or single player):
*        NetGameState.Current.Phase = Phases.Fighting;
*        NetGameState.Current.MatchTimer.Start(180);
*        NetGameState.Current.AddScore(killer.Owner, 1);
*
*        // every machine:
*        NetGameState.Current.PhaseChanged += (old, now) => ShowBanner(now);
*        hud.Text = NetGameState.Current.MatchTimer.Remaining.ToString("0");
*
*        // a Client asks for points (the Host decides: AllowScoreRequest):
*        NetGameState.RequestScore(1);
*
*    Slots: 0 phase, 1 wave, 2 the match timer, 4 to 7 the scores of
*    players 0 to 3; 32 to 63 are free for the game's own NetInt / NetFloat
*    fields (FirstGameSlot). The meaning of a phase number is the game's.
*
* \history
*    -v1.0       Current, Phase, Wave, MatchTimer, scores, RequestScore
*
**************************************************************************/
using System;

namespace Crucible
{
    /// <summary>
    /// The base of a game's one match-wide state object (see NetGameState.cs). Phase, wave and
    /// scores are set by the authority (the Host, or single player) and read everywhere.
    /// </summary>
    public abstract class NetGameState : NetScript
    {
        /// <summary>The first slot free for the game's own fields.</summary>
        public const int FirstGameSlot = 32;

        private const int ScoreSlot = 4;
        private static NetGameState current;

        private readonly NetInt phase = new NetInt(0);
        private readonly NetInt wave = new NetInt(1);
        private readonly NetTimer matchTimer = new NetTimer(2);
        private readonly int[] scores = new int[Network.MaxPlayers];

        /// <summary>The game state of the running scene, or null.</summary>
        public static NetGameState Current
        {
            get
            {
                return !ReferenceEquals(current, null) && !ReferenceEquals(current.Entity, null) && current.Entity.IsAlive
                    ? current : null;
            }
        }

        /// <summary>The match's phase (the game decides what each number means).</summary>
        public int Phase { get { return phase.Value; } set { phase.Value = value; } }

        /// <summary>The wave number.</summary>
        public int Wave { get { return wave.Value; } set { wave.Value = value; } }

        /// <summary>A timer for the match or the current phase (shared clock).</summary>
        public NetTimer MatchTimer { get { return matchTimer; } }

        /// <summary>Every machine: (old, new).</summary>
        public event Action<int, int> PhaseChanged { add { phase.Changed += value; } remove { phase.Changed -= value; } }
        public event Action<int, int> WaveChanged { add { wave.Changed += value; } remove { wave.Changed -= value; } }

        /// <summary>Every machine: (peer, old score, new score).</summary>
        public event Action<int, int, int> ScoreChanged;

        /// <summary>A player's score (0 for an unknown player).</summary>
        public int GetScore(int peer)
        {
            return peer >= 0 && peer < Network.MaxPlayers ? Entity.NetVars.GetInt(ScoreSlot + peer) : 0;
        }

        /// <summary>The authority: sets a player's score.</summary>
        public bool SetScore(int peer, int value)
        {
            if (peer < 0 || peer >= Network.MaxPlayers)
            {
                Log.Error("NetGameState.SetScore: " + peer + " is not a player");
                return false;
            }
            return Entity.NetVars.SetInt(ScoreSlot + peer, value);
        }

        /// <summary>The authority: adds to a player's score.</summary>
        public bool AddScore(int peer, int amount)
        {
            return SetScore(peer, GetScore(peer) + amount);
        }

        /// <summary>
        /// Any machine: points for this machine's player. The authority adds them now; a Client
        /// asks the Host, which adds them if AllowScoreRequest says yes.
        /// </summary>
        public static bool RequestScore(int amount)
        {
            NetGameState state = Current;
            if (state == null)
            {
                Log.Error("NetGameState.RequestScore: the scene has no game state object");
                return false;
            }
            if (Network.IsAuthority)
                return state.AddScore(Math.Max(0, Network.LocalPeerId), amount);
            return Network.SendHelperCommand(NetRouter.ScoreRequest, new NetWriter().WriteInt(amount));
        }

        /// <summary>The Host: may <paramref name="peer"/> have <paramref name="amount"/> points? Yes unless overridden.</summary>
        protected virtual bool AllowScoreRequest(int peer, int amount) { return true; }

        internal static void HandleScoreRequest(int sender, NetReader data)
        {
            int amount = data.ReadInt();
            NetGameState state = Current;
            if (!data.Ok || state == null || !Network.IsAuthority)
                return;
            if (state.AllowScoreRequest(sender, amount))
                state.AddScore(sender, amount);
        }

        internal override void OnEntityBound()
        {
            base.OnEntityBound();
            if (Current != null && !ReferenceEquals(Current, this))
                Log.Warning("NetGameState: the scene has two game state objects - " + GetType().Name + " is used from now on");
            current = this;
            for (int i = 0; i < scores.Length; ++i)
                scores[i] = GetScore(i);
        }

        /// <summary>An override must call base.OnNetVarChanged(slot).</summary>
        public override void OnNetVarChanged(int slot)
        {
            base.OnNetVarChanged(slot);
            int peer = slot - ScoreSlot;
            if (peer < 0 || peer >= scores.Length)
                return;
            int now = GetScore(peer);
            if (now == scores[peer])
                return;
            int old = scores[peer];
            scores[peer] = now;
            if (ScoreChanged != null)
                ScoreChanged(peer, old, now);
        }
    }
}
