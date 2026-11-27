/*************************************************************************
* \file:         QuestStoneDrop.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          26 November 2026
* \version       1.1   the board's ridge rocks (QuestBoardRocks) rise first;
*                      every stone lies wholly inside the floor oval;
*                      each stone gets a QuestBoard quest
* \brief         The main menu's quest table: the first time the camera
*                looks down at it, the quest stones drop onto it one after
*                another. Port of Unity's small-stone arrival
*                (PreMissionStoneMotion; no big stone, no aftershock).
*
* \details
*    Goes on the scene's "manager" entity; MainMenuFlow calls Play(camera)
*    when the camera reaches the quest view and Skip() when it leaves.
*
*    The stones are slabCount copies of the questSlab prefab ("Quest Slab",
*    see QuestSlab.cs), spawned as children of `stones` (a "QuestStones"
*    entity at the centre of the quests table's tray floor, scaled to cancel
*    the table's scale so its children are in metres); without a prefab,
*    `stones`' own children are used instead. Each drop lays them out afresh
*    (Hugo: random every run, like Unity's seeded board): a shuffled order,
*    a random spot inside areaHalfWidth x areaHalfLength at least minSpacing
*    from the others, a random turn up to maxYaw, size stoneScale. They are
*    hidden until their turn, then each one:
*      Fall     starts dropHeight above its spot (+-heightVariation) and
*               falls over ~fallSeconds, speeding up (t*t), tilting from a
*               start angle to its contact angle;
*      Impact   the slab's landing effect (QuestSlab.landingVfx), the
*               hitSound thud (volume hitVolume, a random pitch between
*               hitPitchMin and hitPitchMax) and a small camera shake;
*      Settle   over ~settleSeconds a decaying bounce, squash and wobble;
*      Gap      gapSeconds, then the next stone.
*    Each stone gets one of Unity's three arrival styles (light drop, tumble,
*    tilt and settle), chosen from its place in the drop order, with its
*    small variations seeded by its name.
*
*    The motion is in world space around each stone's rest pose, taken when
*    the drop starts: the tables are scaled, so an offset in the stones'
*    parent space would not be in metres.
*
*    With a QuestBoardRocks on the same entity, its ridge rocks rise out of
*    the tray first (laid out around the stones' spots) and the first stone
*    falls once they are up, as on Unity's board.
*
*    Unity: Animate(duration, t => ...) coroutines; here a small state
*    machine stepped by OnUpdate.
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public sealed class QuestStoneDrop : EntityScript
    {
        // ---- Set in the Inspector ----
        public Entity stones;                // "QuestStones": the slabs are spawned (or placed) as its children
        public Prefab questSlab;             // "Quest Slab": spawned slabCount times; empty = use stones' children
        public int slabCount = 6;            // Unity's board shows about six quests
        public AudioClip hitSound;           // the thud each landing (played at the camera on the SFX bus)

        // ---- The layout, new every drop (metres in QuestStones' space: x across the table, z along it) ----
        // Each stone's size. The script sets it on every slab, so the prefab root's own scale is ignored.
        public float stoneScale = 0.055f;    // (0 = keep the scene's)
        // Half a slab's thickness in its own model units (Slab1: 0.23 below its centre): times its
        // scale, how high its centre sits above the tray floor. Set here, not read from the prefab,
        // because the prefab editor saves a prefab's root at the origin.
        public float slabRestHeight = 0.23f;
        // Stone centres are picked within +-areaHalfWidth across the tray and +-areaHalfLength along it.
        public float areaHalfWidth = 0.2f;
        public float areaHalfLength = 0.4f;
        // ...and a spot is only taken when the whole stone (its turned footprint) is inside this oval,
        // metres from QuestStones: half across (x) and half along (z). The floor (measured from
        // table.glb) is ~+-0.33 x +-0.52 but rounded, with its bottom-left corner cut in to
        // ~(-0.22, -0.35), so the rectangle above alone let a stone hang over a corner (Hugo). This
        // is the largest oval that fits the floor; the stones are flat, so it can reach the rim.
        public float floorHalfWidth = 0.30f;
        public float floorHalfLength = 0.49f;
        // Half a stone's footprint in its own model units (Slab1: 1.34 along its x, 1.12 along its
        // z); times its scale, the rectangle checked against the oval.
        public float slabHalfX = 1.34f;
        public float slabHalfZ = 1.12f;
        public float minSpacing = 0.2f;      // between two stone centres, when the tray has room
        public float baseYaw = 180f;         // degrees every stone is turned first, so its text reads upright
                                             // from the quest camera (0 left it upside down)
        public float maxYaw = 30f;           // degrees each stone is turned at random, either way

        // ---- Unity's PreMissionStoneMotion / QuestBoardImpactFeedback small-stone numbers ----
        // Except the height: Unity's 0.85 m is above our quest camera (0.72 m over the tray), so they
        // would fall past the lens; from 0.4 m a stone comes into view about twice its size.
        public float dropHeight = 0.4f;      // metres above the rest spot
        public float heightVariation = 0.12f;// +- fraction per stone
        public float fallSeconds = 0.32f;
        public float timingVariation = 0.04f;// +- fraction of the fall per stone
        public float settleSeconds = 0.2f;
        public float bounce = 0.045f;        // metres, the first hop after landing
        public float tilt = 7f;              // degrees
        public float gapSeconds = 0.08f;     // before the next stone starts
        public float hitVolume = 0.22f;
        public float hitPitchMin = 0.945f;   // each landing picks a pitch between these
        public float hitPitchMax = 1.055f;   // (1 = as recorded, 2 = an octave up)
        public float shakeStrength = 0.012f; // metres the camera jitters (Unity's 0.018, a little weaker: Hugo)
        public float shakeSeconds = 0.16f;

        /// <summary>True once the stones have dropped (or been skipped): later visits find them in place.</summary>
        public bool HasDropped { get; private set; }

        /// <summary>True while the rocks rise or a stone is still falling or settling.</summary>
        public bool IsBusy { get { return waitingForRocks || current >= 0; } }

        /// <summary>The stones (Quest Slab instances), for QuestPicker.</summary>
        public IEnumerable<Entity> Slabs { get { foreach (Stone stone in list) yield return stone.entity; } }

        // One stone: where it rests and how it arrives.
        private sealed class Stone
        {
            public Entity entity;
            public Vector3 restPosition;
            public Quaternion restRotation;
            public Vector3 restScale;
            public float height, fall, settle, hop;
            public Vector3 startAngles, contactAngles;
        }
        private readonly List<Stone> list = new List<Stone>();
        private QuestBoardRocks rocks;      // optional: the ridge rocks that rise before the drop
        private bool waitingForRocks;       // laid out, the rocks still rising: no stone has fallen yet
        private Entity cameraEntity;
        private readonly Random random = new Random(3401);   // sound pick, pitch and shake (not the motion)

        private enum Phase { Fall, Settle, Gap }
        private int current = -1;           // the stone moving now; -1 = not dropping
        private Phase phase;
        private float timer;

        private bool shaking;
        private float shakeAge;
        private Vector3 shakeBase;          // the camera's position before the shake

        public override void OnCreate()
        {
            if (stones == null) { Log.Warning("QuestStoneDrop: stones is not set (the 'QuestStones' entity)."); return; }
            // Whatever is already under QuestStones is hidden; with a questSlab prefab it stays hidden
            // (hand-placed stand-ins), without one those children are the stones.
            List<Entity> placed = new List<Entity>();
            for (int i = 0; i < stones.Transform.ChildCount; i++)
            {
                Entity child = stones.Transform.GetChild(i);
                if (child == null) continue;
                child.SetActive(false);   // hidden until its turn to drop (or for good)
                placed.Add(child);
            }
            if (questSlab != null)
            {
                for (int i = 0; i < slabCount; i++)
                {
                    // A child of QuestStones, at the prefab's own offset (0.23 up: half a slab's
                    // thickness, so it rests on the floor once Scatter scales it).
                    Entity slab = questSlab.Instantiate(stones);
                    if (slab == null) { Log.Warning("QuestStoneDrop: the questSlab prefab could not be spawned."); break; }
                    slab.SetActive(false);
                    list.Add(new Stone { entity = slab });
                }
            }
            else
            {
                foreach (Entity child in placed) list.Add(new Stone { entity = child });
            }
            if (list.Count == 0) Log.Warning("QuestStoneDrop: no stones (set questSlab, or give QuestStones children).");
            rocks = Entity.GetComponent<QuestBoardRocks>();
        }

        public override void OnUpdate(float deltaTime)
        {
            UpdateShake(deltaTime);
            if (waitingForRocks)
            {
                if (rocks.IsRevealing) return;   // still rising
                waitingForRocks = false;
                Next();                          // they are up: the first stone falls
                return;
            }
            if (current < 0) return;
            timer += deltaTime;
            Stone stone = list[current];
            switch (phase)
            {
                case Phase.Fall:
                {
                    float t = timer / stone.fall;
                    if (t >= 1f)
                    {
                        Pose(stone, 0f, stone.contactAngles, 1f, 1f);
                        Impact(stone);
                        phase = Phase.Settle; timer = 0f;
                        break;
                    }
                    float fall = t * t;   // speeds up as it falls
                    Pose(stone, stone.height * (1f - fall), Lerp(stone.startAngles, stone.contactAngles, fall), 1f, 1f);
                    break;
                }
                case Phase.Settle:
                {
                    float t = timer / stone.settle;
                    if (t >= 1f) { Rest(stone); phase = Phase.Gap; timer = 0f; break; }
                    // Unity's settle: a decaying three-half-wave wobble, a hop on the first half, a squash.
                    float envelope = (1f - t) * (1f - t);
                    float wave = (float)Math.Sin(t * Math.PI * 3.0) * envelope;
                    float hop = Math.Max(0f, (float)Math.Sin(t * Math.PI * 2.0)) * envelope * stone.hop;
                    float squash = 0.018f * Math.Abs(wave);
                    Vector3 angles = stone.contactAngles * envelope + new Vector3(wave * tilt * 0.45f, 0f, -wave * tilt * 0.25f);
                    Pose(stone, hop, angles, 1f + squash, 1f - squash);
                    break;
                }
                case Phase.Gap:
                    if (timer >= gapSeconds) Next();
                    break;
            }
        }

        // ------------------------------------------------------------------ MainMenuFlow's calls

        /// <summary>The camera reached the quest table: drop the stones, the first time only.</summary>
        public void Play(Entity camera)
        {
            if (HasDropped || list.Count == 0) return;
            HasDropped = true;
            cameraEntity = camera;
            Scatter();
            // A fresh board: one quest per slab (QuestBoard's placeholders for now). The slabs are
            // still hidden, but their scripts already exist (only OnCreate waits), so they take it now.
            List<QuestInfo> offers = QuestBoard.Offers(list.Count);
            for (int i = 0; i < list.Count; i++)
            {
                QuestSlab slab = list[i].entity.GetComponent<QuestSlab>();
                if (slab != null) slab.SetQuest(offers[i]);
            }
            for (int i = 0; i < list.Count; i++)
            {
                Stone stone = list[i];
                // Where it rests NOW, after Scatter (the ground can have slid since boot; it is home
                // in this view). The motion below is around these world values.
                stone.restPosition = stone.entity.Transform.WorldPosition;
                stone.restRotation = stone.entity.Transform.WorldRotation;
                stone.restScale = stone.entity.Transform.Scale;
                Profile(stone, i);
            }
            current = -1;
            // The rocks first, laid out around where the stones will land (Scatter's spots); when
            // they are up, OnUpdate starts the drop. No rocks: the drop starts now.
            List<Vector3> spots = new List<Vector3>();
            foreach (Stone stone in list) spots.Add(stone.entity.Transform.Position);
            waitingForRocks = rocks != null && rocks.Play(stones, spots);
            if (!waitingForRocks) Next();
        }

        /// <summary>Leaving mid-drop: the rocks and every stone land at once and the camera stops shaking.</summary>
        public void Skip()
        {
            StopShake();
            if (rocks != null) rocks.Settle();   // no-op once they are up
            if (current < 0 && !waitingForRocks) return;
            // Waiting for the rocks: no stone has fallen yet, so all of them land (from 0).
            for (int i = Math.Max(current, 0); i < list.Count; i++) Rest(list[i]);
            current = -1;
            waitingForRocks = false;
        }

        // ------------------------------------------------------------------ the layout

        /*********************************************************************
         \brief  A new layout for this drop: shuffles the drop order, then puts
                 each stone at a random spot in the tray (local to QuestStones,
                 so in metres), turned a little, at stoneScale.
                 A spot is the best of 40 random tries: the first one at least
                 minSpacing from every stone already placed, or else the one
                 furthest from its nearest neighbour, so a crowded tray still
                 spreads the stones as far apart as it can. A try that would
                 put a corner of the stone outside the floor oval is skipped.
         *********************************************************************/
        private void Scatter()
        {
            Random layout = new Random();   // seeded from the clock: different every run

            // Fisher-Yates shuffle: a random drop order.
            for (int i = list.Count - 1; i > 0; i--)
            {
                int j = layout.Next(i + 1);
                Stone swap = list[i]; list[i] = list[j]; list[j] = swap;
            }

            List<Vector3> placed = new List<Vector3>();
            foreach (Stone stone in list)
            {
                Transform t = stone.entity.Transform;
                // Resting on the floor at its size: half its thickness above QuestStones (the floor).
                if (stoneScale > 0f) t.Scale = new Vector3(stoneScale, stoneScale, stoneScale);
                float height = slabRestHeight * t.Scale.Y;

                // If no try fits (an oval set far too small), the stone goes to the middle, unturned.
                Vector3 best = new Vector3(0f, height, 0f);
                float bestYaw = baseYaw;
                float bestGap = -1f;
                for (int attempt = 0; attempt < 40; attempt++)
                {
                    Vector3 spot = new Vector3(((float)layout.NextDouble() * 2f - 1f) * areaHalfWidth, height,
                                               ((float)layout.NextDouble() * 2f - 1f) * areaHalfLength);
                    // The turn is picked per try now: the footprint, and so the oval check, depends on it.
                    float yaw = baseYaw + ((float)layout.NextDouble() * 2f - 1f) * maxYaw;
                    if (!OnFloor(spot, yaw, t.Scale.X)) continue;   // a corner would hang over the rim
                    float gap = float.MaxValue;   // distance to the nearest stone already placed (flat)
                    foreach (Vector3 other in placed)
                    {
                        float dx = spot.X - other.X, dz = spot.Z - other.Z;
                        gap = Math.Min(gap, (float)Math.Sqrt(dx * dx + dz * dz));
                    }
                    if (gap > bestGap) { best = spot; bestYaw = yaw; bestGap = gap; }
                    if (gap >= minSpacing) break;   // good enough: take it
                }
                placed.Add(best);
                t.Position = best;
                t.Rotation = Quaternion.Euler(0f, bestYaw, 0f);
            }
        }   // QuestStoneDrop::Scatter()

        // True when the whole stone - the four corners of its footprint, turned by yaw - is inside the
        // floor oval. Corners are enough: the oval is convex, so a rectangle with all four corners in
        // it is entirely in it.
        private bool OnFloor(Vector3 spot, float yaw, float scale)
        {
            double r = yaw * Math.PI / 180.0;
            float c = (float)Math.Cos(r), s = (float)Math.Sin(r);
            float hx = slabHalfX * scale, hz = slabHalfZ * scale;
            for (int corner = 0; corner < 4; corner++)
            {
                float lx = (corner & 1) == 0 ? hx : -hx;
                float lz = (corner & 2) == 0 ? hz : -hz;
                // The stone's own x and z, turned by yaw about the up axis.
                float x = spot.X + lx * c + lz * s, z = spot.Z - lx * s + lz * c;
                float ex = x / floorHalfWidth, ez = z / floorHalfLength;
                if (ex * ex + ez * ez > 1f) return false;
            }
            return true;
        }

        // ------------------------------------------------------------------ the drop

        // The next stone starts falling (shown at the top of its fall), or the drop is over.
        private void Next()
        {
            current++;
            if (current >= list.Count) { current = -1; return; }
            Stone stone = list[current];
            stone.entity.SetActive(true);
            Pose(stone, stone.height, stone.startAngles, 1f, 1f);
            phase = Phase.Fall; timer = 0f;
        }

        // Unity's GetArrivalProfile: the style from the stone's order, the small variations from
        // its name (a fixed seed, so it lands the same way every run).
        private void Profile(Stone stone, int index)
        {
            int seed = 17;
            unchecked { foreach (char c in stone.entity.Name ?? string.Empty) seed = seed * 31 + c; }
            Random r = new Random(seed);
            float height = dropHeight * (1f + ((float)r.NextDouble() * 2f - 1f) * heightVariation);
            float time = 1f + ((float)r.NextDouble() * 2f - 1f) * timingVariation;
            float a = tilt * (r.Next(2) == 0 ? -1f : 1f);
            switch (index % 3)
            {
                case 1:   // Tumble
                    stone.height = height; stone.fall = fallSeconds * 1.02f * time; stone.settle = settleSeconds * 0.85f; stone.hop = bounce * 0.65f;
                    stone.startAngles = new Vector3(a, a * 0.25f, -a); stone.contactAngles = new Vector3(-a * 0.3f, 0f, a * 0.2f);
                    break;
                case 2:   // Tilt and settle
                    stone.height = height * 0.94f; stone.fall = fallSeconds * 0.88f * time; stone.settle = settleSeconds * 1.02f; stone.hop = bounce * 0.18f;
                    stone.startAngles = new Vector3(a * 0.5f, 0f, -a * 0.7f); stone.contactAngles = new Vector3(a * 0.65f, 0f, -a * 0.5f);
                    break;
                default:  // Light drop
                    stone.height = height; stone.fall = fallSeconds * 0.94f * time; stone.settle = settleSeconds * 0.7f; stone.hop = bounce;
                    stone.startAngles = new Vector3(a * 0.2f, 0f, a * 0.15f); stone.contactAngles = new Vector3(0f, 0f, 0f);
                    break;
            }
        }

        // A stone `up` metres above its rest spot, tilted by `angles` (degrees, around its own axes),
        // its width scaled by `wide` and its height by `tall` (the squash).
        private static void Pose(Stone stone, float up, Vector3 angles, float wide, float tall)
        {
            Transform t = stone.entity.Transform;
            t.WorldPosition = stone.restPosition + new Vector3(0f, up, 0f);
            t.WorldRotation = stone.restRotation * Quaternion.Euler(angles.X, angles.Y, angles.Z);
            t.Scale = new Vector3(stone.restScale.X * wide, stone.restScale.Y * tall, stone.restScale.Z * wide);
        }

        // Exactly the pose it was placed in (Unity's RestoreRest).
        private static void Rest(Stone stone)
        {
            stone.entity.SetActive(true);
            Transform t = stone.entity.Transform;
            t.WorldPosition = stone.restPosition;
            t.WorldRotation = stone.restRotation;
            t.Scale = stone.restScale;
        }

        private static Vector3 Lerp(Vector3 a, Vector3 b, float t) { return a + (b - a) * t; }

        // ------------------------------------------------------------------ landing feedback

        private void Impact(Stone stone)
        {
            // The slab's own landing effect (QuestSlab.landingVfx), if it is a Quest Slab.
            QuestSlab slab = stone.entity.GetComponent<QuestSlab>();
            if (slab != null) slab.Landed();

            PlayHitSound(stone);
            if (cameraEntity == null || shakeStrength <= 0f) return;
            if (!shaking) shakeBase = cameraEntity.Transform.WorldPosition;   // a new shake: remember the spot
            shaking = true;
            shakeAge = 0f;   // a landing during a shake restarts it from full strength
        }

        /*********************************************************************
         \brief  The landing thud (hitSound) at a random pitch between
                 hitPitchMin and hitPitchMax. Each thud is its own
                 PlayClipAtPoint sound, so its pitch is its own: a thud still
                 ringing keeps its pitch when the next stone lands.
         *********************************************************************/
        private void PlayHitSound(Stone stone)
        {
            if (hitSound == null) return;
            Entity ear = cameraEntity != null ? cameraEntity : stone.entity;
            Entity voice = Audio.PlayClipAtPoint(hitSound, ear.Transform.WorldPosition, hitVolume, AudioBus.SFX);
            if (voice == null) return;
            float low  = System.Math.Min(hitPitchMin, hitPitchMax);   // either way round works
            float high = System.Math.Max(hitPitchMin, hitPitchMax);
            float pitch = low + (float)random.NextDouble() * (high - low);
            voice.AudioSource.Pitch = System.Math.Max(pitch, 0.01f);   // Pitch also changes a sound already playing
        }

        // A small random jitter around the camera's spot, fading out over shakeSeconds.
        private void UpdateShake(float deltaTime)
        {
            if (!shaking) return;
            shakeAge += deltaTime;
            if (shakeAge >= shakeSeconds) { StopShake(); return; }
            float k = shakeStrength * (1f - shakeAge / shakeSeconds);
            Vector3 jitter = new Vector3((float)random.NextDouble() * 2f - 1f, (float)random.NextDouble() * 2f - 1f,
                                         (float)random.NextDouble() * 2f - 1f);
            cameraEntity.Transform.WorldPosition = shakeBase + jitter * k;
        }

        private void StopShake()
        {
            if (shaking && cameraEntity != null) cameraEntity.Transform.WorldPosition = shakeBase;
            shaking = false;
        }

    }   // QUESTSTONEDROP CLASS

}   // NAMESPACE CRUCIBLE.SCRIPTS
