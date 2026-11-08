using System;

namespace Crucible.Scripts
{
    // A blockage that clears once every enemy in a group is dead - the "defeat the enemies
    // to continue" gate.
    //
    // Setup:
    //   1. Give the enemies the gate waits for the same Wake Group (the Enemy script's field,
    //      e.g. "Part 5"). The same name a Checkpoint uses to wake them, so one name does both.
    //   2. Add this script to the blockage's collider entity (the "Blockage" child of a
    //      No_entry_UI) and type that name into Enemy Group.
    //   3. Drag what should disappear onto Hide - usually the whole No_entry_UI, so the
    //      collider, the flashing sign and its text all go together. Empty = just this entity.
    //
    // It only opens once it has seen at least one enemy of the group alive, so a typo in the
    // group name (or enemies that haven't spawned yet) never opens it straight away - it warns
    // instead.
    public sealed class EnemyGroupGate : EntityScript
    {
        public string enemyGroup = "";      // the enemies' Wake Group to wait for
        public Entity hide;                 // switched off when they're all dead (empty = this entity)

        bool sawEnemies, opened, warned;
        float lookedFor;

        public override void OnUpdate(float deltaTime)
        {
            if (opened) return;

            // Anyone of the group still standing? Dead ones count as gone straight away, even
            // while their bodies topple (they're removed a few seconds later).
            int alive = 0;
            foreach (Enemy enemy in Enemy.All)
                if (enemy.wakeGroup == enemyGroup && enemy.Entity.IsActive && !enemy.IsDead) ++alive;

            if (alive > 0) { sawEnemies = true; return; }

            if (!sawEnemies)
            {
                // Give spawning a moment before complaining.
                lookedFor += deltaTime;
                if (!warned && lookedFor > 2f)
                {
                    warned = true;
                    Log.Warning("[EnemyGroupGate] " + Entity.Name + ": no living enemy with Wake Group \"" + enemyGroup
                                + "\" - check the name matches the enemies'. The gate stays shut.");
                }
                return;
            }

            opened = true;
            Entity target = hide != null ? hide : Entity;
            Log.Info("[EnemyGroupGate] " + Entity.Name + ": every enemy in \"" + enemyGroup + "\" is dead - removing " + target.Name + ".");
            target.SetActive(false);   // switches off its collider and everything under it
        }
    }
}
