/*************************************************************************
* \file:         LevelSelectController.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          8 November 2026
* \version       1.0
* \brief         The level select table: hover a slab, click it, accept
*                or decline its quest.
*
* \details
*    Put this on one entity in the Level_Select scene. Every slab needs a
*    LevelSelectSlab script (it holds that slab's quest).
*
*    1. Browsing: the slab under the mouse rises a little and its glow
*       light fades in.
*    2. Click it: the camera moves in so that slab is on the left of the
*       screen, and the QuestDescSlab slides in to its right showing the
*       slab's Title, Quest type and Description.
*    3. Accept loads the slab's level. Decline (or Escape) sends the
*       QuestDescSlab and the camera back to where they started.
*
*    The camera keeps its rotation; only its position moves, so the
*    QuestDescSlab reads the same way it does in the editor.
*
*    Scene references left empty are found by their names in the
*    Level_Select scene (QuestDescSlab, TitleText, QuestTypeText,
*    DescriptionText, quest_accept_button, quest_decline_button, and the
*    main camera).
**************************************************************************/
using System;

namespace Crucible.@Scripts
{
    public sealed class LevelSelectController : EntityScript
    {
        // ---- Scene references (empty = found by name) ----
        public Entity questPanel;        // "QuestDescSlab"
        public Entity titleText;         // "TitleText"
        public Entity questTypeText;     // "QuestTypeText"
        public Entity descriptionText;   // "DescriptionText"
        public Entity acceptButton;      // "quest_accept_button"
        public Entity declineButton;     // "quest_decline_button"
        public Entity cameraEntity;      // the main camera

        // ---- Hover ----
        /// <summary>How high a hovered slab rises (world units).</summary>
        public float hoverLift = 0.15f;
        /// <summary>How high a hovered Accept/Decline button rises.</summary>
        public float buttonHoverLift = 0.05f;
        /// <summary>How quickly the rise and glow follow the mouse (higher is snappier).</summary>
        public float hoverSharpness = 12f;

        // ---- Opening a quest ----
        /// <summary>How far the camera ends up from the slab and QuestDescSlab.</summary>
        public float focusDistance = 7f;
        /// <summary>Space between the slab's right edge and the QuestDescSlab's left edge.</summary>
        public float panelGap = 0.5f;
        /// <summary>
        /// How far above the slab's resting height the QuestDescSlab sits. Keep it high enough
        /// to clear the other slabs it may slide over.
        /// </summary>
        public float panelHeight = 0.45f;
        /// <summary>Seconds the camera and QuestDescSlab take to move in or back out.</summary>
        public float transitionTime = 0.6f;

        // The panel's and buttons' sizes in their models' own units, for the mouse tests
        // (questDescriptionSlab.glb, quest_accept.glb, quest_decline.glb).
        private static readonly Vector3 PanelBoxSize = new Vector3(6.1f, 0.65f, 6.1f);
        private static readonly Vector3 AcceptBoxCenter = new Vector3(-0.02f, 0.06f, -0.09f);
        private static readonly Vector3 AcceptBoxSize = new Vector3(2.48f, 0.3f, 0.66f);
        private static readonly Vector3 DeclineBoxCenter = new Vector3(-0.03f, 0.06f, 0.02f);
        private static readonly Vector3 DeclineBoxSize = new Vector3(2.6f, 0.3f, 0.74f);

        private enum State { Browsing, Opening, Open, Closing, Loading }

        private State state = State.Browsing;
        private Camera viewCamera;
        private Vector3 cameraHome;          // where the camera starts
        private Quaternion cameraRotation;   // kept the whole time
        private Vector3 panelHome;           // where the QuestDescSlab starts

        private LevelSelectSlab hovered;     // under the mouse while browsing
        private LevelSelectSlab selected;    // the open quest's slab

        // The current move of the camera and QuestDescSlab (Opening / Closing).
        private Vector3 cameraFrom, cameraTo, panelFrom, panelTo;
        private float moveTime;

        private ButtonLift accept, decline;

        public override void OnCreate()
        {
            Input.SetMouseLocked(false);   // the table is used with the mouse

            if (questPanel == null) questPanel = Entity.Find("QuestDescSlab");
            if (questPanel == null)
            {
                Log.Error("LevelSelectController: no QuestDescSlab in the scene - set Quest Panel.");
                Enabled = false;
                return;
            }
            if (titleText == null) titleText = questPanel.FindChild("TitleText");
            if (questTypeText == null) questTypeText = questPanel.FindChild("QuestTypeText");
            if (descriptionText == null) descriptionText = questPanel.FindChild("DescriptionText");
            if (acceptButton == null) acceptButton = questPanel.FindChild("quest_accept_button");
            if (declineButton == null) declineButton = questPanel.FindChild("quest_decline_button");

            viewCamera = cameraEntity != null ? cameraEntity.GetComponent<Camera>() : Camera.Main;
            if (viewCamera == null)
            {
                Log.Error("LevelSelectController: no camera - set Camera Entity, or add an enabled Camera.");
                Enabled = false;
                return;
            }
            cameraEntity = viewCamera.Entity;
            cameraHome = cameraEntity.Transform.WorldPosition;
            cameraRotation = cameraEntity.Transform.WorldRotation;
            panelHome = questPanel.Transform.WorldPosition;

            accept = new ButtonLift(acceptButton);
            decline = new ButtonLift(declineButton);
        }

        public override void OnUpdate(float deltaTime)
        {
            // Where the mouse points into the scene. No ray when it is off the game screen
            // (or over game UI), so nothing is hovered or clicked then.
            Ray ray;
            bool aiming = viewCamera.ScreenPointToRay(Input.MousePosition, out ray) && !Input.IsPointerOverUI;
            bool clicked = aiming && Input.IsPressed(KeyCode.MouseLeft);

            hovered = null;
            Entity hoveredButton = null;

            switch (state)
            {
                case State.Browsing:
                    if (aiming) hovered = SlabUnder(ray);
                    if (clicked && hovered != null) OpenQuest(hovered);
                    break;

                case State.Open:
                    if (aiming) hoveredButton = ButtonUnder(ray);
                    if (clicked && hoveredButton == acceptButton) AcceptQuest();
                    else if (clicked && hoveredButton == declineButton) CloseQuest();
                    else if (Input.IsPressed(KeyCode.Escape)) CloseQuest();
                    break;

                case State.Opening:
                case State.Closing:
                    Move(deltaTime);
                    break;
            }

            foreach (LevelSelectSlab slab in LevelSelectSlab.All)
                if (slab.Entity.IsAlive)
                    slab.Animate(slab == hovered || slab == selected, hoverLift, hoverSharpness, deltaTime);
            accept.Animate(hoveredButton != null && hoveredButton == acceptButton, buttonHoverLift, hoverSharpness, deltaTime);
            decline.Animate(hoveredButton != null && hoveredButton == declineButton, buttonHoverLift, hoverSharpness, deltaTime);
        }

        // ---------------------------------------------------------------- picking

        private static LevelSelectSlab SlabUnder(Ray ray)
        {
            LevelSelectSlab nearest = null;
            float nearestDistance = float.MaxValue;
            foreach (LevelSelectSlab slab in LevelSelectSlab.All)
            {
                float distance;
                if (slab.Entity.IsAlive && slab.Entity.IsActive && slab.Enabled &&
                    slab.IsHitBy(ray, out distance) && distance < nearestDistance)
                {
                    nearest = slab;
                    nearestDistance = distance;
                }
            }
            return nearest;
        }

        private Entity ButtonUnder(Ray ray)
        {
            float acceptDistance = float.MaxValue, declineDistance = float.MaxValue;
            bool onAccept = acceptButton != null && PickBox.RayHits(ray, acceptButton, AcceptBoxCenter, AcceptBoxSize, out acceptDistance);
            bool onDecline = declineButton != null && PickBox.RayHits(ray, declineButton, DeclineBoxCenter, DeclineBoxSize, out declineDistance);
            if (onAccept && (!onDecline || acceptDistance <= declineDistance)) return acceptButton;
            return onDecline ? declineButton : null;
        }

        // ---------------------------------------------------------------- the quest

        private void OpenQuest(LevelSelectSlab slab)
        {
            selected = slab;
            SetText(titleText, slab.questTitle);
            SetText(questTypeText, slab.questType);
            SetText(descriptionText, slab.DescriptionWithLineBreaks);

            // "Right" on screen, flattened onto the table.
            Vector3 right = cameraRotation.Right;
            right = new Vector3(right.X, 0f, right.Z).Normalized;
            if (right.Length < 0.5f) right = new Vector3(1f, 0f, 0f);

            // The QuestDescSlab goes just to the right of the slab, clear of its edge...
            Vector3 slabCentre = slab.RestPosition + Vector3.Up * hoverLift;
            float apart = PickBox.HalfExtentAlong(slab.Entity, slab.BoxSize, right) + panelGap
                        + PickBox.HalfExtentAlong(questPanel, PanelBoxSize, right);
            Vector3 panelTarget = slabCentre + right * apart;
            panelTarget.Y = slab.RestPosition.Y + panelHeight;

            // ...and the camera looks at the middle of the two, from focusDistance away.
            Vector3 middle = (slabCentre + panelTarget) * 0.5f;
            StartMove(State.Opening, middle - cameraRotation.Forward * focusDistance, panelTarget);
        }

        private void CloseQuest()
        {
            StartMove(State.Closing, cameraHome, panelHome);
        }

        private void AcceptQuest()
        {
            if (selected == null) return;
            if (string.IsNullOrEmpty(selected.levelScene))
            {
                Log.Warning("LevelSelect: " + selected.Entity.Name + " has no Level Scene set, so Accept does nothing.");
                return;
            }
            if (Scene.Load(selected.levelScene))
                state = State.Loading;   // the scene switches at the start of the next frame
        }

        private static void SetText(Entity textEntity, string text)
        {
            WorldText worldText = textEntity != null ? textEntity.GetComponent<WorldText>() : null;
            if (worldText != null) worldText.Text = text ?? string.Empty;
        }

        // ---------------------------------------------------------------- camera + panel move

        private void StartMove(State moveState, Vector3 cameraTarget, Vector3 panelTarget)
        {
            state = moveState;
            cameraFrom = cameraEntity.Transform.WorldPosition;   // from wherever they are now
            panelFrom = questPanel.Transform.WorldPosition;
            cameraTo = cameraTarget;
            panelTo = panelTarget;
            moveTime = 0f;
        }

        private void Move(float deltaTime)
        {
            moveTime += deltaTime;
            float t = transitionTime > 0f ? Math.Min(moveTime / transitionTime, 1f) : 1f;
            float eased = t * t * (3f - 2f * t);   // smoothstep: eases in and out

            cameraEntity.Transform.WorldPosition = cameraFrom + (cameraTo - cameraFrom) * eased;
            questPanel.Transform.WorldPosition = panelFrom + (panelTo - panelFrom) * eased;

            if (t < 1f) return;
            if (state == State.Opening)
            {
                state = State.Open;
            }
            else
            {
                state = State.Browsing;
                selected = null;
            }
        }

        // ---------------------------------------------------------------- helpers

        // Raises an Accept/Decline button a little while it is hovered.
        private sealed class ButtonLift
        {
            private readonly Entity button;
            private readonly Vector3 rest;   // local position on the QuestDescSlab
            private float lift;

            public ButtonLift(Entity button)
            {
                this.button = button;
                if (button != null) rest = button.Transform.Position;
            }

            public void Animate(bool raised, float height, float sharpness, float deltaTime)
            {
                if (button == null) return;
                lift += ((raised ? height : 0f) - lift) * (1f - (float)Math.Exp(-sharpness * deltaTime));
                button.Transform.Position = rest + Vector3.Up * lift;
            }
        }
    }

    // Ray and box tests on an entity's model-sized box (rotated and scaled with the entity).
    internal static class PickBox
    {
        /// <summary>
        /// Does the ray pass through the box? center and size are in the entity's own (model)
        /// units. distance: how far along the ray it first enters the box.
        /// </summary>
        public static bool RayHits(Ray ray, Entity entity, Vector3 center, Vector3 size, out float distance)
        {
            distance = 0f;
            Transform transform = entity.Transform;
            Quaternion toLocal = transform.WorldRotation.Inverse;
            Vector3 scale = WorldScale(entity);
            if (Math.Abs(scale.X) < 1e-6f || Math.Abs(scale.Y) < 1e-6f || Math.Abs(scale.Z) < 1e-6f) return false;

            // The ray in the entity's own units. Direction is left unnormalised, so distances
            // along it are still world distances.
            Vector3 origin = Divide(toLocal * (ray.Origin - transform.WorldPosition), scale) - center;
            Vector3 direction = Divide(toLocal * ray.Direction, scale);

            float enter = 0f, exit = float.MaxValue;
            if (!Slab(origin.X, direction.X, size.X * 0.5f, ref enter, ref exit)) return false;
            if (!Slab(origin.Y, direction.Y, size.Y * 0.5f, ref enter, ref exit)) return false;
            if (!Slab(origin.Z, direction.Z, size.Z * 0.5f, ref enter, ref exit)) return false;
            distance = enter;
            return true;
        }

        /// <summary>Half the box's width along a world direction (length 1).</summary>
        public static float HalfExtentAlong(Entity entity, Vector3 size, Vector3 axis)
        {
            Quaternion rotation = entity.Transform.WorldRotation;
            Vector3 scale = WorldScale(entity);
            return Math.Abs(Vector3.Dot(rotation * new Vector3(1f, 0f, 0f), axis)) * Math.Abs(scale.X) * size.X * 0.5f
                 + Math.Abs(Vector3.Dot(rotation * new Vector3(0f, 1f, 0f), axis)) * Math.Abs(scale.Y) * size.Y * 0.5f
                 + Math.Abs(Vector3.Dot(rotation * new Vector3(0f, 0f, 1f), axis)) * Math.Abs(scale.Z) * size.Z * 0.5f;
        }

        // The entity's scale times its parents' (fine for the uniform scales used here).
        private static Vector3 WorldScale(Entity entity)
        {
            Vector3 scale = entity.Transform.Scale;
            for (Entity parent = entity.Transform.Parent; parent != null; parent = parent.Transform.Parent)
            {
                Vector3 p = parent.Transform.Scale;
                scale = new Vector3(scale.X * p.X, scale.Y * p.Y, scale.Z * p.Z);
            }
            return scale;
        }

        private static Vector3 Divide(Vector3 v, Vector3 by)
        {
            return new Vector3(v.X / by.X, v.Y / by.Y, v.Z / by.Z);
        }

        // One axis of the ray-box test: narrows [enter, exit] to where the ray is between the
        // box's two faces on this axis. False once the ray cannot be inside the box.
        private static bool Slab(float origin, float direction, float half, ref float enter, ref float exit)
        {
            if (Math.Abs(direction) < 1e-8f)
                return origin >= -half && origin <= half;   // parallel: inside the faces or never
            float a = (-half - origin) / direction;
            float b = (half - origin) / direction;
            if (a > b) { float swap = a; a = b; b = swap; }
            if (a > enter) enter = a;
            if (b < exit) exit = b;
            return enter <= exit;
        }
    }
}
