/*************************************************************************
* \file:         ZooToggle.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \date          8 November 2026
* \version       1.0
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
namespace Crucible.Scripts
{
    public sealed class ZooToggle : EntityScript
    {
        public Entity toggleObject;

        ZooPad pad;
        bool hidden;

        public override void OnCreate()
        {
            pad = new ZooPad(this);
            if (toggleObject != null)
                pad.SetText("Step on me and press F\nto toggle " + toggleObject.Name);
            else
                pad.SetText("No GameObject Assigned");
        }

        public override void OnUpdate(float deltaTime)
        {
            // Switched off on the first frame rather than in OnCreate: the target may not be set up yet.
            if (!hidden && toggleObject != null) { toggleObject.SetActive(false); hidden = true; }
            if (!pad.WasUsed(KeyCode.F) || toggleObject == null) return;
            bool on = !toggleObject.IsActiveSelf;
            Log.Info("Toggling " + toggleObject.Name + (on ? " on" : " off"));
            toggleObject.SetActive(on);
        }

        public override void OnTriggerEnter(Entity other) { pad.Enter(other); }
        public override void OnTriggerExit(Entity other) { pad.Exit(other); }
    }
}
