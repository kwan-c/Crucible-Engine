# Networked Gameplay Test

A multiplayer test of the mech gameplay: a lobby leads into a procedurally
generated level with a spawn plaza and three regions (combat, zoo, parts).
Up to four players. The host runs the game; each player only sends its
input.

Everything lives in `network_tests/network_gameplay_tests` folders:

| What    | Where |
|---------|-------|
| Scripts | `Assets/Scripts/network_tests/network_gameplay_tests/` (namespace `Crucible.Scripts.NetGameplay`) |
| Prefabs | `Assets/Prefabs/network_tests/network_gameplay_tests/` |
| Scenes  | `Assets/Scenes/network_tests/network_gameplay_tests/` |

The teammates' gameplay scripts are **copied, not edited** (their logic is
still changing). Each copy is `Net` + the original's name and says at the
top what changed; the original's notes follow.

## Running it

1. Open `net_gameplay_lobby.scene` in two (up to four) Editors and press Play.
2. One player creates a lobby; the others find and join it, and everyone
   presses Ready. The host starts the game, which loads
   `net_gameplay_test.scene` on every machine with the same seed.
3. Everyone waits on the plaza until all players have the level. Then a
   10 second countdown runs, and nobody can move until "GO!".

Use the lobby (or Host / Client in the Network tab). Pressing Play on
`net_gameplay_test.scene` offline is not a real test: placed players only
become spawn points once a session runs, so offline all four placed mechs
stay live and all follow your keys.

## Controls

| Key | Does |
|-----|------|
| WASD, mouse | move, look |
| Shift | dash (Quick Boost) |
| Space | legs ability (Fast Legs: jump) |
| Tab / Ctrl | head ability (heal) / torso ability (damage amp) |
| LMB / RMB | left / right arm attack |
| Q / E | left / right shoulder attack |
| R | reload |
| V | select the next part in reach |
| 1 / 2 | equip the selected part (left / right, or the only socket) |
| L | loadout panel; while open, 1-5 drop head / arm L / arm R / shoulder L / shoulder R |
| F | use a zoo floor pad |

## The level

`NetSubLevelGenerator` (a copy of `SubLevelGenerator`) builds the terrain
from the host's seed (`Network.SceneSeed`), so every machine has the same
map. It flattens a plaza in the middle and three pads joined to it.

| Place | Where | What |
|-------|-------|------|
| Plaza | centre | 4 spawn points, the guide post, the Players and Controls boards, the match banner |
| Combat | +X | `NetEnemySpawner` waves of golems |
| Zoo | -X | the Zoo's toggles, sound pads and enemy waves |
| Parts | +Z | 24 parts: 4 full sets (heads, arms, shoulders, legs) |

Arrows on the ground and a sign at each region show the way.

## Who does what

- **The owner** (`Entity.IsMine`) reads its keys and camera and sends them
  with `Network.SetLocalInput`: the move, the camera's yaw and pitch, and the
  actions (`NetMechPlayer.NetActions`). Its camera, FOV and HUD are its own.
- **The host** (`Network.IsAuthority`) runs every mech from its owner's
  input (`PlayerActions.Of`): movement, abilities, attacks, pickups, drops,
  damage, respawns. It also runs every enemy and decides every zoo pad.
- **Every machine** draws what the host decided: transforms and animator
  values replicate, and the rest is network variables and data on the
  objects.

### Player (`NetMechPlayer`, `NetMechHud`)

- Health, the part in each socket, ammo, reloads and cooldowns, the
  selected pickup and deaths are written by the host
  (`NetVars` / `NetData` on the mech). The HUD reads them through
  `HudWeapons` / `HudAbilities`.
- Ranged shots aim along the owner's camera from the Camera Pivot and are
  tested with `Network.RaycastAt` at about the moment the owner saw them.
  Melee uses the original box.
- A destroyed mech comes back at its spawn point after `respawnSeconds`.
- Mechs start with only their built-in torso and legs. Everything else
  comes from the parts pad, so every part is a real object that can be
  dropped (L) and picked up by anyone.

### Parts (`NetMechPart`)

Scene network objects: their parent (a mount), pose, scale and id
replicate. The host equips and drops them; each machine highlights its own
player's selection.

### Combat region (`NetEnemySpawner`, `NetEnemyView`)

- Golems are the team's `Enemy` script, unchanged, on network prefabs
  (`Net Golem Mace`, `Net Golem Crossbow`: twice the original size, awake).
- The engine targets the nearest player. The spawner sets `detectRadius` /
  `loseRadius`, caps the count (`maxAlive`) and waits for the countdown.
- On clients `NetEnemyView` switches `Enemy` off and draws the health bar,
  hit flash, warning lane and a ragdoll thrown like the host's.

### Zoo region (`NetZoo*`)

- `NetZooToggle` / `NetZooSoundPlayer`: any player stands on a pad and
  presses F. The host decides; toggle states live on "Zoo State"
  (`NetZooState`), and sounds play on every machine as an effect.
- `NetZooEnemyWaves` spawns `NetZooEnemy` network enemies while their group
  is switched on. They chase the nearest player within `chaseRadius`.

### Match, boards and leaving

- `NetMatchState`: waiting, then the countdown, then playing. The banner
  over the plaza also shows a destroyed player's respawn countdown.
- `NetPlayerBoard`: every player's name, ping, health, parts and deaths.
- `NetHostLostScreen`: when a client's host leaves or stops answering, a
  "host connection lost" screen appears over its own camera, with a button
  (or Enter) back to the lobby.

## Engine change it needed

Several spawn-per-player prefabs may exist in a project now. A scene's
player is the prefab its placed player instances come from
(`NetSceneObjects` → `NetworkSession::ScenePlayerPrefab`). `NetworkedPlayer`
stays the default because its GUID sorts first.

## Known limits

- Projectiles (mech shots, crossbow golems' bolts) fly on the host only.
  Clients get the damage and the attack sound and animation, but not the
  projectile.
- Golems do not drop parts (their drop would exist on the host only).
- Ragdoll pieces fall separately on each machine, so they land close to,
  not exactly at, the host's.
- Lock-on, hit stop and the debug keys are off in the networked mech.
- The zoo platforms are not navigation obstacles (as in the original Zoo),
  so zoo enemies can walk into them.

## Test checklist

1. Lobby → level on every machine with the same map. Countdown, then "GO!".
2. Equip a full set each on the parts pad. Press L and drop a part; another
   player picks it up and sees it in their panel.
3. Combat: golems chase the nearest player. Hits, health bars and deaths
   look the same on every machine.
4. Zoo: F on a toggle starts that group's waves everywhere; F on a sound pad
   is heard everywhere.
5. Get destroyed: the HUD and banner count down, then you are back on your
   spawn point. The Players board counts the death.
6. Stop the host: each client shows "host connection lost" and goes back to
   the lobby with the button or Enter.
