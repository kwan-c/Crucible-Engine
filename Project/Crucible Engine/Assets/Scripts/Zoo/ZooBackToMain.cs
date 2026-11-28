/*************************************************************************
* \file:         ZooBackToMain.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \date          8 November 2026
* \version       1.0
* \brief         Zoo pad: step on it and press G to go back to the main
*                menu. Port of Unity's ZooBacktoMain (+ SceneManagerScript).
*
**************************************************************************/
namespace Crucible.Scripts
{
    public sealed class ZooBackToMain : EntityScript
    {
        /// <summary>Path under Assets, as Scene.Load takes it.</summary>
        public string menuScene = "Scenes/Game/Main Menu Scene V2.scene";

        ZooPad pad;

        public override void OnCreate()
        {
            pad = new ZooPad(this);
            pad.SetText("Press G here to return to main Menu");
        }

        public override void OnUpdate(float deltaTime)
        {
            if (!pad.WasUsed(KeyCode.G) || LoadingScreen.IsLoading) return;
            if (!Scene.Load(menuScene)) Log.Error("ZooBackToMain: could not load " + menuScene);
        }

        public override void OnTriggerEnter(Entity other) { pad.Enter(other); }
        public override void OnTriggerExit(Entity other) { pad.Exit(other); }
    }
}
