/*************************************************************************
* \file:         NetEnemyView.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Networked gameplay test: what a client sees of a golem the
*                host runs - its health bar, hit flash, attack warning lane
*                and death.
*
* \details
*    The second script on the Net Golem prefabs, after Enemy (unchanged).
*    The golem is a network object: the host runs Enemy as usual (its brain,
*    legs and physics are host only - Network Object settings) and its
*    Transform and Animator are replicated.
*
*      HOST    writes the golem's NetVariables each frame: health, maximum,
*              a hit count (one more per hit) and the Telegraph lane (shown,
*              position, rotation, scale).
*      CLIENT  switches its own copy of Enemy off (it would draw a full
*              health bar and fight the replicated animator), then draws:
*                - the health bar (Enemy's HealthBarDisplay, from the synced
*                  health, square on to this machine's camera)
*                - the hit flash when the hit count goes up
*                - the Telegraph lane where the host has it
*                - on death a ragdoll of its own (the host's ragdoll pieces
*                  are not network objects), thrown with the velocity the
*                  host's ragdoll started with (a melee kill's launch, or
*                  the golem's own speed and Enemy's death kick); the golem
*                  itself goes when the host removes it
*
*    Slots (NetVariables): 0 health, 1 maximum, 2 hits, 3 lane shown,
*    4-6 lane position, 7-9 lane scale, 10-13 lane rotation, 14-16 the death
*    launch (written with the health that reaches 0, so they arrive together).
*
* \history
*    -v1.0       health bar, hit flash, lane and death on clients; the host's
*                death launch for the client's ragdoll
*
**************************************************************************/
using System;

namespace Crucible.Scripts.NetGameplay
{
    public sealed class NetEnemyView : EntityScript
    {
        const int VarHealth = 0, VarMaximum = 1, VarHits = 2, VarLaneShown = 3, VarLanePosition = 4, VarLaneScale = 7, VarLaneRotation = 10;
        const int VarLaunch = 14;

        static readonly Color HitColor = new Color(2f, 1.3f, 0.6f);   // Enemy's

        Enemy enemy;
        Entity body, bar, lane;
        Vector3 barScale;
        bool barKnown;

        // host
        float writtenHealth = -1f, writtenMaximum = -1f;
        int hits;
        bool laneWasShown;
        Vector3 lastVelocity, lastKnockback;   // while alive: for the death launch
        bool launchWritten;

        // client
        int seenHits = -1;
        float flashRemaining;
        bool dead;

        public override void OnCreate()
        {
            enemy = GetComponent<Enemy>();
            body = Entity.FindChild("Body");
            bar = Entity.FindChild("Health Bar");
            lane = Entity.FindChild("Telegraph");
            if (bar != null) { barScale = bar.Transform.Scale; barKnown = true; }
            if (enemy == null) Log.Warning("NetEnemyView on " + Entity.Name + ": no Enemy script beside it.");
            // A client's Enemy must not run: the host decides everything (see the file notes).
            if (!Network.IsAuthority && enemy != null) enemy.Enabled = false;
        }

        public override void OnUpdate(float deltaTime)
        {
            if (enemy == null) return;
            if (Network.IsAuthority) Write();
            else Draw(deltaTime);
        }

        // --- host -------------------------------------------------------------------------- //

        void Write()
        {
            if (!Network.IsOnline) return;
            NetVars vars = Entity.NetVars;
            float health = enemy.CurrentHealth, maximum = enemy.MaximumHealth;
            if (health > 0f)
            {
                if (HasComponent<RigidBody>()) lastVelocity = RigidBody.Velocity;
                lastKnockback = enemy.LastKnockback;
            }
            else if (!launchWritten)
            {
                // Before the health: the client reads the launch when it sees the death.
                vars.SetVector3(VarLaunch, DeathLaunch());
                launchWritten = true;
            }
            if (health < writtenHealth - 0.001f) vars.SetInt(VarHits, ++hits);
            if (Math.Abs(health - writtenHealth) > 0.001f && vars.SetFloat(VarHealth, health)) writtenHealth = health;
            if (Math.Abs(maximum - writtenMaximum) > 0.001f && vars.SetFloat(VarMaximum, maximum)) writtenMaximum = maximum;

            bool shown = lane != null && lane.HasComponent<MeshRenderer>() && lane.MeshRenderer.Visible;
            if (shown != laneWasShown) { vars.SetBool(VarLaneShown, shown); laneWasShown = shown; }
            if (!shown) return;
            Transform t = lane.Transform;
            Quaternion r = t.WorldRotation;
            vars.SetVector3(VarLanePosition, t.WorldPosition);
            vars.SetVector3(VarLaneScale, t.Scale);
            vars.SetFloat(VarLaneRotation, r.X); vars.SetFloat(VarLaneRotation + 1, r.Y);
            vars.SetFloat(VarLaneRotation + 2, r.Z); vars.SetFloat(VarLaneRotation + 3, r.W);
        }

        // The velocity Enemy.StartRagdoll gave the host's ragdoll. A melee kill is the only hit
        // that comes with a knockback (Enemy.ApplyMeleeHit: the launch x the golem's mass); any
        // other death keeps the golem's own speed (at most 15 m/s) with a kick backwards.
        Vector3 DeathLaunch()
        {
            Vector3 knockback = enemy.LastKnockback;
            bool meleeKill = knockback.Length > 0f && (knockback - lastKnockback).Length > 1e-3f;
            float mass = HasComponent<RigidBody>() ? RigidBody.Mass : 0f;
            if (meleeKill && mass > 0f) return knockback * (1f / mass);
            Vector3 own = lastVelocity.Length > 15f ? lastVelocity.Normalized * 15f : lastVelocity;
            return own - enemy.Facing * enemy.deathKick + Vector3.Up * 0.5f;
        }

        // --- client ------------------------------------------------------------------------ //

        void Draw(float deltaTime)
        {
            NetVars vars = Entity.NetVars;
            float maximum = vars.GetFloat(VarMaximum);
            if (maximum <= 0f) return;   // nothing from the host yet
            float health = vars.GetFloat(VarHealth);

            int count = vars.GetInt(VarHits);
            if (seenHits >= 0 && count != seenHits && enemy.hitFlash && body != null && body.HasComponent<MaterialParams>())
            {
                body.MaterialParams.Tint = HitColor;
                flashRemaining = enemy.hitFlashDuration;
            }
            seenHits = count;
            if (flashRemaining > 0f)
            {
                flashRemaining -= deltaTime;
                if (flashRemaining <= 0f && body != null && body.HasComponent<MaterialParams>()) body.MaterialParams.ClearTint();
            }

            if (!dead && health <= 0f) Die(vars.GetVector3(VarLaunch));
            DrawLane(vars);
            if (!dead && enemy.healthBar && bar != null && bar.HasComponent<MeshRenderer>()) DrawHealthBar(health, maximum);
        }

        void DrawLane(NetVars vars)
        {
            if (lane == null || !lane.HasComponent<MeshRenderer>()) return;
            bool shown = !dead && vars.GetBool(VarLaneShown);
            if (lane.MeshRenderer.Visible != shown) lane.MeshRenderer.Visible = shown;
            if (!shown) return;
            lane.Transform.WorldPosition = vars.GetVector3(VarLanePosition);
            lane.Transform.WorldRotation = new Quaternion(vars.GetFloat(VarLaneRotation), vars.GetFloat(VarLaneRotation + 1),
                                                          vars.GetFloat(VarLaneRotation + 2), vars.GetFloat(VarLaneRotation + 3));
            lane.Transform.Scale = vars.GetVector3(VarLaneScale);
        }

        // Enemy.UpdateHealthBar with the host's health: square on to this machine's camera.
        void DrawHealthBar(float health, float maximum)
        {
            if (!barKnown) return;
            Camera view = Camera.Main;
            Quaternion facing = view != null ? view.Entity.Transform.WorldRotation : Quaternion.Identity;
            Vector3 right = facing * new Vector3(1f, 0f, 0f);
            float length = enemy.healthBarLength * health / maximum;
            Vector3 start = Transform.WorldPosition + Vector3.Up * enemy.healthBarHeight - right * (enemy.healthBarLength * 0.5f);
            bar.Transform.WorldPosition = start + right * (length * 0.5f);
            bar.Transform.WorldRotation = facing;
            bar.Transform.Scale = new Vector3(Math.Max(length, 0.001f), barScale.Y, barScale.Z);
        }

        // The host's golem has died: this machine's own ragdoll, thrown as the host's was (or, if
        // it cannot be built, the golem just disappears). The host removes the golem itself later.
        void Die(Vector3 launch)
        {
            dead = true;
            if (bar != null && bar.HasComponent<MeshRenderer>()) bar.MeshRenderer.Visible = false;
            if (flashRemaining > 0f && body != null && body.HasComponent<MaterialParams>()) body.MaterialParams.ClearTint();
            if (enemy.ragdollOnDeath && body != null && Ragdoll.Spawn(body, launch)) return;
            if (body != null) body.SetActive(false);
        }
    }
}
