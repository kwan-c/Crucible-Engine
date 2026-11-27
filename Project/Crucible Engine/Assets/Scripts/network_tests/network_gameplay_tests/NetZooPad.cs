/*************************************************************************
* \file:         NetZooPad.cs
* \author:       Tok Duan Jie Nelson (ZooPad, the original);
*                Abhishek Akilandeswari Patchamuthu (this networked copy)
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Networked gameplay test: a copy of ZooPad, the shared "step
*                on me and press F" part of the zoo's floor pads, for any
*                number of players.
*
* \details
*    The host decides: its triggers know which mechs stand on the pad, and
*    a pad is used when one of them sent F (NetMechPlayer.NetActions.Use) -
*    whichever machine that player is on. Clients only show the label.
*    ZooPad itself is unchanged.
*
* \history
*    -v1.0       copy of ZooPad: host triggers, any player's F (an action)
*
* The original's notes follow.
*
* \brief         Shared "step on me and press F" behaviour of the Zoo floor
*                pads (Unity's ZooSoundplayer / ZooToggle / ZooBacktoMain all
*                repeat it).
*
* \details
*    A plain helper object, not a script: each pad script owns one. The pad
*    entity needs a Collider with Is Trigger ticked; the player is recognised
*    by its MechPlayer script (Unity checked for a CharacterController).
*
*        Sound Pad        Collider (trigger), Script: ZooSoundPlayer
*        '- Label         WorldText ("Step on me and press F ...")
*
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.Scripts.NetGameplay
{
    public sealed class NetZooPad
    {
        readonly EntityScript owner;
        readonly List<NetMechPlayer> onPad = new List<NetMechPlayer>();
        WorldText label;

        /// <summary>Somebody is standing on the pad (the host knows).</summary>
        public bool Activated { get { return onPad.Count > 0; } }

        public NetZooPad(EntityScript owner) { this.owner = owner; }

        public void SetText(string text)
        {
            if (label == null)
            {
                Entity e = owner.Entity.FindChild("Label");
                label = e != null ? e.GetComponent<WorldText>() : null;
            }
            if (label != null) label.Text = text;
        }

        /// <summary>The host: a player on the pad pressed F this frame (their input, from any machine).</summary>
        public bool WasUsed()
        {
            if (!Network.IsAuthority) return false;
            for (int i = onPad.Count - 1; i >= 0; i--)
            {
                NetMechPlayer player = onPad[i];
                if (player == null || !player.Entity.IsAlive) { onPad.RemoveAt(i); continue; }
                NetInput input = PlayerActions.Of(player.Entity);
                if (input.IsFresh && input.WasPressed(NetMechPlayer.NetActions.Use)) return true;
            }
            return false;
        }

        public void Enter(Entity other)
        {
            NetMechPlayer player = PlayerOf(other);
            if (player != null && !onPad.Contains(player)) onPad.Add(player);
        }

        public void Exit(Entity other)
        {
            NetMechPlayer player = PlayerOf(other);
            if (player != null) onPad.Remove(player);
        }

        static NetMechPlayer PlayerOf(Entity other)
        {
            if (!Network.IsAuthority || other == null || !other.IsAlive) return null;
            return other.GetComponent<NetMechPlayer>();
        }
    }
}
