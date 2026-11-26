/*************************************************************************
* \file:         KitState.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Networking sample: the game state object of
*                network_sync_test.scene (NetGameState).
*
* \details
*    On "Kit State", a scene network object that replicates NetVariables.
*    The Host accepts a Client's score request only for 1 point at a time
*    (AllowScoreRequest), to show where the Host decides.
*
* \history
*    -v1.0       the sample game state
*
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class KitState : NetGameState
    {
        protected override bool AllowScoreRequest(int peer, int amount)
        {
            return amount == 1;
        }
    }
}
