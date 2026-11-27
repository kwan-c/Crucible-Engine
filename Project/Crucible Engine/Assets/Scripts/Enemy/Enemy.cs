using System;
using System.Collections.Generic;
using Crucible;

namespace Crucible.Scripts
{
    // The golem enemy's one script. A Crucible entity holds only one script, so what
    // (Unity) spreads over several MonoBehaviours is one class here, split over files like
    // Unity's were (it is a partial class):
    //
    //   Enemy.cs                    health, hit flash, health bar, part drop, melee push, hit stop
    //                               (CombatHealth, EnemyCombatFeedback, HealthBarDisplay, EnemyPartDrop,
    //                               EnemyAgent.ApplyMeleePush and its CombatHitStop checks)
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

        // Death (Unity's CombatDeathRagdoll). With ragdollOnDeath the engine turns the "Body"
        // into a ragdoll (ECSRagdollSystem): one physics piece per rigid mesh, jointed, and the
        // mesh follows them; the engine removes the golem with it (rest 5 s, 30 s at most, 20
        // ragdolls at once). Without it, or if the ragdoll can't start, the older topple: the
        // rotation locks come off, a push tips the whole golem over away from the killing hit,
        // and it is removed after toppleSeconds. Either way the brain and legs switch off.
        // Both off: it vanishes at once (CombatHealth's default).
        public bool ragdollOnDeath = true;
        public float deathKick = 1.5f;        // m/s backwards on a ragdoll death that isn't a melee kill (Unity's deathKick)
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

            // The mech's lock-on and weapons only see what is in this list. The root is the
            // golem's middle, so that is where they aim.
            MechCombatTargets.Register(Entity, (amount, from) => ApplyDamage(amount), 0f);

            body = Entity.FindChild("Body");
            bar = Entity.FindChild("Health Bar");
            if (hitFlash && (body == null || !body.HasComponent<MaterialParams>()))
                Log.Warning("[Enemy] " + Entity.Name + ": no child \"Body\" with MaterialParams - hit flash is off");
            if (healthBar && (bar == null || !bar.HasComponent<MeshRenderer>()))
                Log.Warning("[Enemy] " + Entity.Name + ": no child \"Health Bar\" with a MeshRenderer - health bar is off");
            if (bar != null) barScale = bar.Transform.Scale;

            // Dying is handled here (toppling), not by switching the entity off.
            if (toppleOnDeath || ragdollOnDeath) deactivateOnDeath = false;

            // A dynamic body must not be tipped by collisions while it walks: its
            // FlowFieldAgent turns it. (The prefabs lock rotation too; this makes sure.)
            if (HasComponent<RigidBody>() && !RigidBody.IsKinematic && !RigidBody.FreezeRotation)
                RigidBody.FreezeRotation = true;

            CreateController();               // EnemyController.cs
            CreateWeapon();                   // EnemyWeapon.cs
            CreateAnimation();                // EnemyAnimation.cs
            if (startAsleep) Sleep();
        }

        public override void OnUpdate(float deltaTime)
        {
            if (!useHordeCoordination || (PressureTarget != null && EnemyHordeCoordinator.Active == null)) ClearPressure();
            if (meleeHost != null && (!useHordeCoordination || meleeHost != EnemyHordeCoordinator.Active)) ReleaseMeleeAttack();
            if (!useRangedPositioning && HasRangedPosition) ranged.Clear();
            if (specialHost != null && (!useSpecialCoordinator || specialHost != EnemySpecialCoordinator.Active)) ReleaseSpecialAttack();
            if (attacking && (brain == null || !Entity.IsEnabled<StateGraphAgent>())) CancelAttack();
            if (pushOverPending) PushOver();  // the death topple's kick, a frame after unlocking
            Twist(deltaTime);
            UpdatePush(deltaTime);            // a melee push's slide, or standing still in a hit stop (below)
            UpdateLegs();                     // the agent is off while either of those moves or holds it


            // The attack's clock runs whatever the graph is doing, as Unity's EnemyAttack
            // ticked in its own LateUpdate - except in a hit stop, which pauses the wind-up too.
            if (!IsFrozen) UpdateWeapon(deltaTime);   // EnemyWeapon.cs
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
            CancelAttack();
            All.Remove(this);
            MechCombatTargets.Unregister(Entity);
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
            MechCombatTargets.Unregister(Entity);   // a toppling golem can't be locked on to or hit
            ClearPush();                      // a corpse is not slid; the topple owns its motion now

            // No animation on a corpse: there is no death clip, and the controller would keep
            // looping its last clip (Idle). Speed 0 holds the pose it died in (Unity's lockPose);
            // a ragdoll then poses the bones its pieces move on top of that, every frame.
            // A hit stop it was in is ended first, or its end would restart the clip.
            MechHitStop.End(Entity);
            foreach (Entity part in animatedParts) part.Animator.Speed = 0f;

            CancelAttack();                 // a half-done swing must not land from a dead golem
            HideTelegraph();
            if (!(ragdollOnDeath && StartRagdoll()) && toppleOnDeath) Topple();

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

        // The death ragdoll (Unity's CombatDeathRagdoll.OnDeath). A melee kill throws every
        // piece with the swing's launch; any other death keeps the golem's own speed (at most
        // 15 m/s) with a kick backwards and a little lift. The golem's own body then stays where
        // it died, frozen and on the Corpse layer: its mesh follows the pieces, which are
        // separate entities, so nothing needs it to move. False: no ragdoll, topple instead.
        bool StartRagdoll()
        {
            if (body == null) return false;
            Vector3 velocity;
            if (meleeKillPending) velocity = meleeKillVelocity;
            else
            {
                Vector3 own = HasComponent<RigidBody>() ? RigidBody.Velocity : Vector3.Zero;
                if (own.Length > 15f) own = own.Normalized * 15f;
                velocity = own - Facing * deathKick + Vector3.Up * 0.5f;
            }
            // The golem is the ragdoll's owner: the engine removes the two together, once the
            // pieces have rested 5 s, after 30 s at most, or to make room (20 at most).
            if (!Ragdoll.Spawn(body, velocity, Entity)) return false;

            StopForDeath();
            if (HasComponent<Collider>()) Entity.Collider.Layer = PhysicsLayer.Corpse;
            if (HasComponent<RigidBody>()) RigidBody.IsKinematic = true;
            // A safety net only: past the engine's 30 s, so it matters just if the ragdoll could
            // not be built after all (the engine logs why). Destroying a gone entity is ignored.
            Destroy(Entity, 35f);
            return true;
        }

        // Set by ApplyMeleeHit only while a killing blow's damage is applied (Unity's
        // BeginMeleeLaunch / EndMeleeLaunch): OnDied runs inside ApplyDamage and reads it.
        bool meleeKillPending;
        Vector3 meleeKillVelocity;

        // Every death: the agent and brain go first, or the agent would steer the falling body
        // and the graph would try to attack with it. The health bar goes too.
        void StopForDeath()
        {
            if (agent != null) { agent.Stop(); Entity.SetEnabled<FlowFieldAgent>(false); }
            if (HasComponent<StateGraphAgent>()) Entity.SetEnabled<StateGraphAgent>(false);
            if (bar != null && bar.HasComponent<MeshRenderer>()) bar.MeshRenderer.Visible = false;
        }

        // The death topple: the whole golem tips over as one body (no ragdoll).
        void Topple()
        {
            StopForDeath();

            // It still falls and lands on the floor, but the player, other enemies and
            // shots go straight through it. Set before the unlock below: one rebuild for both.
            if (HasComponent<Collider>()) Entity.Collider.Layer = PhysicsLayer.Corpse;

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

        // A hit from the mech's melee (Unity's CombatBlast melee branch). push and killLaunch
        // are velocities in m/s, worked out by the mech (MechPlayer.MeleeHit). A killing hit
        // throws the ragdoll with its launch (meleeKillPending), or hands it to the topple as
        // the knockback (PushOver applies it next frame). Any other hit slides the golem.
        public bool ApplyMeleeHit(float amount, Vector3 push, Vector3 killLaunch, float pushSeconds)
        {
            // Asked BEFORE the damage: once ApplyDamage returns, OnDied has already used the
            // knockback, so the kill has to be known in advance (Unity's BeginMeleeLaunch).
            bool kills = !IsDead && amount >= CurrentHealth;
            if (kills)
            {
                // An impulse is mass x change in speed, so this gives exactly killLaunch m/s.
                float mass = HasComponent<RigidBody>() ? RigidBody.Mass : 0f;
                meleeKillPending = true;
                meleeKillVelocity = killLaunch;
                try { return ApplyDamage(amount, killLaunch * mass); }
                finally { meleeKillPending = false; }   // never leaks into a later, unrelated death
            }
            if (!ApplyDamage(amount)) return false;   // dead already, or a broken amount
            StartPush(push, pushSeconds);
            return true;
        }

        // Melee push (Unity's EnemyAgent.ApplyMeleePush). The golem slides along the swing,
        // upright, slowing evenly to a stop over pushSeconds: about 3 m with Unity's numbers.
        // Its FlowFieldAgent is off meanwhile (UpdateLegs), otherwise it would steer back at once
        // and brake the slide at its 15 m/s2 acceleration (about 1.3 m). The brain keeps running,
        // as in Unity; it just can't walk until the push ends.
        Vector3 pushVelocity;
        float pushLeft, pushDeceleration;

        void StartPush(Vector3 velocity, float seconds)
        {
            velocity = new Vector3(velocity.X, 0f, velocity.Z);   // upright: a living golem isn't lifted
            // Only a dynamic body: its speed is what the slide sets.
            if (velocity.Length < 0.01f || !(seconds > 0f) || !HasComponent<RigidBody>() || RigidBody.IsKinematic) return;
            if (movementHeld) CancelAttack();

            // A second hit adds to the slide and restarts the clock, capped at 22 m/s (Unity's cap).
            pushVelocity = pushVelocity + velocity;
            if (pushVelocity.Length > 22f) pushVelocity = pushVelocity.Normalized * 22f;
            pushLeft = Math.Min(seconds, 3f);
            pushDeceleration = pushVelocity.Length / pushLeft;   // reaches 0 exactly when the time is up
        }

        void UpdatePush(float deltaTime)
        {
            if (IsDead || !HasComponent<RigidBody>() || RigidBody.IsKinematic) return;   // the topple owns a corpse
            if (IsFrozen)
            {
                // Hit stop (Unity's EnemyAgent skips its update): stand still where it is. A push
                // waits with the time it had left and carries on when the stop ends.
                Vector3 v = RigidBody.Velocity;
                RigidBody.Velocity = new Vector3(0f, v.Y, 0f);
                return;
            }
            if (pushLeft <= 0f) return;
            float step = Math.Min(deltaTime, pushLeft);
            pushLeft -= step;

            // Slower by the same amount every second (Unity's linear slow-down).
            float speed = pushVelocity.Length;
            float slower = Math.Max(0f, speed - pushDeceleration * step);
            pushVelocity = speed > 1e-4f ? pushVelocity * (slower / speed) : Vector3.Zero;

            // Set every frame, so friction can't eat the slide early. Only the flat part:
            // gravity keeps the vertical one. Physics still stops it at a wall.
            RigidBody.Velocity = new Vector3(pushVelocity.X, RigidBody.Velocity.Y, pushVelocity.Z);
            if (pushLeft <= 0f) ClearPush();
        }

        void ClearPush()
        {
            pushLeft = 0f;
            pushVelocity = Vector3.Zero;
        }

        // Is this golem in a hit stop (MechHitStop)? Its AI, attack clock and legs wait.
        public bool IsFrozen { get { return MechHitStop.IsFrozen(Entity); } }

        // The legs (FlowFieldAgent) are off while something else moves the golem or holds it: a
        // melee push or a hit stop. Back on as soon as neither applies. Only switched when that
        // changes, and never once dead: the topple has switched them off for good.
        bool legsPaused;

        void UpdateLegs()
        {
            if (agent == null || IsDead) return;
            bool pause = pushLeft > 0f || IsFrozen || movementHeld;
            if (pause == legsPaused) return;
            if (pause)
            {
                if (!Entity.IsEnabled<FlowFieldAgent>()) return;
                Entity.SetEnabled<FlowFieldAgent>(false);
            }
            else Entity.SetEnabled<FlowFieldAgent>(true);
            legsPaused = pause;
        }
    }
}
