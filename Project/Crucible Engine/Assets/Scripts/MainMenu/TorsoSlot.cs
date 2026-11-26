/*************************************************************************
* \file:         TorsoSlot.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          25 November 2026
* \version       1.0
* \brief         Goes on each torso prefab of the main menu's torso carousel
*                (TorsoSelector's torso1..torso6): which torso PART that slot
*                is, for the game.
*
* \details
*    The prefab itself carries the rest of the slot: its model (the
*    MeshRenderer) and its colour (a MaterialParams tint). Confirm copies
*    those onto the golem and hands partId to the game (MechPlayer's
*    starting torso), so one prefab = one choice, and a real torso model
*    later is just another prefab.
*
**************************************************************************/
namespace Crucible.Scripts
{
    public sealed class TorsoSlot : EntityScript
    {
        public string partId = "golem_torso";   // a MechLoadout part id (MechPartKind.Torso)

    }   // TORSOSLOT CLASS

}   // NAMESPACE CRUCIBLE.SCRIPTS
