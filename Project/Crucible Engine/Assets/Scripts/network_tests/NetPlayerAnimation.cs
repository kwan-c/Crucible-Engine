/*************************************************************************
* \file:         NetPlayerAnimation.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief         Networking sample: the networked player's mech legs walk,
*                stand and jump, on every machine.
*
* \details
*    On the NetworkedPlayer prefab, whose "Mech Visual" child is the game's
*    mech (legs with the Mech Legs controller, torso, head, sword). The
*    host moves every player (its CharacterController is Host only), so the
*    host reads each one's speed and whether it stands on the ground, and
*    sets the legs' Integer "Pose" and Float "Speed" the way MechPlayer does
*    in the single-player game:
*
*      not on the ground    Pose 4 (Jump)
*      moving (> 0.1 m/s)   Pose 1 (Locomotion); Speed picks the gait
*      otherwise            Pose 0 (Neutral)
*
*    The prefab replicates Animator (Networking C# Phase 7), so every
*    client's copy of each player plays the same leg animation. On a client
*    this script does nothing: the host owns those parameters.
*
* \history
*    -v1.0       Networking C# Phase 7: the mech player in the arena
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class NetPlayerAnimation : EntityScript
    {
        /// <summary>The child with the legs' AnimatedModel (anywhere below the player).</summary>
        public string legsEntityName = "Legs";

        // The Mech Legs controller's Integer "Pose" values (MechPlayer.LegPose).
        private const int PoseNeutral = 0, PoseLocomotion = 1, PoseJump = 4;

        private Entity legs;
        private bool warned;

        public override void OnCreate()
        {
            legs = FindBelow(Entity, legsEntityName);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (!Network.IsAuthority || legs == null || !legs.IsActive)
                return;   // a dead player's mech is hidden (ArenaView): nothing to animate

            // A dead player's controller is switched off (ArenaRules): it is hidden then, so it stands.
            bool moving = Entity.IsEnabled<CharacterController>();
            CharacterController controller = Entity.CharacterController;
            Vector3 velocity = moving ? controller.Velocity : Vector3.Zero;
            velocity.Y = 0f;
            float speed = velocity.Length;
            int pose = !moving ? PoseNeutral
                     : !controller.IsGrounded ? PoseJump
                     : speed > 0.1f ? PoseLocomotion : PoseNeutral;

            bool ok = legs.Animator.SetInteger("Pose", pose);
            ok = legs.Animator.SetFloat("Speed", speed) && ok;
            if (!ok && !warned)
                Log.Warning("NetPlayerAnimation: '" + legsEntityName + "' needs an AnimatedModel controller with an Integer 'Pose' and a Float 'Speed' (Mech Legs).");
            warned = !ok;
        }

        // Breadth first through the hierarchy: FindChild looks one level down only.
        private static Entity FindBelow(Entity root, string name)
        {
            if (root == null || string.IsNullOrEmpty(name))
                return null;
            System.Collections.Generic.Queue<Entity> open = new System.Collections.Generic.Queue<Entity>();
            open.Enqueue(root);
            while (open.Count > 0)
            {
                Entity e = open.Dequeue();
                for (int i = 0; i < e.Transform.ChildCount; i++)
                {
                    Entity child = e.Transform.GetChild(i);
                    if (child == null) continue;
                    if (child.Name == name) return child;
                    open.Enqueue(child);
                }
            }
            return null;
        }
    }
}
