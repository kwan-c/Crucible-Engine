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
*        Scene.Load("Scenes/Level2.scene");   // a path inside Assets
*        Scene.Reload();                      // restart the current level
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

        /// <summary>Loads the current scene's file again (a restart). False if it was never saved.</summary>
        public static bool Reload()
        {
            return ReloadInternal() != 0;
        }

        /// <summary>The open scene's name.</summary>
        public static string Name
        {
            get { return GetNameInternal() ?? string.Empty; }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int LoadInternal(string path);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int ReloadInternal();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetNameInternal();
    }
}
