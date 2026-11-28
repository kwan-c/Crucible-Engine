# Manual-playtest fixes: completion report

Branch `Hung_M3` at `74f949d0`, all changes uncommitted. Engine, Editor and Game built with MSVC 14.51 (`v145,host=x64,version=14.51`), Debug x64. Unity reference: `E:\Project\Gam3\Gam3333` (scripts under `Assets/Scripts`). No C++, Editor, Inspector, RTTR, FlowField, compiler-configuration or GLB changes.

The Game target now builds as `Build/Game/Debug/Lithomachia.exe` (renamed in the main merge); `Tools/EnemyAIQA/Run.ps1` launches it when present.

## A. Issue table

| # | Issue | Cause | Changes | Test result | Remaining |
|---|---|---|---|---|---|
| 3 | Aim modes and target switching | Crucible had always-on soft aim plus an F toggle for hard lock; no Free Aim; hard-lock switching was Unity's mouse flick. Soft switching had no hysteresis. | Three modes on F / right-stick press (`MechTargetLock` v2, `MechAimModes.cs`). Shared hidden centre cursor with a 0.12 s dwell, current target wins ties, keep circle 1.25x. Hard lock: bounded look offset on top of camera tracking, re-based on a switch. Zoo pads claim F (`MechInputClaims`). | 30 automated checks pass in the real Game (modes, soft/hard switching, overlap, empty space, near/far, range, death, destruction, never the player, no camera snap). | Physical mouse/gamepad feel and the hard-lock look limits (40° / 15°) need a hands-on pass. |
| 4 | Dead player stuck, no respawn in the Zoo | No respawn path; death only stopped the mech. | `MechPlayer.Respawn` (`MechRespawn.cs`) reusing `RefreshLoadout(true)`; Zoo-only `ZooPlayerRespawn` on a new **Zoo Player Respawn** entity. | 16 checks pass (delay, safe spot 137 m from the horde, health, loadout kept, cooldowns, knockback, lock, hit stop, grounded/Neutral, HUD). | No death animation exists for the player (art). |
| 5 | Health bar has no delayed-damage segment | Crucible drew **Bar White** full width as a static track. | `MechTrailingBar.cs` (presentation only) drives **Bar White**; Unity timing 1 s / 0.65 s; hex colour fields on `MechHud`. | 12 checks pass (hold, extend, drain, small/large hits, healing, max-HP change, death, respawn; health never altered). | Colour taste. |
| 6 | Large Crossbow slants down on the shoulder | Both torso shoulder sockets are rolled about 20° outward-down (`Socket_Shoulder.L` x axis = (0.936, -0.349, -0.041)). The ported Unity mount is exact, so Unity inherits the same roll; the round Cannon hides it, the crossbow's 0.78 m limb span shows it. | Per-shoulder `socketPoses` on the Large Crossbow: ±20.5° roll about its own barrel axis through its centre, plus a 0.2 torso-unit lift so it clears the pauldrons. Prefab plus three inline scene copies. | Offline GLB evaluation: limb axis now level (y = 0) on both shoulders, barrel direction and centre unchanged, 0 body intersections over 104 idle/fire poses per side. Retained weapon suite still fires from the animated muzzle (section F). | Visual check in the Editor. Artist note: if shoulder weapons should sit level, author level shoulder sockets or a weapon-specific socket. |
| 7.1 | Telegraphs hard to read; wrong size | Default lit material at half alpha; lanes were sized in local units on half-scale golems (Sword, Mace, Axe), so they showed half the real area; melee lane was Unity's 3 × 2 m, not the strike box. | Existing glow materials per prefab (orange melee = Unity's colour, red ranged/splash, violet Spinner); lanes sized in world metres; melee lane = real strike box; lane at the measured feet height; Cannon splash disc; hidden on release, cancel and death. | Disc shown during wind-up and hidden after release (automated). | Readability against the final level is visual-only. |
| 7.2 | Enemy health bars: missing health not distinguishable | Single green cube; also mis-anchored on half-scale golems (both ends moved). | Root-scale-correct bar; new **Health Bar Missing** child (dark red) per prefab; colours are each prefab's MeshRenderer colours. | Exercised by every enemy run; no engine errors. | Visual check. |
| 8 | Enemy attacks silent | No enemy audio path. | `EnemyAudio.cs`: one 3D voice per enemy via `Audio.PlayClipAtPoint`, linear 4–40 m, started so the clip's peak lands on the release; pending sounds cut on cancel/death; 6-voice crowd cap. | One sound per attack and a cancelled pending sound (automated). | Loudness and mix by ear. Spinner has no clip. |
| 9 | Mace damage late | The Mace's own swing clip (`MaceArm_Fire`, 0.375 s, head lands at frame 7 = 0.29 s) started with the attack; damage released at 0.42 s, after the clip ended. Sword identical. | `weaponContactSeconds` delays the weapon swing so its contact frame meets the release (Mace 0.29 s, Sword 0.25 s). | Swing 0.134 s / hit 0.433 s (Mace), swing 0.184 s / hit 0.433 s (Sword); one hit; dodge and death-before-contact both deal nothing. | Visual sync check. |
| 10 | Throwing Axe fires from too far | 3 / 12 m band (preferred ≈ 8.1 m). | 2.5 / 7 m band, preferred 5 m (new **Preferred Range**), arrive 10 m. | First throw at 6.95 m. | Designer approval. |
| 11 | Ranged enemies stop attacking while retreating | `ApproachRanged` returned false and `RequestAttack` refused shots inside `minimumRange`, so the graph waited for the retreat to finish. | Attack validity separated from standing distance (`RangedShotValid`); new **Minimum Attack Distance** (Cannon 3 m). | All four types fire inside their minimum range, also with ranged positioning off. | Feel. |
| 12 | Heavy Cannon aims at the chest | Ranged aim = collider centre. | Ground-targeted splash committed at attack start (section D). | 13 checks: ground point, splash-only 27, escape, direct 45 + 27, Shield, airborne, slope, low cover. | Cliff edges in a real level. |
| 13 | Knockback | — | Unchanged by design. | Spinner knockback covered by the retained Zoo suite. | — |
| 14 | One of four Spinners never attacked | Not reproduced. | None (coordinator unchanged). | 4 Spinners, 40 s: 5–9 charges each; 17 contested grants all in ticket order; longest continuous wait 2.7 s. | Re-test in the scene where it was seen. |
| 15 | Coordination toggles | — | None. | Each toggle off still attacks. | — |

## B. Unity behaviour and intentional differences

**Aim modes** (`Targeting/MechTargetLockController.cs`). `LockMode` NoLockOn, FreeCameraLockOn, CameraLockOn; one F press (gamepad: right-stick press) advances one mode; play starts in FreeCameraLockOn. Lock range 60 m, screen padding 0.04, switch radius 0.025 and auto-aim radius 0.4 of the smaller screen side on the player prefab.

- *Free Aim*: no target, weapons aim at the screen centre, camera free.
- *Soft Lock*: every frame the enemy whose collider the centre ray crosses, else the one nearest the centre within the switch radius, wins at once; empty space keeps the current target while it stays in the auto-aim circle; otherwise the enemy nearest the centre of the circle. Weapons aim at the selection (AimSolver); camera free.
- *Hard Lock*: the camera looks at the target (rotation damping 0.35 s, orbit response 0.12 s, pitch 20°, height 1.5); the target is kept while alive and in range; death re-targets the nearest by world distance. Switching is a mouse flick: 80 px within 0.2 s at ≥120 px/s, in a 60° cone, 0.25 s cooldown, latched until the mouse rests.
- *Crosshair*: `LockOnCursor` swaps free / lock-on / camera-lock sprites and follows the target's aim point.

**Crucible differences.** Hard-lock switching uses the soft-lock cursor (the design owner's request): the mouse adds a look offset of up to 40° yaw / 15° pitch over the tracking, the same centre cursor and 0.12 s dwell pick the new enemy, and the camera then eases onto it. A dwell and a 1.25x keep circle were added against flicker; Unity switched on the first frame. A locked target close to the camera pivot moves little on screen, so switching away from a very near target needs a larger look offset.

**Health bar** (`UI/MechHealthHud.cs`, Player Health HUD prefab). The white bar holds the ratio before a hit; after 1 s without damage it lerps to the real health over 0.65 s; more damage restarts the delay from wherever the drain was; healing snaps it; a new maximum or death resets it. Colours: healthy (0.3, 0.95, 0.5), white bar white.

**Attacks and audio** (`AI/Behaviour/EnemyAttack.cs`, `Presentation/MechWeaponAnimator.cs`). Release at duration × releaseFrame (Mace and Sword 0.7 s × 0.6; Unity's Axe attacked from 3.2 m). For melee, Unity stretched the weapon's swing clip over the whole attack. The weapon's attack sound plays once at the release (melee only after its range and wall checks, ranged once the projectile spawns) from one voice per weapon (linear 4–40 m, fully 3D for enemies). Telegraph colour (1, 0.4, 0.12, 0.85) for every enemy, alpha fading in; melee lane 3 × 2 m; discs only for the rock elites. Enemy health bar: green line (0.3, 1, 0.35), 2 m, no missing segment.

**Other Crucible differences.** The Mace and Sword swing clips are delayed rather than stretched (keeps the authored speed). Sounds start early so their authored peak lands on the hit. Telegraph colour differs by attack type. Enemy bars show missing health. The Large Crossbow is levelled although Unity's mount has the same roll. The Cannon is ground-targeted (Unity aimed at the body).

## C. Configuration changes

**MechPlayer** (Inspector, new): `startAimMode` 1 (0 Free, 1 Soft, 2 Hard), `lockRange` 60, `autoAimRadius` 0.4, `targetSwitchRadius` 0.025, `targetSwitchDwell` 0.12 s, `softLockKeepScale` 1.25, `hardLookYawLimit` 40°, `hardLookPitchLimit` 15°, `hardLookReturnDelay` 0.35 s, `hardLookReturnTime` 0.3 s (existing hard-lock pitch/height/response unchanged).

**MechHud** (new): `trailingBarDelay` 1 s, `trailingBarDrainTime` 0.65 s, `healthBarColour` "4DF280", `lowHealthBarColour` "FF4D40", `trailingBarColour` "FFFFFF" (hex because the Inspector cannot edit Color fields).

**ZooPlayerRespawn** (Zoo Scene entity **Zoo Player Respawn**): `respawnDelay` 3 s, `safeEnemyDistance` 18 m, `useStartPosition` on, `spawnPointA` = child **Respawn Point B** at (-13, 1, 58), `spawnPointB` empty. Respawn keeps the current loadout and rearms it (Zoo testing workflow) and keeps the chosen aim mode.

**Enemy** (new fields, defaults keep old behaviour): `weaponContactSeconds` 0, `minimumAttackDistance` 0, `preferredRange` 0, `groundTargetSplash` off, `groundImpactOffset` -1 (auto), `groundProbeDepth` 8, `attackSound`, `attackSoundPeak` 0, `attackSoundVolume` 0.8, `attackSoundMinDistance` 4, `attackSoundMaxDistance` 40.

| Prefab | Changed |
|---|---|
| Golem Mace | `weaponContactSeconds` 0.29; release unchanged at 0.6 × 0.7 = 0.42 s (it already matched the body lunge, `Fodder_MeleeSlash` frame 15 at speed 1.19). Weapon swing before: 0.00–0.375 s, head landing at 0.29 s. After: 0.13–0.505 s, landing at 0.42 s. Mace_attack, peak 0.42. Orange glow telegraph. |
| Golem Sword | `weaponContactSeconds` 0.25 (swing from 0.17 s); StoneSword_attack, peak 0.18. Orange telegraph. |
| Golem Throwing Axe | Minimum 3 → 2.5, attack 12 → 7, arrive 12 → 10, preferred ≈8.1 → 5 m. Projectile travel 32 m, damage, cooldown, timing, spread 0 and drop unchanged. ThrowingAxe_attack, peak 0.42. Red telegraph. |
| Golem Crossbow | CrossbowArm_attack, peak 0.02. Red telegraph. |
| Golem Heavy Crossbow | Crossbow_attack, peak 0.02. Red telegraph. |
| Golem Cannon | `groundTargetSplash` on, `minimumAttackDistance` 3; Cannon_attack, peak 0. Red telegraph and new **Telegraph Splash** disc. Damage verified unchanged: 200 × 0.225 = 45 direct, 120 × 0.225 = 27 splash, 4 m radius. |
| Spinner Wind-up Test | Violet telegraph; no sound (no asset). |
| All seven | New **Health Bar Missing** child, colour (0.45, 0.06, 0.05). |
| Part Large Crossbow (+ Zoo, Showcase, TutorialMap inline pickups) | `socketPoses` ShoulderR -0.9444, -0.2979, -0.8212 / (0.0174, -0.0773, -0.0013, 0.9969) → -1.5500, -0.3258, -0.5093 / (0.0175, 0.1013, 0.0010, 0.9947); ShoulderL 0.8583, -0.2979, -0.8212 / same rotation → 1.4652, -0.3269, -0.5173 / (0.0167, -0.2534, -0.0037, 0.9672). |

## D. How the main mechanisms work

**Retreat-attacks.** `ApproachRanged` picks the same movement order as before (hold, planned spot, retreat, chase). Its return value is now `RangedShotValid`: inside Attack Range, outside Minimum Attack Distance, facing, clear line. The graph's Request node therefore starts an attack whenever the weapon is ready, also mid-retreat; **Wait For Weapon Attack** holds still for the wind-up and release (the shared Fodder body has no upper-body layer to fire on the move), and **Recover** resumes the same retreat request. Cooldowns, telegraphs, committed aim and release timing are unchanged. No graph asset changed.

**Cannon ground targeting.** At attack start the Cannon raycasts straight down from the target's centre against the level layers (not bodies), at a point moved toward the Cannon by capsule radius + shell radius + 0.15 m (1.35 m for the Zoo mech), so the shell meets the ground before the player's capsule. The point is committed and shown as a 4 m red disc during the 0.98 s wind-up. The shell flies at the point one shell radius above it (so its sphere touches down there), bursts on the first contact and splashes around the actual impact point. A shell that reaches the point untouched bursts there. Direct hits on the target use its real capsule. If no ground is found within 8 m, or level geometry blocks the line to the point, the Cannon falls back to its ordinary body shot. Enemy splash only damages its target, as before.

**Spinner starvation: not confirmed.** With four Spinners for 40 s, the coordinator's queue served all 17 contested grants in ticket order and no continuously eligible Spinner waited longer than 2.7 s. Charge counts differ (5 to 9) because Spinners that overshoot on a charge spend time out of the 8.25 m admission range walking back. That is legitimate ineligibility, so the coordinator was not changed.

**Coordination off.** Horde off: Sword uses ordinary navigation and attacks. Ranged Positioning off: the cached spot is cleared and approach, retreat, facing and firing continue. Special Coordinator off: the Spinner charges on its own readiness and cooldown.

## E. Files

Paths under `Project/Crucible Engine`.

- **New scripts:** `Assets/Scripts/Player/MechAimModes.cs`, `MechInputClaims.cs`, `MechRespawn.cs`, `MechTrailingBar.cs`; `Assets/Scripts/Enemy/EnemyAudio.cs`; `Assets/Scripts/Zoo/ZooPlayerRespawn.cs` (with `.meta`).
- **Changed scripts:** `Player/MechTargetLock.cs`, `MechPlayer.cs`, `MechHud.cs`, `MechLoadout.cs` (Rearm/Reset), `MechMath.cs` (hex colours), `MechProjectile.cs` (ground target); `Enemy/Enemy.cs`, `EnemyWeapon.cs`, `EnemyAnimation.cs`, `EnemySpinnerAttack.cs` (sound hook, release count), `EnemyController.cs`*, `EnemyRangedPositioning.cs`*; `Zoo/ZooPad.cs`, `ZooCombatSpawnPad.cs`.
- **Prefabs:** Golem Sword, Mace, Throwing Axe, Crossbow, Cannon, Heavy Crossbow; `Zoo/Spinner Wind-up Test`; `Parts/Part Large Crossbow`.
- **Scenes:** Zoo Scene (respawn entity, crossbow pickup), Player Showcase (crossbow pickup pose), TutorialMap (crossbow pickup pose; the **Enemy Coordination** host restored from `b3458dca` after the main merge dropped it; its five placed Maces re-synced to the updated Golem Mace prefab by opening and saving the scene in the Editor).
- **QA and docs:** `Tools/EnemyAIQA/Run.ps1` (Lithomachia.exe, new fixtures), new `PlaytestFixesProbe.cs`, `EnemyAIProbe.cs` (two ranged checks follow the new firing rule), `WeaponPortSmoke.cs` (stationary Cannon damage 72 → 27); `Docs/EnemyMovement.md`, this file.

\* Overlap with the movement teammate: `ApproachRanged`'s return value and `RequestAttack` (EnemyController), and the preferred ring (EnemyRangedPositioning). Steering, avoidance and flow-field code are untouched.

## F. Test results

**Compile.** All gameplay scripts and the three probes compile with the shipped Mono `mcs` against the current ScriptCore. The Game target and the Editor's managed scripts rebuilt without errors.

**Automated runtime (real Game, isolated Zoo copy).** New `PlaytestFixesProbe`: 99 passed, 0 failed. Baseline suites: see the table below.

| Run (label) | Passed | Failed | What it covers |
|---|---:|---:|---|
| Zoo enemy movement (`FinalZoo`) | 68 | 0 | Spinner, coordination, all four ranged enemies with cover and toggles (two checks updated for the new firing rule), mixed group, Zoo budget. |
| Zoo weapon regression (`FinalWeapons`) | 179 | 0 | 23 player weapons, six armed enemies, drops, mounts, animated muzzles including the Large Crossbow on its new pose. |
| Golem_test (`FinalGolem`) | 10 | 0 | Authored spawners, six prefab types, coordination host, death tracking. |
| Layers_test (`FinalLayers`) | 10 | 0 | Same on the second scene. |
| TutorialMap (`FinalTutorial3`) | 11 | 0 | Five placed Maces and drops, player/HUD, coordination, a Mace damaging the player. The first run failed "Tutorial scene provides horde coordination": the merge from main (`b2210ac1`) had dropped the **Enemy Coordination** entity added in `b3458dca`. It was restored unchanged; this rerun is on the final scene, after the Editor re-sync of the placed Maces. |
| Player Showcase (`FinalShowcase`) | 10 | 0 | Animated pickups, right-shoulder Cannon, Zoo-only content isolation. |
| **Total** | **288** | **0** | Plus 99/99 in the new playtest probe. No engine errors logged in any run. |

**Offline geometry.** Large Crossbow orientation and body clearance (above).

**Input-driven gameplay, visual, audio: not verified.** Mode changes were driven through the targeting API and the hard-lock look offset through MechPlayer's own fields; no physical keyboard, mouse or gamepad input was used. No visual or listening check was done: telegraph readability, health-bar colours, crossbow alignment on screen, camera feel, sound timing and loudness all still need a person.

**Manual tests still to do.** In the Zoo: press F through the three modes (also on a floor pad, where F must not change mode); switch in each mode while moving and firing; die and respawn; watch the trailing bar; equip the Large Crossbow on both shoulders; fight each enemy type and listen; let four Spinners and a mixed group attack.

## G. Remaining work by owner

- **TutorialMap re-sync (done).** The scene was opened in the Editor, which updated its five placed Mace instances to the changed prefab, and saved. The Editor's re-sync dropped the stored whole-list override (`ScriptComponent/scripts`) on four of them and reset their `wakeGroup` from "Part 5" to the prefab's "Part 3"; the value was put back and the scene saved again through the Editor, which recorded the override itself. Final check: all five keep their encounter group and carry the new timing, sound, telegraph material and missing-health bar.
- **Technical team (Editor issues found, not changed):** `Editor --scene <path>` is ignored, because `main.cpp` sets the startup scene on a config copy and then builds `Core` from a fresh load. Re-syncing an instance whose override is stored as a whole script list replaces the instance's script field values with the prefab's, losing overridden values such as `wakeGroup`; check other scenes' instances after the next prefab change. Scene-to-hierarchy selection highlighting remains theirs (untouched).
- **Movement teammate:** review the ranged admission change in `EnemyController.cs`; obstacle avoidance and pathfinding remain theirs.
- **Artists:** shoulder sockets are rolled 20° (see issue 6); a player death animation; a Spinner charge clip.
- **Audio:** a Spinner charge sound (none exists); optional wind-up and Cannon impact sounds (the mechanism supports a pending sound); a mix pass.
- **Final level:** re-check telegraph contrast, the Cannon's ground probe at ledges and cliffs, and respawn placement (Zoo only).
