namespace Crucible.Scripts
{
    public sealed class ZooCombatSpawnPad : EntityScript
    {
        public Prefab enemyPrefab;
        public bool guaranteedTestDrop;
        public int maximumAlive = 4;
        readonly System.Collections.Generic.List<Entity> living = new System.Collections.Generic.List<Entity>();

        ZooPad pad;
        Entity spawnPoint, spawned;
        bool configureSpawn;

        public override void OnCreate()
        {
            pad = new ZooPad(this);
            spawnPoint = Entity.FindChild("Spawn Point");
        }

        public override void OnTriggerEnter(Entity other) { pad?.Enter(other); }
        public override void OnTriggerExit(Entity other) { pad?.Exit(other); }

        public override void OnUpdate(float deltaTime)
        {
            Enemy enemy = spawned != null ? spawned.GetComponent<Enemy>() : null;
            if (configureSpawn && enemy != null)
            {
                configureSpawn = false;
                enemy.Wake();
                if (guaranteedTestDrop) enemy.dropChancePercent = 100f;
            }
            if (pad.WasUsed(KeyCode.G)) SpawnEnemy();
        }

        void SpawnEnemy()
        {
            if (enemyPrefab == null || spawnPoint == null) return;
            for (int i = living.Count - 1; i >= 0; i--)
            {
                Entity occupant = living[i];
                if (!occupant.IsAlive || !occupant.IsActive || occupant.GetComponent<Enemy>()?.IsDead == true)
                    living.RemoveAt(i);
                else if ((occupant.Transform.WorldPosition - spawnPoint.Transform.WorldPosition).Length < 2f) return;
            }
            if (living.Count >= System.Math.Max(1, maximumAlive)) return;
            spawned = enemyPrefab.Instantiate(spawnPoint.Transform.WorldPosition, spawnPoint.Transform.WorldRotation);
            configureSpawn = spawned != null;
            if (configureSpawn) living.Add(spawned);
        }
    }
}
