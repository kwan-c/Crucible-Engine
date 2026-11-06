using Crucible;

namespace Crucible.Scripts
{
    // The crossbow golem's bolt (Unity's MechProjectile, enemy side). Spawned by the
    // Attack Target node already turned towards its aim point; it flies straight along its
    // Transform.Forward and never homes, like the Unity bolt.
    //
    // It needs no collider: each frame it casts a ray over the distance it is about to move,
    // so a fast bolt can't skip through a thin wall between two frames.
    //
    // Speed and damage are set on the bolt prefab, not by the attacker: a spawned prefab's
    // script only exists from the next frame, so the attacker can't hand it values.
    public sealed class Bolt : EntityScript
    {
        public float speed = 18f;
        public float damage = 12f;
        public float maxDistance = 25f;      // Unity: Mathf.Max(25, attackRange * 2)

        float travelled;

        public override void OnUpdate(float deltaTime)
        {
            Vector3 direction = Transform.Forward;
            float step = speed * deltaTime;

            RaycastHit hit;
            if (Physics.Raycast(Transform.WorldPosition, direction, out hit, step, Entity))
            {
                // Enemies don't hurt each other (no friendly fire), as in Gam3333.
                CombatHealth target = CombatHealth.Find(hit.Entity);
                if (target != null && !(target is Enemy)) target.ApplyDamage(damage);
                Destroy(Entity);              // spent on whatever it hit, wall or player
                return;
            }

            // A root entity, so its local position is its world position.
            Transform.Position = Transform.Position + direction * step;
            travelled += step;
            if (travelled >= maxDistance) Destroy(Entity);
        }
    }
}
