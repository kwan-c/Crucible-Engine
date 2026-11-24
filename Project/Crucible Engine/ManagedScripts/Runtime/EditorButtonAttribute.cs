/*************************************************************************
* \file:         EditorButtonAttribute.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          11 November 2026
* \version       1.0
* \brief         [EditorButton]: shows a script method as a button in the
*                Inspector that runs it outside Play. Unity's [ContextMenu].
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Put it on a public method with no parameters that returns void:
*
*        [EditorButton]
*        public void Generate() { ... }
*
*    Clicking the button builds a temporary copy of the script on that
*    entity, with the fields as the Inspector shows them, and calls only
*    that method: OnCreate, OnUpdate and OnDestroy never run. Entity calls
*    (GetComponent, Transform, Terrain.Build ...) work during it; systems
*    that only exist in Play (physics, navigation, audio) do nothing, so
*    check Application.IsPlaying before using them.
*
*    What the method changes in the scene is real: components it writes
*    are saved with the scene, and the change can't be undone. Runtime-only
*    data (a Terrain's grid) is never saved, so it is safe to preview.
*
*    C++ side: ScriptRuntime::GetEditorButtons / RunEditorButton, and the
*    Inspector's script section (Editor/.../ImGuiPanel_Inspector.cpp).
**************************************************************************/
using System;

namespace Crucible
{
    /// <summary>Shows this method as an Inspector button that runs it outside Play (Unity's [ContextMenu]). Public, no parameters, void.</summary>
    [AttributeUsage(AttributeTargets.Method, AllowMultiple = false, Inherited = true)]
    public sealed class EditorButtonAttribute : Attribute
    {
    }
}
