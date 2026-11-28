using System;
using Crucible;

namespace Crucible.Scripts
{
    // Enemy, part 5: the body's animation (and a weapon's, if it has one). Unity-style: this
    // script only sets the Animator's parameters; the controllers decide which clip plays
    // and how it blends (Assets/Animation/Enemy Fodder.animcontroller for the body,
    // Shoulder Cannon.animcontroller for the cannon).
    //
    // Every frame:
    //   Speed   - how fast it is really moving, m/s (the body's velocity for a dynamic golem).
    //             The controller: idle under 0.3, walk under 2.9 (the walk clip is made for
    //             1.6 m/s), jog above (made for 4.2 m/s).
    //   Strafe  - how sideways that movement is, -1..1: +1 straight to its left (+X, the way
    //             the strafe-left clips go), -1 to its right. Past +-0.6 the strafe clips play.
    //             Crossbows and cannons walk sideways a lot (they face the player while they
    //             reposition); maces mostly face where they walk.
    // On a hit:   Hit, with HitDirection 0 front, 1 back, 2 left, 3 right - the side the hit came
    //             from, taken as the direction to its target (the player is almost always who hit it).
    // On attacks, by attackAnimation:
    //   "Slash"   - Attack when the swing starts (Fodder_MeleeSlash, timed by the controller so
    //               its strike lands at releaseFrame).
    //   "Cannon"  - Charge when the attack starts, Fire when the shot leaves.
    // The body, head and animated weapon each consume the parameters their controller defines.
    //   "None"    - nothing (the crossbow: there is no crossbow clip yet).
    //   A cancelled attack sends Cancel, so a charge doesn't hang in its loop.
    //
    // Not ported 1:1: Unity blended walk and strafe by angle in a blend tree, and sped the
    // clips up to match the golem's speed. Crucible's Animator has neither yet, so it switches
    // clips at the thresholds above (with a 0.25 s crossfade), and a mace running at 6 m/s
    // plays the 4.2 m/s jog - its feet slide a little.
    public sealed partial class Enemy
    {
        public string attackAnimation = "Slash";   // "Slash", "Cannon" or "None" - see above

        // Everything with an Animator that moves with the golem: the "Body", and the parts
        // mounted on it, found by name - "Head" (the melee golems' ENEMYPART_Head, which carries
        // the same 16 clips as the body and runs the body's controller, so it stays in step)
        // and the equipped weapon. Every parameter goes to all of them; a part whose controller lacks one
        // just ignores it (the cannon has no Speed), as Unity's Animator does.
        Entity[] animatedParts = new Entity[0];
        Animator equippedWeaponAnimator;
        bool warnedNoAnimator;

        void CreateAnimation()
        {
            var found = new System.Collections.Generic.List<Entity>();
            Entity weapon = Entity.FindChild("Weapon Model") ?? Entity.FindChild("Cannon");
            foreach (Entity part in new[] { body, Entity.FindChild("Head"), weapon })
                if (part != null && part.HasComponent<Animator>()) found.Add(part);
            animatedParts = found.ToArray();
            if (weapon != null && weapon.HasComponent<Animator>()) equippedWeaponAnimator = weapon.Animator;
        }

        void SetFloat(string name, float value) { foreach (Entity part in animatedParts) part.Animator.SetFloat(name, value); }
        void SetInteger(string name, int value) { foreach (Entity part in animatedParts) part.Animator.SetInteger(name, value); }
        void Trigger(string name) { foreach (Entity part in animatedParts) part.Animator.SetTrigger(name); }

        void UpdateAnimation()
        {
            if (body == null || !IsReady) return;

            // The body's real velocity: physics moves a dynamic golem, the agent a kinematic one.
            Vector3 velocity = HasComponent<RigidBody>() && !RigidBody.IsKinematic
                ? RigidBody.Velocity
                : (agent != null ? agent.Velocity : Vector3.Zero);
            velocity = new Vector3(velocity.X, 0f, velocity.Z);
            float speed = velocity.Length;

            Vector3 left = Transform.WorldRotation * new Vector3(1f, 0f, 0f);
            float strafe = speed > 0.01f ? Vector3.Dot(velocity, left) / speed : 0f;

            SetFloat("Speed", speed);
            SetFloat("Strafe", strafe);
            if (!warnedNoAnimator && !body.Animator.SetFloat("Speed", speed))
            {
                warnedNoAnimator = true;
                Log.Warning("[Enemy] " + Entity.Name + ": its \"Body\" has no Animator with a Speed parameter - no walk animation");
            }
        }

        // Called from OnDamaged.
        void AnimateHit()
        {
            if (body == null) return;

            // Which side the hit came from: towards its target if it has one, else from
            // opposite the knockback, else the front.
            Vector3 from = Vector3.Zero;
            if (!ReferenceEquals(target, null) && target.IsAlive) from = Flat(target.Transform.WorldPosition - Position);
            else if (LastKnockback.Length > 1e-3f) from = Flat(LastKnockback) * -1f;

            int direction = 0;
            if (from.Length > 1e-3f)
            {
                from = from.Normalized;
                float ahead = Vector3.Dot(from, Facing);
                float side = Vector3.Dot(from, Transform.WorldRotation * new Vector3(1f, 0f, 0f));
                if (ahead >= 0.7071f) direction = 0;          // within 45 degrees of straight ahead
                else if (ahead <= -0.7071f) direction = 1;    // within 45 degrees of straight behind
                else direction = side > 0f ? 2 : 3;
            }
            SetInteger("HitDirection", direction);
            Trigger("Hit");
        }

        // Called from EnemyWeapon at the three moments of an attack.
        void AnimateAttackStart()
        {
            if (previewSpinnerWindup)
            {
                if (attackAnimation == "Spinner") Trigger("Spin");
                return;
            }
            if (attackAnimation == "Slash") Trigger("Attack");
            else if (attackAnimation == "Cannon") Trigger("Charge");
            if (melee && WeaponSwingTime <= 0f) SwingWeapon();   // else EnemyWeapon starts it so it connects on the hit
        }

        // The equipped melee weapon's own Fire clip (MaceArm_Fire, SwordArm_Fire). Once per attack.
        void SwingWeapon()
        {
            if (weaponSwung) return;
            weaponSwung = true;
            equippedWeaponAnimator?.SetTrigger("Fire");
        }

        void AnimateAttackRelease()
        {
            if (attackAnimation == "Cannon") Trigger("Fire");
            else if (!melee) equippedWeaponAnimator?.SetTrigger("Fire");
        }

        void AnimateAttackCancel()
        {
            if (attackAnimation == "Slash" || attackAnimation == "Cannon" || attackAnimation == "Spinner") Trigger("Cancel");
        }

    }
}
