/*************************************************************************
* \file:         NetPlayerBoard.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Networked gameplay test: the plaza's player board - every
*                player's name, ping, health, parts and deaths, the same on
*                every machine.
*
* \details
*    On "Player Board" in net_gameplay_test.scene; writes its "Text" child
*    (a WorldText) twice a second. Everything it shows is already synced:
*    names and pings come from the session (Network.GetPeerName / GetPing),
*    health, parts and deaths from each NetMechPlayer (the host's values).
*    Offline it lists this machine's mech.
*
* \history
*    -v1.0       players, ping, health, parts, deaths
*
**************************************************************************/
using System;
using System.Text;

namespace Crucible.Scripts.NetGameplay
{
    public sealed class NetPlayerBoard : EntityScript
    {
        public float refreshSeconds = 0.5f;

        WorldText text;
        float nextRefresh;

        public override void OnCreate()
        {
            Entity child = Entity.FindChild("Text");
            text = child != null ? child.GetComponent<WorldText>() : null;
        }

        public override void OnUpdate(float deltaTime)
        {
            if (text == null || Time.ElapsedTime < nextRefresh) return;
            nextRefresh = Time.ElapsedTime + refreshSeconds;
            var board = new StringBuilder("PLAYERS\n");
            int listed = 0;
            for (int i = 0; i < NetMechPlayer.All.Count; i++)
            {
                NetMechPlayer player = NetMechPlayer.All[i];
                if (player == null || !player.Entity.IsAlive || player.Health == null) continue;
                int peer = player.Entity.Owner;
                string name = Network.IsOnline ? Network.GetPeerName(peer) : "You";
                if (string.IsNullOrEmpty(name)) name = "Player " + peer;
                if (player.Entity.IsMine) name += " (you)";
                int ping = Network.IsOnline ? Network.GetPing(peer) : 0;
                string health = player.Health.IsDead ? "DESTROYED"
                              : (int)Math.Ceiling(player.Health.Current) + " / " + (int)Math.Ceiling(player.Health.Maximum);
                board.Append(name).Append("\n   ")
                     .Append(ping >= 0 ? ping + " ms" : "- ms").Append("   HP ").Append(health)
                     .Append("   parts ").Append(player.MountedPartCount).Append("/5")
                     .Append("   deaths ").Append(player.Deaths).Append("\n");
                listed++;
            }
            if (listed == 0) board.Append("(nobody yet)");
            string value = board.ToString();
            if (text.Text != value) text.Text = value;
        }
    }
}
