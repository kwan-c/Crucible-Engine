/*************************************************************************
* \file:         NetHostLostScreen.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Networked gameplay test: what a client sees when the host
*                is gone - a "host connection lost" screen over a camera of
*                its own, and a way back to the lobby.
*
* \details
*    On "Host Lost" in net_gameplay_test.scene, with two switched-off
*    children found by name:
*
*        Host Lost            Script: NetHostLostScreen
*        |- Fallback Camera   Camera: renders the level once the player's own
*        |                    camera has gone with the session
*        '- Lost Screen       ScreenSpace UICanvas (shows the cursor):
*           |- Title, Message UIText ("HOST CONNECTION LOST", the reason)
*           '- Back           UIButton, action "netgameplay.lost.back"
*
*    When a client's session ends after it had joined (the host left or
*    stopped answering), both switch on, the message says why
*    (Network.DescribeEnd) and the cursor is freed. Back (or Enter) loads
*    lobbyScene. The host and a single-player game never show it; nor does a
*    client that left by itself.
*
* \history
*    -v1.0       lost screen, fallback camera, back to the lobby
*
**************************************************************************/
namespace Crucible.Scripts.NetGameplay
{
    public sealed class NetHostLostScreen : EntityScript
    {
        public const string BackAction = "netgameplay.lost.back";

        /// <summary>Where Back goes (inside Assets).</summary>
        public string lobbyScene = "Scenes/network_tests/network_gameplay_tests/net_gameplay_lobby.scene";

        Entity fallbackCamera, screen;
        UIText message;
        bool joinedAsClient, showing;

        public override void OnCreate()
        {
            fallbackCamera = Entity.FindChild("Fallback Camera");
            screen = Entity.FindChild("Lost Screen");
            Entity text = screen != null ? screen.FindChild("Message") : null;
            message = text != null ? text.GetComponent<UIText>() : null;
            if (fallbackCamera != null) fallbackCamera.SetActive(false);
            if (screen != null) screen.SetActive(false);
            // Already in the session when this scene loaded (the lobby's scene change).
            joinedAsClient = Network.IsOnline && !Network.IsHost;
        }

        public override void OnNetworkStarted() { joinedAsClient = !Network.IsHost; }

        public override void OnNetworkStopped(string reason)
        {
            if (!joinedAsClient || showing) return;
            joinedAsClient = false;
            if (Network.LastEndCode == 0) return;   // this machine ended it (left, or Play stopped)
            Show(Network.DescribeEnd(true));
            Log.Info("NetHostLostScreen: " + reason);
        }

        void Show(string why)
        {
            showing = true;
            if (fallbackCamera != null) fallbackCamera.SetActive(true);
            if (screen != null) screen.SetActive(true);
            if (message != null) message.Text = why + "\n\nThe game ended because the host is no longer connected.";
            Input.SetMouseLocked(false);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (showing && (Input.IsPressed(KeyCode.Enter) || Input.IsPressed(KeyCode.KPEnter))) Back();
        }

        public override void OnUIAction(string actionId)
        {
            if (showing && actionId == BackAction) Back();
        }

        void Back()
        {
            showing = false;
            if (!Scene.Load(lobbyScene)) Log.Error("NetHostLostScreen: could not load " + lobbyScene);
        }
    }
}
