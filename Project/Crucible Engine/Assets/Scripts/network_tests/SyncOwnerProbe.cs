/*************************************************************************
* \file:         SyncOwnerProbe.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          26 November 2026
* \version       1.0
* \brief         Networking sample: logs OnOwnershipChanged on every
*                machine.
*
* \details
*    On "Sync Crate" in network_sync_test.scene, running Everywhere. When
*    the host gives the crate to another player (SyncDemo, key 8), every
*    machine logs the change; the crate's Owner only light is on only on
*    the owner's machine.
*
* \history
*    -v1.0       ownership sample
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class SyncOwnerProbe : EntityScript
    {
        public override void OnOwnershipChanged(int previousOwner, int newOwner)
        {
            Log.Info("SyncOwnerProbe: " + Entity.Name + " owner " + previousOwner + " -> " + newOwner
                + (Entity.IsMine ? " (now mine: its light is on here)" : " (not mine)"));
        }
    }
}
