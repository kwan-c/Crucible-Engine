/*************************************************************************
* \file:         NetZooEnemy.cs
* \author:       Tok Duan Jie Nelson (ZooEnemy, the original);
*                Abhishek Akilandeswari Patchamuthu (this networked copy)
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Networked gameplay test: a copy of ZooEnemy that goes for
*                whichever player is nearest (within chaseRadius), run by
*                the host.
*
* \details
*    On the Net Zoo Small / Elite Enemy prefabs (network objects: Transform
*    and Animator replicated; RigidBody, Collider and FlowFieldAgent host
*    only). Changes from ZooEnemy (unchanged):
*      - the host runs it; on a client it only exists to be drawn (its
*        Moving / Fast / Hit animator values come from the host)
*      - the target is the nearest living NetMechPlayer within chaseRadius,
*        chosen again every retargetSeconds; none in range: it stands
*      - its hits go through the player's health (NetMechPlayer.ApplyDamage)
*      - it is removed with Destroy on the host, which removes it everywhere
*
* \history
*    -v1.0       copy of ZooEnemy: host only, nearest player in a radius
*
* The original's notes follow.
*
* \brief         Placeholder enemy for the Zoo: chases the player on the
*                flow field, swings at it in reach, takes the player's hits
*                and dies. Stands in for Unity's Behavior Graph enemies until
*                that AI is ported.
*
* \details
*    Prefab setup (Assets/Prefabs/Zoo/Zoo Small Enemy.cprefab):
*
*        Zoo Small Enemy   RigidBody (kinematic), Collider (capsule, so the
*                          player's weapons can hit it), FlowFieldAgent
*                          (Start With: Nothing), Script: ZooEnemy
*        '- Visual         MeshRenderer ENEMY_Fodder + AnimatedModel with the
*                          Zoo Fodder controller (Moving / Fast bools, Hit trigger)
*
*    The scene needs a FlowFieldSurface (the ground) for the agent to walk.
*    Health and costs follow Unity's prefabs (Enemy_Fodder 80-100 hp, the
*    elites 200-400); the attack is a simple timed hit, not Unity's.
*
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.Scripts.NetGameplay
{
    public sealed class NetZooEnemy : EntityScript
    {
        public float maximumHealth = 100f;
        public float attackRange = 3f;      // metres from the player
        public float attackDamage = 10f;
        public float attackInterval = 1.5f;
        public bool fast = false;           // jog instead of walk (elites)
        public float chaseRadius = 30f;     // goes for the nearest player this close
        public float retargetSeconds = 0.5f;

        static readonly List<NetZooEnemy> alive = new List<NetZooEnemy>();

        /// <summary>How many zoo enemies the host has right now (the wave's population cap).</summary>
        public static int AliveCount { get { return alive.Count; } }

        public float Health { get; private set; }

        FlowFieldAgent agent;
        Animator animator;
        NetMechPlayer target;
        float nextAttack, nextRetarget;
        bool dead, host;

        public override void OnCreate()
        {
            host = Network.IsAuthority;
            if (!host) return;   // a client only draws it
            alive.Add(this);
            Health = maximumHealth;
            agent = GetComponent<FlowFieldAgent>();
            Entity visual = Entity.FindChild("Visual");
            animator = visual != null ? visual.Animator : null;
            if (animator != null) animator.SetBool("Fast", fast);
            Collider body = GetComponent<Collider>();
            float aimHeight = body != null ? body.Center.Y * Transform.Scale.Y : 1f;
            MechCombatTargets.Register(Entity, (amount, from) => TakeDamage(amount), aimHeight);
        }

        public override void OnDestroy()
        {
            if (!host) return;
            alive.Remove(this);
            MechCombatTargets.Unregister(Entity);
        }

        // The nearest living player within chaseRadius, or null.
        NetMechPlayer Nearest()
        {
            NetMechPlayer best = null;
            float bestDistance = chaseRadius;
            foreach (NetMechPlayer player in NetMechPlayer.All)
            {
                if (player == null || !player.Entity.IsAlive || player.IsDead) continue;
                float d = MechMath.Flat(player.Transform.WorldPosition - Transform.WorldPosition).Length;
                if (d <= bestDistance) { bestDistance = d; best = player; }
            }
            return best;
        }

        public override void OnUpdate(float deltaTime)
        {
            if (!host || dead) return;
            float now = Time.ElapsedTime;
            if (now >= nextRetarget || target == null || !target.Entity.IsAlive || target.IsDead)
            {
                nextRetarget = now + retargetSeconds;
                NetMechPlayer next = Nearest();
                if (next != target)
                {
                    target = next;
                    if (agent != null) { if (target != null) agent.Arrive(target.Entity); else agent.Stop(); }
                }
            }

            Vector3 velocity = agent != null ? agent.Velocity : Vector3.Zero;
            bool moving = MechMath.Flat(velocity).Length > 0.3f;
            if (animator != null) animator.SetBool("Moving", moving);
            if (target == null) return;

            // Swing at the player when in reach (a placeholder for Unity's melee attack pool).
            Vector3 offset = target.Transform.WorldPosition - Transform.WorldPosition;
            if (MechMath.Flat(offset).Length <= attackRange && now >= nextAttack)
            {
                nextAttack = now + attackInterval;
                target.ApplyDamage(attackDamage);
            }
        }

        public void TakeDamage(float amount)
        {
            if (dead || amount <= 0f) return;
            Health -= amount;
            if (animator != null) animator.SetTrigger("Hit");
            if (Health > 0f) return;
            dead = true;
            if (agent != null) agent.Stop();
            alive.Remove(this);
            MechCombatTargets.Unregister(Entity);
            Destroy(Entity, 0.15f);   // the host's Destroy removes it on every machine
        }
    }
}
