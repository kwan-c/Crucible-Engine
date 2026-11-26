/*************************************************************************
* \file:         KitDemo.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Networking sample: the C# helpers - named messages, synced
*                fields, effects, authority helpers and the game state -
*                checked in two Editors.
*
* \details
*    On "Kit Demo" in network_sync_test.scene (a scene network object that
*    replicates NetVariables); the game state is "Kit State" (KitState.cs).
*    Every machine prints what it sees (README section 8.29 has the
*    checklist):
*
*        M  Host: a named event (every machine counts it, twice: once in a
*           handler, once in OnNetworkEvent); Client: a named command (the
*           Host counts it per sender)
*        F  Host: health - 10 (a NetInt), then a 3 s cooldown (a NetTimer);
*           Client: refused by Network.RequireAuthority
*        R  any machine: an effect - the text flashes on every machine once
*        G  Host: next phase        H  Host: a 30 s match timer
*        P  any machine: +1 score for whoever pressed it
*
* \history
*    -v1.0       sample for the networking helpers
*
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class KitDemo : NetScript
    {
        private static readonly NetMessage Ping = NetMessage.Named("kitdemo.ping");     // Host -> everyone
        private static readonly NetMessage Hello = NetMessage.Named("kitdemo.hello");   // Client -> Host

        private static int flashes;          // effects played on this machine
        private static int lastFlashFrom = -1;
        private static float flashUntil;

        // The text flashes yellow for a moment; the extra byte says who pressed R.
        private static readonly NetEffect Flash = NetEffect.Register("kitdemo.flash", (target, position, extra) =>
        {
            ++flashes;
            lastFlashFrom = extra.ReadByte();
            flashUntil = Time.ElapsedTime + 0.4f;
        });

        private readonly NetInt health = new NetInt(slot: 0, initial: 100);
        private readonly NetTimer cooldown = new NetTimer(slot: 1);

        private WorldText text;
        private Color baseColor;
        private int healthChanges, cooldownsOver;
        private int pingsByHandler, pingsByOverride, reservedHeard;
        private readonly Dictionary<int, int> hellos = new Dictionary<int, int>();

        public override void OnCreate()
        {
            text = Entity.GetComponent<WorldText>();
            if (text != null)
                baseColor = text.Color;
            health.Changed += (old, now) => ++healthChanges;
            NetMessage.OnEvent(Ping, (data, target) => ++pingsByHandler, this);
            NetMessage.OnCommand(Hello, (sender, data, target) =>
            {
                int n;
                hellos.TryGetValue(sender, out n);
                hellos[sender] = n + 1;
            }, this);
        }

        public override void OnNetworkEvent(ushort kind, NetReader data)
        {
            if (Ping.Is(kind)) ++pingsByOverride;
            if (kind >= NetRouter.FirstReservedKind) ++reservedHeard;   // never: the helpers keep their own
        }

        public override void OnNetworkCommand(int sender, ushort kind, NetReader data)
        {
            if (kind >= NetRouter.FirstReservedKind) ++reservedHeard;
        }

        public override void OnUpdate(float deltaTime)
        {
            NetGameState state = NetGameState.Current;
            if (Input.IsPressed(KeyCode.M))
            {
                if (Network.IsAuthority) Ping.SendEvent();
                else Hello.SendCommand();
            }
            if (Input.IsPressed(KeyCode.F) && Network.RequireAuthority("Kit Demo damage (F)") && !cooldown.IsRunning)
            {
                health.Value = health.Value <= 10 ? 100 : health.Value - 10;
                cooldown.Start(3.0);
            }
            if (Input.IsPressed(KeyCode.R))
                Flash.Play(Entity, Entity.Transform.Position, new NetWriter().WriteByte((byte)System.Math.Max(0, Network.LocalPeerId)));
            if (state != null && Input.IsPressed(KeyCode.G) && Network.RequireAuthority("next phase (G)"))
                state.Phase = state.Phase + 1;
            if (state != null && Input.IsPressed(KeyCode.H) && Network.RequireAuthority("match timer (H)"))
                state.MatchTimer.Start(30.0);
            if (Input.IsPressed(KeyCode.P))
                NetGameState.RequestScore(1);
            if (cooldown.JustFinished())
                ++cooldownsOver;

            if (text == null)
                return;
            text.Color = Time.ElapsedTime < flashUntil ? new Color(1f, 0.9f, 0.2f, 1f) : baseColor;
            text.Text = Describe(state);
        }

        private string Describe(NetGameState state)
        {
            string role = !Network.IsOnline ? "offline" : Network.IsHost ? "HOST (M F R G H P)" : "CLIENT " + Network.LocalPeerId + " (M R P)";
            string helloLine = string.Empty;
            foreach (KeyValuePair<int, int> pair in hellos)
                helloLine += "  " + pair.Key + ": " + pair.Value;

            string players = string.Empty;
            foreach (int peer in Network.IsOnline ? Network.Peers : new int[] { 0 })
            {
                Entity player = Network.IsOnline ? Network.GetPlayerEntity(peer) : null;
                players += "  " + peer + (player == null ? ": -" : Network.IsLocalPlayer(player) ? ": mine" : ": other");
            }

            string scores = string.Empty;
            if (state != null)
                foreach (int peer in Network.IsOnline ? Network.Peers : new int[] { 0 })
                    scores += "  " + peer + ": " + state.GetScore(peer);

            return role
                + "\nmessage " + Ping.Name + ": handler " + pingsByHandler + ", override " + pingsByOverride
                + (Network.IsAuthority ? "   hello from" + (helloLine.Length > 0 ? helloLine : " -") : "")
                + "\nhealth " + health.Value + " (changed " + healthChanges + ")   cooldown " + cooldown + " s (over " + cooldownsOver + ")"
                + "\neffects played here " + flashes + (lastFlashFrom >= 0 ? " (last from " + lastFlashFrom + ")" : "")
                + "   reserved heard by scripts " + reservedHeard
                + "\nplayers" + players
                + (state == null ? "\nno game state"
                    : "\nphase " + state.Phase + "   timer " + state.MatchTimer + " s   scores" + scores)
                + (Network.LastAuthorityRefusal.Length > 0 ? "\nrefused here: " + Network.LastAuthorityRefusal : "");
        }
    }
}
