/*************************************************************************
* \file:         LagDemo.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Networking sample: lag compensation (E6), checked in two Editors.
*
* \details
*    On "Lag Demo" in network_sync_test.scene (a scene network object that
*    replicates NetVariables). A player presses Z to fire along the yellow
*    line; the shot carries the player's Network.ViewTick. The host tests it
*    twice - rewound (Network.RaycastAt at that tick) and at the present
*    (Physics.Raycast) - and counts the hits on the target (README 8.31).
*    With latency simulated in the Network window, a client that fires as
*    the target crosses the line on its screen hits with the rewound test
*    and mostly misses with the present one.
*
* \history
*    -v1.0       sample for Network.RaycastAt
*
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class LagDemo : NetScript
    {
        private static readonly NetMessage Fire = NetMessage.Named("lagdemo.fire");

        // The firing line: from here along +Z (the yellow marker).
        private static readonly Vector3 LineStart = new Vector3(8f, 0.75f, -13.5f);
        private static readonly Vector3 LineDirection = new Vector3(0f, 0f, 1f);
        private const float LineLength = 13f;

        private readonly NetInt shots = new NetInt(slot: 0);
        private readonly NetInt rewoundHits = new NetInt(slot: 1);
        private readonly NetInt presentHits = new NetInt(slot: 2);
        private readonly NetInt lastRewindMs = new NetInt(slot: 3);
        private readonly NetInt lastShooter = new NetInt(slot: 4);

        private Entity target;
        private WorldText text;

        public override void OnCreate()
        {
            text = Entity.GetComponent<WorldText>();
            target = Entity.Find("Lag Target");
            NetMessage.OnCommand(Fire, (sender, data, about) => Judge(sender, data.ReadFloat()), this);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (Input.IsPressed(KeyCode.Z) && Network.IsReady)
                Fire.SendCommand(new NetWriter().WriteFloat((float)Network.ViewTick));
            if (text != null) text.Text = Describe();
        }

        // The host: one shot, tested at the shooter's view tick and at the present.
        private void Judge(int sender, float viewTick)
        {
            if (!Network.IsAuthority || target == null) return;
            RaycastHit hit;
            bool rewound = Network.RaycastAt(viewTick, LineStart, LineDirection, out hit, LineLength)
                           && Network.ObjectOf(hit.Entity) == target;
            bool present = Physics.Raycast(LineStart, LineDirection, out hit, LineLength)
                           && Network.ObjectOf(hit.Entity) == target;
            shots.Value = shots.Value + 1;
            if (rewound) rewoundHits.Value = rewoundHits.Value + 1;
            if (present) presentHits.Value = presentHits.Value + 1;
            double back = (Network.Tick - viewTick) * 1000.0 / Network.TickRate;
            lastRewindMs.Value = (int)System.Math.Round(System.Math.Min(back, Network.MaxRewindSeconds * 1000.0));
            lastShooter.Value = sender;
        }

        private string Describe()
        {
            string role = !Network.IsOnline ? "offline" : Network.IsHost ? "HOST" : "CLIENT " + Network.LocalPeerId;
            double behind = (Network.Tick - Network.ViewTick) * 1000.0 / Network.TickRate;
            return "LAG DEMO - " + role + " - Z fires along the yellow line"
                + "\nshots " + shots.Value + "   rewound hits " + rewoundHits.Value + "   present-time hits " + presentHits.Value
                + "\nlast shot: player " + lastShooter.Value + ", rewound " + lastRewindMs.Value + " ms"
                + "\nthis machine draws others " + behind.ToString("0") + " ms behind   (limit " + (Network.MaxRewindSeconds * 1000f).ToString("0") + " ms)";
        }
    }
}
