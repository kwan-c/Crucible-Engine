/*************************************************************************
* \file:         NetBolt.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          17 November 2026
* \version       1.1
* \brief         Networking sample: a projectile of the arena shooter. It
*                flies, hits and removes itself on the host only; every
*                other machine just sees it move.
*
* \details
*    On the NetBolt prefab this script is "Host only" in the Network Object
*    table and the Transform is Replicated: the host runs the flight and
*    the hit test, and every client is shown the result. Nothing a client
*    could change decides whether a shot hits.
*
*    The hit test is a raycast along each frame's step, on the host, where
*    the players' capsules are (CharacterController is Host only). The
*    shooter's own character is looked through unless the rules allow self
*    damage. A hit on a character is passed to ArenaRules.ApplyHit, which
*    decides what it does; anything solid stops the bolt.
*
*    Entity.Owner is the player who fired (ArenaRules spawns the bolt with
*    that owner): that is whose hit it is.
*
* \history
*    -v1.0       Phase 6 sample: hits NetTarget
*    -v1.1       arena shooter: hits players through ArenaRules
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class NetBolt : EntityScript
    {
        /// <summary>Metres per second.</summary>
        public float speed = 40f;
        /// <summary>Health taken from a player it hits.</summary>
        public int damage = 25;
        /// <summary>Seconds before a bolt that hit nothing is removed.</summary>
        public float lifetime = 2f;

        private float age;
        private bool spent;
        private ArenaRules rules;

        public override void OnCreate()
        {
            rules = ArenaRules.Find();
        }

        public override void OnUpdate(float deltaTime)
        {
            // "Host only" already keeps this off on clients; the check also covers a prefab
            // that was set up wrongly.
            if (!Network.IsAuthority || spent)
                return;

            age += deltaTime;
            if (age >= lifetime)
            {
                Remove();
                return;
            }

            Vector3 from = Transform.WorldPosition;
            Vector3 direction = Transform.Forward;
            float step = speed * deltaTime;
            Entity shooter = Network.GetPlayerEntity(Entity.Owner);
            Entity ignore = rules != null && rules.allowSelfDamage ? null : shooter;
            RaycastHit hit;
            if (Physics.Raycast(from, direction, out hit, step, ignore))
            {
                int victim = ArenaRules.PeerOfPlayer(hit.Entity);
                if (victim >= 0 && rules != null)
                    rules.ApplyHit(victim, Entity.Owner, damage, hit.Point);
                Remove();   // a wall, the ground or a player: the bolt is spent
                return;
            }
            Transform.WorldPosition = from + direction * step;
        }

        private void Remove()
        {
            spent = true;
            Network.Despawn(Entity);   // gone on every machine at the end of this frame
        }
    }
}
