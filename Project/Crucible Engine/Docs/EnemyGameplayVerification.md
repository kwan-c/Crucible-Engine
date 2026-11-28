# Enemy gameplay completion and verification

## A. Baseline and current branch

Work started on a clean `Hung_M3` at `a4956fbb` (`weapons, enemies, fix health bar`). The restored weapon port, health HUD and teammate changes were already in that commit. This implementation is in the working tree. The local build uses `Build/Crucible.slnx`, Debug x64, Visual Studio 18 2026, `v145,host=x64,version=14.51` and C++23.

The repository already contained the movement controller and graph actions, horde pressure, ranged position planning, weapon catalogue and six armed prefabs. Git history traces the controller and coordination work to teammate commits including `c7fcda75` and `b7563a28`. The baseline Spinner could wind up on a Zoo rig. The combat pad, sockets and weapon tests provided a working starting point.

## B. Completed enemy features

| Feature | Previous status | Current behavior | Implementation location | Runtime evidence |
|---|---|---|---|---|
| Spinner charge | Wind-up preview | Telegraph, fixed-heading physical charge, contact damage and knockback, recovery and cancellation | `EnemySpinnerAttack.cs`, `EnemyDirectedMovement.cs`, Spinner graphs | 18 damage once on contact. A sidestep avoided the full charge. A shield blocked it. Wall, ledge, death and interruption cases passed. |
| Melee pressure and reservations | Existing pressure planner with incomplete slot/admission support | Nearby melee actors choose separate reachable positions. Optional slots limit simultaneous basic attacks and put waiting actors on an outer ring. | `EnemyHordeCoordinator.cs`, `EnemyAttackReservations.cs` | Sword and Mace tested approaching from one side, around cover, in both modes, and after a reservation holder died. |
| Ranged positioning | Existing planner. Axe and Heavy Crossbow lacked a useful minimum range. | Four shooters approach, retreat, space out and seek a clear shot. Each keeps its authored weapon. | `EnemyController.cs`, `EnemyRangedPositioning.cs`, two ranged prefabs | Every type found a position and attacked around solid cover. Every type also retreated and fired again with its planner off. |
| Special attack admission | No separate local special coordinator | Nearby Spinners targeting the same player share a fair queue and start interval. | `EnemySpecialCoordinator.cs`, `EnemyAttackReservations.cs` | Two Spinners took turns on one target, charged two different targets concurrently, and attacked independently with coordination off. |
| Inspector controls | Participation depended on actor/scene setup | Three independent checkboxes on `Enemy.cs`, all enabled by default and consulted by the relevant role | `EnemyController.cs` | Live field changes cleared stale goals. Disabling a host or a participant preserved ordinary attacks and navigation. |
| Encounter integration | Single-prefab spawners, Zoo placeholder choices and five placed Tutorial Maces | Six armed prefabs in existing repeatable encounters, actual prefab cost in the Zoo, coordinated Tutorial encounters | `EnemySpawner.cs`, `ZooEnemyWaves.cs`, four scenes | Both fixed-wave scenes produced all six. Zoo charged 25 points for one of each. A placed Tutorial Mace woke and hit the player. |

## C. Spinner implementation

The graph calls **Acquire and Approach → Request Spinner Charge → Finish Spinner Charge → Recover**. Admission checks the actor, target, readiness, facing/range, physical movement and lane, then requests a local turn if enabled. On admission it starts its 3.5 s cooldown once, locks a horizontal direction and shows an orange lane along it. The body visual spins and hops during a 0.9 s wind-up. The body and head reuse the preview rig.

The rigid body charges at 12 m/s for up to 0.75 s, approximately 9 m. Each update measures the actual segment traveled in physics. The attack checks that segment against the target capsule and checks for world cover. This also works when a Jolt body cast omits the player character controller. An accepted hit deals 18 damage once, starts a decaying 32 m/s knockback lasting up to 0.65 s, and ends the charge. The existing player health code rejects damage during the equipped Shield's active window. A guarded contact spends the one contact attempt and produces no push. The player uses its existing collision-aware movement call to apply knockback.

A miss, obstruction, unsupported ground, stall or timeout leads to recovery. The attack releases its special slot as the charge ends and holds the body for 0.8 s before returning navigation to the graph. Hit stop pauses the attack and motion. A shove, graph interruption, invalidated target, death or disable cancels the attack and its temporary state. The warning and cosmetic pose clear. The missing production torso and authored spin animation keep this actor on the Zoo test pad.

## D. Movement architecture

1. The State Graph supplies the current target. Behavior Graph actions request approach, retreat, a position, an attack or recovery.
2. `EnemyController.cs` selects the movement intent. The horde or ranged planner can supply a destination. `EnemyLocalPositionQuery.cs` checks ground, slope, clearance and the baked grid when available.
3. `Enemy.Move` issues ordinary orders to the existing `FlowFieldAgent`. Its component owns walking speed, avoidance and steering. A missing host or rejected candidate lets the actor fall back to ordinary movement. Wander restores the speed it temporarily reduced and honors newer speed edits from other systems.
4. On special admission, `HoldForSpecial` suspends ordinary navigation. `StartDirectedMovement` takes direction, speed and duration. `AdvanceDirectedMovement` returns the measured segment and checks supported travel. `ReleaseSpecialMovement` returns control when other holds, such as hit stop or a shove, allow it. Attack timing and damage stay in the Spinner script.

For movement changes, start with the methods and field locations in the [movement teammate handover](EnemyMovement.md).

## E. Coordination systems

| Selection in the Inspector | Participation and defaults | Fallback and cleanup |
|---|---|---|
| Sword, Mace or Spinner → **Enemy.cs → Use Horde Coordination** | Continuous pressure by default. The scene host plans within 10 m every 0.2 s with 1.8 m peer spacing. Optional **Reserved Positions** provides four attack slots, eight waiting slots on a 5.2 m ring, two concurrent basic melee attacks and 0.18 s start spacing. Spinner uses ordinary pressure before its special. | Disable participation or the host to use ordinary approach. Invalid or occupied points trigger another candidate or ordinary pursuit. Ended, cancelled and dead holders release their turns. |
| Any projectile enemy → **Enemy.cs → Use Ranged Positioning** | On each actor, the default planner interval is 0.35 s and peer spacing is 3 m. Checks clear shots, grounded positions, distance and same-target peer positions. Replans during cooldown when crowded. | Turning it off clears the cached position. Approach, retreat, facing, weapon timing and firing continue. A committed attack retains its aim. |
| Spinner → **Enemy.cs → Use Special Coordinator** | The scene host defaults to a 24 m radius, one simultaneous special per nearby same-target group and a 0.55 s minimum between starts. Spinner retries admission every 0.15 s. | Off or missing host permits independent specials. A waiting ticket expires after 0.65 s without a request. Start spacing survives early cancellation. Separate pools keep ordinary melee and special admissions independent. |

Each of the four integrated scenes uses one **Enemy Coordination** entity. Actor fields are authored on the prefab. Coordinator limits are authored on the host. Goals, active charges, cooldowns and tickets are runtime state. To restore participation defaults, enable the three checkboxes. The stock Inspector displays the public fields of the shared Enemy script for every role.

## F. Enemy configuration

Distances are metres. Speed is ordinary FlowField speed in m/s. **Min / attack / arrive** means retreat threshold, attack admission distance and transition from long-range chase to arrival. **Action / release** gives the armed attack duration in seconds and the normalized release point. Damage uses the actor's existing multiplier on the shared weapon definition. Drop percentages below are prefab settings. Dedicated weapon test pads can guarantee a drop.

| Enemy | HP / cost | Speed | Min / attack / arrive | Cooldown (s) | Action / release | Effective damage | Spread / drop |
|---|---:|---:|---|---:|---|---|---|
| Sword Fodder | 100 / 1 | 6 | 0 / 2 / 12 | 1.05 | 0.7 / 0.6 | 15 melee | 0° / 5% |
| Mace Fodder | 120 / 1 | 6 | 0 / 2.2 / 12 | 1.05 | 0.7 / 0.6 | 12.5 melee | 0° / 25% |
| Throwing Axe Fodder | 90 / 1 | 6 | 3 / 12 / 12 | 1.5 | 0.7 / 0.6 | 15 projectile | 0° / 5% |
| Small Crossbow Fodder | 80 / 2 | 5 | 5 / 8 / 14 | 5.333 | 1.0 / 0.7 | 20 projectile | 12° / 25% |
| Heavy Cannon | 320 / 10 | 3.5 | 11 / 18 / 24 | 2.5 | 1.4 / 0.7 | 45 direct + 27 splash, 4 m radius | 0° / 25% |
| Heavy Crossbow | 200 / 10 | 5 | 12 / 30 / 32 | 1.0 | 0.7 / 0.6 | 19.64 piercing | 0° / 5% |
| Spinner, Zoo rig | 120 / 2 | 4 | 0 / 8 / 12 | 3.5 | 0.9 s wind-up | 18 charge | fixed heading / 0% |

- **Preserved Crucible values.** All seven HP/cost pairs, the armed weapons, modifiers, attack timings, loot and weapon-side muzzle offsets. Detection/loss distances are 40/60 for six actors and 65/85 for Heavy Cannon. The four projectile travel limits are 32 m for Axe, 40 m for Small Crossbow, 40 m for Cannon and 80 m for Heavy Crossbow.
- **Small-enemy weapon mounts.** Sword, Mace, Throwing Axe and Small Crossbow use the Cannon's top mount, `Socket_Shoulder` (node 17 on `Models/ENEMY_Fodder.glb`). On each prefab, select **Body → Weapon Model** to edit the weapon's alignment. Sword, Mace and Axe keep their separate crystal head. The five placed Tutorial Maces use the same mount and alignment as the Mace prefab.
- **Unity reference applied.** Spinner ordinary speed is 4 m/s. The charge uses the timing, speed, push and damage in section C, a 0.8 m contact radius and a visual hop of 0.32 m at 2.5 Hz. The 24 m special radius, one local Spinner and 0.55 s start spacing also follow the reference. The original scripts are `Gam3333/Assets/Scripts/AI/BehaviorGraph/EnemySpinnerAttack.cs` and `Gam3333/Assets/Scripts/AI/Coordination/EnemySpecialCoordinator.cs`.
- **New tunable Crucible choices.** Axe now starts retreating at 3 m and can attack from 12 m. Heavy Crossbow starts retreating at 12 m and enters arrival at 32 m, with its existing 30 m attack range. These give both actors room to reposition. The planner's preferred ring is 55% of the way between its near and far candidate rings. For Axe, Small Crossbow, Cannon and Heavy Crossbow the preferred distances work out to approximately 8.1, 6.8, 15.0 and 22.1 m. Cover and peers can change the selected position. Section E lists the coordination defaults.

## G. Mission integration

| Location | What actually runs |
|---|---|
| **Zoo Scene** | The Small and Elite switches control the approved rosters. Four small actors cycle through one group and two heavy actors through the other. With both on, the groups alternate while budget permits. Existing limits are one spawn every 2 s, eight alive, 50 points per cycle and a 15 s refill delay. Each accepted spawn pays its own prefab cost once. The cost check completes before its graph runs. Deaths leave the living count. Spinner has a separate manual pad. |
| **Golem_test**, **Layers_test** | Each has two fixed-count spawners. The existing settings are three per wave per spawner, a 30 s interval and a 3 s stagger. The fodder roster cycles Mace, Sword, Throwing Axe and Small Crossbow. The elite roster cycles Cannon and Heavy Crossbow. Spawn points remain the spawners' authored children. These encounters count actors rather than spending a resource budget. |
| **TutorialMap** | Five authored Mace enemies keep their encounter gates, mounts and matching drops. The scene has a coordination host and no baked FlowField surface. The existing agent and local physics checks handle the tested encounter. |
| **Capture** (Scenes/Game/Missions) | The Spawn Director runs `Wave Capture` while an area is held, using the same `Prefabs/Golem *` prefabs as the Zoo plus `Prefabs/Zoo/Spinner Wind-up Test`. It has an **Enemy Coordination** host like the Zoo. Every new mission scene needs one too (see [Enemy movement](EnemyMovement.md)). |
| **Game Scene** | Contains menus/HUD-related objects and no implemented roaming or objective wave director. The referenced 90/10 roaming and 70/30 survival weighting, objective-associated spawn sites and mutual exclusion have no integration point here. The current graph target is the nearest player marker. Separate players can form independent coordination groups. Mission objectives require a future target provider. |

## H. Files modified

Paths in each row are relative to the stated directory. Every listed file under `Assets` also has a corresponding `.meta` at the same path with that suffix. Existing asset GUIDs are preserved. The three new script assets each have a new unique GUID. The 27 asset and metadata pairs include four scene files whose linked spawners or coordination hosts changed.

| Directory | Exact files | Reason |
|---|---|---|
| `Assets/Scripts/Enemy` | **New** `EnemyDirectedMovement.cs`, `EnemyAttackReservations.cs`, `EnemySpecialCoordinator.cs` | Isolate physical charges, share a fair admission implementation and expose the special host. |
| `Assets/Scripts/Enemy` | `Enemy.cs`, `EnemyController.cs` | Preserve lifecycle, graph ownership, ordinary movement and the three participation switches. Primary overlap with the movement teammate. |
| `Assets/Scripts/Enemy` | `EnemyLocalPositionQuery.cs`, `EnemyRangedPositioning.cs`, `EnemyHordeCoordinator.cs` | Ground and clearance checks for scaled actors, cached firing poses, melee pressure and optional reservations. Also coordinate edits here with the movement teammate. |
| `Assets/Scripts/Enemy` | `EnemyWeapon.cs`, `EnemySpinnerAttack.cs`, `EnemySpawner.cs` | Connect existing attacks to admission/cancellation, finish the Spinner attack, and cycle existing actor prefabs with death tracking. |
| `Assets/Scripts/AI` | `EnemyGraphActions.cs` | Give the existing Spinner sequence a dedicated, abortable charge completion action. |
| `Assets/Scripts/Player` | `MechPlayer.cs`, `MechWeaponEffects.cs` | Feed accepted knockback through the player motor and preserve the hook's movement ownership. |
| `Assets/Scripts/Zoo` | `ZooCombatSpawnPad.cs`, `ZooEnemyWaves.cs` | Repeatable group spawns, affordable roster selection, safe spawn ground and actual budget accounting. |
| `Assets/BehaviorGraphs`, `Assets/StateGraphs` | `Spinner Wind-up.behaviorgraph`, `Spinner Wind-up.stategraph` respectively | Preserve linked asset IDs, run the charge, and expose the state as **Spinner Charge**. |
| `Assets/Prefabs` | `Golem Throwing Axe.cprefab`, `Golem Heavy Crossbow.cprefab`, `Zoo/Spinner Wind-up Test.cprefab` | Author the two ranged bands and Spinner ordinary speed. |
| `Assets/Prefabs` | `Enemy Spawner Mace.cprefab`, `Enemy Spawner Cannon.cprefab` | Register four fodder and two heavy prefabs. |
| `Assets/Scenes` | `Golem_test.scene`, `Layers_test.scene` | Update each pair of placed spawners and add a coordination host. Linked versions match their source prefab hashes. |
| `Assets/Scenes/Game/TestFolder` | `Zoo Scene.scene`, `TutorialMap.scene` | Add coordination hosts. Connect the Zoo wave rosters and label its Spinner pad. |
| `Tools/EnemyAIQA` | **New** `Run.ps1`, `EnemyAIProbe.cs`, `EnemySpawnProbe.cs`, `WeaponPortSmoke.cs`, `WeaponPortSceneProbe.cs` | Reproducible isolated Game runs, movement and wave checks, and retained weapon/scene regression coverage. |
| `Docs` | **New** `EnemyMovement.md`, `EnemyGameplayVerification.md` | Movement teammate handover, tuning, evidence and test instructions. |

## I. Test results

**Build.** Engine, Editor, Game and the managed assemblies built using the pinned Debug x64 configuration. The Mono probe compilation prints the existing CS8001 SDK-path warning and succeeds. The Editor separately launched, initialized Mono and ImGui, opened the uninstrumented Zoo with 253 entities and 921 components, then shut down normally.

**Actual Game runtime.** Six runs launched the native Game with an isolated copy of its assets and a test-only managed script. Probes invoke the gameplay APIs for setup and input-like actions. The runtime executes the real Behavior Graphs, physics, attacks and rendering. The tests and synthetic targets are outside the production script assembly and scenes.

| Run | Passed | What it established |
|---|---:|---|
| Zoo enemy movement | 68 | Independent Spinner, physical travel, shield, hit stop, shove, dodging, death/cancellation, live speed edits, turn taking, multi-target admission, melee fallback when all positions are occupied, all four ranged enemies with cover and toggles, ledge safety, mixed group and Zoo budget. |
| Zoo weapon regression | 175 | Retained coverage across 23 player weapons, equip/fire, ammo/reload where applicable, health/HUD, shield, hook, hit reactions, six armed enemies, drops and mounts. Includes a normal camera hard-lock acquisition and the finished Spinner. |
| TutorialMap | 11 | Five authored Maces and drops, player/HUD initialization, coordination, one Mace acquiring and damaging the player, and health bar fill. |
| Player Showcase | 10 | Six approved animated pickups, right-shoulder Cannon attachment, player/HUD and isolation of Zoo-only content. |
| Golem_test | 10 | Authored spawner settings, six actual prefab types, coordination hosts and death tracking. The probe checks the original cadence, then accelerates it for the roster exercise. |
| Layers_test | 10 | Same checks on the separate authored scene and its own linked instances. |
| **Total** | **284** | **Zero failed checks.** |

The independent charge measured 11.97 m/s, and the dodge case traveled 8.98 m without damaging the player. The shield case lost zero HP. Two same-target Spinners both received repeat turns. A pair with separate targets charged concurrently. The 21-actor Zoo test used three of every type and observed 14 attacking. Whole-Game frame times across 281 Debug frames at 960 × 540 averaged 21.37 ms, with a 40.05 ms maximum. Those numbers cover one short run on this machine. AI-only cost, extended stability and performance on another machine are unmeasured.

**Asset checks.** At the end of the movement milestone, the 27 edited/new assets passed content-hash, path and GUID checks. Both six-actor rosters, the four placed wave spawner versions and the original actor child hierarchies were verified. The subsequent head-mount correction passed 179 Zoo weapon checks (including four animated comparisons to the Cannon socket) and 11 Tutorial checks.

**Still manual.** Physical keyboard/controller input, telegraph contrast from the camera, perceived knockback, sustained play and designer approval of the firing bands. The Zoo grid has 0.6 m body padding, smaller than some enemy bodies. A wide actor can be routed toward a gap it cannot fit through. Local clearance rejects unsafe destinations and charges. The mixed test covered a finite set of positions.

## J. Testing instructions

1. Open `Build/Crucible.slnx` in Visual Studio, select **Debug | x64**, make **Editor** the startup project and press **F5**. Open **Assets → Scenes → Game → TestFolder → Zoo Scene** and enter Play.
2. Step onto **Spinner Charge Test** and press **G**. Stay in the orange lane once, then sidestep a second charge after the lane appears. Expect one hit and a push on the first run. On a miss, the actor continues along the original line, stops and recovers. Equip Shield and raise it just before contact to compare.
3. For a mixed group, stand on the **Small Enemies** and **Elite Enemies** floor switches and press **F** on each. For a controlled group, use **G** on the individual combat pads. Press G again after the prior enemy moves 2 m clear of the spawn point. Each pad defaults to four living enemies.
4. Spawn Sword and Mace and draw them around cover. They should approach from separate angles where space allows. On the scene's **Enemy Coordination → Enemy Horde Coordinator.cs**, enable **Reserved Positions** and set **Maximum Melee Attacks** to 1 to see one basic melee turn at a time.
5. Spawn Throwing Axe or a crossbow. Move inside its minimum range, then back behind cover. Watch it retreat and choose a clear shot. Turn **Use Ranged Positioning** off on the spawned actor to compare ordinary ranged approach and retreat.
6. Spawn two Spinners close together and stay in their admission range. With **Use Special Coordinator** enabled, the two should take turns. Disable it on both to allow overlapping charges. Switch **Use Horde Coordination** independently on either actor to compare its approach to the same target.
7. Change actor settings on the prefab's root **Enemy.cs** outside Play and save the prefab. Walking speed is on **FlowFieldAgent**. Scene-wide limits are on the **Enemy Coordination** entity. For a narrow-gap check, lure a large golem toward a passage narrower than its body and compare with an open approach. The [handover](EnemyMovement.md) names the corresponding movement methods.

Run the probes from the engine root after building Debug. Run one invocation at a time because they share the isolated `Build/EnemyAIQA` directory. Logs are named by `-Label`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\EnemyAIQA\Run.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\EnemyAIQA\Run.ps1 -Probe WeaponPortSmoke -Label Weapons
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\EnemyAIQA\Run.ps1 -Probe EnemySpawnProbe -Scene Golem_test -Label GolemWaves
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\EnemyAIQA\Run.ps1 -Probe EnemySpawnProbe -Scene Layers_test -Label LayersWaves
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\EnemyAIQA\Run.ps1 -Probe WeaponPortSceneProbe -Scene "Game/TestFolder/TutorialMap" -Label Tutorial
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\EnemyAIQA\Run.ps1 -Probe WeaponPortSceneProbe -Scene "Game/TestFolder/Player Showcase" -Label Showcase
```

## K. Deferred work

- The `EnemyParent` refactor, restructuring `CombatHealth`, shared definition assets and a unified shared-weapon Inspector.
- The production Spinner torso and authored spin animation, and an additional artist-provided shoulder socket for alternating Heavy Cannon mounts. Spinner stays on the Zoo test pad. Heavy Cannon retains its single approved mount.
- Hammer Fodder, Orbiting Rock Elite, Throw-and-Pull Rock Elite, bosses, Cube Test and Zoo Fodder as gameplay archetypes.
- A production mission director, objective targets and roaming/survival wave rules. Current navigation uses its existing baked padding and the available gameplay physics queries.
