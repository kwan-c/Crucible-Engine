/*************************************************************************
* \file:         Graphics.cs
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          17 November 2026
* \version       1.0
* \brief         What the renderer is doing that a script may wait for.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*        if (Graphics.ShadersReady) FinishLoading();
*
*    A material's shader (a hand-written one, or a shader graph) is turned
*    into pipelines for this GPU in the background the first time it is
*    used, so the frame never stalls. Until its pipelines are ready, its
*    materials draw with the default shader: a new level can show plain
*    surfaces for its first few frames on a machine that has not run it
*    before. A loading screen that waits for ShadersReady before it fades
*    out hides that (LoadingScreen.cs does). Later starts on the same
*    machine are much faster: the pipeline cache keeps what was built.
*
*    C++ side: Engine/src/Scripting/Bindings/GraphicsBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>The renderer's state, for loading screens.</summary>
    public static class Graphics
    {
        /// <summary>
        /// How many materials' shaders are still being built in the background (their materials
        /// draw with the default shader meanwhile). 0 when there is no renderer.
        /// </summary>
        public static int PendingShaderBuilds
        {
            get { int v; return GetPendingShaderBuildsInternal(out v) != 0 ? v : 0; }
        }

        /// <summary>Every material shader in use is built: everything looks as it is meant to.</summary>
        public static bool ShadersReady
        {
            get { return PendingShaderBuilds == 0; }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetPendingShaderBuildsInternal(out int count);
    }
}
