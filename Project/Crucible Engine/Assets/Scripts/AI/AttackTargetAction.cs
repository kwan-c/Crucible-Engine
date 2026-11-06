using System;
using Crucible;
using Crucible.AI;
using Action = Crucible.AI.Action;   // System.Action has the same name

namespace Crucible.Scripts
{
    // Port of Gam3333's ArriveState + EnemyAttack together. In Unity they ran side by side:
    // ArriveState kept closing in and facing the player, and EnemyAttack swung whenever the
    // enemy was in range and off cooldown. Here one node does both: it keeps the agent on
    // Arrive (which also turns it to face the target) and attacks when it can.
    //
    // An attack: wind-up for FireDuration seconds, and the hit (melee) or the bolt
    // (crossbow) comes ReleaseFrame of the way through, the moment the animation would
    // release. Range is the agent's Stopping Distance + 0.5 m, like Unity's attackRange.
    //
    // Never finishes on its own (an arrow takes it out); fails if the target is gone.
    [NodeDescription(
        name: "Attack Target",
        description: "Closes in on the target and attacks it on a cooldown. Melee deals Damage; Crossbow fires the Enemy script's bolt prefab (the bolt has its own damage).",
        story: "Self attacks [Target] with [Kind]",
        category: "Action/Combat",
        id: "attack_target")]
    public sealed class AttackTargetAction : NavigationAction
    {
        public enum AttackKind { Melee, Crossbow }

        public BlackboardVariable<Entity> Target = BlackboardVariable<Entity>.Link("Target");
        public AttackKind Kind = AttackKind.Melee;
        public float Damage = 20f;           // melee only
        public float Cooldown = 1.8f;        // seconds from one attack's start to the next
        public float FireDuration = 0.7f;    // length of one attack
        public float ReleaseFrame = 0.6f;    // 0..1: when in the attack the hit lands / bolt flies
        public float FacingAngle = 70f;      // degrees either side of straight ahead it can hit
        public float VerticalReach = 2.5f;   // metres above or below it can hit

        Entity issued;                       // the target last given to the agent
        CombatHealth attackTarget;           // who the current attack is aimed at
        Vector3 aim;                         // crossbow: aimed once, the bolt never homes
        float age;
        float readyAt;                       // Time.ElapsedTime when the cooldown is over
        bool attacking, released;
        bool warnedNoBolt;

        protected override Status OnStart()
        {
            if (Agent == null) return Status.Failure;
            issued = Target.Value;
            if (!IsThere(issued)) return Status.Failure;
            Agent.Arrive(issued);
            attacking = false;
            return Status.Running;
        }

        protected override Status OnUpdate()
        {
            if (Agent == null) return Status.Failure;
            Entity target = Target.Value;
            if (!IsThere(target)) return Status.Failure;
            if (target != issued) { issued = target; Agent.Arrive(target); }   // only a NEW target is re-issued

            if (attacking)
            {
                // A lost or dead target cancels the hit that hasn't landed yet.
                // (A bolt already in the air keeps flying.)
                if (attackTarget == null || attackTarget.IsDead || !attackTarget.Entity.IsActive)
                {
                    attacking = false;
                    return Status.Running;
                }

                age += DeltaTime;
                if (!released && age >= FireDuration * ReleaseFrame)
                {
                    released = true;
                    if (Kind == AttackKind.Melee)
                    {
                        // Checked again at the release: it may have dodged during the wind-up.
                        Entity victim = attackTarget.Entity;
                        if (CanReach(victim) && ClearPath(victim)) attackTarget.ApplyDamage(Damage);
                    }
                    else LaunchBolt();
                }
                if (age >= FireDuration) attacking = false;
                return Status.Running;
            }

            TryAttack(target);
            return Status.Running;
        }

        protected override void OnEnd()
        {
            attacking = false;               // leaving the state drops a half-done attack
            base.OnEnd();                    // NavigationAction: stop
        }

        void TryAttack(Entity target)
        {
            if (Time.ElapsedTime < readyAt) return;            // cooling down

            CombatHealth health = CombatHealth.Find(target);
            if (health == null || health.IsDead) return;       // nothing to hurt
            if (!CanReach(target) || !ClearPath(target)) return;

            attackTarget = health;
            aim = target.Transform.WorldPosition;
            age = 0f;
            readyAt = Time.ElapsedTime + Cooldown;
            attacking = true;
            released = false;
        }

        // In range (flat distance), within VerticalReach, and roughly in front.
        bool CanReach(Entity target)
        {
            Vector3 offset = target.Transform.WorldPosition - Self.Transform.WorldPosition;
            if (Math.Abs(offset.Y) > VerticalReach) return false;

            Vector3 flat = new Vector3(offset.X, 0f, offset.Z);
            float distance = flat.Length;
            if (distance > Agent.StoppingDistance + 0.5f) return false;
            if (distance < 0.03f) return true;                  // on top of it: any facing will do

            // The agent turns +Z towards its target, and Crucible's Transform.Forward is -Z,
            // so the way the golem faces is -Forward.
            Vector3 facing = Self.Transform.Forward * -1f;
            facing = new Vector3(facing.X, 0f, facing.Z).Normalized;
            float cosine = Vector3.Dot(facing, flat * (1f / distance));
            float degrees = (float)(Math.Acos(Math.Max(-1f, Math.Min(1f, cosine))) * 180.0 / Math.PI);
            return degrees <= FacingAngle;
        }

        // Nothing between it and the target. The ray starts half a metre up, like Unity's,
        // and sees through the golem itself; a hit on the target or one of its children counts
        // as clear. Unity looked at every hit; Crucible's ray gives the nearest one, which is
        // all this needs.
        bool ClearPath(Entity target)
        {
            Vector3 origin = Self.Transform.WorldPosition + Vector3.Up * 0.5f;
            Vector3 offset = target.Transform.WorldPosition - origin;
            float distance = offset.Length;
            if (distance < 0.01f) return true;

            RaycastHit hit;
            if (!Physics.Raycast(origin, offset * (1f / distance), out hit, distance, Self)) return true;
            for (Entity e = hit.Entity; !ReferenceEquals(e, null) && e.IsAlive; e = e.Transform.Parent)
                if (e == target) return true;
            return false;
        }

        void LaunchBolt()
        {
            Enemy enemy = Self.GetComponent<Enemy>();
            Prefab bolt = enemy != null ? enemy.boltPrefab : null;
            if (bolt == null)
            {
                if (!warnedNoBolt) Log.Warning("[Attack Target] " + Self.Name + ": Crossbow needs a Bolt Prefab on its Enemy script");
                warnedNoBolt = true;
                return;
            }

            // Unity's fallback muzzle: half a metre up and one metre ahead. 1.5 m here, to
            // start clear of the golem's 1.7 m deep body box.
            Vector3 facing = Self.Transform.Forward * -1f;
            Vector3 start = Self.Transform.WorldPosition + Vector3.Up * 0.5f + facing * 1.5f;
            Vector3 direction = (aim - start).Normalized;
            if (direction.Length < 0.5f) direction = facing;

            // LookRotation points the bolt's Transform.Forward along direction; the Bolt
            // script flies that way.
            bolt.Instantiate(start, Quaternion.LookRotation(direction));
        }
    }
}
