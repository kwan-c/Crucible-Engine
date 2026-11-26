/*************************************************************************
* \file:         QuestDetails.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          26 November 2026
* \version       1.0
* \brief         The quest tablet on the main menu's quest table: when a
*                quest opens it drops in beside the stone with Unity's fall
*                and rattle, shows the quest, and its Accept / Decline stone
*                buttons lift under the mouse. Port of Unity's
*                PreMissionQuestDetailsSlab (its questDescSlabAnimFall clip).
*
* \details
*    Goes on the scene's "manager" entity next to QuestPicker, which calls
*    Show() part way through opening a quest (Unity: at 70% of the camera's
*    approach) and Hide() when it closes. Accept calls QuestPicker's
*    AcceptQuest(), Decline its Close() (as Back and Esc do).
*
*    The tablet is one copy of Xavier's QuestDescSlab prefab (the
*    questDescriptionSlab.glb model with TitleText, QuestTypeText,
*    DescriptionText, RewardText, quest_accept_button and
*    quest_decline_button), spawned hidden at start. Each text has its own
*    fixed spot (Hugo's layout), with room for a two-line title.
*    It reads with its own +Z up the page and its face on +Y, so it is
*    turned to the camera: +Y toward the lens, +Z up the screen. That puts
*    Accept on the right and Decline on the left, as in Unity.
*
*    Where it goes (Unity's RefreshPlacement): flat, tabletHeight above the
*    tray floor, centred in the screen area left..right x bottom..top
*    (Unity's 0.52..0.98 x 0.06..0.94, beside the open stone) and as big as
*    fits in it - all worked out for where the camera ends up, since it is
*    still moving in when the tablet appears.
*
*    Unity's fall clip, 0.5 s, in the tablet's own units:
*      0    - 0.167  drops from 19.36 above to 1.89, tipping back 60 degrees
*                    (it lands on its bottom edge);
*      0.167 - 0.25  rattles sideways (+-0.12);
*      0.283 - 0.417 falls flat;
*      0.417 - 0.5   rattles again (+-0.16).
*    The two falling stretches speed up (t*t) as the stone drop does; the
*    rattle keys are joined by straight lines.
*
**************************************************************************/
using System;

namespace Crucible.Scripts
{
    public sealed class QuestDetails : EntityScript
    {
        // ---- Set in the Inspector ----
        public Prefab tablet;                // "QuestDescSlab" (Assets/Prefabs/Levl_Select_Prefabs)
        public AudioClip landSound;          // start_quest_click.wav: Unity's briefing arrival
        public float landVolume = 1f;
        public AudioClip hoverSound;         // button_hover.mp3, when the mouse reaches a button
        public AudioClip clickSound;         // button_click.mp3, when one is clicked
        public float buttonVolume = 0.155f;  // Unity's menu one-shots (volume 1 through a 0.155 source)

        // ---- Where it sits (Unity's numbers) ----
        public float viewportLeft = 0.52f;   // the screen area it fits in, 0..1 across...
        public float viewportRight = 0.98f;
        public float viewportBottom = 0.06f; // ...and 0..1 up
        public float viewportTop = 0.94f;
        // Metres above the tray floor (Unity's 2.4 board units): clear of the rocks (up to ~0.13).
        public float tabletHeight = 0.16f;

        // ---- Buttons (Unity's highlight: up 0.65 -> 1.0 and 10% bigger) ----
        public float buttonLift = 0.35f;     // in the tablet's units
        public float buttonGrow = 0.1f;
        public float buttonSharpness = 12f;  // how quickly they follow the mouse

        // ---- Two-line titles ----
        // The prefab is laid out for a one-line title, in a bottom-aligned box, so a second line grows
        // up toward the border. A title that wraps moves the title and subtitle down lineDrop per
        // extra line instead: 0.5 puts a two-line title back at ~1.96 / subtitle ~1.17, where two-line
        // titles looked right (Hugo), under the prefab's one-line spot (2.46 / 1.668). The engine can't tell
        // a script how many lines it drew, so the title is word-wrapped here with an estimated
        // letter width: about half the font size ("Escort the Core" at 0.44 filled ~3.3 of the
        // 4.4-unit box in Hugo's screenshot). If a title wraps but doesn't move, raise letterWidth
        // a little; if a one-line title moves, lower it.
        public float letterWidth = 0.5f;     // a letter's (or space's) width, times the font size
        public float lineDrop = 0.5f;        // tablet units, per extra title line

        /// <summary>True while the tablet is out (falling or resting).</summary>
        public bool IsShown { get; private set; }

        // The tablet model's size across and up the page, in its own units (Xavier's PanelBoxSize).
        private const float TabletWidth = 6.1f, TabletHeight = 6.1f;
        // Turns the tablet to face a camera: its +Y to the camera's +Z (toward the viewer), its +Z to
        // the camera's +Y (up the screen). 180 degrees about (0, 1, 1); Xavier's texts use the same.
        private static readonly Quaternion FaceCamera = new Quaternion(0f, 0.70710678f, 0.70710678f, 0f);
        // The buttons' boxes in their models' units (Xavier's, for quest_accept.glb / quest_decline.glb).
        private static readonly Vector3 AcceptBoxCenter = new Vector3(-0.02f, 0.06f, -0.09f);
        private static readonly Vector3 AcceptBoxSize = new Vector3(2.48f, 0.3f, 0.66f);
        private static readonly Vector3 DeclineBoxCenter = new Vector3(-0.03f, 0.06f, 0.02f);
        private static readonly Vector3 DeclineBoxSize = new Vector3(2.6f, 0.3f, 0.74f);

        // Unity's questDescSlabAnimFall keys: time, position (x, y, z) and tilt about x (degrees).
        // `fall` marks a stretch that speeds up toward its end (gravity); the rest are straight lines.
        private struct Key
        {
            public float time, x, y, z, tilt; public bool fall;
            public Key(float time, float x, float y, float z, float tilt, bool fall = false)
            { this.time = time; this.x = x; this.y = y; this.z = z; this.tilt = tilt; this.fall = fall; }
        }
        private static readonly Key[] Fall =
        {
            new Key(0f,          0f,    19.36f, 0f,          0f,  true),   // drops in, tipping back
            new Key(0.16666667f, 0f,    1.89f,  -0.3077525f, -60f),        // on its bottom edge: rattle
            new Key(0.18333334f, 0.06f, 1.89f,  -0.23f,      -60f),
            new Key(0.2f,        0.02f, 1.89f,  -0.34f,      -60f),
            new Key(0.21666667f, -0.12f, 1.89f, -0.13f,      -60f),
            new Key(0.23333333f, 0.11f, 1.89f,  -0.36f,      -60f),
            new Key(0.25f,       0f,    1.89f,  -0.3077525f, -60f),
            new Key(0.28333333f, 0f,    1.89f,  -0.3077525f, -60f, true),  // falls flat
            new Key(0.41666666f, 0f,    0f,     0f,          0f),          // flat: rattle again
            new Key(0.43333334f, 0.07f, 0f,     -0.05f,      0f),
            new Key(0.45f,       -0.04f, 0f,    0.02f,       0f),
            new Key(0.46666667f, 0.08f, 0f,     0.16f,       0f),
            new Key(0.48333332f, 0f,    0f,     -0.11f,      0f),
            new Key(0.5f,        0f,    0f,     0f,          0f),
        };

        private QuestPicker picker;
        private AudioSource audio;
        private Entity root, acceptButton, declineButton;
        private Camera viewCamera;
        private Vector3 restPosition;          // where it rests, world
        private Quaternion restRotation;
        private float scale;                   // metres per tablet unit
        private float time;                    // into the fall
        private bool interactive;              // fallen: the buttons respond
        private Entity hoveredButton;
        private Button accept, decline;
        private Entity titleText, typeText;
        private Vector3 titleRest, typeRest;   // their prefab spots: the one-line layout (tablet units)

        public override void OnCreate()
        {
            picker = Entity.GetComponent<QuestPicker>();
            if (tablet == null) { Log.Warning("QuestDetails: tablet is not set (the 'QuestDescSlab' prefab)."); return; }
            root = tablet.Instantiate();
            if (root == null) { Log.Warning("QuestDetails: the tablet prefab could not be spawned."); return; }
            acceptButton = root.FindChild("quest_accept_button");
            declineButton = root.FindChild("quest_decline_button");
            accept = new Button(acceptButton);
            decline = new Button(declineButton);
            titleText = root.FindChild("TitleText");
            typeText = root.FindChild("QuestTypeText");
            if (titleText != null) titleRest = titleText.Transform.Position;
            if (typeText != null) typeRest = typeText.Transform.Position;
            root.SetActive(false);   // hidden until a quest opens

            // The quest stones' AudioSource (QuestStoneDrop plays its thuds there), else one here.
            QuestStoneDrop drop = Entity.GetComponent<QuestStoneDrop>();
            Entity stones = drop != null ? drop.stones : null;
            audio = (stones != null ? stones.GetComponent<AudioSource>() : null) ?? Entity.GetComponent<AudioSource>();
        }

        // ------------------------------------------------------------------ QuestPicker's calls

        /*********************************************************************
         \brief  Fills the tablet with `quest` and drops it in beside the open
                 stone, placed for the camera's final spot (`cameraPosition`,
                 with camera's own rotation) and floorY (the tray floor).
         *********************************************************************/
        public void Show(QuestInfo quest, Entity camera, Vector3 cameraPosition, float floorY)
        {
            if (root == null || quest == null || camera == null) return;
            viewCamera = camera.GetComponent<Camera>();
            if (viewCamera == null || !Place(camera.Transform.WorldRotation, cameraPosition, floorY)) return;

            SetText("TitleText", quest.Title);
            SetText("QuestTypeText", "SIDE QUEST / " + TypeLabel(quest.Type));   // Unity's "SIDE QUEST / Elimination"
            // Each text has its own fixed spot on the tablet (Hugo's layout: room for a two-line title,
            // the reward on its own line above the buttons). A tablet without a RewardText gets the
            // reward under the briefing instead.
            string reward = "Reward: " + quest.Reward + " Scraps";
            if (SetText("RewardText", reward)) SetText("DescriptionText", quest.Briefing);
            else SetText("DescriptionText", quest.Briefing + "\n\n" + reward);
            PlaceTitle(quest.Title);

            root.SetActive(true);
            accept.Reset();
            decline.Reset();
            hoveredButton = null;
            interactive = false;
            time = 0f;
            Pose(0f);   // at the top of the drop before it is first drawn
            IsShown = true;
            if (audio != null && landSound != null) { audio.Pitch = 1f; audio.PlayOneShot(landSound, landVolume); }
        }

        /// <summary>Gone at once (Unity's Hide), e.g. as the stone flies home.</summary>
        public void Hide()
        {
            if (root != null) root.SetActive(false);
            IsShown = false;
            interactive = false;
            hoveredButton = null;
        }

        // ------------------------------------------------------------------ every frame

        public override void OnUpdate(float deltaTime)
        {
            if (!IsShown) return;
            if (!interactive)
            {
                // Unity steps its clip with at most 0.05 s a frame, so a hitch can't skip the rattle.
                time += Math.Min(deltaTime, 0.05f);
                Pose(time);
                if (time >= Fall[Fall.Length - 1].time) interactive = true;
                return;
            }

            Entity under = ButtonUnderMouse();
            if (under != hoveredButton)
            {
                hoveredButton = under;
                if (under != null) Play(hoverSound);
            }
            float follow = 1f - (float)Math.Exp(-buttonSharpness * deltaTime);
            accept.Animate(hoveredButton == acceptButton, buttonLift, buttonGrow, follow);
            decline.Animate(hoveredButton == declineButton, buttonLift, buttonGrow, follow);

            if (hoveredButton == null || !Input.IsPressed(KeyCode.MouseLeft)) return;
            Play(clickSound);
            if (picker == null) return;
            if (hoveredButton == acceptButton) picker.AcceptQuest();
            else picker.Close();   // Decline: as Back does (it hides this tablet)
        }

        // ------------------------------------------------------------------ placing it

        /*********************************************************************
         \brief  Unity's RefreshPlacement for a camera at `cameraPosition`
                 turned `cameraRotation`: the tablet lies flat on a plane
                 tabletHeight above the floor, where the ray through the
                 middle of its screen area meets that plane, sized to fill
                 the area across or up, whichever runs out first.
         *********************************************************************/
        private bool Place(Quaternion cameraRotation, Vector3 cameraPosition, float floorY)
        {
            Ray centre, side, top, aim;
            float midX = (viewportLeft + viewportRight) * 0.5f, midY = (viewportBottom + viewportTop) * 0.5f;
            if (!viewCamera.ViewportPointToRay(new Vector2(0.5f, 0.5f), out centre) ||
                !viewCamera.ViewportPointToRay(new Vector2(1f, 0.5f), out side) ||
                !viewCamera.ViewportPointToRay(new Vector2(0.5f, 1f), out top) ||
                !viewCamera.ViewportPointToRay(new Vector2(midX, midY), out aim)) return false;

            // How wide and tall the view is at a depth of 1 (the rays' directions only depend on the
            // camera's rotation, which stays the same while it moves in).
            Vector3 forward = centre.Direction.Normalized;
            float sideDepth = Vector3.Dot(side.Direction, forward), topDepth = Vector3.Dot(top.Direction, forward);
            float aimDepth = Vector3.Dot(aim.Direction, forward);
            if (sideDepth <= 1e-4f || topDepth <= 1e-4f || aimDepth <= 1e-4f || aim.Direction.Y > -1e-4f) return false;
            float halfWidthAtOne = Math.Abs(Vector3.Dot(side.Direction, cameraRotation.Right)) / sideDepth;
            float halfHeightAtOne = Math.Abs(Vector3.Dot(top.Direction, cameraRotation.Up)) / topDepth;

            // From the camera's final spot along the aim ray down to the tablet's plane.
            float along = (floorY + tabletHeight - cameraPosition.Y) / aim.Direction.Y;
            if (along <= 0f) return false;   // the plane is not below the camera
            float depth = along * aimDepth;
            float fitAcross = (viewportRight - viewportLeft) * 2f * depth * halfWidthAtOne / TabletWidth;
            float fitUp = (viewportTop - viewportBottom) * 2f * depth * halfHeightAtOne / TabletHeight;

            scale = Math.Min(fitAcross, fitUp);
            restPosition = cameraPosition + aim.Direction * along;
            restRotation = cameraRotation * FaceCamera;
            root.Transform.Scale = new Vector3(scale, scale, scale);
            return true;
        }   // QuestDetails::Place()

        // The tablet `t` seconds into Unity's fall clip: offset and tilt in its own frame, so the
        // whole motion turns and scales with where it rests.
        private void Pose(float t)
        {
            int k = 0;
            while (k < Fall.Length - 2 && t >= Fall[k + 1].time) k++;
            Key a = Fall[k], b = Fall[k + 1];
            float u = Math.Max(0f, Math.Min((t - a.time) / (b.time - a.time), 1f));
            if (a.fall) u *= u;   // speeding up as it falls
            Vector3 offset = new Vector3(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u);
            float tilt = a.tilt + (b.tilt - a.tilt) * u;

            root.Transform.WorldPosition = restPosition + restRotation * (offset * scale);
            root.Transform.WorldRotation = restRotation * Quaternion.Euler(tilt, 0f, 0f);
        }

        // ------------------------------------------------------------------ helpers

        // The Accept or Decline button under the mouse (the nearer if both), or null.
        private Entity ButtonUnderMouse()
        {
            Ray ray;
            if (viewCamera == null || Input.IsPointerOverUI || !viewCamera.ScreenPointToRay(Input.MousePosition, out ray)) return null;
            float onAccept = float.MaxValue, onDecline = float.MaxValue;
            bool hitAccept = acceptButton != null && PickBox.RayHits(ray, acceptButton, AcceptBoxCenter, AcceptBoxSize, out onAccept);
            bool hitDecline = declineButton != null && PickBox.RayHits(ray, declineButton, DeclineBoxCenter, DeclineBoxSize, out onDecline);
            if (hitAccept && (!hitDecline || onAccept <= onDecline)) return acceptButton;
            return hitDecline ? declineButton : null;
        }

        // Sets the WorldText on the tablet's child `child`; false when there is no such text.
        private bool SetText(string child, string text)
        {
            Entity entity = root.FindChild(child);
            WorldText worldText = entity != null ? entity.GetComponent<WorldText>() : null;
            if (worldText == null) return false;
            worldText.Text = text ?? string.Empty;
            return true;
        }

        /*********************************************************************
         \brief  The title and subtitle at their prefab spots for a one-line
                 title, or lineDrop lower per extra line (down the page is the
                 tablet's -Z). Starts from the prefab spots every time, so a
                 short title after a long one goes back up.
         *********************************************************************/
        private void PlaceTitle(string title)
        {
            WorldText text = titleText != null ? titleText.GetComponent<WorldText>() : null;
            if (text == null) return;
            float down = (CountLines(title, text.FontSize, text.Size.X) - 1) * lineDrop;
            titleText.Transform.Position = titleRest - new Vector3(0f, 0f, down);
            if (typeText != null) typeText.Transform.Position = typeRest - new Vector3(0f, 0f, down);
        }

        // How many lines `text` takes in a box `width` wide at `fontSize`: a greedy word wrap (as the
        // engine's) with every letter and space letterWidth x fontSize wide.
        private int CountLines(string text, float fontSize, float width)
        {
            if (string.IsNullOrEmpty(text) || fontSize <= 0f || width <= 0f) return 1;
            float letter = letterWidth * fontSize;
            int lines = 1;
            float used = 0f;   // width of the current line so far
            foreach (string word in text.Split(' '))
            {
                if (word.Length == 0) continue;
                float wordWidth = word.Length * letter;
                if (used > 0f && used + letter + wordWidth > width) { lines++; used = wordWidth; }   // next line
                else used += (used > 0f ? letter : 0f) + wordWidth;
            }
            return lines;
        }

        private static string TypeLabel(QuestType type)
        {
            return type == QuestType.Koth ? "KOTH" : type == QuestType.Elimination ? "Elimination" : "Escort";
        }

        private void Play(AudioClip clip)
        {
            if (audio == null || clip == null) return;
            audio.Pitch = 1f;   // the stone thuds vary it
            audio.PlayOneShot(clip, buttonVolume);
        }

        // One stone button: rises and grows a little while hovered (eased toward it each frame).
        private sealed class Button
        {
            private readonly Entity entity;
            private readonly Vector3 restPosition, restScale;   // on the tablet
            private float amount;                               // 0 = at rest, 1 = fully raised

            public Button(Entity entity)
            {
                this.entity = entity;
                if (entity == null) return;
                restPosition = entity.Transform.Position;
                restScale = entity.Transform.Scale;
            }

            public void Reset()
            {
                amount = 0f;
                Apply(0f, 0f);
            }

            public void Animate(bool raised, float lift, float grow, float follow)
            {
                amount += ((raised ? 1f : 0f) - amount) * follow;
                Apply(lift, grow);
            }

            private void Apply(float lift, float grow)
            {
                if (entity == null) return;
                entity.Transform.Position = restPosition + Vector3.Up * (lift * amount);
                entity.Transform.Scale = restScale * (1f + grow * amount);
            }
        }

    }   // QUESTDETAILS CLASS

}   // NAMESPACE CRUCIBLE.SCRIPTS
