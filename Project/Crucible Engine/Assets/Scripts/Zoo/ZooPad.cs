/*************************************************************************
* \file:         ZooPad.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \date          8 November 2026
* \version       1.0
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
namespace Crucible.Scripts
{
    public sealed class ZooPad
    {
        readonly EntityScript owner;
        WorldText label;

        /// <summary>The player is standing on the pad.</summary>
        public bool Activated { get; private set; }

        public ZooPad(EntityScript owner) { this.owner = owner; }

        public void SetText(string text)
        {
            if (label == null)
            {
                Entity e = owner.Entity.FindChild("Label");
                label = e != null ? e.GetComponent<WorldText>() : null;
            }
            if (label != null) label.Text = text;
        }

        /// <summary>F pressed this frame while the player stands on the pad (and has the mouse).</summary>
        public bool WasUsed(KeyCode key)
        {
            return Activated && Input.IsMouseLocked && Input.IsPressed(key);
        }

        public void Enter(Entity other) { if (IsPlayer(other)) Activated = true; }
        public void Exit(Entity other) { if (IsPlayer(other)) Activated = false; }

        static bool IsPlayer(Entity other)
        {
            return other != null && other.IsAlive && other.GetComponent<MechPlayer>() != null;
        }
    }
}
