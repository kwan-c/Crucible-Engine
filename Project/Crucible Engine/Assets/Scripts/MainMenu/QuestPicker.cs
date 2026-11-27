/*************************************************************************
* \file:         QuestPicker.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          26 November 2026
* \version       1.0
* \brief         Picking a quest on the main menu's quest table: the stone
*                under the mouse lifts and brightens; a click opens it (the
*                camera zooms in so the stone sits on the left of the
*                screen); Back, Esc or Decline sends it home.
*
* \details
*    Goes on the scene's "manager" entity next to MainMenuFlow and
*    QuestStoneDrop. MainMenuFlow calls Begin(camera) once the camera is over
*    the quest table and End() when it leaves; while a quest is open
*    (IsOpen) its Back button and Esc call Close() instead of leaving.
*    Nothing reacts until QuestStoneDrop has finished dropping the stones.
*
*    A quest opens like Unity's stone inspection (PreMissionStoneMotion),
*    in phases:
*      Press    0.08 s: the stone squashes a little under the click;
*      Open     0.72 s (smootherstep): it lifts inspectLift on a small arc,
*               turns straight (baseYaw) with a slight tilt and grows 4.5%,
*               while the camera eases down and across until the stone sits
*               at inspectViewportX of the screen, inspectWidth wide (both
*               measured from Hugo's video of the Unity prototype);
*      Return   0.64 s: all of that backwards, from wherever it got to;
*      Settle   0.12 s: a small squash as it lands, then browsing again.
*    Unity's lift and arc are in its board units; times 0.068 (the board's
*    scale here, see QuestBoardRocks.rockScale) they are metres.
*
*    Picking reuses Xavier's PickBox (LevelSelectController.cs): a ray from
*    the mouse against each stone's model-sized box, so no colliders.
*
*    Once the open move is briefingRevealAt of the way (Unity: 0.7), the
*    quest tablet drops in beside the stone (QuestDetails, same entity);
*    its Accept calls AcceptQuest(), its Decline Close(). Enter accepts too
*    (Unity's default).
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public sealed class QuestPicker : EntityScript
    {
        // ---- Hover (Hugo: lift a little and brighten) ----
        public float hoverLift = 0.01f;       // metres the stone under the mouse rises
        public float hoverSharpness = 12f;    // how quickly lift and brightness follow (Xavier's)

        // ---- Opening a quest (Unity's numbers) ----
        public float pressSeconds = 0.08f;
        public float openSeconds = 0.72f;
        public float returnSeconds = 0.64f;
        public float settleSeconds = 0.12f;
        public float inspectLift = 0.015f;    // metres up while open (Unity 0.22 board units)
        public float inspectArc = 0.0024f;    // metres of extra rise mid-move (Unity 0.035)
        public float inspectGrow = 0.045f;    // the open stone is this much bigger
        public float inspectTiltX = -5f;      // degrees, Unity's slight tilt of the open stone
        public float inspectTiltZ = 1.5f;
        // Where the open stone sits on screen: its centre at this fraction of the width (from the
        // left) and height, and its width this fraction of the screen's. Measured from the video.
        public float inspectViewportX = 0.25f;
        public float inspectViewportY = 0.5f;
        public float inspectWidth = 0.17f;
        public float briefingRevealAt = 0.7f; // how far into the open move the tablet starts to fall

        /// <summary>True from the click until the stone has settled back: Back closes the quest then.</summary>
        public bool IsOpen { get { return state != State.Off && state != State.Browsing; } }

        /// <summary>The open quest (null when none is open).</summary>
        public QuestInfo OpenQuest { get { return picked != null && picked.script != null ? picked.script.Quest : null; } }

        // Slab1.glb's box in its own units (min -1.338 -0.230 -1.109, max 1.331 0.281 1.128).
        private static readonly Vector3 SlabBoxCenter = new Vector3(-0.0036f, 0.0257f, 0.0095f);
        private static readonly Vector3 SlabBoxSize = new Vector3(2.6683f, 0.511f, 2.2376f);

        private enum State { Off, Browsing, Pressing, Opening, Open, Returning, Settling, Loading }

        // One stone: its resting pose (local to QuestStones, so in metres) and how lit up it is.
        private sealed class Slab
        {
            public Entity entity;
            public QuestSlab script;
            public Vector3 restPosition;
            public Quaternion restRotation;
            public Vector3 restScale;
            public float glow;     // 0..1: hover lift and brightness, eased
        }

        private QuestStoneDrop drop;
        private QuestDetails details;          // the quest tablet (optional)
        private bool detailsShown;             // the tablet has been dropped in for this opening
        private List<Slab> slabs;              // null until the drop has finished
        private State state = State.Off;
        private Entity cameraEntity;
        private Camera viewCamera;
        private Slab hovered, picked;
        private float timer;

        // The current move: the picked stone and the camera, from -> to.
        private Vector3 fromPosition, fromScale, cameraFrom, cameraHome, cameraOpen;
        private Quaternion fromRotation, openRotation;

        public override void OnCreate()
        {
            drop = Entity.GetComponent<QuestStoneDrop>();
            if (drop == null) Log.Warning("QuestPicker: no QuestStoneDrop on this entity.");
            details = Entity.GetComponent<QuestDetails>();
        }

        // ------------------------------------------------------------------ MainMenuFlow's calls

        /// <summary>The camera is over the quest table: start reacting (once the stones are down).</summary>
        public void Begin(Entity camera)
        {
            cameraEntity = camera;
            viewCamera = camera != null ? camera.GetComponent<Camera>() : null;
            if (viewCamera == null) { Log.Warning("QuestPicker: Begin needs the menu camera."); return; }
            state = State.Browsing;
        }

        /// <summary>Leaving the quest table: every stone back to rest, unlit; nothing reacts.</summary>
        public void End()
        {
            if (slabs != null)
                foreach (Slab slab in slabs) { slab.glow = 0f; PoseAtRest(slab); }
            if (picked != null && cameraEntity != null) cameraEntity.Transform.WorldPosition = cameraHome;
            if (details != null) details.Hide();
            picked = hovered = null;
            state = State.Off;
        }

        /// <summary>Back / Esc / Decline: the open stone (and the camera) go home, from wherever they are.</summary>
        public void Close()
        {
            if (state != State.Pressing && state != State.Opening && state != State.Open) return;
            if (details != null) details.Hide();   // Unity's: gone at once as the stone heads home
            CaptureFrom();
            state = State.Returning;
            timer = 0f;
        }

        // ------------------------------------------------------------------ every frame

        public override void OnUpdate(float deltaTime)
        {
            if (state == State.Off || state == State.Loading) return;
            if (slabs == null)
            {
                // The stones are still dropping (or the rocks rising): wait, then note where they rest.
                if (drop == null || drop.IsBusy || !drop.HasDropped) return;
                CaptureRest();
            }

            timer += deltaTime;
            switch (state)
            {
                case State.Browsing:
                    hovered = SlabUnderMouse();
                    if (hovered != null && Input.IsPressed(KeyCode.MouseLeft)) Pick(hovered);
                    break;

                case State.Pressing:
                {
                    // A quick squash under the click (Unity: 2.5% at the middle of the press).
                    float t = Math.Min(timer / pressSeconds, 1f);
                    float squash = 1f - 0.025f * (float)Math.Sin(t * Math.PI);
                    picked.entity.Transform.Scale = picked.restScale * squash;
                    if (t >= 1f) StartOpening();
                    break;
                }

                case State.Opening:
                {
                    float e = Ease(Math.Min(timer / openSeconds, 1f));
                    PosePicked(e, picked.restPosition + Vector3.Up * inspectLift, openRotation, picked.restScale * (1f + inspectGrow));
                    cameraEntity.Transform.WorldPosition = Lerp(cameraFrom, cameraOpen, e);
                    // Unity starts the briefing during the approach, once the stone is nearly there.
                    if (!detailsShown && e >= briefingRevealAt && details != null)
                    {
                        detailsShown = true;
                        float floorY = picked.entity.Transform.Parent != null ? picked.entity.Transform.Parent.Transform.WorldPosition.Y : 0f;
                        details.Show(OpenQuest, cameraEntity, cameraOpen, floorY);   // placed for where the camera ends up
                    }
                    if (e >= 1f) state = State.Open;
                    break;
                }

                case State.Open:
                    if (Input.IsPressed(KeyCode.Enter)) AcceptQuest();   // Unity: Enter accepts
                    break;

                case State.Returning:
                {
                    float e = Ease(Math.Min(timer / returnSeconds, 1f));
                    PosePicked(e, picked.restPosition, picked.restRotation, picked.restScale);
                    cameraEntity.Transform.WorldPosition = Lerp(cameraFrom, cameraHome, e);
                    if (e >= 1f) { state = State.Settling; timer = 0f; }
                    break;
                }

                case State.Settling:
                {
                    // Unity's landing: wider and flatter for a moment.
                    float t = Math.Min(timer / settleSeconds, 1f);
                    float c = 0.012f * (float)Math.Sin(t * Math.PI);
                    Vector3 s = picked.restScale;
                    picked.entity.Transform.Scale = new Vector3(s.X * (1f + c), s.Y * (1f - c), s.Z * (1f + c));
                    if (t >= 1f) { PoseAtRest(picked); picked = null; state = State.Browsing; }
                    break;
                }
            }
            UpdateGlow(deltaTime);
        }

        // ------------------------------------------------------------------ hover

        // Each stone eases toward lit (hovered, or the open one) or unlit; the unpicked ones also
        // lift with it. The picked stone's position belongs to its open / return move.
        private void UpdateGlow(float deltaTime)
        {
            float follow = 1f - (float)Math.Exp(-hoverSharpness * deltaTime);
            foreach (Slab slab in slabs)
            {
                float target = (slab == hovered && state == State.Browsing) || slab == picked ? 1f : 0f;
                slab.glow += (target - slab.glow) * follow;
                if (slab.script != null) slab.script.SetHighlight(slab.glow);
                if (slab != picked) slab.entity.Transform.Position = slab.restPosition + Vector3.Up * (hoverLift * slab.glow);
            }
        }

        // The nearest stone under the mouse, or null (also when the mouse is over the UI).
        private Slab SlabUnderMouse()
        {
            Ray ray;
            if (Input.IsPointerOverUI || !viewCamera.ScreenPointToRay(Input.MousePosition, out ray)) return null;
            Slab nearest = null;
            float nearestDistance = float.MaxValue;
            foreach (Slab slab in slabs)
            {
                float distance;
                if (slab.entity.IsActive && PickBox.RayHits(ray, slab.entity, SlabBoxCenter, SlabBoxSize, out distance)
                    && distance < nearestDistance)
                {
                    nearest = slab;
                    nearestDistance = distance;
                }
            }
            return nearest;
        }

        // ------------------------------------------------------------------ opening a quest

        private void Pick(Slab slab)
        {
            picked = slab;
            hovered = null;
            cameraHome = cameraEntity.Transform.WorldPosition;   // the quest view, to come back to
            state = State.Pressing;
            timer = 0f;
        }

        private void StartOpening()
        {
            CaptureFrom();
            // Straight (the drop's base turn, so the text reads upright) with Unity's slight tilt.
            float baseYaw = drop != null ? drop.baseYaw : 180f;
            openRotation = Quaternion.Euler(inspectTiltX, baseYaw, inspectTiltZ);
            cameraOpen = CameraOpenSpot();
            detailsShown = false;
            state = State.Opening;
            timer = 0f;
        }

        /*********************************************************************
         \brief  Where the camera goes so the open stone sits at
                 (inspectViewportX, inspectViewportY) on screen, inspectWidth
                 of the screen wide. The camera keeps its rotation (it looks
                 straight down here), so the sums use rays from its own
                 viewport: how wide the view is at a depth of 1, and the
                 direction from the lens through the stone's screen spot.
         *********************************************************************/
        private Vector3 CameraOpenSpot()
        {
            Ray centre, edge, aim;
            if (!viewCamera.ViewportPointToRay(new Vector2(0.5f, 0.5f), out centre) ||
                !viewCamera.ViewportPointToRay(new Vector2(1f, 0.5f), out edge) ||
                !viewCamera.ViewportPointToRay(new Vector2(inspectViewportX, inspectViewportY), out aim))
                return cameraHome;
            Vector3 forward = centre.Direction.Normalized;
            Vector3 right = cameraEntity.Transform.WorldRotation.Right;
            float edgeDepth = Vector3.Dot(edge.Direction, forward), aimDepth = Vector3.Dot(aim.Direction, forward);
            if (edgeDepth <= 1e-4f || aimDepth <= 1e-4f) return cameraHome;
            float halfWidthAtOne = Math.Abs(Vector3.Dot(edge.Direction, right)) / edgeDepth;   // tan(half the view's width)

            // The open stone, in the world: lifted from where it rests, turned straight, grown.
            Transform board = picked.entity.Transform.Parent != null ? picked.entity.Transform.Parent.Transform : null;
            Quaternion boardRotation = board != null ? board.WorldRotation : Quaternion.Identity;
            Vector3 lift = Vector3.Up * (inspectLift - hoverLift * picked.glow);
            Vector3 stone = picked.entity.Transform.WorldPosition + lift;
            Quaternion turned = boardRotation * openRotation;
            float scale = picked.restScale.X * (1f + inspectGrow);   // QuestStones cancels the table's scale
            float width = 2f * scale * (Math.Abs(Vector3.Dot(turned * new Vector3(1f, 0f, 0f), right)) * SlabBoxSize.X * 0.5f
                                      + Math.Abs(Vector3.Dot(turned * new Vector3(0f, 0f, 1f), right)) * SlabBoxSize.Z * 0.5f);

            // Deep enough that `width` fills inspectWidth of the screen, along the aim ray.
            float depth = width / (inspectWidth * 2f * halfWidthAtOne);
            return stone - aim.Direction * (depth / aimDepth);
        }   // QuestPicker::CameraOpenSpot()

        /// <summary>Accept (the tablet's button or Enter): remember the quest and load its level.</summary>
        public void AcceptQuest()
        {
            if (state != State.Open && state != State.Opening) return;
            QuestInfo quest = OpenQuest;
            if (quest != null && QuestBoard.Accept(quest)) state = State.Loading;   // the scene switches next frame
        }

        // ------------------------------------------------------------------ poses

        // Remembers where the picked stone and the camera are now: the start of the next move.
        private void CaptureFrom()
        {
            Transform t = picked.entity.Transform;
            fromPosition = t.Position;
            fromRotation = t.Rotation;
            fromScale = t.Scale;
            cameraFrom = cameraEntity.Transform.WorldPosition;
        }

        // The picked stone `e` (0..1, eased) of the way from the captured pose to the given one,
        // with Unity's arc: a little extra rise that peaks mid-move.
        private void PosePicked(float e, Vector3 position, Quaternion rotation, Vector3 scale)
        {
            Transform t = picked.entity.Transform;
            float crest = (float)Math.Sin(e * Math.PI);
            t.Position = Lerp(fromPosition, position, e) + Vector3.Up * (inspectArc * crest * crest);
            t.Rotation = Quaternion.Slerp(fromRotation, rotation, e);
            t.Scale = Lerp(fromScale, scale, e);
        }

        private static void PoseAtRest(Slab slab)
        {
            Transform t = slab.entity.Transform;
            t.Position = slab.restPosition;
            t.Rotation = slab.restRotation;
            t.Scale = slab.restScale;
            if (slab.script != null) slab.script.SetHighlight(0f);
        }

        // Once, after the drop: where each stone rests. They only move again through this script.
        private void CaptureRest()
        {
            slabs = new List<Slab>();
            foreach (Entity entity in drop.Slabs)
            {
                Transform t = entity.Transform;
                slabs.Add(new Slab
                {
                    entity = entity,
                    script = entity.GetComponent<QuestSlab>(),
                    restPosition = t.Position,
                    restRotation = t.Rotation,
                    restScale = t.Scale,
                });
            }
        }

        // Unity's PreMissionStoneMotion.Ease: smootherstep, no jolt at either end.
        private static float Ease(float t) { return t * t * t * (t * (t * 6f - 15f) + 10f); }

        private static Vector3 Lerp(Vector3 a, Vector3 b, float t) { return a + (b - a) * t; }

    }   // QUESTPICKER CLASS

}   // NAMESPACE CRUCIBLE.SCRIPTS
