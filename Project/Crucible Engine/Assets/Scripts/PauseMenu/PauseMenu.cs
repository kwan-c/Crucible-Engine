/*************************************************************************
* \file:         PauseMenu.cs
* \brief         In-game pause menu. Goes on the "PauseMenuParent" entity
*                (the screen-space UICanvas that holds Dim, Options, Abandon
*                and AreYouSure).
*
* \details
*    Esc            shows the pause canvas (UICanvas.Enabled on this entity)
*                   and, pressed again, hides it - closing the options menu and
*                   the "Are you sure?" box with it. The mouse is freed while
*                   paused and given back on resume if it was captured before.
*
*    Button Action Ids (a UIButton whose On Click is "Custom"):
*        OpenOptions       Options button: shows OptionsCanvas, as MainMenuFlow does.
*        CloseOptions      OptionsCanvas' Done button: saves the settings, hides it.
*        OpenAreYouSure    Abandon button: switches on the AreYouSure entity's UIRect.
*        CloseAreYouSure   No button: switches that UIRect off again.
*    The Yes button is a plain LoadScene button and needs nothing here.
*
*    Everything starts hidden whatever the scene saved.
**************************************************************************/
namespace Crucible.Scripts
{
    public sealed class PauseMenu : EntityScript
    {
        /// <summary>The options menu's own canvas (found by the name "OptionsCanvas" if left empty).</summary>
        public Entity optionsCanvas;

        /// <summary>The confirm box (found as a child named "AreYouSure" if left empty).</summary>
        public Entity areYouSure;

        private UICanvas canvas;            // this entity's canvas: the pause menu itself
        private bool paused;
        private bool restoreMouseLock;      // the mouse was captured when the menu opened

        public override void OnCreate()
        {
            canvas = Entity.GetComponent<UICanvas>();
            if (canvas == null) Log.Warning("PauseMenu: '" + Entity.Name + "' has no UICanvas.");

            if (optionsCanvas == null) optionsCanvas = Entity.Find("OptionsCanvas");
            if (optionsCanvas == null) Log.Warning("PauseMenu: optionsCanvas is not set (no \"OptionsCanvas\" found).");
            if (areYouSure == null) areYouSure = Entity.FindChild("AreYouSure");
            if (areYouSure == null) Log.Warning("PauseMenu: areYouSure is not set (no \"AreYouSure\" child found).");

            // Hidden at start, whatever the scene saved. The mouse is left alone: CursorLock owns it.
            if (canvas != null) canvas.Enabled = false;
            SetAreYouSureOpen(false);
            SetOptionsOpen(false);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (LoadingScreen.IsLoading) return;
            if (Input.IsPressed(KeyCode.Escape)) SetPaused(!paused);
        }

        // Every UIButton click reaches every script: pick out ours by Action Id.
        public override void OnUIAction(string actionId)
        {
            if (!paused) return;

            if (actionId == "OpenOptions") SetOptionsOpen(true);
            else if (actionId == "CloseOptions") CloseOptionsAndSave();
            else if (actionId == "OpenAreYouSure") SetAreYouSureOpen(true);
            else if (actionId == "CloseAreYouSure") SetAreYouSureOpen(false);
        }

        /*********************************************************************
         \brief  Shows (true) or hides (false) the whole pause menu.
         *********************************************************************/
        private void SetPaused(bool pause)
        {
            if (pause == paused) return;
            paused = pause;
            if (canvas != null) canvas.Enabled = pause;

            if (pause)
            {
                // Free the mouse so the buttons can be clicked; gameplay input only runs while it is captured.
                restoreMouseLock = Input.IsMouseLocked;
                Input.SetMouseLocked(false);
                return;
            }

            // Closing the menu closes whatever is open inside it.
            SetAreYouSureOpen(false);
            CloseOptionsAndSave();
            if (restoreMouseLock) Input.SetMouseLocked(true);
            restoreMouseLock = false;
        }

        /*********************************************************************
         \brief  Shows (true) or hides (false) the options menu: OptionsCanvas'
                 UICanvas on or off, like MainMenuFlow.SetOptionsOpen.
         *********************************************************************/
        private void SetOptionsOpen(bool open)
        {
            UICanvas options = optionsCanvas != null ? optionsCanvas.GetComponent<UICanvas>() : null;
            if (options == null)
            {
                if (optionsCanvas != null) Log.Warning("PauseMenu: '" + optionsCanvas.Name + "' has no UICanvas.");
                return;
            }

            // Both canvases are saved at sort order 0: make sure the options draw over the pause menu.
            if (open && canvas != null && options.SortOrder <= canvas.SortOrder)
                options.SortOrder = canvas.SortOrder + 1;
            options.Enabled = open;
        }

        // The Done button (and closing the pause menu): keep any changed settings, then hide the options.
        private void CloseOptionsAndSave()
        {
            UICanvas options = optionsCanvas != null ? optionsCanvas.GetComponent<UICanvas>() : null;
            if (options == null || !options.Enabled) return;

            if (!InputSettings.Save()) Log.Warning("PauseMenu: could not save settings.");
            options.Enabled = false;
        }

        // The AreYouSure entity's UIRect on or off: its children (Yes, No, text) go with it.
        private void SetAreYouSureOpen(bool open)
        {
            if (areYouSure == null) return;
            if (!areYouSure.SetEnabled<UIRect>(open))
                Log.Warning("PauseMenu: '" + areYouSure.Name + "' has no UIRect.");
        }
    }
}
