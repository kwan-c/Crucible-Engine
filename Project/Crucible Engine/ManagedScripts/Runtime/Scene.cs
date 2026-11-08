/*************************************************************************
* \file:         Scene.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Scene loading for scripts. Part of Crucible.ScriptCore.dll.
*
* \details
*    Unity's SceneManager.LoadScene, by file:
*
*        Scene.Load("Scenes/Level2.scene");   // a path inside Assets (behind the loading screen)
*        Scene.LoadImmediate(path);           // skip the loading screen
*        Scene.Reload();                      // restart the current level
*        Scene.DontDestroyOnLoad(Entity);     // keep this root entity across loads
*        Scene.BeginPreload(path);            // loading screens: load the next scene's
*        float p = Scene.StepPreload();       //   assets over several frames first
*
*    The switch happens at the start of the next frame, not immediately, so
*    the rest of this frame runs as normal. Every script in the old scene
*    then gets OnDestroy and the new scene's scripts start. Static fields and
*    Time.ElapsedTime carry over; nothing else does. Stop still returns the
*    Editor to the scene that was open when Play was pressed.
*
*    C++ side: Engine/src/Scripting/Bindings/SceneBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>Loading scenes. Unity's SceneManager.</summary>
    public static class Scene
    {
        /// <summary>
        /// Switches to another scene at the start of the next frame.
        /// <paramref name="path"/> is inside Assets, e.g. "Scenes/Level2.scene".
        /// Returns false (and logs why) if the file is missing or not a .scene.
        /// </summary>
        public static bool Load(string path)
        {
            return !string.IsNullOrEmpty(path) && LoadInternal(path) != 0;
        }

        /// <summary>
        /// Like Load, but skips the loading screen: the switch happens at the start of the next
        /// frame no matter what. For an instant restart and the like.
        /// </summary>
        public static bool LoadImmediate(string path)
        {
            return !string.IsNullOrEmpty(path) && LoadImmediateInternal(path) != 0;
        }

        // ----------------------------------------------------------------------------------
        //  For the loading screen (Assets/Scripts/LoadingScreen.cs). The engine spawns
        //  Prefabs/LoadingScreen.cprefab at the start of every play session; its script claims
        //  scene changes, so Load / Reload / a UIButton's LoadScene are HELD until it lets them
        //  through. Gameplay scripts just call Load as usual.
        // ----------------------------------------------------------------------------------

        /// <summary>Takes (true) or gives back (false) every scene change. Giving back lets a held one through.</summary>
        public static void ClaimLoads(bool claimed)
        {
            ClaimLoadsInternal(claimed ? 1 : 0);
        }

        /// <summary>A scene change is waiting for the loading screen.</summary>
        public static bool HasHeldLoad
        {
            get { return HasHeldLoadInternal() != 0; }
        }

        /// <summary>Changes whenever a newer scene change replaces the held one.</summary>
        public static int HeldLoadSerial
        {
            get { return HeldLoadSerialInternal(); }
        }

        /// <summary>Starts preloading the held scene's assets (then StepPreload). Returns how many were queued.</summary>
        public static int BeginPreloadHeld()
        {
            return BeginPreloadHeldInternal();
        }

        /// <summary>Lets the held scene change through; it happens at the start of the next frame.</summary>
        public static void ReleaseHeldLoad()
        {
            ReleaseHeldLoadInternal();
        }

        /// <summary>Loads the current scene's file again (a restart). False if it was never saved.</summary>
        public static bool Reload()
        {
            return ReloadInternal() != 0;
        }

        /// <summary>
        /// Starts loading the assets <paramref name="path"/> uses (models, materials, textures,
        /// fonts, prefabs...) while the current scene is still open - for a loading screen.
        /// Call StepPreload every frame until it returns 1, then Load the same scene: what was
        /// preloaded stays loaded through the switch. Returns how many assets were queued
        /// (0 = all loaded already), or -1 (and logs why) if the scene isn't in the project.
        /// </summary>
        public static int BeginPreload(string path)
        {
            return string.IsNullOrEmpty(path) ? -1 : BeginPreloadInternal(path);
        }

        /// <summary>
        /// Loads queued assets for about <paramref name="budgetMs"/> milliseconds (at least one
        /// asset) and returns the progress, 0-1. Weighted by file size, so the value follows the
        /// loading time rather than the number of files.
        /// </summary>
        public static float StepPreload(float budgetMs = 8f)
        {
            return StepPreloadInternal(budgetMs);
        }

        /// <summary>How much of the current preload is done, 0-1 (1 when nothing is queued).</summary>
        public static float PreloadProgress
        {
            get { return GetPreloadProgressInternal(); }
        }

        /// <summary>Drops what is still queued; assets already loaded stay loaded.</summary>
        public static void CancelPreload()
        {
            CancelPreloadInternal();
        }

        /// <summary>The open scene's name.</summary>
        public static string Name
        {
            get { return GetNameInternal() ?? string.Empty; }
        }

        /// <summary>
        /// Unity's DontDestroyOnLoad: keeps <paramref name="entity"/> and its children alive
        /// through Scene.Load / Scene.Reload, scripts and all. Only works on a root entity
        /// during Play. Lasts until it is destroyed or Play stops. Returns false (and logs why)
        /// if it could not be marked.
        /// </summary>
        /// <remarks>
        /// Reloading the scene the entity came from brings a second copy in with the scene.
        /// Guard against that the usual way: a static Instance, and Destroy(Entity) in the
        /// copy's OnCreate if one already exists.
        /// </remarks>
        public static bool DontDestroyOnLoad(Entity entity)
        {
            return entity != null
                && DontDestroyOnLoadInternal((int)entity.Index, (int)entity.Generation) != 0;
        }

        /// <summary>Whether a scene change will keep this entity (it or a parent was marked).</summary>
        public static bool IsDontDestroyOnLoad(Entity entity)
        {
            return entity != null
                && IsDontDestroyOnLoadInternal((int)entity.Index, (int)entity.Generation) != 0;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int LoadInternal(string path);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int ReloadInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetNameInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int BeginPreloadInternal(string path);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float StepPreloadInternal(float budgetMs);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetPreloadProgressInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void CancelPreloadInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int LoadImmediateInternal(string path);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ClaimLoadsInternal(int claimed);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int HasHeldLoadInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int HeldLoadSerialInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int BeginPreloadHeldInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ReleaseHeldLoadInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int DontDestroyOnLoadInternal(int index, int generation);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsDontDestroyOnLoadInternal(int index, int generation);
    }
}
