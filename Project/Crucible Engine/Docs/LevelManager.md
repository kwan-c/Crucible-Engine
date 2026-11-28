# Level Manager

`Prefabs/Level/Level Manager` (script `Assets/Scripts/Level/Mission/LevelManager.cs`) is one place to read how a level is going: game time, the player, objectives and the horde. It is a port of Unity's `LevelManager` and, like that one, it **only observes**. It never changes the mission, the waves, drops or enemies.

## Adding it to a level

Drag **Prefabs/Level/Level Manager** into the scene as a root entity. Use one per scene; a second one logs a warning and switches itself off. It finds the `MissionController` (on **Sub Level Generator**), the **Spawn Director** and the **PauseMenuParent** by name, so nothing needs linking. Set **Mission** or **Spawner** in the Inspector only if those entities have other names. Capture.scene already has one.

Other scripts read it with `LevelManager.Active` (null when the scene has none) or subscribe to `StateUpdated`, which fires once a frame after the values are updated. `Refresh()` re-reads everything without advancing the clock.

## What it reads

| Group | Properties | Source |
|---|---|---|
| Level | `ElapsedGameSeconds`, `Paused`, `MissionState`, `MissionFailed` | Frame time. The clock stops while the pause menu is open, while loading, and once the mission is complete or failed. |
| Player | `Player`, `PlayerMech`, `PlayerState` (Missing / Alive / Dead), `PlayerHealth`, `PlayerMaximumHealth`, `PlayerHealthFraction`, `PlayerPosition`, `PlayerVelocity`, `MovementState`, `Grounded`, `QuickBoosting`, `Running`, `PlayerCount`, `PlayersAlive` | The first of `SubLevelGenerator.Players()` (else the first entity tagged **Player**) and its `MechPlayer`. |
| Objectives | `ObjectivesCompleted` / `ObjectivesTotal` (capture areas plus the extract), `AreasCompleted` / `AreasTotal`, `CurrentArea`, `CurrentAreaProgress`, `ObjectiveProgress` | `MissionController` |
| Horde | `ActiveEnemies`, `TotalSpawned`, `TotalLeashed`, `SpawningEnabled`, `RoamingActive`, `CurrentWave`, `WaveResourceLeft` | `SpawnDirector`, `Enemy.All` |
| Each enemy | `Enemies[i]`: `CurrentHealth`, `MaximumHealth`, `DropChancePercent`, `WeaponId`, `Melee`, `AttackDamage`, `SplashDamage`, `SpecialDamage` | The `Enemy` script. Damage per hit is the catalogue weapon's damage × `weaponDamageMultiplier`, the same sum `EnemyWeapon` uses. `SpecialDamage` is the spinner charge. |

A missing source, for example in a test scene, reads as empty or zero, and the manager looks for it again twice a second.

The Crucible Inspector only shows plain fields, so these values are not visible there. Set **Log Seconds** above 0 to print a one-line summary that often, for example:

```
LevelManager: 42.2 s | mission InProgress | objectives 1/4 (area 2 0%) | player Alive 240/250 (1/1 alive) | enemies 38, spawned 38, wave - (0 left)
```

## Tuning fields: not applied yet

**Drop Rate Multiplier**, **Horde Size Multiplier**, **Enemy Damage Multiplier** and **Enemy Health Multiplier** start at 1 and are only stored. `AppliesTuning` is `false`, and nothing reads them. Once there are real progression numbers, apply them where each value is used:

- **Horde size:** `SpawnDirector.ScaleWave` / `SetBudget`.
- **HP:** `CombatHealth.SetMaximum`.
- **Damage:** the enemy's `weaponDamageMultiplier`.
- **Drops:** the enemy's `dropChancePercent`.

At that point, change `AppliesTuning` to return true.

## Test

```
Tools/EnemyAIQA/Run.ps1 -Probe LevelManagerProbe -Scene "Game/Missions/Capture" -Label LevelManager -TimeoutSeconds 220
```

The probe runs the real Capture mission and checks each value against its source. It covers the clock, pause, player health after damage, objective counts through a real 40 s capture, the horde counts and per-enemy damage. It also sets every multiplier to 3× and checks that nothing changes.
