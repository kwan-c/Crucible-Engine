/*************************************************************************
* \file:         Application.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          11 November 2026
* \version       1.0
* \brief         Unity's Application.isPlaying. Part of
*                Crucible.ScriptCore.dll.
*
* \details
*    Scripts normally only run in Play, so this is false only inside an
*    [EditorButton] method run from the Inspector: use it there to skip
*    what needs Play (physics, navigation, moving the player).
*
*    C++ side: Engine/src/Scripting/Bindings/ApplicationBindings.cpp.
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>The engine's run state (Unity's Application).</summary>
    public static class Application
    {
        /// <summary>True during a Play session (Playing or Paused); false in an [EditorButton] call.</summary>
        public static bool IsPlaying { get { return IsPlayingInternal() != 0; } }

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int IsPlayingInternal();
    }
}
