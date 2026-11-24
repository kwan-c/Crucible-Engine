/*************************************************************************
 * \file:         MainMenuManager.cs
 * \brief:        Main Menu V2 scene controller.
 *
 * \details
 *    Written for the "Main Menu Scene V2" scene. The public fields below
 *    are set in the Inspector:
 *
 *        camPosition1 / camPosition2 - camera waypoint entities.
 *        moveSpeed                   - camera speed (units/sec) and
 *                                      rotation speed (degrees/sec).
 *        headRotBef / headRotAft     - head  parent X rotation, start -> end.
 *        torsoRotBef / torsoRotAft   - torso parent X rotation, start -> end.
 *        logoModel, headParent, torsoParent, legsParent, canvas - entities.
 *
 *    Flow (all in Play mode):
 *        1. OnCreate -> camera snaps to camPosition1; head/torso are set to
 *           their "Bef" X rotations.
 *        2. IntroRotate -> head/torso rotate to their "Aft" X rotations over
 *           time via Quaternion.RotateTowards. When both arrive, the
 *           "PlayStart" trigger is fired (Logo_Start -> Logo_Idle).
 *        3. Start Button's "Start" action -> "Explode" trigger is fired,
 *           the Canvas' UICanvas is disabled, and the camera moves from
 *           camPosition1 to camPosition2 over time.
 *        4. When the camera reaches camPosition2 the logo's MeshRenderer is
 *           disabled (the engine cannot report "animation finished"), and a
 *           left-click raycast onto table1 moves the camera to table1/CamPos
 *           and rotates it -90 degrees on X.
 *
 *    ANIMATION NOTE:
 *    The C# Animator exposes ONLY controller-parameter writes
 *    (SetBool / SetFloat / SetInteger / SetTrigger / ResetTrigger). There is
 *    no Play(clip) / CrossFade / PlayInFixedTime method and no way to read
 *    whether a clip has finished. Therefore the script drives the logo with
 *    Trigger parameters. For these calls to do anything, MainMenuLogo.animcontroller
 *    must define the two Trigger parameters below and transitions that use them:
 *        "PlayStart" -> transition into the "Logo_Start" state (which already
 *                       auto-transitions to "Logo_Idle").
 *        "Explode"   -> transition into the "Logo_Explode" state.
 **************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class MainMenuManager : EntityScript
    {
        /// <summary>Where the camera starts (a position entity).</summary>
        public Entity camPosition1;

        /// <summary>Where the camera moves after "Start" is pressed.</summary>
        public Entity camPosition2;

        /// <summary>Camera speed: units/second for movement, degrees/second for rotation.</summary>
        public float moveSpeed = 2f;

        /// <summary>Head parent X rotation before the intro.</summary>
        public float headRotBef;
        /// <summary>Head parent X rotation after the intro.</summary>
        public float headRotAft;
        /// <summary>Torso parent X rotation before the intro.</summary>
        public float torsoRotBef;
        /// <summary>Torso parent X rotation after the intro.</summary>
        public float torsoRotAft;

        /// <summary>The logo model entity (has the AnimatedModel / AnimatorController).</summary>
        public Entity logoModel;
        /// <summary>The head parent entity.</summary>
        public Entity headParent;
        /// <summary>The torso parent entity.</summary>
        public Entity torsoParent;
        /// <summary>The legs parent entity (reserved for later use).</summary>
        public Entity legsParent;
        /// <summary>The Canvas entity (its UICanvas is disabled on Start).</summary>
        public Entity canvas;

        // Animator TRIGGER parameter names. These are parameters, not state names.
        private const string TriggerPlayStart = "PlayStart";
        private const string TriggerExplode = "Explode";

        private Entity cameraEntity;
        private Camera camera;
        private Entity table1;
        private Entity table1CamPos;

        private enum MenuState
        {
            IntroRotate,        // head/torso rotating from Bef to Aft
            Menu,               // idle, waiting for the Start button
            ExplodeAndMove,     // logo exploding, camera moving to camPosition2
            SelectingTable,     // camera at camPosition2, waiting for a table1 click
            MovingToTable1      // camera flying to table1/CamPos, pitching -90 deg
        }

        private MenuState state = MenuState.IntroRotate;

        public override void OnCreate()
        {
            cameraEntity = Entity.Find("Camera");
            table1 = Entity.Find("table1");

            if (cameraEntity == null) Log.Warning("MainMenuManager: no entity named 'Camera' found.");
            if (table1 == null)       Log.Warning("MainMenuManager: no entity named 'table1' found.");
            if (logoModel == null)    Log.Warning("MainMenuManager: logoModel is not assigned.");
            if (headParent == null)   Log.Warning("MainMenuManager: headParent is not assigned.");
            if (torsoParent == null)  Log.Warning("MainMenuManager: torsoParent is not assigned.");

            // Fall back to a scene lookup if the Canvas field was not assigned yet.
            if (canvas == null)
                canvas = Entity.Find("Canvas");

            if (cameraEntity != null)
                camera = cameraEntity.GetComponent<Camera>();

            // table1's camera anchor is its child entity named "CamPos".
            if (table1 != null)
            {
                table1CamPos = table1.FindChild("CamPos");
                if (table1CamPos == null)
                    Log.Warning("MainMenuManager: table1 has no child named 'CamPos'.");
            }

            // 1. Put the Camera at camPosition1 the moment the scene opens.
            if (cameraEntity != null && camPosition1 != null)
                cameraEntity.Transform.WorldPosition = camPosition1.Transform.WorldPosition;

            // 2. Start the head/torso at their "Bef" X-axis rotations.
            if (headParent != null)
                headParent.Transform.Rotation = Quaternion.Euler(headRotBef, 0f, 0f);
            if (torsoParent != null)
                torsoParent.Transform.Rotation = Quaternion.Euler(torsoRotBef, 0f, 0f);

            state = MenuState.IntroRotate;
        }

        public override void OnUIAction(string actionId)
        {
            // The Start Button has On Click = "Custom" and Action Id = "Start".
            if (actionId != "Start" || state != MenuState.Menu)
                return;

            // Play the Logo_Explode animation.
            if (logoModel != null)
                logoModel.Animator.SetTrigger(TriggerExplode);

            // Disable the Canvas' UICanvas while the explosion plays.
            if (canvas != null)
            {
                UICanvas ui = canvas.GetComponent<UICanvas>();
                if (ui != null)
                    ui.Enabled = false;
            }

            // Begin moving the camera from camPosition1 to camPosition2.
            state = MenuState.ExplodeAndMove;
        }

        public override void OnUpdate(float deltaTime)
        {
            switch (state)
            {
                case MenuState.IntroRotate:
                    UpdateIntroRotate(deltaTime);
                    break;

                case MenuState.Menu:
                    break;

                case MenuState.ExplodeAndMove:
                    if (cameraEntity == null || camPosition2 == null)
                        break;
                    if (MoveTowards(cameraEntity.Transform, camPosition2.Transform.WorldPosition, moveSpeed, deltaTime))
                    {
                        // The engine cannot report "Logo_Explode finished", so
                        // reaching camPosition2 is used as the end of the explode.
                        if (logoModel != null)
                            logoModel.SetEnabled<MeshRenderer>(false);
                        state = MenuState.SelectingTable;
                    }
                    break;

                case MenuState.SelectingTable:
                    if (cameraEntity != null && Input.IsPressed(KeyCode.MouseLeft) && RaycastHitsTable1())
                        state = MenuState.MovingToTable1;
                    break;

                case MenuState.MovingToTable1:
                    if (cameraEntity == null)
                        break;
                    // Move toward table1's CamPos child and pitch -90 degrees on X.
                    if (table1CamPos != null)
                        MoveTowards(cameraEntity.Transform, table1CamPos.Transform.WorldPosition, moveSpeed, deltaTime);
                    RotateTowards(cameraEntity.Transform, Quaternion.Euler(-90f, 0f, 0f), moveSpeed, deltaTime);
                    break;
            }
        }

        /// <summary>
        /// Rotates the head/torso parents from their "Bef" to "Aft" X rotations.
        /// Once both arrive, fires the "PlayStart" trigger and enters the menu.
        /// </summary>
        private void UpdateIntroRotate(float deltaTime)
        {
            bool headDone = true;
            bool torsoDone = true;

            if (headParent != null)
                headDone = RotateTowardsX(headParent.Transform, headRotAft, moveSpeed, deltaTime);
            if (torsoParent != null)
                torsoDone = RotateTowardsX(torsoParent.Transform, torsoRotAft, moveSpeed, deltaTime);

            if (headDone && torsoDone)
            {
                // Play Logo_Start (it already leads into Logo_Idle in the Animator).
                if (logoModel != null)
                    logoModel.Animator.SetTrigger(TriggerPlayStart);
                state = MenuState.Menu;
            }
        }

        /// <summary>
        /// Casts a ray from the mouse cursor through the game screen and reports
        /// whether it hit table1's Collider.
        /// </summary>
        private bool RaycastHitsTable1()
        {
            if (camera == null || table1 == null)
                return false;

            Ray ray;
            if (!camera.ScreenPointToRay(Input.MousePosition, out ray))
                return false;

            RaycastHit hit;
            if (!Physics.Raycast(ray, out hit, 1000f))
                return false;

            return hit.Entity == table1;
        }

        /// <summary>
        /// Moves a Transform toward <paramref name="target"/> at <paramref name="speed"/>
        /// units per second. Returns true once it has arrived.
        /// </summary>
        private static bool MoveTowards(Transform transform, Vector3 target, float speed, float deltaTime)
        {
            Vector3 current = transform.WorldPosition;
            Vector3 toTarget = target - current;
            float distance = toTarget.Length;
            float step = speed * deltaTime;

            if (distance <= step || distance < 1e-4f)
            {
                transform.WorldPosition = target;
                return true;
            }

            transform.WorldPosition = current + toTarget * (step / distance);
            return false;
        }

        /// <summary>
        /// Rotates a Transform's local X rotation toward <paramref name="targetX"/>
        /// degrees at <paramref name="degreesPerSecond"/>. Returns true when arrived.
        /// </summary>
        private static bool RotateTowardsX(Transform transform, float targetX, float degreesPerSecond, float deltaTime)
        {
            Quaternion target = Quaternion.Euler(targetX, 0f, 0f);
            transform.Rotation = Quaternion.RotateTowards(transform.Rotation, target, degreesPerSecond * deltaTime);
            return Quaternion.Angle(transform.Rotation, target) < 0.5f;
        }

        /// <summary>
        /// Rotates a Transform toward <paramref name="target"/> at
        /// <paramref name="degreesPerSecond"/> degrees per second (world space).
        /// </summary>
        private static void RotateTowards(Transform transform, Quaternion target, float degreesPerSecond, float deltaTime)
        {
            transform.WorldRotation = Quaternion.RotateTowards(transform.WorldRotation, target, degreesPerSecond * deltaTime);
        }
    }
}
