using System;
using Crucible;

namespace Crucible.Scripts
{
    // Port of Gam3333's CombatHealth (Unity): one health pool for a whole body, shared by
    // the player and the enemies. It takes damage, and when health reaches 0 it "dies":
    // by default it switches its entity off, like Unity's gameObject.SetActive(false), which
    // also takes it out of physics, navigation and the enemies' "nearest player" search.
    //
    // Not sealed on purpose: an entity can hold only ONE script in Crucible, so the enemy's
    // extras (hit flash, health bar, part drop) live in Enemy, which derives from this.
    // Anything that deals damage asks for CombatHealth and gets an Enemy just the same.
    public class CombatHealth : EntityScript
    {
        public float maximumHealth = 100f;

        // Off for the player: switching the player off would also switch off its camera
        // (a child), and the screen would go black. Dying then only logs and stops damage.
        public bool deactivateOnDeath = true;

        public float CurrentHealth { get; private set; }
        public float MaximumHealth { get { return maximumHealth; } }
        public bool IsDead { get { return CurrentHealth <= 0f; } }

        // Unity's Awake. A class deriving from this must call base.OnCreate() first,
        // or it starts at 0 health and counts as dead.
        public override void OnCreate()
        {
            if (maximumHealth < 1f) maximumHealth = 1f;
            CurrentHealth = maximumHealth;
        }

        // Changes capacity. Without refill, lost health is not restored and a lower maximum
        // cuts current health to fit; refill is for the start, at full health.
        public void SetMaximum(float maximum, bool refill)
        {
            maximumHealth = Math.Max(1f, maximum);
            CurrentHealth = refill ? maximumHealth : Math.Min(CurrentHealth, maximumHealth);
        }

        // Returns false if nothing was taken: already dead, or a zero, negative or broken
        // amount. "!(amount > 0f)" also catches NaN, which fails every comparison.
        public bool ApplyDamage(float amount)
        {
            if (IsDead || !(amount > 0f) || float.IsInfinity(amount)) return false;

            float applied = Math.Min(CurrentHealth, amount);
            CurrentHealth -= applied;
            OnDamaged(applied);

            if (IsDead)
            {
                // OnDied first: a part drop must spawn while the entity is still on.
                OnDied();
                if (deactivateOnDeath) Entity.SetActive(false);
                else Log.Info("[CombatHealth] " + Entity.Name + " died");
            }
            return true;
        }

        // Damage plus a shove. knockback is an impulse (mass x metres per second, in world
        // axes) for a dynamic RigidBody - an enemy flies back and its FlowFieldAgent steers
        // back in as the speed fades. Bodies that aren't dynamic (the player's
        // CharacterController, a kinematic prop) just take the damage. A killing blow's
        // shove is NOT applied here: dying can change the body (an Enemy unlocks its
        // rotation to topple, which rebuilds it and loses a push given the same frame),
        // so OnDied gets it through LastKnockback and applies it itself.
        public bool ApplyDamage(float amount, Vector3 knockback)
        {
            LastKnockback = knockback;
            bool hurt = ApplyDamage(amount);
            if (hurt && !IsDead && knockback.Length > 0f && Entity.HasComponent<RigidBody>() && !Entity.RigidBody.IsKinematic)
                Entity.RigidBody.ApplyImpulse(knockback);
            return hurt;
        }

        // The knockback of the last hit that came with one (zero if none yet).
        public Vector3 LastKnockback { get; private set; }

        // Unity's Damaged and Died events, as methods a derived class overrides.
        // (C# events would work too, but nothing outside the entity listens yet.)
        protected virtual void OnDamaged(float amount) {}
        protected virtual void OnDied() {}

        // The CombatHealth on this entity or the nearest parent with one, like Unity's
        // GetComponentInParent. A ray hits the collider it touches, which can be a child
        // (an arm, a head), and the health sits on the root.
        public static CombatHealth Find(Entity entity)
        {
            for (Entity current = entity; !ReferenceEquals(current, null) && current.IsAlive; current = current.Transform.Parent)
            {
                CombatHealth health = current.GetComponent<CombatHealth>();
                if (health != null) return health;
            }
            return null;
        }
    }
}
