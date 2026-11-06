using System;
using Crucible;

namespace Crucible.Scripts
{
    // The golem enemy's one script. In Gam3333 (Unity) these were four MonoBehaviours on the
    // enemy: CombatHealth, EnemyCombatFeedback (hit flash), HealthBarDisplay and
    // EnemyPartDrop. A Crucible entity holds only one script, so they are merged here, and
    // this derives from CombatHealth so damage code finds it with GetComponent<CombatHealth>().
    //
    // Movement and decisions are NOT here: the FlowFieldAgent moves it and the
    // StateGraphAgent's graph decides (Unity's EnemyAgent + EnemyStates + EnemyAttack).
    //
    // Children it looks for by name (the Golem prefabs have them):
    //   "Body"       - the visible mesh, with a MaterialParams component, for the hit flash
    //   "Health Bar" - a thin cube (MeshRenderer) this script moves and stretches each frame
    public sealed class Enemy : CombatHealth
    {
        // Crossbow golems: the bolt the Attack Target node fires. A node can't hold a Prefab
        // field, so it lives here (in Unity it was on EnemyAttack). The bolt's damage is a
        // field on the bolt prefab itself.
        public Prefab boltPrefab;

        // Hit flash (EnemyCombatFeedback).
        public bool hitFlash = true;
        public float hitFlashDuration = 0.08f;

        // Health bar (HealthBarDisplay): a thin bar floating above the enemy, always square on
        // to the camera, full length at full health and shrinking from the right. Its colour
        // and thickness are the "Health Bar" child's MeshRenderer colour and Y/Z scale.
        public bool healthBar = true;
        public float healthBarHeight = 1.4f;  // above the enemy's position (its middle), as in Unity
        public float healthBarLength = 2f;    // metres at full health

        // Part drop (EnemyPartDrop): a chance to spawn a loose part when it dies.
        public Prefab dropPrefab;
        public float dropChancePercent = 25f;
        public float dropSpawnHeight = 1f;
        public float dropPopVelocity = 3f;    // small upward pop so the drop reads as an event; 0 = just falls

        // Unity's hit colour (2, 1.3, 0.6). Tint multiplies the base colour, so values above
        // 1 brighten it, like Unity's HDR colour did.
        static readonly Color HitColor = new Color(2f, 1.3f, 0.6f);

        static readonly Random random = new Random();

        Entity body, bar;
        Vector3 barScale;                     // the bar's prefab scale: Y and Z (thickness) are kept
        float flashRemaining;

        public override void OnCreate()
        {
            base.OnCreate();                  // full health first: CombatHealth's Awake

            body = Entity.FindChild("Body");
            bar = Entity.FindChild("Health Bar");
            if (hitFlash && (body == null || !body.HasComponent<MaterialParams>()))
                Log.Warning("[Enemy] " + Entity.Name + ": no child \"Body\" with MaterialParams - hit flash is off");
            if (healthBar && (bar == null || !bar.HasComponent<MeshRenderer>()))
                Log.Warning("[Enemy] " + Entity.Name + ": no child \"Health Bar\" with a MeshRenderer - health bar is off");
            if (bar != null) barScale = bar.Transform.Scale;
        }

        public override void OnUpdate(float deltaTime)
        {
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

        protected override void OnDamaged(float amount)
        {
            if (!hitFlash || body == null || !body.HasComponent<MaterialParams>()) return;
            body.MaterialParams.Tint = HitColor;
            flashRemaining = hitFlashDuration;
        }

        protected override void OnDied()
        {
            // Unity's death effect (CombatFeedback particles) is not ported yet.
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
    }
}
