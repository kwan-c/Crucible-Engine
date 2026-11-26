/*************************************************************************
* \file:         TravelDemo.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.1
* \brief         Networking sample: keeping network objects across scene changes
*                (E5), checked in two Editors.
*
* \details
*    On "Travel Demo" in network_sync_test.scene and in
*    network_travel_test.scene. The HOST's keys (README section 8.30):
*
*        Y  spawn the Travel Case (once)
*        I  add an item to the case (its NetData)
*        U  travel to the other scene, keeping the players and the case
*
*    Every machine shows the case's NetworkId and items, its own player's
*    NetworkId, and on the host how long the last travel took until every
*    client was ready (OnAllPeersReady, which now waits for each client to
*    draw a few frames of the new scene).
*
* \history
*    -v1.0       sample for KeepAcrossScenes and KeepPlayersAcrossScenes
*    -v1.1       the case spawns in front of the panel (the demo texts are low panels now)
*
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class TravelDemo : EntityScript
    {
        /// <summary>The Travel Case prefab (Prefabs/network_tests/TravelCase.cprefab).</summary>
        public Prefab travelCase;
        /// <summary>Ticked in network_travel_test.scene: the other scene is then the arena.</summary>
        public bool inTravelScene;

        private const string ArenaScene = "Scenes/network_tests/network_sync_test.scene";
        private const string TravelScene = "Scenes/network_tests/network_travel_test.scene";
        private static readonly string[] Names = { "Map", "Key", "Coin", "Lamp", "Rope", "Gem", "Flag", "Book" };

        // Static: this script's object belongs to its scene, so each scene has its own copy.
        private static int nextName;
        private static double travelStarted = -1.0;
        private static string lastTravel = "-";

        private WorldText text;

        public override void OnCreate()
        {
            text = Entity.GetComponent<WorldText>();
        }

        public override void OnNetworkSceneLoading()
        {
            travelStarted = Time.ElapsedTime;
        }

        public override void OnAllPeersReady()
        {
            if (travelStarted >= 0.0)
                lastTravel = (Time.ElapsedTime - travelStarted).ToString("0.00") + " s until every client was ready";
            travelStarted = -1.0;
        }

        public override void OnUpdate(float deltaTime)
        {
            if (Network.IsHost) HandleKeys();
            if (text != null) text.Text = Describe();
        }

        private void HandleKeys()
        {
            TravelCase box = TravelCase.Current;
            if (Input.IsPressed(KeyCode.Y) && box == null && travelCase != null)
                Network.Spawn(travelCase, Entity.Transform.Position + new Vector3(0f, -1f, -2.5f), Quaternion.Identity);   // in front of the panel
            if (Input.IsPressed(KeyCode.I) && box != null)
                box.AddItem(Names[nextName++ % Names.Length]);
            if (Input.IsPressed(KeyCode.U))
            {
                Network.KeepPlayersAcrossScenes = true;
                if (box != null) Network.KeepAcrossScenes(box.Entity);
                Network.LoadScene(inTravelScene ? ArenaScene : TravelScene);
            }
        }

        private string Describe()
        {
            string role = !Network.IsOnline ? "offline (start a Host)" : Network.IsHost ? "HOST (Y I U)" : "CLIENT " + Network.LocalPeerId;
            TravelCase box = TravelCase.Current;
            Entity me = Network.LocalPlayerEntity;
            return "TRAVEL DEMO - " + (inTravelScene ? "travel scene" : "arena") + " - " + role
                + "\ncase " + (box == null ? "none (Y)" : "#" + box.Entity.NetworkId + (Network.IsKeptAcrossScenes(box.Entity) ? " kept" : "") + ": " + box.Items())
                + "\nmy player #" + (me == null ? "-" : me.NetworkId.ToString())
                + (Network.IsHost ? "\nlast travel: " + lastTravel : "");
        }
    }
}
