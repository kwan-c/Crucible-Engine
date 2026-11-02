/*************************************************************************
* \file:         Collision.cs
* \author:       <author>
* \par:          Email: <email>
* \date          2 November 2026
* \version       1.0
* \brief         What EntityScript.OnCollisionEnter / OnCollisionExit get.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*        public override void OnCollisionEnter(Collision hit)
*        {
*            if (hit.ImpactSpeed > 5f) TakeDamage(hit.ImpactSpeed);
*            var enemy = hit.Other.GetComponent<Enemy>();
*        }
*
*    From Hugo's PhysicsEvents::CollisionEnter (Physics/PhysicsEvents.h).
*
**************************************************************************/
namespace Crucible
{
    /// <summary>A collision as seen from one of the two entities. Unity's Collision.</summary>
    public struct Collision
    {
        /// <summary>The other entity. It may have been destroyed since - check IsAlive.</summary>
        public Entity Other { get; private set; }
        /// <summary>Where they touched, world space (zero for OnCollisionExit).</summary>
        public Vector3 Point { get; private set; }
        /// <summary>The contact normal, pointing from this entity toward Other (zero for exit).</summary>
        public Vector3 Normal { get; private set; }
        /// <summary>How fast they closed in along the normal, m/s - scale damage or volume by it.</summary>
        public float ImpactSpeed { get; private set; }

        internal Collision(Entity other, Vector3 point, Vector3 normal, float impactSpeed) : this()
        {
            Other = other;
            Point = point;
            Normal = normal;
            ImpactSpeed = impactSpeed;
        }

        public override string ToString()
        {
            return "Collision with " + Other + " at " + Point + " (" + ImpactSpeed.ToString("0.0") + " m/s)";
        }
    }
}
