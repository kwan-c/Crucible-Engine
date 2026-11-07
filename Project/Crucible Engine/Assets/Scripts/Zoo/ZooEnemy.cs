/*************************************************************************
* \file:         ZooEnemy.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \date          8 November 2026
* \version       1.0
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

namespace Crucible.Scripts
{
    public sealed class ZooEnemy : EntityScript
    {
        public float maximumHealth = 100f;
        public float attackRange = 3f;      // metres from the player
        public float attackDamage = 10f;
        public float attackInterval = 1.5f;
        public bool fast = false;           // jog instead of walk (elites)
        public string playerName = "Player";

        static readonly List<ZooEnemy> alive = new List<ZooEnemy>();
        /// <summary>How many Zoo enemies exist right now (the wave's population cap).</summary>
        public static int AliveCount { get { return alive.Count; } }

        public float Health { get; private set; }

        FlowFieldAgent agent;
        Animator animator;
        Entity player;
        MechPlayer playerScript;
        float nextAttack;
        bool dead;

        public override void OnCreate()
        {
            alive.Add(this);
            Health = maximumHealth;
            agent = GetComponent<FlowFieldAgent>();
            Entity visual = Entity.FindChild("Visual");
            animator = visual != null ? visual.Animator : null;
            if (animator != null) animator.SetBool("Fast", fast);
            // Aim at the collider's centre (Unity's Enemy_Fodder aimPoint sits at its body centre).
            Collider body = GetComponent<Collider>();
            float aimHeight = body != null ? body.Center.Y * Transform.Scale.Y : 1f;
            MechCombatTargets.Register(Entity, (amount, from) => TakeDamage(amount), aimHeight);
            FindPlayer();
        }

        public override void OnDestroy()
        {
            alive.Remove(this);
            MechCombatTargets.Unregister(Entity);
        }

        void FindPlayer()
        {
            player = Entity.Find(playerName);
            playerScript = player != null ? player.GetComponent<MechPlayer>() : null;
            if (agent != null) agent.Arrive(player);   // stops at the agent's Stopping Distance
        }

        public override void OnUpdate(float deltaTime)
        {
            if (dead) return;
            if (player == null || !player.IsAlive) { FindPlayer(); if (player == null) return; }

            Vector3 velocity = agent != null ? agent.Velocity : Vector3.Zero;
            bool moving = MechMath.Flat(velocity).Length > 0.3f;
            if (animator != null) animator.SetBool("Moving", moving);

            // Swing at the player when in reach (a placeholder for Unity's melee attack pool).
            Vector3 offset = player.Transform.WorldPosition - Transform.WorldPosition;
            float now = Time.ElapsedTime;
            if (MechMath.Flat(offset).Length <= attackRange && now >= nextAttack && playerScript != null && !playerScript.Health.IsDead)
            {
                nextAttack = now + attackInterval;
                playerScript.Health.ApplyDamage(attackDamage);
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
            Destroy(Entity, 0.15f);
        }
    }
}
