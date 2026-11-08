/*************************************************************************
* \file:         CursorLock.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          8 November 2026
* \version       1.0
* \brief         Sets the cursor lock a scene starts with.
*
* \details
*    The engine never locks the cursor by itself. Put this on one entity
*    per scene: Locked on for gameplay (mouse-look), off for a menu scene.
*    The request carries over Scene.Load, so a menu scene reached from a
*    level needs one with Locked off.
*
*    A menu canvas, or the editor's release key / Pause, can still hold
*    the cursor free; this request applies again once they let go.
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class CursorLock : EntityScript
    {
        /// <summary>True: hide and capture the cursor (gameplay). False: keep it free (menus).</summary>
        public bool locked = true;

        public override void OnCreate()
        {
            Input.SetMouseLocked(locked);
        }
    }
}
