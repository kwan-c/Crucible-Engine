/*************************************************************************
* \file:         MainMenuFlow.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          25 November 2026
* \version       1.0
* \brief         The main menu's camera moves between its four views:
*                Title (camera position 1), Tables (position 2), Torsos
*                (position 3) and Quests (above the quests table).
*
* \details
*    Goes on the scene's "manager" entity, next to MainMenuIntro, and waits
*    for the intro to finish before it takes any input.
*      Title  -> Tables   Start: the logo explodes, the title buttons fade
*                         out, and after startDelay the camera pans down
*                         to position 2.
*      Tables -> Torsos   click the Torsos table: the camera pans to
*                         position 3 while the ground and its tables slide
*                         away from the golem (+Z), out of the shot.
*                         The torso carousel (TorsoSelector, same entity)
*                         then opens: the other torsos drop in from above.
*      Torsos -> Tables   Back (or Confirm, which first locks in the torso
*                         in front): the other torsos lift away, leaving the
*                         golem, then the ground slides home, then the
*                         camera pans back (Hugo's order).
*      Tables -> Quests   click the Quests table: the camera pans above it,
*                         looking down, its "Quests" label fades out, the
*                         screen around the table darkens (questVignette),
*                         Back appears, and the first time the quest stones
*                         drop onto it (QuestStoneDrop).
*      In Quests          the stones can be picked (QuestPicker, same
*                         entity): while a quest is open, Back and Esc
*                         close it instead of leaving the table.
*      Quests -> Tables   Back (a drop still running lands at once); the
*                         label and the light fade back during the pan.
*      Tables -> Title    Back: the camera pans up, the logo comes back
*                         (Return trigger), the title buttons fade in.
*    The torso arrows and Confirm only show in the torso view, Back in the
*    torso and quest views (not at the tables); they all fade in on arrival
*    and out as the view is left (over the quest table Back sits top-left,
*    clear of the quest tablet). Esc goes back one level
*    from every view. While the camera is
*    moving every click and key is ignored, so a move can't start halfway
*    through another.
*
*    The title buttons and the logo are MainMenuIntro's fields (same
*    entity), so they are only wired once.
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public sealed class MainMenuFlow : EntityScript
    {
        // ---- The camera and where it stops (set in the Inspector) ----
        public Entity viewCamera;            // the "Camera" entity
        public Entity camPosition1;          // Title: looking at the golem
        public Entity camPosition2;          // Tables
        public Entity camPosition3;          // Torsos: the golem in the centre
        public float panSeconds = 1.5f;

        // ---- The tables ----
        public Entity torsoTable;            // table2 ("Torsos")
        public Entity questTable;            // table3 ("Quests")
        // Metres above the tray floor's centre (QuestStones). 0.78 made the table fill ~90% of the
        // screen (with a 60 degree vertical FOV the 0.77 m side needs 0.77 / 0.9 / (2 tan 30) =
        // 0.74 m to the rim, 0.034 m above the floor); 0.72 (Hugo: a little closer) fills ~97%.
        public float questHeight = 0.72f;
        public float questPitch = -90f;      // degrees around X: -90 looks straight down
        public float hoverBrighten = 0.35f;  // how far a hovered label moves toward white (0..1)
        // Darkens the screen around the quest table while it is open, so the quests stand out (Unity's
        // board sits on black). "Quest Vignette": its own canvas behind the menu's (sortOrder -1), so
        // Back stays bright; a full-screen image clear over the table, dark toward the edges.
        public Prefab questVignette;
        public float vignetteAlpha = 0.8f;   // how dark the edges get (0..1)

        // ---- The ground the tables stand on ----
        public Entity ground;                // "TablesNGroundParent"
        public float groundSlide = 8f;       // metres toward the camera side (+Z), away from the golem
        public float groundSeconds = 1f;

        // ---- UI: Back (torso and quest views) and the torso view's buttons (Hugo: none at the tables) ----
        public Entity backButton;            // Custom, Action Id "Back"
        public Entity leftArrow;             // Custom, Action Id "TorsoPrev" (step 4)
        public Entity rightArrow;            // Custom, Action Id "TorsoNext" (step 4)
        public Entity confirmButton;         // Custom, Action Id "Confirm"   (step 4)
        public float buttonFadeSeconds = 0.3f;
        public float startDelay = 0.9f;      // after Start, before the pan: the logo's explode (0.83 s) plays out
        // Over the quest table Back moves to the top-left (Hugo: at its usual spot it covers the quest
        // tablet), and goes back to its scene spot on leaving. In reference pixels from its anchor (the
        // screen centre), +y down: (-760, -440) puts the 320 x 120 button 40 px in from the top-left
        // corner of the 1920 x 1080 canvas. (Scripts can't change a UIRect's anchors, only its offset.)
        public float questBackX = -760f;
        public float questBackY = -440f;

        private enum View { Title, Tables, Torsos, Quests }
        private View view = View.Title;

        private MainMenuIntro intro;         // owns the logo and the title buttons
        private TorsoSelector torsos;        // the torso carousel (optional: without it the view is empty)
        private QuestStoneDrop questStones;  // the quest table's stone drop (optional)
        private QuestPicker questPicker;     // picking a stone to open its quest (optional)
        private UIImage vignette;           // the spawned questVignette's image (null without one)
        private Camera cameraComponent;      // for the mouse ray
        private Vector3 groundHome;          // where the ground starts (and returns to)
        private UIRect backRect;             // Back's place on screen
        private Vector2 backHome;            // ...as set in the scene (torso view)

        // The move in progress: every tween here runs together, then `arrived` runs once.
        private readonly List<MenuTween> running = new List<MenuTween>();
        private Action arrived;

        // The tables' labels and their scene colours (hover brightens, then puts these back).
        private WorldText torsoLabel, questLabel;
        private Color torsoLabelColour, questLabelColour;
        private WorldText hovered;           // the label lit right now, or null

        public override void OnCreate()
        {
            intro = Entity.GetComponent<MainMenuIntro>();
            if (intro == null) Log.Warning("MainMenuFlow: no MainMenuIntro on this entity.");
            torsos = Entity.GetComponent<TorsoSelector>();
            questStones = Entity.GetComponent<QuestStoneDrop>();
            questPicker = Entity.GetComponent<QuestPicker>();
            if (questVignette != null)
            {
                Entity overlay = questVignette.Instantiate();   // a root canvas, saved at alpha 0
                Entity image = overlay != null ? overlay.FindChild("Quest Vignette") : null;
                vignette = image != null ? image.GetComponent<UIImage>() : null;
            }
            if (viewCamera == null || camPosition1 == null || camPosition2 == null || camPosition3 == null)
                Log.Warning("MainMenuFlow: set viewCamera and camPosition1/2/3.");
            if (torsoTable == null || questTable == null) Log.Warning("MainMenuFlow: set torsoTable and questTable.");

            if (viewCamera != null) cameraComponent = viewCamera.GetComponent<Camera>();
            if (ground != null) groundHome = ground.Transform.WorldPosition;
            backRect = backButton != null ? backButton.GetComponent<UIRect>() : null;
            if (backRect != null) backHome = backRect.Position;
            SetTorsoButtonsActive(false);
            if (backButton != null) backButton.SetActive(false);   // until a view that has it (ShowButtons)

            torsoLabel = LabelOf(torsoTable);
            questLabel = LabelOf(questTable);
            if (torsoLabel != null) torsoLabelColour = torsoLabel.Color;
            if (questLabel != null) questLabelColour = questLabel.Color;

            // Boot: the camera starts at the title spot, whatever the scene saved (it is saved at
            // position 2 so the tables are easy to edit).
            if (viewCamera != null && camPosition1 != null)
            {
                viewCamera.Transform.WorldPosition = camPosition1.Transform.WorldPosition;
                viewCamera.Transform.WorldRotation = camPosition1.Transform.WorldRotation;
            }
        }

        public override void OnUpdate(float deltaTime)
        {
            // The intro owns the screen until its buttons are in.
            if (intro != null && !intro.IsFinished) return;

            if (running.Count > 0)
            {
                bool allDone = true;
                foreach (MenuTween tween in running)
                {
                    tween.Update(deltaTime);
                    if (!tween.Done) allDone = false;
                }
                if (!allDone) return;
                running.Clear();
                // Taken out before it runs: it may start the next move (Back from Torsos does).
                Action next = arrived;
                arrived = null;
                if (next != null) next();
                return;
            }

            if (Input.IsPressed(KeyCode.Escape)) { Back(); return; }
            if (view == View.Tables) UpdateTables();
        }

        public override void OnUIAction(string actionId)
        {
            if (running.Count > 0) return;   // mid-move: ignore
            if (actionId == "Start" && view == View.Title && (intro == null || intro.IsFinished)) StartToTables();
            else if (actionId == "Back") Back();
            else if (actionId == "Confirm" && view == View.Torsos)
            {
                // Lock in the torso in front, then leave exactly as Back does (the lift leaves it on the golem).
                if (torsos != null) torsos.Confirm();
                Back();
            }
        }

        // ------------------------------------------------------------------ moves

        // Title -> Tables (the Start button): the logo explodes and the buttons fade out at once,
        // and only after startDelay does the camera pan down - two moves in a row.
        private void StartToTables()
        {
            SetTitleButtonsClickable(false);
            if (intro != null && intro.logo != null) intro.logo.Animator.SetTrigger("Explode");
            List<MenuTween> first = new List<MenuTween> { MenuTween.Wait(startDelay) };
            FadeTitleButtons(0f, first);   // shorter than the delay, so they are gone before the pan
            Run(first, () =>
            {
                // Off once faded: an invisible button still counts as UI under the mouse, and at
                // position 2 these sit over the bottom of the Quests table, blocking its clicks.
                SetTitleButtonsActive(false);
                // The explode (0.83 s) is over by now and holds its last frame, letters thrown at
                // the camera: hidden before the pan so none can hang in view. Return brings it back.
                if (intro != null && intro.logo != null) intro.logo.SetEnabled<MeshRenderer>(false);
                Run(PanTo(camPosition2), () => view = View.Tables);
            });
        }

        /*********************************************************************
         \brief  One level up from the current view (the Back button or Esc).
                 Nothing on the title.
         *********************************************************************/
        private void Back()
        {
            switch (view)
            {
                case View.Torsos:
                    // Three moves in a row: the other torsos lift away (the view's buttons fading
                    // out with them), then the ground comes home, then the camera pans back.
                    MenuTween fold = torsos != null ? torsos.Close() : null;
                    List<MenuTween> lift = new List<MenuTween> { fold };
                    foreach (Entity button in new[] { backButton, leftArrow, rightArrow, confirmButton })
                        FadeButton(button, 0f, lift);
                    Run(lift, () =>
                    {
                        SetTorsoButtonsActive(false);   // faded out: off
                        HideBack();
                        Run(new List<MenuTween> { SlideGround(groundHome) }, () =>
                            Run(PanTo(camPosition2), () => view = View.Tables));
                    });
                    break;

                case View.Quests:
                    // A quest is open: Back closes it (its stone flies home), the view stays.
                    if (questPicker != null && questPicker.IsOpen) { questPicker.Close(); break; }
                    if (questPicker != null) questPicker.End();
                    // Leaving mid-drop: every stone lands at once (and the shake stops) before the pan.
                    if (questStones != null) questStones.Skip();
                    List<MenuTween> outOfQuests = PanTo(camPosition2);
                    outOfQuests.Add(FadeLabel(questLabel, questLabelColour.A, panSeconds));   // the label comes back
                    if (vignette != null) outOfQuests.Add(MenuTween.Fade(vignette, 0f, panSeconds));   // and the light
                    FadeButton(backButton, 0f, outOfQuests);   // Back fades out as the pan starts
                    Run(outOfQuests, () =>
                    {
                        HideBack();   // off, and home from the top-left once nobody can see it move
                        view = View.Tables;
                    });
                    break;

                case View.Tables:
                    ClearHover();
                    if (intro != null && intro.logo != null)
                    {
                        intro.logo.SetEnabled<MeshRenderer>(true);
                        intro.logo.Animator.SetTrigger("Return");   // Logo_Explode -> Logo_Idle
                    }
                    List<MenuTween> tweens = PanTo(camPosition1);
                    SetTitleButtonsActive(true);   // back on (still unclickable) so they can fade in
                    FadeTitleButtons(1f, tweens);
                    Run(tweens, () =>
                    {
                        SetTitleButtonsClickable(true);
                        view = View.Title;
                    });
                    break;
            }
        }

        // At the tables: light the label under the mouse; a click on its table goes there.
        private void UpdateTables()
        {
            Entity table = TableUnderMouse();
            WorldText label = table == torsoTable ? torsoLabel : table == questTable ? questLabel : null;
            if (label != hovered)
            {
                ClearHover();
                if (label != null) label.Color = Color.Lerp(label == torsoLabel ? torsoLabelColour : questLabelColour, Color.White, hoverBrighten);
                hovered = label;
            }
            if (table == null || !Input.IsPressed(KeyCode.MouseLeft)) return;

            ClearHover();
            if (table == torsoTable)
            {
                // Camera to position 3 while the ground slides out of the shot, together.
                List<MenuTween> tweens = PanTo(camPosition3);
                tweens.Add(SlideGround(groundHome + new Vector3(0f, 0f, groundSlide)));
                Run(tweens, () =>
                {
                    ShowButtons(backButton, leftArrow, rightArrow, confirmButton);
                    if (torsos != null) torsos.Open();   // the other torsos drop into the ring
                    view = View.Torsos;
                });
            }
            else
            {
                // Centred over the tray (QuestStones sits at its floor's centre); the table's own
                // origin is a few centimetres off, enough to cut one edge at this distance.
                Entity centre = questStones != null && questStones.stones != null ? questStones.stones : questTable;
                Vector3 above = centre.Transform.WorldPosition + new Vector3(0f, questHeight, 0f);
                List<MenuTween> tweens = new List<MenuTween>
                {
                    MenuTween.Move(viewCamera.Transform, above, panSeconds),
                    MenuTween.Rotate(viewCamera.Transform, Quaternion.Euler(questPitch, 0f, 0f), panSeconds),
                    // The "Quests" label fades out over the first half of the pan: once chosen it's
                    // in the way (it floats above the table, between the camera and the stones).
                    FadeLabel(questLabel, 0f, panSeconds * 0.5f)
                };
                // The surroundings darken as the camera comes over the table.
                if (vignette != null) tweens.Add(MenuTween.Fade(vignette, vignetteAlpha, panSeconds));
                Run(tweens, () =>
                {
                    if (backRect != null) backRect.Position = new Vector2(questBackX, questBackY);   // top-left, clear of the tablet
                    ShowButtons(backButton);   // Back (only) in this view too
                    if (questStones != null) questStones.Play(viewCamera); // the stones drop, first visit only
                    if (questPicker != null) questPicker.Begin(viewCamera); // picking starts once they are down
                    view = View.Quests;
                });
            }
        }

        // ------------------------------------------------------------------ helpers

        // Starts a move: these tweens run together, then whenDone runs once.
        private void Run(List<MenuTween> tweens, Action whenDone)
        {
            running.Clear();
            foreach (MenuTween tween in tweens) if (tween != null) running.Add(tween);
            arrived = whenDone;
            // Nothing to wait for (no carousel, no ground set): go straight on. Without this an
            // empty move would never "arrive" (OnUpdate only finishes moves that have tweens).
            if (running.Count == 0)
            {
                arrived = null;
                if (whenDone != null) whenDone();
            }
        }

        // The camera's move and turn to a spot, both over panSeconds.
        private List<MenuTween> PanTo(Entity spot)
        {
            List<MenuTween> tweens = new List<MenuTween>();
            if (viewCamera == null || spot == null) return tweens;
            tweens.Add(MenuTween.Move(viewCamera.Transform, spot.Transform.WorldPosition, panSeconds));
            tweens.Add(MenuTween.Rotate(viewCamera.Transform, spot.Transform.WorldRotation, panSeconds));
            return tweens;
        }

        private MenuTween SlideGround(Vector3 to)
        {
            return ground != null ? MenuTween.Move(ground.Transform, to, groundSeconds) : null;
        }

        // Adds fades for the title buttons' pictures and labels. 1 = the scene's look (assumed
        // solid: MainMenuIntro restores their saved opacities, which are all 1 in this scene).
        private void FadeTitleButtons(float alpha, List<MenuTween> into)
        {
            if (intro == null) return;
            foreach (Entity button in new[] { intro.startButton, intro.optionsButton, intro.quitButton })
                FadeButton(button, alpha, into);
        }

        // Adds fades for one button's picture and its "Text" label, over buttonFadeSeconds.
        private void FadeButton(Entity button, float alpha, List<MenuTween> into)
        {
            if (button == null) return;
            UIImage image = button.GetComponent<UIImage>();
            if (image != null) into.Add(MenuTween.Fade(image, alpha, buttonFadeSeconds));
            Entity label = button.FindChild("Text");
            UIText text = label != null ? label.GetComponent<UIText>() : null;
            if (text != null) into.Add(MenuTween.Fade(text, alpha, buttonFadeSeconds));
        }

        // A view's buttons appear (Hugo: fading in, not popping): each on, from fully clear, up to
        // the scene's look, all together. It runs as a move, so clicks and keys wait out its
        // buttonFadeSeconds like any other (nothing can be pressed before it can be seen).
        private void ShowButtons(params Entity[] buttons)
        {
            List<MenuTween> fadeIn = new List<MenuTween>();
            foreach (Entity button in buttons)
            {
                if (button == null) continue;
                List<MenuTween> clear = new List<MenuTween>();
                FadeButton(button, 0f, clear);
                foreach (MenuTween tween in clear) tween.Finish();   // clear at once, before it shows
                button.SetActive(true);
                FadeButton(button, 1f, fadeIn);   // made after the clear, so it fades up from 0
            }
            Run(fadeIn, null);
        }

        // Back has faded out: off, and back to its usual spot (it only moves over the quest table).
        private void HideBack()
        {
            if (backButton != null) backButton.SetActive(false);
            if (backRect != null) backRect.Position = backHome;
        }

        // The two arrows and Confirm on or off at once: the torso view's own buttons. Shown with
        // ShowButtons and faded out before this turns them off, so this is never seen as a pop.
        private void SetTorsoButtonsActive(bool active)
        {
            foreach (Entity button in new[] { leftArrow, rightArrow, confirmButton })
                if (button != null) button.SetActive(active);
        }

        private void SetTitleButtonsActive(bool active)
        {
            if (intro == null) return;
            foreach (Entity button in new[] { intro.startButton, intro.optionsButton, intro.quitButton })
                if (button != null) button.SetActive(active);   // its "Text" child goes with it
        }

        private void SetTitleButtonsClickable(bool clickable)
        {
            if (intro == null) return;
            foreach (Entity button in new[] { intro.startButton, intro.optionsButton, intro.quitButton })
            {
                UIButton ui = button != null ? button.GetComponent<UIButton>() : null;
                if (ui != null) ui.Interactable = clickable;
            }
        }

        // The table (torsos or quests) under the mouse, or null. The Back button blocks it.
        private Entity TableUnderMouse()
        {
            if (cameraComponent == null || Input.IsPointerOverUI) return null;
            Ray ray;
            if (!cameraComponent.ScreenPointToRay(Input.MousePosition, out ray)) return null;
            RaycastHit hit;
            if (!Physics.Raycast(ray, out hit, 1000f)) return null;
            if (hit.Entity == torsoTable || hit.Entity == questTable) return hit.Entity;
            return null;
        }

        // Fades a table's label to `alpha`, keeping its colour (null without a label: Run skips it).
        private MenuTween FadeLabel(WorldText label, float alpha, float seconds)
        {
            if (label == null) return null;
            float from = label.Color.A;
            return MenuTween.Value(from, alpha, seconds, a =>
            {
                Color colour = label.Color;
                colour.A = a;
                label.Color = colour;
            });
        }

        private void ClearHover()
        {
            if (hovered == torsoLabel && torsoLabel != null) torsoLabel.Color = torsoLabelColour;
            if (hovered == questLabel && questLabel != null) questLabel.Color = questLabelColour;
            hovered = null;
        }

        // A table's "World Text" child (its label), or null.
        private static WorldText LabelOf(Entity table)
        {
            Entity label = table != null ? table.FindChild("World Text") : null;
            return label != null ? label.GetComponent<WorldText>() : null;
        }

    }   // MAINMENUFLOW CLASS

}   // NAMESPACE CRUCIBLE.SCRIPTS
