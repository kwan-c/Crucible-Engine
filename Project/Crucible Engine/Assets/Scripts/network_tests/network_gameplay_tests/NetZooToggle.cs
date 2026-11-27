/*************************************************************************
* \file:         NetZooToggle.cs
* \author:       Tok Duan Jie Nelson (ZooToggle, the original);
*                Abhishek Akilandeswari Patchamuthu (this networked copy)
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Networked gameplay test: a copy of ZooToggle. Any player
*                steps on it and presses F; the host flips it and every
*                machine switches toggleObject the same way.
*
* \details
*    The state is NetZooState's variable number stateSlot (give every
*    toggle its own). Each machine applies it to toggleObject every frame;
*    the zoo's waves (NetZooEnemyWaves, host) read the groups' on / off.
*    ZooToggle itself is unchanged.
*
* \history
*    -v1.0       copy of ZooToggle: host decides, state in NetZooState
*
* The original's notes follow.
*
* \brief         Zoo floor switch: step on it and press F to switch an
*                entity on or off. Port of Unity's ZooToggle. In the Zoo it
*                switches the Small / Elite Enemies groups that ZooEnemyWaves
*                watches.
*
* \details
*    toggleObject is an Entity field (set it in the Inspector); it starts
*    switched off, as in Unity.
*
**************************************************************************/
namespace Crucible.Scripts.NetGameplay
{
    public sealed class NetZooToggle : EntityScript
    {
        public Entity toggleObject;
        public int stateSlot = 0;   // NetZooState's variable for this toggle

        NetZooPad pad;

        public override void OnCreate()
        {
            pad = new NetZooPad(this);
            if (toggleObject != null)
                pad.SetText("Step on me and press F\nto toggle " + toggleObject.Name);
            else
                pad.SetText("No GameObject Assigned");
        }

        public override void OnUpdate(float deltaTime)
        {
            NetZooState state = NetZooState.Current;
            if (toggleObject == null || state == null) return;
            if (pad.WasUsed())
            {
                bool next = !state.Get(stateSlot);
                Log.Info("Toggling " + toggleObject.Name + (next ? " on" : " off"));
                state.Set(stateSlot, next);
            }
            bool on = state.Get(stateSlot);   // starts off, as in Unity
            if (toggleObject.IsActiveSelf != on) toggleObject.SetActive(on);
        }

        public override void OnTriggerEnter(Entity other) { pad.Enter(other); }
        public override void OnTriggerExit(Entity other) { pad.Exit(other); }
    }
}
