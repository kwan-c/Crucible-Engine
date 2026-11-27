using System;

namespace Crucible.Scripts
{
    public sealed partial class Enemy
    {
        public bool previewSpinnerWindup;
        public float spinnerWindup = 0.9f;
        public float spinnerRecovery = 0.8f;
        public float spinnerCooldown = 3.5f;
        public float chargeDamage = 18f;
        public float chargeSpeed = 12f;
        public float chargeDuration = .75f;
        public float contactRadius = .8f;
        public float knockbackSpeed = 32f;
        public float knockbackDuration = .65f;
        public float visualHopHeight = .32f;
        public float visualHopFrequency = 2.5f;

        public enum SpinnerPhase { Idle, Windup, Charge, Recovery }
        public SpinnerPhase SpinnerStage { get; private set; }
        Vector3 chargeDirection, bodyRest;
        Quaternion bodyRotation;
        bool attemptedContact, laneBlocked;
        float retrySpecialAt;

        public bool TrySpinnerWindup()
        {
            if (!previewSpinnerWindup || Time.ElapsedTime < retrySpecialAt) return false;
            retrySpecialAt = Time.ElapsedTime + .15f;
            return RequestAttack();
        }

        bool PrepareSpinner(Entity victim)
        {
            if (!CanHoldForSpecial() || !MechMath.Finite(chargeSpeed * chargeDuration)
                || !(chargeSpeed > 0f && chargeDuration > 0f && contactRadius > 0f)) return false;
            Vector3 offset = Flat(victim.Transform.WorldPosition - Position);
            chargeDirection = offset.Length > .01f ? offset.Normalized : Facing;
            laneBlocked = !LaneSupported(Position, chargeDirection, Math.Min(offset.Length, chargeSpeed * chargeDuration));
            if (laneBlocked) return false;
            if (!AdmitSpecialAttack()) return false;
            HoldForSpecial();
            ClearPressure();
            attemptedContact = false;
            if (body != null) { bodyRest = body.Transform.Position; bodyRotation = body.Transform.Rotation; }
            SpinnerStage = SpinnerPhase.Windup;
            return true;
        }

        internal bool HasChargeLaneFrom(Vector3 from)
        {
            Vector3 delta = Flat(Target.Transform.WorldPosition - from);
            return delta.Length < .01f || LaneSupported(from, delta.Normalized, Math.Min(delta.Length, chargeSpeed * chargeDuration));
        }

        public void AwaitSpecialOpening()
        {
            Vector3 goal;
            var horde = useHordeCoordination ? EnemyHordeCoordinator.Active : null;
            if (horde != null && horde.TryPressurePosition(this, out goal))
            {
                CurrentAction = "Position for charge";
                Move(Intent.Pressure, goal);
            }
            else Move(laneBlocked ? Intent.Chase : Intent.HoldFacing);
        }

        void TickSpinner(float dt)
        {
            if (attackTarget == null || !attackTarget.Entity.IsAlive || !attackTarget.Entity.IsActive || attackTarget.IsDead)
            { CancelAttack(); return; }
            age += dt;
            switch (SpinnerStage)
            {
                case SpinnerPhase.Windup:
                    CurrentAction = "Telegraph charge";
                    float progress = Math.Min(1f, age / Math.Max(.001f, spinnerWindup));
                    SpinVisual(720f * progress * progress);
                    if (age < spinnerWindup) return;
                    SpinnerStage = SpinnerPhase.Charge;
                    released = true;
                    age = 0f;
                    StartDirectedMovement(chargeDirection, chargeSpeed, chargeDuration);
                    goto case SpinnerPhase.Charge;
                case SpinnerPhase.Charge:
                    CurrentAction = "Charge";
                    SpinVisual(age * 1200f);
                    Vector3 from, to;
                    bool moving = AdvanceDirectedMovement(dt, out from, out to);
                    if (TryChargeContact(from, to) || !moving)
                    {
                        SetPlanarSpeed(Vector3.Zero);
                        ReleaseSpecialAttack();
                        RestoreSpinVisual();
                        SpinnerStage = SpinnerPhase.Recovery;
                        age = 0f;
                    }
                    break;
                case SpinnerPhase.Recovery:
                    CurrentAction = "Charge recovery";
                    if (age >= spinnerRecovery) CancelAttack();
                    break;
            }
        }

        // The character motor is not reliably returned by Jolt body queries. Sweep the
        // measured path against its vertical capsule, just as projectiles test their target.
        bool TryChargeContact(Vector3 from, Vector3 to)
        {
            Entity victim = attackTarget.Entity;
            if (attemptedContact || attackTarget is Enemy) return false;
            Vector3 centre = AimPoint(victim);
            Vector3 travel = Flat(to - from);
            float lengthSq = Vector3.Dot(travel, travel);
            float t = lengthSq > 1e-6f ? MechMath.Clamp01(Vector3.Dot(Flat(centre - from), travel) / lengthSq) : 0f;
            Vector3 closest = from + (to - from) * t;
            CharacterController capsule = victim.GetComponent<CharacterController>();
            float radius = capsule != null ? capsule.Radius : 0f;
            float halfAxis = capsule != null ? Math.Max(0f, capsule.Height * .5f - radius) : 0f;
            if (capsule == null && victim.HasComponent<Collider>())
            {
                Vector3 size = victim.Collider.WorldSize;
                radius = Math.Max(.01f, Math.Min(size.X, size.Z) * .5f);
                halfAxis = Math.Max(0f, size.Y * .5f - radius);
            }
            Vector3 delta = Flat(closest - centre);
            float dy = Math.Max(0f, Math.Abs(closest.Y - centre.Y) - halfAxis);
            float reach = contactRadius + radius;
            if (Vector3.Dot(delta, delta) + dy * dy > reach * reach || !ClearPathFrom(closest, victim, true)) return false;
            attemptedContact = true;
            if (!attackTarget.ApplyDamage(chargeDamage)) return false;
            MechPlayer player = attackTarget as MechPlayer;
            if (player != null)
            {
                player.ApplyKnockback(chargeDirection, knockbackSpeed, knockbackDuration);
                player.ConfirmHurtBy(Entity);
            }
            return true;
        }

        void SpinVisual(float degrees)
        {
            if (body == null) return;
            float hop = visualHopHeight * Math.Abs((float)Math.Sin(age * Math.PI * visualHopFrequency));
            body.Transform.Position = bodyRest + Vector3.Up * (hop / Math.Max(.001f, Math.Abs(Transform.Scale.Y)));
            body.Transform.Rotation = bodyRotation * Quaternion.Euler(0f, degrees, 0f);
        }

        void RestoreSpinVisual()
        {
            if (body == null || !body.IsAlive) return;
            body.Transform.Position = bodyRest;
            body.Transform.Rotation = bodyRotation;
        }

        void EndSpinner()
        {
            RestoreSpinVisual();
            ReleaseSpecialAttack();
            ReleaseSpecialMovement();
            SpinnerStage = SpinnerPhase.Idle;
        }
    }
}
