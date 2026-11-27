/*************************************************************************
* \file:         NetMatchState.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Networked gameplay test: the match's state - waiting for
*                the players, a countdown in which nobody can move, then
*                play. A NetGameState, so every machine sees the same phase
*                and the same countdown.
*
* \details
*    On "Match State" in net_gameplay_test.scene (a scene network object
*    that replicates NetVariables). The host decides:
*
*        Waiting    until every player in the session has the level (the
*                   lobby's scene change: OnAllPeersReady; a session started
*                   in this scene: every player ready). Offline: at once.
*        Countdown  countdownSeconds on the shared clock (MatchTimer); the
*                   mechs stand at their spawn points and ignore movement
*        Playing    everyone may move (NetMechPlayer asks MovementAllowed)
*
*    Every machine writes the banner text (the "Match Banner" WorldText
*    above the plaza) from the phase and the timer.
*
* \history
*    -v1.0       waiting, a 10 second countdown, playing; a destroyed local
*                player's respawn countdown
*
**************************************************************************/
namespace Crucible.Scripts.NetGameplay
{
    public sealed class NetMatchState : NetGameState
    {
        public const int Waiting = 0, Countdown = 1, Playing = 2;

        /// <summary>Seconds the players wait at their spawn points before they may move.</summary>
        public float countdownSeconds = 10f;
        /// <summary>The WorldText that shows the phase (found by name).</summary>
        public string bannerName = "Match Banner";

        private WorldText banner;
        private float waitingSince = -1f;

        /// <summary>The match state of the scene, or null (no such object: then everyone may move).</summary>
        public static NetMatchState Match { get { return Current as NetMatchState; } }

        /// <summary>May the mechs move now? True when there is no match state at all.</summary>
        public static bool MovementAllowed
        {
            get { NetMatchState m = Match; return m == null || m.Phase == Playing; }
        }

        public override void OnCreate()
        {
            Entity found = Entity.Find(bannerName);
            banner = found != null ? found.GetComponent<WorldText>() : null;
        }

        // The lobby's scene change: every client has the level and has drawn it.
        public override void OnAllPeersReady()
        {
            if (Network.IsAuthority && Phase == Waiting) StartCountdown();
        }

        public override void OnUpdate(float deltaTime)
        {
            if (Network.IsAuthority) Decide();
            if (banner != null) banner.Text = BannerText();
        }

        private void Decide()
        {
            if (Phase == Waiting)
            {
                if (waitingSince < 0f) waitingSince = Time.ElapsedTime;
                // Offline, or a session started in this scene: everyone in it is ready (a short wait first,
                // so the players have spawned).
                bool everyoneReady = true;
                foreach (int peer in Network.IsOnline ? Network.Peers : new int[0])
                    if (!Network.IsPeerReady(peer)) everyoneReady = false;
                if (everyoneReady && Time.ElapsedTime - waitingSince > 1f) StartCountdown();
            }
            else if (Phase == Countdown && MatchTimer.JustFinished())
            {
                Phase = Playing;
            }
        }

        private void StartCountdown()
        {
            Phase = Countdown;
            MatchTimer.Start(countdownSeconds);
        }

        private string BannerText()
        {
            NetMechPlayer local = NetMechPlayer.Local;
            if (local != null && local.Health != null && local.Health.IsDead)
                return "Destroyed - back in " + System.Math.Ceiling(local.RespawnRemaining).ToString("0");
            switch (Phase)
            {
                case Waiting:   return "Waiting for players...";
                case Countdown: return "Get ready  " + System.Math.Ceiling(MatchTimer.Remaining).ToString("0");
                default:        return MatchTimer.Remaining <= 0.0 && Network.Time - MatchTimer.EndTick / (double)Network.TickRate < 2.0 ? "GO!" : "";
            }
        }
    }
}
