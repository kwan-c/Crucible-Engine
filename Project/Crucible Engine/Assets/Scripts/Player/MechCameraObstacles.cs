/*************************************************************************
* \file:         MechCameraObstacles.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          18 November 2026
* \version       1.0
* \brief         Keeps the third-person camera out of walls. Port of Unity's
*                MechCameraObstacles (with CameraControlThirdPerson's
*                Obstacle Avoidance settings).
*
* \details
*    A plain object owned by MechPlayer (one script per entity). The rig:
*
*        Camera Pivot   (CameraOrbit; the mouse turns it)
*        '- Camera      authored at a local spot such as (1.4, 0.6, 9):
*                       right of the shoulder, a little up, 9 m back
*
*    Unity's Third Person Follow rig has two segments, and both are kept:
*    pivot -> "hand" (the shoulder offset, the camera's spot with Z = 0) and
*    hand -> camera (the boom straight back along the pivot's +Z). A ball of
*    the camera's radius is swept along each, against level geometry only
*    (the NonMoving layer): the mech itself, enemies, corpses and trigger
*    pickups never pull the camera in. The hand is corrected at once; the
*    camera snaps in at once and eases back out (returnDamping).
*
*    Limits: scripts run before physics, so the mech can still move up to
*    one physics step after this runs (a fast dash backwards can touch a wall
*    for one frame). A ball already inside a wall at the start of a segment
*    puts the end at the start (Unity pushes it out with ComputePenetration,
*    which the engine does not have). The rig is unscaled (Player and Camera
*    Pivot at scale 1), so local metres are world metres.
*
**************************************************************************/
using System;

namespace Crucible.Scripts
{
    public sealed class MechCameraObstacles
    {
        // Unity's CameraControlThirdPerson defaults. MechPlayer copies its Inspector fields in.
        public float CameraRadius = 0.35f;     // minimum clearance from a wall, metres
        public float ReturnDamping = 0.25f;    // seconds to ease back out (99% of the way)

        float correction;                      // how far the camera is pulled in along the boom now
        bool started;

        /// <summary>
        /// Where the camera should sit, in the pivot's local frame. rest is its authored local
        /// position; ignore is the mech (skipped by the casts as well as by the layer).
        /// </summary>
        public Vector3 Resolve(Transform pivot, Vector3 rest, Camera camera, Entity ignore, float deltaTime)
        {
            Vector3 root = pivot.WorldPosition;
            Quaternion rotation = pivot.WorldRotation;
            Vector3 hand = root + rotation * new Vector3(rest.X, rest.Y, 0f);
            Vector3 end = root + rotation * rest;
            float radius = Radius(camera);

            // Segment 1, pivot -> hand: corrected at once, with a slightly bigger ball so the
            // boom never starts touching the wall it is about to be checked against.
            float handCorrection = 0f;
            hand = Sweep(root, hand, radius * 1.05f, -1f, ref handCorrection, ignore);
            // Segment 2, hand -> camera: snaps in, eases out.
            Vector3 placed = Sweep(hand, end, radius, started ? deltaTime : -1f, ref correction, ignore);
            started = true;

            // Back into the pivot's frame, where the camera child lives.
            return rotation.Inverse * (placed - root);
        }

        // Unity's Resolve: sweep a ball from start to end, and keep it short of the first wall.
        // displacement is how far back from end the result sits; dt < 0 means "jump straight there".
        Vector3 Sweep(Vector3 start, Vector3 end, float radius, float dt, ref float displacement, Entity ignore)
        {
            Vector3 boom = end - start;
            float length = boom.Length;
            if (length < 1e-4f) return end;
            Vector3 direction = boom * (1f / length);

            // How far back from end the ball must stop: 0 when nothing is in the way.
            float desired = 0f;
            RaycastHit hit;
            if (Physics.SphereCast(start, radius, direction, out hit, length,
                                   LayerMask.GetMask(PhysicsLayer.NonMoving), ignore))
                desired = length - Math.Max(0f, hit.Distance);   // Distance 0: already touching at the start

            // Into the wall at once (Unity's DampingIntoCollision 0), so the view never shows its
            // inside; back out smoothly (DampingFromCollision), so it doesn't pop when the wall ends.
            if (dt < 0f || desired > displacement) displacement = desired;
            else displacement += (desired - displacement) * DampFraction(dt);
            return end - direction * displacement;
        }

        // Cinemachine's Damper.Damp: the fraction of the gap closed this frame, such that 99% of
        // it is closed after ReturnDamping seconds whatever the frame rate (ln 0.01 = -4.605).
        float DampFraction(float dt)
        {
            if (ReturnDamping < 1e-4f) return 1f;   // no damping: straight back out
            return 1f - (float)Math.Exp(-4.605170186 * dt / ReturnDamping);
        }

        // Unity's CameraCollisionRadius: a clear camera centre is not enough, the corners of the
        // near plane can still cross a wall (wide screens, the dash FOV kick). Never less than
        // CameraRadius.
        float Radius(Camera camera)
        {
            float near = Math.Max(0.001f, camera.NearClip);
            float halfHeight = near * (float)Math.Tan(MechMath.Clamp(camera.FieldOfView, 1f, 179f) * 0.5f * Math.PI / 180.0);
            Vector2 screen = Camera.ScreenSize;
            float aspect = screen.Y > 0f ? screen.X / screen.Y : 16f / 9f;
            float corner = (float)Math.Sqrt(halfHeight * halfHeight * (1f + aspect * aspect) + near * near);
            return Math.Max(CameraRadius, corner + 0.02f);
        }
    }
}
