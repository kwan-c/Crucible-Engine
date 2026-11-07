using System;
using System.Collections.Generic;
using Crucible;

namespace Crucible.Scripts
{
    // The golem enemy's one script. A Crucible entity holds only one script, so what Gam3333
    // (Unity) spreads over several MonoBehaviours is one class here, split over files like
    // Unity's were (it is a partial class):
    //
    //   Enemy.cs                    health, hit flash, health bar, part drop
    //                               (CombatHealth, EnemyCombatFeedback, HealthBarDisplay, EnemyPartDrop)
    //   EnemyController.cs          what the graph nodes ask for: sensing, approaching,
    //                               moving, recovering (EnemyGraphController + EnemyAgent's intents)
    //   EnemyWeapon.cs              the attack: wind-up, hit or bolt, cooldown, and the
    //                               orange warning lane (EnemyAttack + EnemyWeaponTelegraph)
    //   EnemyAnimation.cs           the body's Animator parameters: walking, hit reacts, attack clips
    //   EnemyRangedPositioning.cs   where a crossbow stands to get a clear shot (its own class, as in Unity)
    //
    // It derives from CombatHealth, so damage code finds it with GetComponent<CombatHealth>().
    // The decisions are the state graph's (Behavior graphs in Unity): its nodes in
    // Scripts/AI/EnemyGraphActions.cs call the methods in EnemyController.cs.
    //
    // Children it looks for by name (the Golem prefabs have them):
    //   "Body"       - the visible mesh, with a MaterialParams component, for the hit flash
    //   "Health Bar" - a thin cube (MeshRenderer) this script moves and stretches each frame
    //   "Telegraph"  - a flat cube (MeshRenderer) shown on the ground during a wind-up
    public sealed partial class Enemy : CombatHealth
    {
        // Hit flash (EnemyCombatFeedback).
        public bool hitFlash = true;
        public float hitFlashDuration = 0.08f;

        // Health bar (HealthBarDisplay): a thin bar floating above the enemy, always square on
        // to the camera, full length at full health and shrinking from the right. Its colour
        // and thickness are the "Health Bar" child's MeshRenderer colour and Y/Z scale.
        public bool healthBar = true;
        public float healthBarHeight = 1.4f;  // above the enemy's position (its middle), as in Unity
        public float healthBarLength = 2f;    // metres at full health

        // Death (stand-in for Unity's CombatDeathRagdoll until bones exist for a real
        // ragdoll): the brain and legs switch off, the rotation locks come off, a push
        // tips it over in the direction of the killing hit, and it is removed after
        // toppleSeconds. Off: it vanishes at once, as before (CombatHealth's default).
        public bool toppleOnDeath = true;
        public float toppleSeconds = 4f;
        public float toppleTorque = 6000f;    // newton-metres for toppleTwistSeconds: enough to lift a 200 kg golem over its edge (gravity resists with about 1700)
        public float toppleTwistSeconds = 0.25f;

        // Part drop (EnemyPartDrop): a chance to spawn a loose part when it dies.
        public Prefab dropPrefab;
        public float dropChancePercent = 25f;
        public float dropSpawnHeight = 1f;
        public float dropPopVelocity = 3f;    // small upward pop so the drop reads as an event; 0 = just falls

        // Unity's hit colour (2, 1.3, 0.6). Tint multiplies the base colour, so values above
        // 1 brighten it, like Unity's HDR colour did.
        static readonly Color HitColor = new Color(2f, 1.3f, 0.6f);

        static readonly Random random = new Random();

        // Every enemy alive (Unity's EnemyAgent.All): the crossbows' spacing and NavProbe
        // walk it. Dead enemies are switched off, not destroyed, so check Entity.IsActive.
        public static readonly List<Enemy> All = new List<Enemy>();

        Entity body, bar;
        Vector3 barScale;                     // the bar's prefab scale: Y and Z (thickness) are kept
        float flashRemaining;

        public override void OnCreate()
        {
            base.OnCreate();                  // full health first: CombatHealth's Awake
            All.Add(this);

            body = Entity.FindChild("Body");
            bar = Entity.FindChild("Health Bar");
            if (hitFlash && (body == null || !body.HasComponent<MaterialParams>()))
                Log.Warning("[Enemy] " + Entity.Name + ": no child \"Body\" with MaterialParams - hit flash is off");
            if (healthBar && (bar == null || !bar.HasComponent<MeshRenderer>()))
                Log.Warning("[Enemy] " + Entity.Name + ": no child \"Health Bar\" with a MeshRenderer - health bar is off");
            if (bar != null) barScale = bar.Transform.Scale;

            // Dying is handled here (toppling), not by switching the entity off.
            if (toppleOnDeath) deactivateOnDeath = false;

            // A dynamic body must not be tipped by collisions while it walks: its
            // FlowFieldAgent turns it. (The prefabs lock rotation too; this makes sure.)
            if (HasComponent<RigidBody>() && !RigidBody.IsKinematic && !RigidBody.FreezeRotation)
                RigidBody.FreezeRotation = true;

            CreateController();               // EnemyController.cs
            CreateWeapon();                   // EnemyWeapon.cs
            CreateAnimation();                // EnemyAnimation.cs
        }

        public override void OnUpdate(float deltaTime)
        {
            if (pushOverPending) PushOver();  // the death topple's kick, a frame after unlocking
            Twist(deltaTime);


            // The attack's clock runs whatever the graph is doing, as Unity's EnemyAttack
            // ticked in its own LateUpdate.
            UpdateWeapon(deltaTime);          // EnemyWeapon.cs
            UpdateAnimation();                // EnemyAnimation.cs: Speed and Strafe for the body's Animator

            // Hit flash: tinted for a few frames, then back to the material's own colour.
            if (flashRemaining > 0f)
            {
                flashRemaining -= deltaTime;
                if (flashRemaining <= 0f && body != null && body.HasComponent<MaterialParams>())
                    body.MaterialParams.ClearTint();
            }

            // Every frame, not only when health changes: the camera moves, and the bar
            // follows it round (Unity did this in LateUpdate).
            if (healthBar && bar != null && bar.HasComponent<MeshRenderer>()) UpdateHealthBar();
        }

        // Port of HealthBarDisplay.LateUpdate. Unity drew a line from "start" to
        // start + right * (2 * health fraction); here a cube is stretched over the same span.
        void UpdateHealthBar()
        {
            // Unity's Camera.main. The bar turns with the camera, so its X axis is the
            // camera's right and it is always seen square on.
            Camera view = Camera.Main;
            Quaternion facing = view != null ? view.Entity.Transform.WorldRotation : Quaternion.Identity;
            Vector3 right = facing * new Vector3(1f, 0f, 0f);

            // The left end stays put and the bar shrinks towards it, as in Unity.
            float length = healthBarLength * CurrentHealth / MaximumHealth;
            Vector3 start = Transform.WorldPosition + Vector3.Up * healthBarHeight - right * (healthBarLength * 0.5f);

            // A cube is centred on its position, so it goes in the middle of the filled part.
            bar.Transform.WorldPosition = start + right * (length * 0.5f);
            bar.Transform.WorldRotation = facing;
            // Scale is local; the golem roots are at scale 1, so local is world here.
            // Never exactly 0: a zero scale can't be inverted for lighting.
            bar.Transform.Scale = new Vector3(Math.Max(length, 0.001f), barScale.Y, barScale.Z);
        }

        // A static list outlives Play: without this, Stop would leave every enemy
        // of the last session in it for the next.
        public override void OnDestroy()
        {
            All.Remove(this);
        }

        protected override void OnDamaged(float amount)
        {
            AnimateHit();                     // EnemyAnimation.cs: the hit-react clip from the side it came from
            if (!hitFlash || body == null || !body.HasComponent<MaterialParams>()) return;
            body.MaterialParams.Tint = HitColor;
            flashRemaining = hitFlashDuration;
        }

        protected override void OnDied()
        {
            CancelAttack();                   // a half-done swing must not land from a dead golem
            HideTelegraph();
            if (toppleOnDeath) Topple();

            // Unity's death particles (CombatFeedback) are not ported.
            if (dropPrefab == null) return;
            if (random.NextDouble() * 100.0 >= dropChancePercent) return;

            Vector3 position = Transform.WorldPosition + Vector3.Up * dropSpawnHeight;
            Quaternion rotation = Quaternion.Euler(0f, (float)(random.NextDouble() * 360.0), 0f);

            // A root entity, not a child: this enemy is switched off right after OnDied,
            // and a child would vanish with it.
            Entity drop = dropPrefab.Instantiate(position, rotation);
            if (drop != null && dropPopVelocity > 0f && drop.HasComponent<RigidBody>())
                drop.RigidBody.Velocity = Vector3.Up * dropPopVelocity;   // Unity's ForceMode.VelocityChange
        }

        // The death topple. The order matters: the agent and brain go first, or the agent
        // would steer the falling body and the graph would try to attack with it.
        void Topple()
        {
            if (agent != null) { agent.Stop(); Entity.SetEnabled<FlowFieldAgent>(false); }
            if (HasComponent<StateGraphAgent>()) Entity.SetEnabled<StateGraphAgent>(false);
            if (bar != null && bar.HasComponent<MeshRenderer>()) bar.MeshRenderer.Visible = false;

            if (HasComponent<RigidBody>() && !RigidBody.IsKinematic)
            {
                // Unlocking makes physics rebuild the body at the end of this frame, and a
                // rebuild keeps only its straight-line speed - a spin given now would be
                // lost. So the push comes next frame (PushOver, from OnUpdate).
                RigidBody.FreezeRotation = false;
                pushOverPending = true;
            }

            Destroy(Entity, toppleSeconds);
        }

        bool pushOverPending;

        // The killing hit's knockback (held back by CombatHealth, see ApplyDamage), and
        // the start of the twist that tips it over away from the hit (Twist, below).
        // A shove alone doesn't topple a golem: tested, a push near its middle just slid
        // it 10 m across the floor, upright - its friction is too low to trip it.
        void PushOver()
        {
            pushOverPending = false;
            if (!HasComponent<RigidBody>()) return;
            if (LastKnockback.Length > 0f) RigidBody.ApplyImpulse(LastKnockback);

            // Which way it falls: away from the hit, or backwards if the hit had no shove.
            Vector3 away = new Vector3(LastKnockback.X, 0f, LastKnockback.Z);
            away = away.Length > 1e-3f ? away.Normalized : Facing * -1f;

            // Turning round up x away swings the top towards "away" (right-hand rule).
            twistAxis = Vector3.Cross(Vector3.Up, away).Normalized;
            twistLeft = toppleTwistSeconds;
        }

        Vector3 twistAxis;
        float twistLeft;

        // A torque for toppleTwistSeconds - RigidBody.AddTorque is a push for this frame,
        // so it is given every frame until the time is up. Physics owns the fall after that.
        void Twist(float deltaTime)
        {
            if (twistLeft <= 0f || !HasComponent<RigidBody>()) return;
            RigidBody.AddTorque(twistAxis * toppleTorque);
            twistLeft -= deltaTime;
        }
    }
}
