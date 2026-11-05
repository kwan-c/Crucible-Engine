namespace Crucible.AI
{
    /// <summary>Base of the distance conditions: FlatDistanceTo.</summary>
    public abstract class DistanceCondition : Condition
    {
        /// <summary>
        /// Flat (XZ) metres from the enemy to <paramref name="target"/>, or a negative number if
        /// the target is unset or gone. Height is ignored, like the agents' own distances.
        /// </summary>
        protected float FlatDistanceTo(Entity target)
        {
            if (ReferenceEquals(target, null) || !target.IsAlive || Self == null) return -1f;
            Vector3 a = Self.Transform.WorldPosition;
            Vector3 b = target.Transform.WorldPosition;
            float dx = b.X - a.X, dz = b.Z - a.Z;
            return (float)System.Math.Sqrt(dx * dx + dz * dz);
        }
    }
}
