/*************************************************************************
* \file:         ZooPad.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \date          8 November 2026
* \version       1.0
* \brief         Shared "step on me and press G" behaviour of the Zoo floor
*                pads (Unity's ZooSoundplayer / ZooToggle / ZooBacktoMain all
*                repeat it).
*
* \details
*    A plain helper object, not a script: each pad script owns one. The pad
*    entity needs a Collider with Is Trigger ticked; the player is recognised
*    by its MechPlayer script (Unity checked for a CharacterController).
*
*        Sound Pad        Collider (trigger), Script: ZooSoundPlayer
*        '- Label         WorldText ("Step on me and press G ...")
*
**************************************************************************/
namespace Crucible.Scripts
{
    public sealed class ZooPad
    {
        readonly EntityScript owner;
        readonly KeyCode key;
        WorldText label;

        /// <summary>The player is standing on the pad.</summary>
        public bool Activated { get; private set; }

        /// <summary>
        /// key: the pad's interact key, G by default (F swaps parts, Z cycles the aim / lock-on mode). While the
        /// player stands on the pad it claims that key
        /// (MechInputClaims), so no other system acts on it while the pad is in use.
        /// </summary>
        public ZooPad(EntityScript owner, KeyCode key = KeyCode.G) { this.owner = owner; this.key = key; }

        public void SetText(string text)
        {
            if (label == null)
            {
                Entity e = owner.Entity.FindChild("Label");
                label = e != null ? e.GetComponent<WorldText>() : null;
            }
            if (label != null) label.Text = text;
        }

        /// <summary>The key pressed this frame while the player stands on the pad (and has the mouse).</summary>
        public bool WasUsed(KeyCode key)
        {
            return Activated && Input.IsMouseLocked && Input.IsPressed(key);
        }

        public void Enter(Entity other)
        {
            if (!IsPlayer(other)) return;
            Activated = true;
            MechInputClaims.Claim(owner, key);
        }

        public void Exit(Entity other)
        {
            if (!IsPlayer(other)) return;
            Activated = false;
            MechInputClaims.Release(owner, key);
        }

        static bool IsPlayer(Entity other)
        {
            return other != null && other.IsAlive && other.GetComponent<MechPlayer>() != null;
        }
    }
}
