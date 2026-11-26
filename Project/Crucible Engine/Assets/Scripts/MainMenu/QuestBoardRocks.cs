/*************************************************************************
* \file:         QuestBoardRocks.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          26 November 2026
* \version       1.0
* \brief         The little ridge rocks on the main menu's quest table: the
*                first time the quests are shown they rise out of the tray in
*                a far-to-near wave, then QuestStoneDrop drops the stones.
*                Port of Unity's PreMissionBoardTerrain (layout and reveal;
*                not its "slide away and break" reaction to a picked quest).
*
* \details
*    Goes on the scene's "manager" entity next to QuestStoneDrop, which calls
*    Play(board, slabSpots) once its stones are laid out and waits while
*    IsRevealing; Settle() ends the rise at once (leaving the view mid-way).
*
*    Each rock is a copy of rockPrefab ("Board Rock": a MeshRenderer and a
*    MaterialParams) spawned as a child of the board ("QuestStones", in
*    metres), showing one of the ridge1..8 models. Those are Unity's ridge
*    shape (5 cross-sections of 5 points, flat shaded) built at unit length,
*    height and width (Models/MainMenu/BoardRidge01-08.glb), so each rock is
*    stretched to its own random size here, like Unity's per-rock mesh.
*
*    The layout is Unity's, in Unity's board units (x along the board
*    -7.2..8.35, z across it +-3.92), squeezed onto an oval inside our tray
*    floor (ovalHalfWidth x ovalHalfLength; the floor itself is a lopsided
*    rounded shape, so a rectangle let rocks into its cut corners: Hugo):
*      - anchor spots: two rim ranges along the long sides, one along the
*        right end, and a few inner outcrops; shuffled each run;
*      - each rock tries its anchor (jittered), then smaller sizes, then
*        anywhere on the board; its whole footprint must be inside the
*        oval, clear of every quest stone (slabClearance) and of every
*        other rock. A crowded board just gets fewer rocks;
*      - the front row (near the bottom of the screen) is lower, and the
*        right end's rocks run across the board instead of along it.
*
*    The reveal (Unity's Reveal/ApplyReveal): each rock starts below the
*    floor by its height plus a margin and rises over riseSeconds with a
*    smootherstep, starting up to revealStagger later the nearer it is
*    (plus a little randomness), then settlePause before the stones drop.
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public sealed class QuestBoardRocks : EntityScript
    {
        // ---- Set in the Inspector ----
        public Prefab rockPrefab;            // "Board Rock": one rock (MeshRenderer + MaterialParams)
        public Model ridge1;                 // BoardRidge01-08: each rock shows one at random
        public Model ridge2;
        public Model ridge3;
        public Model ridge4;
        public Model ridge5;
        public Model ridge6;
        public Model ridge7;
        public Model ridge8;

        // ---- Layout ----
        public int rockCount = 24;           // Unity's ridgeCount (at most 32); a crowded board gets fewer
        // Metres per Unity board unit, for the rocks' size: our tray floor is 1.06 m long, Unity's
        // board 15.55 units. Bigger = bigger rocks (their spots follow the oval, not this).
        public float rockScale = 0.068f;
        // The oval every rock stays inside, metres from QuestStones: half across (x) and half along (z).
        // The floor (measured from table.glb) is ~+-0.33 x +-0.52 with its bottom-left corner cut
        // in to ~(-0.22, -0.35); this oval clears it everywhere by 3-6 cm. Smaller = rocks further in.
        public float ovalHalfWidth = 0.27f;
        public float ovalHalfLength = 0.45f;
        // No rock comes nearer a quest stone's centre than this (metres): the stone's own reach
        // (~0.1 m at stoneScale 0.055, turned) plus Unity's 0.18-unit clearance.
        public float slabClearance = 0.11f;

        // ---- Unity's reveal timing ----
        public float riseSeconds = 0.9f;
        public float revealStagger = 0.38f;  // the nearest rocks start up to this much later
        public float settlePause = 0.1f;     // after the last rock, before the stones drop

        /// <summary>True while the rocks are rising; QuestStoneDrop waits for this to end.</summary>
        public bool IsRevealing { get; private set; }

        // Unity's sizes, in its board units: length along the ridge, width across it, height.
        private const float LengthMin = 0.9f, LengthMax = 1.8f;
        private const float WidthMin = 0.45f, WidthMax = 0.85f;
        private const float HeightMin = 0.7f, HeightMax = 1.9f;
        // Unity's board: x from -7.2 to 8.35 (centre 0.575, half 7.775), z +-3.92.
        private const float BoardCentreX = 0.575f, BoardHalfX = 7.775f, BoardHalfZ = 3.92f;

        private sealed class Rock
        {
            public Entity entity;
            public Vector3 rest;    // local to the board, metres
            public float depth;     // metres below rest it starts the rise
            public float delay;     // 0..1, times revealStagger
        }
        private readonly List<Rock> rocks = new List<Rock>();
        private float elapsed;

        // An on-screen footprint: the rock's flat box on the floor (board space, metres).
        private struct Box { public float minX, maxX, minZ, maxZ; }

        public override void OnUpdate(float deltaTime)
        {
            if (!IsRevealing) return;
            // Unity clamps the step too: a long frame (the first one after a load) must not skip the rise.
            elapsed += Math.Min(deltaTime, 0.05f);
            if (elapsed >= riseSeconds + revealStagger + settlePause) { Settle(); return; }
            Apply(elapsed);
        }

        /*********************************************************************
         \brief  Lays the rocks out around the quest stones (slabSpots: their
                 centres, local to board) and starts them rising. Once only.
                 Returns false when nothing will rise (already done, or no
                 prefab / models), so the caller need not wait.
         *********************************************************************/
        public bool Play(Entity board, List<Vector3> slabSpots)
        {
            if (rocks.Count > 0 || board == null) return false;
            if (rockPrefab == null) { Log.Warning("QuestBoardRocks: rockPrefab is not set (the 'Board Rock' prefab)."); return false; }
            List<Model> models = new List<Model>();
            foreach (Model m in new[] { ridge1, ridge2, ridge3, ridge4, ridge5, ridge6, ridge7, ridge8 })
                if (m != null) models.Add(m);
            if (models.Count == 0) { Log.Warning("QuestBoardRocks: no ridge models set (BoardRidge01-08)."); return false; }

            Build(board, slabSpots, models);
            if (rocks.Count == 0) return false;
            elapsed = 0f;
            IsRevealing = true;
            Apply(0f);   // under the floor before the first frame draws them
            return true;
        }

        /// <summary>Every rock to its resting spot at once (the rise is over or skipped).</summary>
        public void Settle()
        {
            if (!IsRevealing) return;
            IsRevealing = false;
            foreach (Rock rock in rocks) rock.entity.Transform.Position = rock.rest;
        }

        // ------------------------------------------------------------------ the layout

        /*********************************************************************
         \brief  Unity's BuildGeometry: one rock per anchor (24 by default),
                 each given up to 64 tries. Tries 0-9 jitter around the
                 anchor; from 10 the rock is smaller; from 14 it may go
                 anywhere on the board; from 32 smaller again. A try is kept
                 when the rock lies on the floor, clear of every quest stone
                 and of every rock already placed.
         *********************************************************************/
        private void Build(Entity board, List<Vector3> slabSpots, List<Model> models)
        {
            // Clock seed, so a new layout every run; the xor (Unity's) keeps it apart from the
            // stones' own clock-seeded layout made in the same frame.
            Random random = new Random(Environment.TickCount ^ 0x4A73C19);

            // Unity's anchors, board units: the two long rims, the right end, the inner outcrops.
            List<float[]> anchors = new List<float[]>();
            foreach (float z in new[] { 3.55f, -3.55f })
                foreach (float x in new[] { -6.7f, -4.1f, -2.3f, 2.3f, 4.1f, 6.7f }) anchors.Add(new[] { x, z });
            foreach (float z in new[] { -2.6f, -1.2f, 0.3f, 1.7f, 2.9f }) anchors.Add(new[] { 7.7f, z });
            foreach (float x in new[] { -3.5f, 3.5f })
                foreach (float z in new[] { -1.7f, 0f, 1.7f }) anchors.Add(new[] { x, z });
            anchors.Add(new[] { -6.9f, 0f });
            for (int i = anchors.Count - 1; i > 0; i--)   // Fisher-Yates: which anchors come first
            {
                int j = random.Next(i + 1);
                float[] swap = anchors[i]; anchors[i] = anchors[j]; anchors[j] = swap;
            }

            List<Box> occupied = new List<Box>();
            float gap = 0.04f * rockScale;   // Unity's 0.04-unit gap between two rocks
            for (int i = 0; i < Math.Min(rockCount, 32); i++)
            {
                float[] anchor = anchors[i % anchors.Count];
                for (int attempt = 0; attempt < 64; attempt++)
                {
                    float length = Between(random, LengthMin, LengthMax), width = Between(random, WidthMin, WidthMax);
                    if (attempt >= 10) { length *= 0.65f; width *= 0.75f; }
                    if (attempt >= 32) { length *= 0.70f; width *= 0.75f; }
                    float ux = anchor[0] + Signed(random) * 0.32f, uz = anchor[1] + Signed(random) * 0.20f;
                    if (attempt >= 14)   // the anchor is crowded: a small outcrop anywhere instead
                    {
                        ux = -6.8f + 14.8f * (float)random.NextDouble();
                        uz = -3.6f + 7.2f * (float)random.NextDouble();
                    }
                    // Lower along the front rim, so the near row does not hide the stones (Unity).
                    float height = Between(random, HeightMin, HeightMax) * (uz < -2.5f ? 0.65f : 1f) * (attempt >= 10 ? 0.7f : 1f);
                    if (attempt >= 32) height *= 0.7f;

                    // Our rock models run along their own x. Unity's yaw 0 runs along its board (our
                    // z), so ours is 90; the right end's rocks run across (Unity 90, ours 0).
                    float yaw = (ux > 7f ? 0f : 90f) + Signed(random) * 26f;
                    Vector3 spot = ToFloor(ux, uz, 0.045f * rockScale);
                    Box box = Footprint(spot, yaw, length * rockScale, width * rockScale);

                    if (!InOval(spot, yaw, length * rockScale, width * rockScale)) continue;   // past the oval
                    if (NearSlab(box, slabSpots)) continue;
                    Box padded = new Box { minX = box.minX - gap, maxX = box.maxX + gap, minZ = box.minZ - gap, maxZ = box.maxZ + gap };
                    if (Overlaps(padded, occupied)) continue;

                    Entity entity = rockPrefab.Instantiate(board);
                    if (entity == null) { Log.Warning("QuestBoardRocks: the rockPrefab could not be spawned."); return; }
                    Transform t = entity.Transform;
                    t.Rotation = Quaternion.Euler(0f, yaw, 0f);
                    t.Scale = new Vector3(length * rockScale, height * rockScale, width * rockScale);
                    MeshRenderer renderer = entity.GetComponent<MeshRenderer>();
                    if (renderer != null) renderer.Model = models[random.Next(models.Count)];
                    // Unity's per-rock shade: 74-94% of the stone colour.
                    MaterialParams shade = entity.GetComponent<MaterialParams>();
                    float g = Between(random, 0.74f, 0.94f);
                    if (shade != null) shade.Tint = new Color(g, g, g);

                    // Far rocks first: wave 0 at the far rim (top of the screen), 1 at the near one.
                    float wave = (BoardHalfZ - uz) / (2f * BoardHalfZ);
                    rocks.Add(new Rock
                    {
                        entity = entity,
                        rest = spot,
                        depth = (height + 0.18f) * rockScale,   // fully under the floor, plus Unity's margin
                        delay = Clamp01(wave * 0.75f + (float)random.NextDouble() * 0.25f),
                    });
                    occupied.Add(padded);
                    break;
                }
            }
        }   // QuestBoardRocks::Build()

        // Unity board units to our tray (QuestStones' space, metres). Unity's board runs along its x and
        // ours along z; seen from the quest camera our +x is up the screen and +z right, as Unity's +z
        // (far) and +x (right). Unity's board is a rectangle and our limit an oval, so the spot is
        // squeezed from the square onto the disc (u * sqrt(1 - v*v/2), v * sqrt(1 - u*u/2)): the rim
        // rows follow the oval's edge instead of sitting off its corners.
        private Vector3 ToFloor(float ux, float uz, float y)
        {
            float u = Clamp((ux - BoardCentreX) / BoardHalfX, -1f, 1f);   // along, -1..1
            float v = Clamp(uz / BoardHalfZ, -1f, 1f);                     // across, -1..1
            float along = u * (float)Math.Sqrt(1f - v * v * 0.5f);
            float across = v * (float)Math.Sqrt(1f - u * u * 0.5f);
            return new Vector3(across * ovalHalfWidth, y, along * ovalHalfLength);
        }

        // True when the rock's whole footprint - the four corners of its turned rectangle - is inside
        // the oval. Corners are enough: the oval is convex, so a rectangle with all four corners in it
        // is entirely in it.
        private bool InOval(Vector3 spot, float yaw, float length, float width)
        {
            double r = yaw * Math.PI / 180.0;
            float c = (float)Math.Cos(r), s = (float)Math.Sin(r);
            float halfLength = length * 0.53f, halfWidth = width * 0.5f;   // 0.53: the shape's jitter
            for (int corner = 0; corner < 4; corner++)
            {
                float lx = (corner & 1) == 0 ? halfLength : -halfLength;
                float lz = (corner & 2) == 0 ? halfWidth : -halfWidth;
                // The rock's own x (length) and z (width), turned by yaw about the up axis.
                float x = spot.X + lx * c + lz * s, z = spot.Z - lx * s + lz * c;
                float ex = x / ovalHalfWidth, ez = z / ovalHalfLength;
                if (ex * ex + ez * ez > 1f) return false;
            }
            return true;
        }

        // The flat box a rock covers: its length along its own x and width along its z (the models
        // reach 0.53 of their length with the shape's jitter), turned by yaw.
        private static Box Footprint(Vector3 spot, float yaw, float length, float width)
        {
            double r = yaw * Math.PI / 180.0;
            float c = (float)Math.Abs(Math.Cos(r)), s = (float)Math.Abs(Math.Sin(r));
            float hx = c * length * 0.53f + s * width * 0.5f;
            float hz = s * length * 0.53f + c * width * 0.5f;
            return new Box { minX = spot.X - hx, maxX = spot.X + hx, minZ = spot.Z - hz, maxZ = spot.Z + hz };
        }

        // True when the box comes within slabClearance of a quest stone's centre.
        private bool NearSlab(Box box, List<Vector3> slabSpots)
        {
            if (slabSpots == null) return false;
            foreach (Vector3 spot in slabSpots)
            {
                float dx = Math.Max(0f, Math.Max(box.minX - spot.X, spot.X - box.maxX));
                float dz = Math.Max(0f, Math.Max(box.minZ - spot.Z, spot.Z - box.maxZ));
                if (dx * dx + dz * dz < slabClearance * slabClearance) return true;
            }
            return false;
        }

        private static bool Overlaps(Box box, List<Box> others)
        {
            foreach (Box o in others)
                if (box.minX < o.maxX && o.minX < box.maxX && box.minZ < o.maxZ && o.minZ < box.maxZ) return true;
            return false;
        }

        // ------------------------------------------------------------------ the reveal

        // Unity's ApplyReveal: each rock up from depth below its spot, smootherstep, by its own delay.
        private void Apply(float time)
        {
            foreach (Rock rock in rocks)
            {
                float t = Clamp01((time - rock.delay * revealStagger) / Math.Max(0.01f, riseSeconds));
                float eased = t * t * t * (t * (t * 6f - 15f) + 10f);   // smootherstep: no jolt at either end
                rock.entity.Transform.Position = rock.rest - new Vector3(0f, rock.depth * (1f - eased), 0f);
            }
        }

        private static float Between(Random random, float min, float max) { return min + (max - min) * (float)random.NextDouble(); }
        private static float Signed(Random random) { return (float)random.NextDouble() * 2f - 1f; }
        private static float Clamp01(float v) { return Clamp(v, 0f, 1f); }
        private static float Clamp(float v, float min, float max) { return v < min ? min : v > max ? max : v; }

    }   // QUESTBOARDROCKS CLASS

}   // NAMESPACE CRUCIBLE.SCRIPTS
