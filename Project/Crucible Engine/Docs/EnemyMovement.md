# Enemy movement and coordination

Open `Build/Crucible.slnx`, choose **Debug | x64**, and run **Editor**. Open **Zoo Scene** under **Assets → Scenes → Game → TestFolder**, enter Play, capture the mouse, stand on a labelled combat pad and press **G**. Press G again after the previous enemy clears the marker. Each combat pad allows four living enemies by default. Sword and Mace demonstrate surrounding, the four projectile enemies demonstrate firing positions, and **Spinner Charge Test** demonstrates the special attack. Stand on the Small and Elite group switches and press **F** on each to run the budgeted mixed encounter.

## Where to change movement

Paths below are relative to `Assets/Scripts`. The entry points on the first four rows are the movement teammate's main integration points.

| Caller | File and method | Input, result and side effects |
|---|---|---|
| Enemy graph approach and recovery actions | `Enemy/EnemyController.cs` · `Enemy.Approach`, `ApproachRanged`, `Recover` | Read the current target and distances. Select approach, retreat, hold or a goal. An approach succeeds when the enemy can attack. |
| Those decisions | `Enemy/EnemyController.cs` · `Move(intent, goal)` | Translates one intent into an existing FlowFieldAgent order. Change ordinary steering, stopping, facing or retreat here. The agent component owns speed, acceleration, avoidance and separation. |
| Melee and ranged planners | `Enemy/EnemyLocalPositionQuery.cs` · `TryGoal(candidate, out standing)` | Checks the baked grid when present, floor height and slope, body clearance and direct travel. Returns a grounded destination. The physical collider determines foot height for the scaled golems. |
| Spinner attack | `Enemy/EnemyDirectedMovement.cs` · `HoldForSpecial`, `StartDirectedMovement`, `AdvanceDirectedMovement`, `ReleaseSpecialMovement` | Takes the movement authority, accepts a fixed heading, speed and duration, then returns the previous and current physics positions. Checks floor samples, grid and a body sweep before driving horizontal rigid-body velocity. Change charging, obstacle response or ledge handling here. |
| Approach and the Spinner waiting for permission | `Enemy/EnemyHordeCoordinator.cs` · `TryPressurePosition(member, out position)` | Suggests a spaced, validated destination relative to that member's target. Tries nearby angles, caches the result, and returns false when ordinary approach should take over. |
| Ranged approach and cooldown | `Enemy/EnemyRangedPositioning.cs` · `TryGetGoal(minimum, interval, spacing, out position)` | Scores reachable standing points with line of fire. Reuses a cached result and considers the positions and goals of other shooters against the same target. |
| Weapon or Spinner admission | `Enemy/EnemySpecialCoordinator.cs` and `Enemy/EnemyHordeCoordinator.cs` · `TryAcquire` and `TryAdmit` | Return permission at attack start. Both use `Enemy/EnemyAttackReservations.cs` for expiring waiting tickets and cleanup. Each coordinator owns its own pool. |
| Damage, push, freeze and death | `Enemy/Enemy.cs` · `StartPush`, `UpdatePush`, `UpdateLegs`, `OnDied` | A shove interrupts the special. Hit stop freezes its clock and planar velocity. The legs resume when the living actor has relinquished every movement hold. Death hands the body to its existing ragdoll or topple path. |
| Spinner contact | `Player/MechPlayer.cs` · `ApplyKnockback(direction, speed, duration)` | Adds a linearly decaying horizontal displacement to the player's existing collision-aware motor. Hit stop pauses it. The existing hook owns a hooked frame and consumes the elapsed impulse time. |

**Playtest-fix changes in this area** (details in [PlaytestFixesVerification.md](PlaytestFixesVerification.md)). `ApproachRanged` still chooses the same movement order, but its return value now means "a shot is valid from here now" (`RangedShotValid`): within **Attack Range**, outside the new **Minimum Attack Distance**, facing and with a clear line. A ranged enemy that is retreating or repositioning therefore fires when its weapon is ready, holds still for the wind-up and release, then resumes the same movement request. `RequestAttack` no longer refuses ranged shots inside **Minimum Range**, which is now only where the enemy prefers to stand. `EnemyRangedPositioning.TryGetGoal` uses the new **Preferred Range** field when it is above 0, else the old 55% ring. No steering, avoidance or flow-field code changed.

## How Spinner runs

**Acquire and Approach → Request Spinner Charge → Finish Spinner Charge → Recover.** The existing Spinner state and behavior graph files keep their asset IDs. The request node checks readiness and may keep positioning while waiting. The finish node owns cancellation if the graph aborts. Precise timing lives in `EnemySpinnerAttack.cs`.

1. On admission, the attack captures a horizontal direction, stops ordinary navigation, and starts its cooldown once. The full orange lane uses that direction and the configured charge distance. Only the visible body and its attached head spin and hop.
2. At release the dynamic body charges. Each tick compares its actual travelled segment to the target capsule and checks world obstruction. A character can sidestep the committed line. Accepted damage goes through the normal health, shield and hit-stop path, followed by player knockback. Each execution can attempt contact with its target once.
3. A hit, obstruction, unsupported ground, stall or timeout ends the charge. Local attack permission is released at that point. The body stays still through recovery, then returns to the graph. Cancellation hides the lane, restores the visual transform and relinquishes movement. Destroying a target can cancel a committed charge.

## Inspector controls

| Select | Control | What to test |
|---|---|---|
| Sword, Mace or Spinner → **Enemy.cs** | **Use Horde Coordination** | Off uses ordinary navigation and attacks. On gives melee a local ring or pressure point. Spinner can use the approach planner before its charge. |
| Any of the four ranged enemies → **Enemy.cs** | **Use Ranged Positioning** | Off keeps range-aware approach, retreat, facing and firing. On enables firing-angle and spacing plans. Tune **Minimum Range**, **Attack Range**, **Ranged Spacing** and **Ranged Planning Interval** on the actor. |
| Spinner → **Enemy.cs** | **Use Special Coordinator** | Off permits independent charges. On makes otherwise-ready attacks request a local turn. **Charge Damage**, **Charge Speed**, **Charge Duration**, **Contact Radius**, wind-up, recovery, cooldown, knockback and hop settings are on the same script. |
| Scene hierarchy → **Enemy Coordination** → **Enemy Horde Coordinator.cs** | **Reserved Positions** | Off by default for continuous melee pressure. On uses discrete attack/wait positions and limits simultaneous basic melee attacks against one target. Default limit 2, start spacing 0.18 s, four attack locations, eight waiting locations, waiting radius 5.2 m. Continuous planning uses a 10 m radius, 0.2 s interval and 1.8 m spacing. |
| **Enemy Coordination** → **Enemy Special Coordinator.cs** | **Local Radius**, **Maximum Special Attacks**, **Minimum Start Spacing** | Defaults are 24 m, one active special, 0.55 s. Two Spinners against different players can attack concurrently. Cancelling early keeps the minimum start spacing. |

Disable all three participation switches on the test actors to isolate ordinary movement. Targeting, combat, drops, collision and native avoidance continue. Turn the switches back on to restore the participation defaults. Play-mode changes apply to that instance. Edit the prefab outside Play to keep a setting for future spawns. These are ordinary public fields on the existing Enemy partial class, so the Inspector also displays fields used by other roles.

Change walking speed on the actor's **FlowFieldAgent**. A charge has its own speed and leaves the agent's walking speed intact. `Enemy.SetSlowed` remembers the speed when wandering begins and restores it when wandering ends. If another system edits the speed during the wander, its newer value takes precedence.

A scene enables coordination by placing one **Enemy Coordination** entity with both coordinator scripts. The Zoo, TutorialMap, Golem_test and Layers_test include it. Enemies still operate independently in a scene without a host. The native graph supplies the nearest player marker as its target. Mission objectives currently have no targeting hook.

The graph actions, position query, permission pool and directed movement boundary are the pieces to retain during the future `EnemyParent` refactor. Full roster settings, changed assets and test evidence are in [Enemy gameplay verification](EnemyGameplayVerification.md).
