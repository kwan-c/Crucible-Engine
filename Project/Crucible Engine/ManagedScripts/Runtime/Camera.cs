/*************************************************************************
* \file:         Camera.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Script access to cameras (the Camera component) and screen
*                maths: rays from the mouse, world -> screen positions.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*        Camera cam = Camera.Main;                              // what the game is seen through
*        Ray ray;
*        if (cam.ScreenPointToRay(Input.MousePosition, out ray) ||   // mouse over the Game view
*            cam.ViewportPointToRay(new Vector2(0.5f, 0.5f), out ray)) // or the screen centre (locked cursor)
*        {
*            RaycastHit hit;
*            if (Physics.Raycast(ray, out hit, 200f)) ...
*        }
*        Vector2 bar;
*        if (cam.WorldToScreenPoint(enemy.Transform.WorldPosition, out bar)) ... // health bar position
*
*    Screen positions are game-screen pixels with the origin at the TOP-LEFT
*    (the UI's space; Camera.ScreenSize is its size). ScreenPointToRay is the
*    exception: it takes WINDOW pixels, exactly what Input.MousePosition gives,
*    and maps them onto the Game view itself.
*
*    A camera looks down its local -Z (Transform.Forward). The enabled camera
*    with the highest Priority is the one that renders (Camera.Main).
*
*    C++ side: Engine/src/Scripting/Bindings/CameraBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>A line starting at Origin going along Direction (length 1). Unity's Ray.</summary>
    public struct Ray
    {
        public Vector3 Origin;
        public Vector3 Direction;
        public Ray(Vector3 origin, Vector3 direction) { Origin = origin; Direction = direction.Normalized; }
        /// <summary>The point <paramref name="distance"/> metres along the ray.</summary>
        public Vector3 GetPoint(float distance) { return Origin + Direction * distance; }
        public override string ToString() { return "Ray from " + Origin + " along " + Direction; }
    }

    /// <summary>An entity's Camera component. Unity's Camera.</summary>
    public sealed class Camera
    {
        private readonly EntityIdentity entity;
        internal Camera(EntityIdentity owner) { entity = owner; }

        private int I { get { return (int)entity.Index; } }
        private int G { get { return (int)entity.Generation; } }

        /// <summary>The entity this camera is on (move or turn it through Entity.Transform).</summary>
        public Entity Entity { get { return new Entity(entity.Index, entity.Generation); } }

        /// <summary>
        /// The camera the game is seen through: the enabled one with the highest Priority.
        /// Null if there is none. Looked up each call, so cache it in OnCreate if you use it a lot.
        /// </summary>
        public static Camera Main
        {
            get
            {
                int index, generation;
                return GetMainInternal(out index, out generation) != 0
                    ? new Camera(new EntityIdentity(unchecked((uint)index), unchecked((uint)generation)))
                    : null;
            }
        }

        /// <summary>The game screen's size in pixels (the Game view in the editor, the window in the game).</summary>
        public static Vector2 ScreenSize
        {
            get { Vector2 v; return GetScreenSizeInternal(out v) != 0 ? v : Vector2.Zero; }
        }

        // --- settings ----------------------------------------------------------- //

        /// <summary>Vertical field of view in degrees (zoom in by lowering it).</summary>
        public float FieldOfView
        {
            get { float v; return GetFieldOfViewInternal(I, G, out v) != 0 ? v : 60f; }
            set { SetFieldOfViewInternal(I, G, ref value); }
        }

        /// <summary>Nearest distance drawn, in metres.</summary>
        public float NearClip
        {
            get { float v; return GetNearClipInternal(I, G, out v) != 0 ? v : 0.1f; }
            set { SetNearClipInternal(I, G, ref value); }
        }

        /// <summary>Farthest distance drawn, in metres.</summary>
        public float FarClip
        {
            get { float v; return GetFarClipInternal(I, G, out v) != 0 ? v : 1000f; }
            set { SetFarClipInternal(I, G, ref value); }
        }

        /// <summary>The enabled camera with the highest Priority renders. Raise it to switch to this camera.</summary>
        public int Priority
        {
            get { int v; return GetPriorityInternal(I, G, out v) != 0 ? v : 0; }
            set { SetPriorityInternal(I, G, value); }
        }

        /// <summary>A disabled camera never renders.</summary>
        public bool Enabled
        {
            get { int v; return GetEnabledInternal(I, G, out v) != 0 && v != 0; }
            set { SetEnabledInternal(I, G, value ? 1 : 0); }
        }

        // --- screen maths ------------------------------------------------------- //

        /// <summary>
        /// A ray from the camera through a WINDOW position - pass Input.MousePosition.
        /// False if that point is not on the game screen (in the editor: the mouse is not
        /// over the Game view).
        /// </summary>
        public bool ScreenPointToRay(Vector2 windowPosition, out Ray ray)
        {
            Vector3 origin, direction;
            bool ok = ScreenPointToRayInternal(I, G, ref windowPosition, out origin, out direction) != 0;
            ray = ok ? new Ray(origin, direction) : new Ray();
            return ok;
        }

        /// <summary>
        /// A ray through a point given as 0..1 across and down the screen; (0.5, 0.5) is the
        /// centre - the aim point while the cursor is locked. False if there is no game screen.
        /// </summary>
        public bool ViewportPointToRay(Vector2 viewportPoint, out Ray ray)
        {
            Vector3 origin, direction;
            bool ok = ViewportPointToRayInternal(I, G, ref viewportPoint, out origin, out direction) != 0;
            ray = ok ? new Ray(origin, direction) : new Ray();
            return ok;
        }

        /// <summary>
        /// Where a world position appears on screen, in game-screen pixels (top-left origin).
        /// False if it is behind the camera. It may be off-screen: compare with ScreenSize.
        /// </summary>
        public bool WorldToScreenPoint(Vector3 worldPosition, out Vector2 screenPosition)
        {
            float depth;
            return WorldToScreenPoint(worldPosition, out screenPosition, out depth);
        }

        /// <summary>As above, plus how far in front of the camera it is, in metres (for scaling markers).</summary>
        public bool WorldToScreenPoint(Vector3 worldPosition, out Vector2 screenPosition, out float depth)
        {
            return WorldToScreenPointInternal(I, G, ref worldPosition, out screenPosition, out depth) != 0;
        }

        public override string ToString() { return "Camera on " + Entity; }

        // --- internal calls (CameraBindings.cpp) -------------------------------- //

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMainInternal(out int index, out int generation);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetScreenSizeInternal(out Vector2 value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetFieldOfViewInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetFieldOfViewInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetNearClipInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetNearClipInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetFarClipInternal(int index, int generation, out float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetFarClipInternal(int index, int generation, ref float value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetPriorityInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetPriorityInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetEnabledInternal(int index, int generation, out int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int SetEnabledInternal(int index, int generation, int value);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int ScreenPointToRayInternal(int index, int generation, ref Vector2 windowPoint,
                                                           out Vector3 origin, out Vector3 direction);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int ViewportPointToRayInternal(int index, int generation, ref Vector2 point,
                                                             out Vector3 origin, out Vector3 direction);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int WorldToScreenPointInternal(int index, int generation, ref Vector3 worldPoint,
                                                             out Vector2 pixels, out float depth);
    }
}
