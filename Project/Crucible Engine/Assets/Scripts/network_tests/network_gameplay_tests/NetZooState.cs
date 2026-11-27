/*************************************************************************
* \file:         NetZooState.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Networked gameplay test: the zoo region's shared switches.
*                Each NetZooToggle keeps its on / off in one of this
*                object's network variables (its stateSlot), so every
*                machine sees the same groups switched on.
*
* \details
*    On "Zoo State" in net_gameplay_test.scene: a scene network object
*    that replicates NetVariables. The host writes, everyone reads. Offline
*    the values are kept here (there is nothing to send).
*
* \history
*    -v1.0       toggle states for the zoo region
*
**************************************************************************/
namespace Crucible.Scripts.NetGameplay
{
    public sealed class NetZooState : EntityScript
    {
        public static NetZooState Current { get; private set; }

        readonly bool[] local = new bool[NetVars.SlotCount];

        public override void OnCreate() { Current = this; }
        public override void OnDestroy() { if (Current == this) Current = null; }

        public bool Get(int slot)
        {
            if (slot < 0 || slot >= NetVars.SlotCount) return false;
            return Network.IsOnline ? Entity.NetVars.GetBool(slot) : local[slot];
        }

        /// <summary>The host (or offline) only.</summary>
        public void Set(int slot, bool on)
        {
            if (!Network.IsAuthority || slot < 0 || slot >= NetVars.SlotCount) return;
            local[slot] = on;
            if (Network.IsOnline) Entity.NetVars.SetBool(slot, on);
        }
    }
}
