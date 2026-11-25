namespace Crucible.Scripts
{
    public sealed class ZooCombatSpawnPad : EntityScript
    {
        public Prefab enemyPrefab;
        public bool guaranteedTestDrop;

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
            Enemy prior = spawned != null ? spawned.GetComponent<Enemy>() : null;
            if (spawned != null && (prior == null || !prior.IsDead)) return;
            spawned = enemyPrefab.Instantiate(spawnPoint.Transform.WorldPosition, spawnPoint.Transform.WorldRotation);
            configureSpawn = spawned != null;
        }
    }
}
