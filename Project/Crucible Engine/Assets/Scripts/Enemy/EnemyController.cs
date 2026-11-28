using System;
using Crucible;

namespace Crucible.Scripts
{
    // Enemy, part 2: what the graph's nodes ask for. Port of Gam3333's EnemyGraphController
    // ("the graph owns decision order; existing systems own execution") together with the
    // movement intents of its EnemyAgent (Hold, Wander, Chase, Arrive, Retreat, Position).
    //
    // Unity's intents become FlowFieldAgent orders, given again every frame a node runs
    // (in Unity, a frame without a new intent meant Hold):
    //   Hold                 Stop()                     keeps its facing (a committed swing stays aimed)
    //   Hold facing target   Stop() + FaceTarget(target)
    //   Wander               MoveTo(a point far ahead on a drifting heading), at wanderSpeedFraction
    //   Chase                FollowField()              (Unity: the flow field, or straight at the player);
    //                                                   a crossbow also faces the target (Unity's Zigzag mode)
    //   Arrive               Arrive(target)             slows from slowingDistance, stops at stoppingDistance
    //   Retreat              MoveTo(away from target) + FaceTarget(target)
    //   Position goal        MoveTo(goal, 0.3) + FaceTarget(target)
    //   Pressure             MoveTo(ring point, stop 0.15, slow 1.6) + FaceTarget(target),
    //                        only in a scene with an EnemyHordeCoordinator
    //   Orbit (not Unity)    MoveTo(a point further round the target, stop 0.3, slow 0.5) + FaceTarget(target):
    //                        strafeWhileAttacking circling, also during the throw
    //   Back off (not Unity) Position / Retreat at retreatSpeedFraction, holding fire: retreatInsteadOfFiring
    //
    public sealed partial class Enemy
    {
        // Awareness (Unity's EnemyAgent).
        public float detectRadius = 40f;      // notices the player this close
        public float loseRadius = 60f;        // ... and gives up beyond this (keep it above detectRadius)
        public float arriveRadius = 12f;      // further than this: chase on the flow field; closer: Arrive
        public float attackRange = 2.2f;      // the distance it attacks from
        public float wanderSpeedFraction = 0.4f;

        // Ranged firing positions (EnemyGraphController). Above 0 on a ranged enemy, it keeps
        // between minimumRange and attackRange from the player, at a spot with a clear shot.
        // These are where it prefers to STAND. Whether it may SHOOT is separate: anywhere within
        // attackRange (its admission distance) with a clear, facing shot and no closer than the
        // weapon's minimumAttackDistance (EnemyWeapon.cs) - so it also fires while retreating.
        public float minimumRange = 0f;
        public float preferredRange = 0f;              // ranged: preferred firing distance; 0 = 55% between the near and far rings
        public float rangedPlanningInterval = 0.35f;   // seconds between re-planning its spot
        public float rangedSpacing = 3f;               // metres it keeps from other shooters
        public bool useHordeCoordination = true;
        public bool useRangedPositioning = true;
        public bool useSpecialCoordinator = true;

        // Back off instead of firing (Heavy Crossbow). Inside minimumRange it stops shooting
        // and walks slowly to a spot further out (sideways or straight back), until it is
        // retreatClearance metres past minimumRange. Only then does it shoot again - and a shot
        // still holds it still for its wind-up. Pinned (barely moving for retreatCorneredSeconds)
        // it fires anyway, so a golem in a corner is not a free kill.
        public bool retreatInsteadOfFiring = false;
        public float retreatSpeedFraction = 0.55f;
        public float retreatClearance = 4f;
        public float retreatCorneredSeconds = 2f;

        // Circle the target while attacking (Throwing Axe). Inside its firing band it keeps
        // walking round the target at preferredRange instead of standing, and keeps walking
        // (at strafeAttackSpeedFraction) through the throw itself. It changes direction every
        // strafeSwitchMin..Max seconds, or at once when the way round is blocked.
        public bool strafeWhileAttacking = false;
        public float strafeStepDegrees = 35f;          // how far round the circle its next point is
        public float strafeSwitchMin = 2f;
        public float strafeSwitchMax = 4.5f;
        public float strafeAttackSpeedFraction = 0.6f;

        // Tutorial/encounter use: stands idle with its brain off until Wake() is called.
        public bool startAsleep = false;
        public string wakeGroup = "";
        bool asleep;

        enum Intent { Hold, HoldFacing, Wander, Chase, Arrive, Retreat, Position, Pressure, Orbit }

        // Horde pressure (EnemyCoordinationMember's part of it): the ring point the scene's
        // EnemyHordeCoordinator worked out for this golem, and when to work it out again.
        public bool HasPressureGoal { get; private set; }
        public Vector3 PressureGoal { get; private set; }
        public Entity PressureTarget { get; private set; }
        public Vector3 PressurePlannedTarget { get; private set; }   // where the target was when it was planned
        public float PressurePlanAt { get; private set; }
        // A fixed sideways nudge of up to 0.3 m, different per golem (Unity's ApproachBias), so
        // golems coming in on the same bearing aim at slightly different points.
        public float ApproachBias { get; private set; }

        internal bool PressureWaiting { get; private set; }
        public void SetPressureGoal(Vector3 goal, Entity target, Vector3 targetPosition, float replanAt,
                                    bool available = true, bool waiting = false)
        {
            HasPressureGoal = available;
            PressureWaiting = waiting;
            PressureGoal = goal;
            PressureTarget = target;
            PressurePlannedTarget = targetPosition;
            PressurePlanAt = replanAt;
        }

        public void ClearPressure() { HasPressureGoal = false; PressureTarget = null; }

        // Unity's Part.MovementQueryMask: what counts as the world for moving and standing -
        // the arena and props, not bodies. Position checks and melee's wall test use it, so
        // they look straight past golems and the player.
        public static readonly int MovementQueryMask = LayerMask.GetMask(PhysicsLayer.NonMoving, PhysicsLayer.Moving);

        FlowFieldAgent agent;
        StateGraphAgent brain;
        EnemyLocalPositionQuery positions;
        EnemyRangedPositioning ranged;
        float speedBeforeSlow, appliedSlowSpeed;
        float speedFraction = 1f;

        // Back-off and circling state (see retreatInsteadOfFiring / strafeWhileAttacking).
        bool retreating;
        float retreatStuckFor;
        int orbitDirection = 1;
        float orbitSwitchAt, orbitPlanAt;
        bool hasOrbitGoal;
        Vector3 orbitGoal;
        public bool IsBackingOff { get { return retreating; } }

        Entity target;
        CombatHealth targetHealth;
        float distanceToTarget = float.PositiveInfinity;
        bool detected;
        float wanderAngle;

        // What it is doing, in Unity's words ("Approach", "In range", "Recover"...): for NavProbe and debugging.
        public string CurrentAction { get; private set; } = "Stopped";
        public Entity Target { get { return target; } }
        public float DistanceToTarget { get { return distanceToTarget; } }
        public bool TargetDetected { get { return detected; } }
        public bool IsRanged { get { return !melee; } }
        public bool UsesRangedPositioning { get { return IsRanged && useRangedPositioning; } }
        // Unity: melee roles with no coordinator keep "continuous pressure" - they pursue
        // the player through their cooldown instead of waiting for a turn.
        public bool ContinuousMeleePressure
        {
            get
            {
                var horde = useHordeCoordination ? EnemyHordeCoordinator.Active : null;
                return melee && (previewSpinnerWindup || horde == null || !horde.reservedPositions);
            }
        }
        public bool HasRangedPosition { get { return ranged != null && ranged.HasGoal; } }
        public Vector3 RangedPosition { get { return HasRangedPosition ? ranged.Goal : Position; } }
        public Vector3 Position { get { return Transform.WorldPosition; } }

        // The way the golem faces, flat. The agent turns +Z towards where it goes, and
        // Crucible's Transform.Forward is -Z, hence * -1.
        public Vector3 Facing
        {
            get
            {
                Vector3 f = Transform.Forward * -1f;
                Vector3 flat = new Vector3(f.X, 0f, f.Z);
                return flat.Length > 1e-4f ? flat.Normalized : new Vector3(0f, 0f, 1f);
            }
        }

        void Sleep()
        {
            asleep = true;
            if (agent != null) agent.Stop();                                        // stop walking first: a switched-off brain gives no more orders
            if (HasComponent<StateGraphAgent>()) Entity.SetEnabled<StateGraphAgent>(false);
        }

        // Unity's IsReady: alive, switched on, and able to move.
        public bool IsReady { get { return agent != null && !IsDead && Entity.IsAlive && Entity.IsActive
            && Enabled && Entity.IsEnabled<EntityScript>(); } }

        public void Wake()
        {
            // Not a corpse: one killed asleep stays "asleep", and its body lies around for up to
            // 30 s as a ragdoll; waking it would switch its brain back on (StopForDeath turned it off).
            if (!asleep || IsDead) return;
            asleep = false;
            if (HasComponent<StateGraphAgent>()) Entity.SetEnabled<StateGraphAgent>(true);
        }

        void CreateController()
        {
            agent = GetComponent<FlowFieldAgent>();
            brain = GetComponent<StateGraphAgent>();
            if (agent == null) Log.Warning("[Enemy] " + Entity.Name + ": needs a FlowFieldAgent - it won't move or attack");
            if (brain == null) Log.Warning("[Enemy] " + Entity.Name + ": needs a StateGraphAgent - nothing tells it what to do");
            physicalBaseOffset = BaseOffset;
            if (HasComponent<Collider>() && Entity.Collider.Shape == ColliderShape.Box)
                physicalBaseOffset = Entity.Collider.WorldSize.Y * .5f - Entity.Collider.Center.Y * Transform.Scale.Y;
            wanderAngle = (float)(random.NextDouble() * Math.PI * 2.0);
            positions = new EnemyLocalPositionQuery(this);
            ranged = new EnemyRangedPositioning(this);

            // Unity worked the bias out from where the golem was when it switched on.
            Vector3 p = Position;
            ApproachBias = (float)Math.Sin(p.X * 0.61f + p.Z * 0.97f) * 0.3f;
        }

        // --- Sensing -------------------------------------------------------------------- //

        // Unity's SensePlayer: there is a target with health, alive, and within detectRadius
        // - or loseRadius once detected, so it doesn't flicker at the edge.
        public bool SensePlayer()
        {
            if (!IsReady) return detected = false;
            ResolveTarget();
            detected = targetHealth != null && !targetHealth.IsDead && targetHealth.Entity.IsActive
                    && distanceToTarget <= (detected ? loseRadius : detectRadius);
            if (!detected) { ranged.Clear(); ClearPressure(); retreating = false; hasOrbitGoal = false; }
            return detected;
        }

        // The graph's "Target" variable (the nearest player, until perception exists),
        // like Unity's EnemyAgent.ResolveTarget. A new target starts undetected.
        void ResolveTarget()
        {
            Entity next = null;
            if (brain != null) brain.GetVariableValue("Target", out next);
            if (!ReferenceEquals(next, null) && !next.IsAlive) next = null;
            if (next != target)
            {
                if (attacking && next != attackTarget?.Entity) CancelAttack();
                detected = false; ranged.Clear(); ClearPressure();
            }

            target = next;
            targetHealth = target != null ? CombatHealth.Find(target) : null;
            distanceToTarget = target != null ? Flat(target.Transform.WorldPosition - Position).Length : float.PositiveInfinity;
        }

        // --- Moving ------------------------------------------------------------------------ //

        // Unity's Approach: true once it stands where it can hit (and holds there).
        public bool Approach()
        {
            CurrentAction = "Approach";
            if (IsRanged) return ApproachRanged(false);

            if (minimumRange > 0f && distanceToTarget < minimumRange)
            {
                Move(Intent.Retreat); CurrentAction = "Reposition";
                return false;
            }

            bool inReach = TargetInReach();
            EnemyHordeCoordinator horde = useHordeCoordination ? EnemyHordeCoordinator.Active : null;
            bool waiting = horde != null && horde.NeedsWaitingPosition(this);
            if (!waiting && distanceToTarget <= attackRange + 0.25f && inReach) { ClearPressure(); Hold("In range"); return true; }

            // A scene with an EnemyHordeCoordinator: within its radius, walk at full speed to
            // this golem's own point on the attack ring (Unity's "Close to attack").
            Vector3 ringPoint;
            if (horde != null && horde.TryPressurePosition(this, out ringPoint))
            {
                CurrentAction = "Close to attack";
                Move(Intent.Pressure, ringPoint);
                return false;
            }

            // Far away: the flow field. Close: Arrive, which slows to a stop at attack range and
            // turns to face. Close but can't hit (not facing, a wall): chase again.
            bool chase = distanceToTarget > arriveRadius || (!inReach && distanceToTarget <= attackRange + 0.5f);
            if (!waiting && !inReach && distanceToTarget <= Math.Min(attackRange, weaponReach) + .25f
                && target != null && Math.Abs(target.Transform.WorldPosition.Y - Position.Y) <= verticalReach
                && ClearPathFrom(Position, target, true))
                Move(Intent.HoldFacing);
            else Move(chase ? Intent.Chase : Intent.Arrive);
            return false;
        }

        // Unity's ApproachRanged: shoot from where it stands if that is between minimumRange
        // and attackRange with a clear shot; otherwise walk to a planned firing spot. While
        // cooling down, crossbows also spread out if another shooter is too close.
        //
        // Movement and attack validity are separate. The movement order is chosen exactly as
        // before (hold, planned spot, retreat, chase); the return value says whether a shot is
        // valid from where it stands right now (RangedShotValid), so a ranged enemy that is
        // retreating or repositioning still fires when its weapon is ready: the graph's Request
        // node starts the attack, Wait For Weapon Attack holds it still for the wind-up and the
        // release (the body has no upper-body layer to fire on the move), and Recover resumes the
        // retreat with the same movement request.
        bool ApproachRanged(bool coolingDown)
        {
            float distance = distanceToTarget;
            if (distance > Math.Max(arriveRadius, attackRange + 2f))
            {
                ranged.Clear();
                retreating = false;
                hasOrbitGoal = false;
                CurrentAction = "Approach firing range";
                Move(Intent.Chase);
                return false;
            }

            // Heavy Crossbow: too close means back off, not shoot.
            if (retreatInsteadOfFiring && KeepBackingOff(distance)) return BackOff();

            // Throwing Axe: in its band with a clear line, keep circling instead of standing.
            if (strafeWhileAttacking && distance >= minimumRange && distance <= attackRange + 0.25f)
            {
                bool clearHere = HasClearRangedPathFrom(Position);
                if (clearHere && Orbit(1f))
                {
                    CurrentAction = coolingDown ? "Circle target / cooldown" : "Circle target";
                    return RangedShotValid(clearHere);
                }
            }
            hasOrbitGoal = false;

            bool firingDistance = distance >= minimumRange && distance <= attackRange + 0.25f;
            bool clear = HasClearRangedPathFrom(Position);
            bool spread = UsesRangedPositioning && coolingDown && ranged.Crowded(rangedSpacing, rangedPlanningInterval);
            bool shotValid = RangedShotValid(clear);

            // Ready shots always win over better spacing.
            if (firingDistance && clear && !spread)
            {
                if (ranged.HasGoal) ranged.Clear();
                CurrentAction = coolingDown ? "Hold firing position / cooldown" : "Aim from firing position";
                Move(Intent.HoldFacing);
                return shotValid;
            }

            Vector3 spot;
            if (UsesRangedPositioning && ranged.TryGetGoal(minimumRange, rangedPlanningInterval, rangedSpacing, out spot))
            {
                CurrentAction = distance < minimumRange ? "Reposition"
                              : spread ? "Spread ranged firing positions" : "Move to clear firing angle";
                Move(Intent.Position, spot);
                return shotValid;
            }

            CurrentAction = distance < minimumRange ? "Reposition" : "Navigate to firing position";
            Move(distance < minimumRange ? Intent.Retreat : Intent.Chase);
            return shotValid;
        }

        // A shot from here, now: inside the admission distance (attackRange), outside the
        // weapon's own minimum safe distance, facing the target and with a clear line - whatever
        // distance it would rather stand at. TryAttack re-checks the same rules.
        bool RangedShotValid(bool clear)
        {
            return clear && target != null && distanceToTarget <= attackRange + 0.25f
                && distanceToTarget >= minimumAttackDistance && CanReach(target);
        }

        // Starts backing off inside minimumRange; stops once retreatClearance past it, so it
        // does not flick between "back off" and "shoot" on the line.
        bool KeepBackingOff(float distance)
        {
            if (!retreating && distance < minimumRange)
            {
                retreating = true;
                retreatStuckFor = 0f;
                ranged.Clear();           // the last plan was a firing spot, maybe closer in
            }
            else if (retreating && distance >= minimumRange + Math.Max(0f, retreatClearance))
            {
                retreating = false;
                ranged.Clear();
            }
            return retreating;
        }

        // Walks slowly to a spot past the clearance line (any angle up to 90 degrees round,
        // so it also steps out to the side), else straight back. No shot while it does -
        // unless it has been pinned in place long enough to count as cornered.
        bool BackOff()
        {
            Vector3 spot;
            float outer = minimumRange + Math.Max(0f, retreatClearance);
            if (UsesRangedPositioning && ranged.TryGetGoal(outer, rangedPlanningInterval, rangedSpacing, out spot))
                Move(Intent.Position, spot, retreatSpeedFraction);
            else Move(Intent.Retreat, Vector3.Zero, retreatSpeedFraction);

            float moving = agent != null ? Flat(agent.Velocity).Length : 0f;
            float expected = agent != null ? agent.Speed * 0.25f : 0f;
            retreatStuckFor = moving < Math.Max(0.15f, expected) ? retreatStuckFor + Time.DeltaTime : 0f;
            if (retreatStuckFor >= retreatCorneredSeconds)
            {
                CurrentAction = "Cornered - firing";
                return RangedShotValid(HasClearRangedPathFrom(Position));
            }
            CurrentAction = "Back off (holding fire)";
            return false;
        }

        bool Cornered { get { return retreating && retreatStuckFor >= retreatCorneredSeconds; } }

        // A point strafeStepDegrees further round a circle about the target, at its preferred
        // range, that it can stand on with a clear shot. Re-planned every 0.2 s; blocked one way,
        // it tries the other. False: no way round from here (the caller falls back to standing).
        bool Orbit(float speed)
        {
            if (target == null) return false;
            float now = Time.ElapsedTime;
            if (now >= orbitSwitchAt)
            {
                if (orbitSwitchAt > 0f && random.NextDouble() < 0.5) orbitDirection = -orbitDirection;
                orbitSwitchAt = now + strafeSwitchMin + (float)random.NextDouble() * Math.Max(0f, strafeSwitchMax - strafeSwitchMin);
            }

            if (!hasOrbitGoal || now >= orbitPlanAt || Flat(orbitGoal - Position).Length < 0.6f)
            {
                orbitPlanAt = now + 0.2f;
                hasOrbitGoal = false;
                Vector3 centre = target.Transform.WorldPosition;
                Vector3 radial = Flat(Position - centre);
                radial = radial.Length > 0.03f ? radial.Normalized : Facing * -1f;
                float low = minimumRange + 0.5f, high = Math.Max(low, attackRange - 0.5f);
                float radius = preferredRange > 0f ? preferredRange : (minimumRange + attackRange) * 0.5f;
                radius = Math.Max(low, Math.Min(high, radius));
                for (int attempt = 0; attempt < 2 && !hasOrbitGoal; ++attempt)
                {
                    Vector3 candidate = centre + Quaternion.AngleAxis(orbitDirection * strafeStepDegrees, Vector3.Up) * radial * radius;
                    Vector3 standing;
                    if (TryGoal(candidate, out standing) && HasClearRangedPathFrom(standing))
                    {
                        orbitGoal = standing;
                        hasOrbitGoal = true;
                    }
                    else
                    {
                        orbitDirection = -orbitDirection;    // blocked (wall, ledge, another golem): go the other way
                        orbitSwitchAt = now + strafeSwitchMin;
                    }
                }
            }
            if (!hasOrbitGoal) return false;
            Move(Intent.Orbit, orbitGoal, speed);
            return true;
        }

        // The Wait For Weapon Attack node's order while a swing or shot plays out. Usually it
        // stands still (keeping the swing's facing); a circling thrower keeps circling, slower.
        public void HoldForAttack()
        {
            if (strafeWhileAttacking && IsRanged && !previewSpinnerWindup && target != null
                && distanceToTarget <= attackRange + 0.25f && distanceToTarget >= minimumRange * 0.5f
                && Orbit(strafeAttackSpeedFraction))
            {
                CurrentAction = "Attack on the move";
                return;
            }
            Hold("Attack windup / release");
        }

        public void Wander() { CurrentAction = "Wander"; Move(Intent.Wander); }
        public void Hold(string action) { CurrentAction = action; Move(Intent.Hold); }

        // Unity's Recover: wait out the cooldown. Melee keeps chasing meanwhile; a crossbow
        // keeps (or improves) its firing position.
        public bool Recover()
        {
            if (AttackInProgress) Hold("Recover");
            else if (IsRanged) ApproachRanged(true);
            else
            {
                bool inPosition = Approach();
                CurrentAction = inPosition ? "Recover" : "Pursue during cooldown";
            }
            return CooldownRemaining <= 0f;
        }

        // Unity's RequestAttack. Melee keeps the old rule (never closer than minimumRange). Ranged
        // enemies are not blocked by minimumRange - that is only where they prefer to stand - but
        // never fire inside their weapon's minimumAttackDistance.
        public bool RequestAttack()
        {
            if (IsRanged)
                return distanceToTarget >= minimumAttackDistance && (!retreating || Cornered) && TryAttack();
            return (minimumRange <= 0f || distanceToTarget >= minimumRange) && TryAttack();
        }

        void Move(Intent intent) { Move(intent, Vector3.Zero, 1f); }
        void Move(Intent intent, Vector3 goal) { Move(intent, goal, 1f); }

        // speed: a fraction of the agent's own speed for this order (wandering always uses
        // wanderSpeedFraction).
        void Move(Intent intent, Vector3 goal, float speed)
        {
            if (agent == null || movementHeld || pushLeft > 0f || IsFrozen || IsDead) return;
            SetSpeedFraction(intent == Intent.Wander ? wanderSpeedFraction : speed);

            switch (intent)
            {
                case Intent.Hold:       agent.FaceTarget(null);   agent.Stop(); break;
                case Intent.HoldFacing: agent.FaceTarget(target); agent.Stop(); break;
                case Intent.Wander:     agent.FaceTarget(null);   agent.MoveTo(WanderPoint()); break;
                // Unity's crossbow chases in its Zigzag pathing mode, which faces the player
                // whichever way it walks (so it reads as sidestepping); every other golem
                // faces where it walks. The sway itself isn't ported, only the facing.
                case Intent.Chase:      agent.FaceTarget(IsRanged ? target : null); agent.FollowField(); break;
                case Intent.Arrive:     agent.FaceTarget(null);   agent.Arrive(target); break;
                // 0.3 m: the spot was checked to have a clear shot, so it must end up ON
                // it, not StoppingDistance short (a melee golem's would be its attack range).
                case Intent.Position:   agent.FaceTarget(target); agent.MoveTo(goal, 0.3f); break;
                // Circling: the point keeps moving ahead of it, so no slowing down on the way.
                case Intent.Orbit:      agent.FaceTarget(target); agent.MoveTo(goal, 0.3f, 0.5f); break;
                // Unity's SetPositionGoal: full speed until SlowingDistance from the ring point,
                // facing the target so it is ready to swing when it gets there.
                case Intent.Pressure:
                    agent.FaceTarget(target);
                    agent.MoveTo(goal, EnemyHordeCoordinator.StoppingDistance, EnemyHordeCoordinator.SlowingDistance);
                    break;
                case Intent.Retreat:
                {
                    // Straight away from the player at full speed, still facing it.
                    Vector3 away = target != null ? Flat(Position - target.Transform.WorldPosition) : Vector3.Zero;
                    away = away.Length > 1e-3f ? away.Normalized : Facing * -1f;
                    agent.FaceTarget(target);
                    agent.MoveTo(Position + away * 6f);
                    break;
                }
            }
        }

        // Slows the agent to a fraction of its own speed (wandering, backing off, throwing on the
        // move) and gives back only the speed this changed. Live tuning or a movement effect may
        // take ownership of the agent's speed meanwhile: then its value is kept.
        void SetSpeedFraction(float fraction)
        {
            fraction = Math.Max(0f, Math.Min(1f, fraction));
            if (Math.Abs(fraction - speedFraction) < 1e-4f) return;
            if (speedFraction < 1f && agent.Speed != appliedSlowSpeed)
            {
                speedFraction = 1f;                       // someone else set the speed: it is the new full speed
                if (fraction >= 1f) return;
            }
            if (speedFraction >= 1f) speedBeforeSlow = agent.Speed;
            if (fraction >= 1f) agent.Speed = speedBeforeSlow;
            else
            {
                agent.Speed = Math.Max(0f, speedBeforeSlow * fraction);
                appliedSlowSpeed = agent.Speed;
            }
            speedFraction = fraction;
        }

        // Steering.Wander (Reynolds): a point on a small circle ahead, nudged a little each
        // frame, so the heading drifts smoothly. Unity steered with it as a force; here it is
        // a MoveTo point pushed 20 m out (MoveTo slows near its point, which wander must not).
        Vector3 WanderPoint()
        {
            Vector3 velocity = agent.Velocity;
            Vector3 heading = Flat(velocity);
            heading = heading.Length > 0.01f ? heading.Normalized : Facing;

            wanderAngle += (float)(random.NextDouble() * 2.0 - 1.0) * 2f * Time.DeltaTime;   // WanderJitter 2 rad/s
            Vector3 displacement = new Vector3((float)Math.Cos(wanderAngle), 0f, (float)Math.Sin(wanderAngle));   // WanderRadius 1
            Vector3 desired = (heading * 2f + displacement).Normalized;                       // WanderDistance 2
            return Position + desired * 20f;
        }

        // Could it go and stand at "candidate"? Unity's EnemyLocalPositionQuery.TryGoal (see
        // EnemyLocalPositionQuery.cs): "standing" is then the spot on the ground, which may be
        // a step up or down from here - so always move to "standing", not to "candidate".
        public bool TryGoal(Vector3 candidate, out Vector3 standing)
        {
            return positions.TryGoal(candidate, out standing);
        }

        // For the position query: metres from its position down to its feet, and its body radius.
        public float BaseOffset { get { return agent != null ? agent.BaseOffset : 0f; } }
        public float BodyRadius { get { return agent != null ? agent.Radius : 0.5f; } }
        float physicalBaseOffset;
        internal float GroundAtFeet() { return Position.Y - physicalBaseOffset; }

        static Vector3 Flat(Vector3 v) { return new Vector3(v.X, 0f, v.Z); }
    }
}
