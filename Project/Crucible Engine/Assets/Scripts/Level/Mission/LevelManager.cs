/*************************************************************************
* \file:         LevelManager.cs
* \brief         One place to read how a level is going: game time, the
*                player, objectives and the horde. Port of Unity's
*                LevelManager (Assets/Scripts/Level/Mission/LevelManager.cs).
*
* \details
*    OBSERVATION ONLY, as in Unity. It reads; it never changes the mission,
*    the waves, the drops or any enemy. The four tuning multipliers below
*    are stored for when the team has real numbers, and AppliesTuning is
*    false: nothing reads them yet. Wire them in where the value is used
*    (SpawnDirector for horde size, Enemy for HP / damage / drop chance)
*    once the progression rules are decided.
*
*    Use: drop Prefabs/Level/Level Manager into a level (one per scene; a
*    second one switches itself off). Other scripts read it with
*    LevelManager.Active, or listen to StateUpdated (once a frame).
*
*    What it reads, each frame:
*      Level       ElapsedGameSeconds - counts only while the level is being
*                  played: not while the pause menu ("PauseMenuParent"'s
*                  UICanvas) is open, not while loading, and not once the
*                  mission is complete or failed. Paused, MissionState,
*                  MissionFailed.
*      Player      the first player (SubLevelGenerator.Players, else the
*                  first entity tagged "Player"): Missing / Alive / Dead,
*                  health, position, velocity, movement state, grounded,
*                  quick boost, auto-run. PlayerCount / PlayersAlive for
*                  co-op.
*      Objectives  MissionController: capture areas done / total, the
*                  extract, the current area and its hold progress.
*      Horde       SpawnDirector: enemies alive (Enemy.All), total spawned,
*                  leashed, current wave and its resource left. Enemies:
*                  each living enemy's HP, drop chance and damage per hit
*                  (its catalogue weapon x weaponDamageMultiplier, as
*                  EnemyWeapon uses it; spinner charge damage separately).
*    Missing sources (a test scene, a level still loading) read as empty /
*    zero and are looked for again twice a second.
*
*    logSeconds > 0 writes a one-line summary to the console that often,
*    since the Inspector shows only the fields below, not these values.
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public enum LevelPlayerState { Missing, Alive, Dead }

    public sealed class LevelManager : EntityScript
    {
        // Sources: leave empty to find them by name.
        public Entity mission;                      // has MissionController ("Sub Level Generator" if empty)
        public Entity spawner;                      // has SpawnDirector (SpawnDirector.Instance if empty)

        // Future tuning - NOT APPLIED. Stored only; see AppliesTuning.
        public float dropRateMultiplier = 1f;
        public float hordeSizeMultiplier = 1f;
        public float enemyDamageMultiplier = 1f;
        public float enemyHealthMultiplier = 1f;

        public float logSeconds = 0f;               // > 0: console summary every this many seconds

        /// <summary>A living enemy as the level manager last read it.</summary>
        public struct EnemyInfo
        {
            public Enemy Enemy;
            public float CurrentHealth, MaximumHealth;
            public float DropChancePercent;
            public string WeaponId;                 // catalogue weapon, "" for an unarmed / legacy enemy
            public bool Melee;
            public float AttackDamage;              // one hit or shot: weapon damage x weaponDamageMultiplier (else its own damage)
            public float SplashDamage;              // splash weapons (cannon), same multiplier; 0 otherwise
            public float SpecialDamage;             // spinner charge damage; 0 if it has no special
        }

        const float SearchInterval = 0.5f;
        const string PauseMenuName = "PauseMenuParent";

        static LevelManager active;
        /// <summary>The scene's level manager, or null.</summary>
        public static LevelManager Active
        {
            get { return active != null && active.Entity != null && active.Entity.IsAlive ? active : null; }
        }

        /// <summary>Always false: the tuning multipliers are not used by anything yet.</summary>
        public bool AppliesTuning { get { return false; } }

        // Level
        public float ElapsedGameSeconds { get; private set; }
        public bool Paused { get; private set; }
        public MissionState MissionState { get; private set; }
        public bool MissionFailed { get; private set; }
        // Player
        public Entity Player { get; private set; }
        public MechPlayer PlayerMech { get; private set; }
        public LevelPlayerState PlayerState { get; private set; }
        public Vector3 PlayerPosition { get; private set; }
        public float PlayerHealth { get; private set; }
        public float PlayerMaximumHealth { get; private set; }
        public float PlayerHealthFraction { get; private set; }
        public Vector3 PlayerVelocity { get; private set; }
        public MechMovementState MovementState { get; private set; }
        public bool Grounded { get; private set; }
        public bool QuickBoosting { get; private set; }
        public bool Running { get; private set; }
        public int PlayerCount { get; private set; }
        public int PlayersAlive { get; private set; }
        // Objectives
        public int ObjectivesCompleted { get; private set; }   // capture areas + the extract
        public int ObjectivesTotal { get; private set; }
        public int AreasCompleted { get; private set; }
        public int AreasTotal { get; private set; }
        public int CurrentArea { get; private set; }            // 1, 2, 3 ...; 0 once every area is done
        public float CurrentAreaProgress { get; private set; }  // 0..1 of the current area's hold
        public float ObjectiveProgress { get; private set; }    // ObjectivesCompleted / ObjectivesTotal
        // Horde
        public int ActiveEnemies { get; private set; }
        public int TotalSpawned { get; private set; }
        public int TotalLeashed { get; private set; }
        public bool SpawningEnabled { get; private set; }
        public bool RoamingActive { get; private set; }
        public string CurrentWave { get; private set; }
        public int WaveResourceLeft { get; private set; }
        public IList<EnemyInfo> Enemies { get { return enemies.AsReadOnly(); } }

        // The sources it read from (null when missing).
        public MissionController Mission { get { return controller; } }
        public SpawnDirector Spawner { get { return director; } }

        /// <summary>Once a frame, after the values above are updated.</summary>
        public event Action<LevelManager> StateUpdated;

        readonly List<EnemyInfo> enemies = new List<EnemyInfo>();
        readonly Dictionary<string, MechPartDef> weapons = new Dictionary<string, MechPartDef>();
        MissionController controller;
        SubLevelGenerator generator;
        SpawnDirector director;
        UICanvas pauseCanvas;
        float nextSearch, nextLog;
        bool duplicate;

        public override void OnCreate()
        {
            if (Active != null && Active != this)
            {
                duplicate = true;
                Log.Warning("LevelManager: " + Entity.Name + " is a second level manager in this scene - switched off");
                return;
            }
            active = this;
            CurrentWave = "";
        }

        public override void OnDestroy()
        {
            if (active == this) active = null;
        }

        public override void OnUpdate(float deltaTime)
        {
            if (duplicate) return;
            Tick(deltaTime);
        }

        /// <summary>Reads everything again now without advancing the clock.</summary>
        public void Refresh() { Tick(0f); }

        void Tick(float deltaTime)
        {
            FindSources();

            MissionState = controller != null ? controller.State : MissionState.InProgress;
            MissionFailed = controller != null && controller.Failed;
            Paused = LoadingScreen.IsLoading || (pauseCanvas != null && pauseCanvas.Enabled);
            bool playing = !Paused && !MissionFailed && MissionState != MissionState.Complete;
            if (playing && deltaTime > 0f && !float.IsNaN(deltaTime) && !float.IsInfinity(deltaTime))
                ElapsedGameSeconds += deltaTime;

            ReadPlayers();
            ReadObjectives();
            ReadHorde();

            if (logSeconds > 0f && Time.ElapsedTime >= nextLog)
            {
                nextLog = Time.ElapsedTime + logSeconds;
                Log.Info(Summary());
            }
            if (StateUpdated != null) StateUpdated(this);
        }

        void FindSources()
        {
            if (controller != null && (controller.Entity == null || !controller.Entity.IsAlive)) controller = null;
            if (director != null && (director.Entity == null || !director.Entity.IsAlive)) director = null;
            if (controller != null && director != null && pauseCanvas != null) return;
            // Not every frame: a test scene may simply not have one.
            if (Time.ElapsedTime < nextSearch) return;
            nextSearch = Time.ElapsedTime + SearchInterval;

            if (controller == null)
            {
                Entity e = mission != null && mission.IsAlive ? mission : Entity.Find("Sub Level Generator");
                controller = e != null ? e.GetComponent<MissionController>() : null;
                generator = e != null ? e.GetComponent<SubLevelGenerator>() : null;
            }
            if (director == null)
                director = spawner != null && spawner.IsAlive ? spawner.GetComponent<SpawnDirector>() : SpawnDirector.Instance;
            if (pauseCanvas == null)
            {
                Entity menu = Entity.Find(PauseMenuName);
                pauseCanvas = menu != null ? menu.GetComponent<UICanvas>() : null;
            }
        }

        void ReadPlayers()
        {
            List<Entity> players = generator != null ? generator.Players() : new List<Entity>();
            if (generator == null)
                foreach (Entity e in Entity.FindAllWithTag("Player")) if (e != null && e.IsAlive) players.Add(e);

            PlayerCount = players.Count;
            PlayersAlive = 0;
            foreach (Entity p in players)
            {
                MechPlayer m = p.GetComponent<MechPlayer>();
                if (m == null || !m.IsDead) PlayersAlive++;
            }

            Player = players.Count > 0 ? players[0] : null;
            PlayerMech = Player != null ? Player.GetComponent<MechPlayer>() : null;
            MechHealth health = PlayerMech != null ? PlayerMech.Health : null;
            PlayerState = Player == null ? LevelPlayerState.Missing
                        : PlayerMech != null && PlayerMech.IsDead ? LevelPlayerState.Dead : LevelPlayerState.Alive;
            PlayerPosition = Player != null ? Player.Transform.WorldPosition : Vector3.Zero;
            PlayerHealth = health != null ? health.Current : 0f;
            PlayerMaximumHealth = health != null ? health.Maximum : 0f;
            PlayerHealthFraction = health != null ? health.Normalized : 0f;
            PlayerVelocity = PlayerMech != null ? PlayerMech.Velocity : Vector3.Zero;
            MovementState = PlayerMech != null ? PlayerMech.State : default(MechMovementState);
            Grounded = PlayerMech != null && PlayerMech.IsGrounded;
            QuickBoosting = PlayerMech != null && PlayerMech.IsQuickBoosting;
            Running = PlayerMech != null && PlayerMech.IsAutoRunning;
        }

        void ReadObjectives()
        {
            AreasCompleted = AreasTotal = ObjectivesCompleted = ObjectivesTotal = CurrentArea = 0;
            CurrentAreaProgress = ObjectiveProgress = 0f;
            if (controller == null) return;

            AreasCompleted = controller.AreasDone;
            AreasTotal = controller.AreasTotal;
            CurrentArea = controller.CurrentArea;
            bool hasExtract = false;
            foreach (MissionController.Zone z in controller.Zones)
            {
                if (z.IsExtract) hasExtract = true;
                else if (z.Order == CurrentArea) CurrentAreaProgress = MechMath.Clamp01(z.Progress);
            }
            ObjectivesTotal = AreasTotal + (hasExtract ? 1 : 0);
            ObjectivesCompleted = AreasCompleted + (MissionState == MissionState.Complete ? 1 : 0);
            ObjectiveProgress = ObjectivesTotal > 0 ? (float)ObjectivesCompleted / ObjectivesTotal : 0f;
        }

        void ReadHorde()
        {
            enemies.Clear();
            foreach (Enemy enemy in Enemy.All)
            {
                if (enemy == null || enemy.Entity == null || !enemy.Entity.IsAlive || !enemy.Entity.IsActive || enemy.IsDead) continue;
                MechPartDef weapon = Weapon(enemy.equippedWeaponId);
                float multiplier = enemy.weaponDamageMultiplier;
                enemies.Add(new EnemyInfo
                {
                    Enemy = enemy,
                    CurrentHealth = enemy.CurrentHealth,
                    MaximumHealth = enemy.MaximumHealth,
                    DropChancePercent = enemy.dropChancePercent,
                    WeaponId = enemy.equippedWeaponId ?? "",
                    Melee = enemy.melee,
                    AttackDamage = weapon != null ? weapon.Damage * multiplier : enemy.damage,
                    SplashDamage = weapon != null ? weapon.SplashDamage * multiplier : 0f,
                    SpecialDamage = enemy.previewSpinnerWindup ? enemy.chargeDamage : 0f,
                });
            }
            ActiveEnemies = enemies.Count;

            SpawningEnabled = director != null && director.Enabled;
            TotalSpawned = director != null ? director.TotalSpawned : 0;
            TotalLeashed = director != null ? director.TotalLeashed : 0;
            RoamingActive = director != null && director.RoamingActive;
            CurrentWave = director != null ? director.CurrentWave : "";
            WaveResourceLeft = director != null ? director.ResourceLeft : 0;
        }

        // Same lookup EnemyWeapon makes; null for no / an unknown id.
        MechPartDef Weapon(string id)
        {
            if (string.IsNullOrEmpty(id)) return null;
            MechPartDef def;
            if (!weapons.TryGetValue(id, out def))
            {
                def = MechPartCatalogue.Find(id);
                if (def != null && !def.IsWeapon) def = null;
                weapons[id] = def;
            }
            return def;
        }

        /// <summary>One line with the main values, as logSeconds prints it.</summary>
        public string Summary()
        {
            return "LevelManager: " + ElapsedGameSeconds.ToString("0.0") + " s" + (Paused ? " (paused)" : "")
                + " | mission " + MissionState + (MissionFailed ? " FAILED" : "")
                + " | objectives " + ObjectivesCompleted + "/" + ObjectivesTotal
                + (CurrentArea > 0 ? " (area " + CurrentArea + " " + (CurrentAreaProgress * 100f).ToString("0") + "%)" : "")
                + " | player " + PlayerState + " " + PlayerHealth.ToString("0") + "/" + PlayerMaximumHealth.ToString("0")
                + " (" + PlayersAlive + "/" + PlayerCount + " alive)"
                + " | enemies " + ActiveEnemies + ", spawned " + TotalSpawned + ", wave " + (CurrentWave.Length > 0 ? CurrentWave : "-")
                + " (" + WaveResourceLeft + " left)";
        }
    }
}
