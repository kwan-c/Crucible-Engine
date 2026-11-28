# Crucible Networking: How to Use It

This guide explains how to make objects networked, how to start a Host and a Client
from the Editor, and how to build a scene like `Assets/Scenes/network_tests/Network_Test.scene`.

> **LAN only.** Networking currently works only on a local network: two Editors on the
> same PC, or PCs on the same LAN or Wi-Fi. Playing over the internet (NAT traversal,
> relay servers, matchmaking) is not supported yet.

At most 4 players can play together: the Host plus up to 3 Clients.

The example scenes are in `Assets/Scenes/network_tests/`: `Network_Test`, `network_gamplay` (the arena
shooter), `network_lobby` and `network_sync_test` (the sync checklist, section 8.22).

For a full game built on all of this (a lobby, a procedural level, networked mechs, enemies,
parts and zoo pads, with its own player prefab), see `Docs/NetworkGameplayTest.md` and
`Assets/Scenes/network_tests/network_gameplay_tests/`.

---

## 1. The Network Object component (`NetworkObjectAuthoring`)

Any object that should exist on every machine and stay in sync needs a
**NetworkObjectAuthoring** component on its **root** entity.

To add it: select the entity, then click **Add Component > NetworkObjectAuthoring** in the Inspector.

- Put it on the **root** only. A network object cannot contain another one. The Inspector
  won't let you add it if an entity above or below already has it.
- It works on a **prefab root** (a network prefab, which can be spawned) or on an
  **entity placed in a scene** (a scene object).

### 1.1 The policy table

The Inspector shows one row for every component on the object and its children.

| Column | Choices | What it means |
|---|---|---|
| **Component** | — | The component's name. Hover over it to see whether it's on a child entity. |
| **Network state** | `Local` / `Replicated: <serializer>` | **Local** (the default): each machine keeps the value it loaded from the scene or prefab, and the Host never sends it. **Replicated**: the Host sends this component's value to every Client. For example, `Transform` offers `Replicated: Transform/Full` (position and rotation), `Replicated: Transform/YawOnly` (position and yaw only, which is cheaper), and the same two with `+Scale` (section 8.15). |
| **Runs on** | `Everywhere` / `Host only` / `Owner only` | **Everywhere**: the component runs on every machine. **Host only**: switched off on Clients, used for physics, AI and gameplay logic, since the Host has authority. **Owner only**: switched off everywhere except on the machine that owns the object, used for the player's camera or HUD. |
| **Sync on/off** | `Off` / `Root` / `Synced children` / `Both` | Sends whether this component is enabled or disabled to the Clients: on the root, on every synced child that has it (section 8.20), or both. |

Rule of thumb:
- Things that **move** → `Transform` set to **Replicated**.
- Parts of an object that **move on their own** (a torso that aims, a turret head) → list them
  under **Synced children** (section 1.3) and set **NetChildTransforms** to **Replicated**.
- Things whose **look changes** in play (part swaps, tints, hiding a model) → **NetRenderers**
  set to **Replicated: Renderers** (section 8.18).
- Things that **simulate** (RigidBody, Collider, CharacterController, PlayerController, AI) → **Host only**.
- Things only **the player who owns the object** should have (Camera, CameraOrbit) → **Owner only**.
- Everything else (MeshRenderer, lights, names) → leave it **Local**.

### 1.2 The other settings

| Setting | Default | What it does |
|---|---|---|
| **Send priority** | 1.0 | How strongly this object competes for each Client's bandwidth. A higher value means it's updated more often. Only the Host uses it. |
| **Critical** | off | Sent in **every** snapshot, ahead of the bandwidth budget. Use it for players or a boss. Use it sparingly. |
| **Allow scene placement** | on | When this object is saved in a scene, it becomes a **networked scene object**: the Host and every Client load their own copy and link them up instead of spawning one. |
| **Spawn per player** | off | Marks this prefab as **a player**. The Host spawns one for itself and one for each Client that joins, and that player owns it. Only a prefab can be a player. **Several prefabs may have it on**: each scene uses the one its placed players are copies of (section 5.2); a scene with none uses the default, the first such prefab by GUID (`NetworkedPlayer`). |

At the bottom of the component, **"Network settings are valid"** (green) means the object
is ready. If something is wrong, a red message says what. Leftover settings that refer to a
component the object no longer has show a **Remove** button.

### 1.3 Synced children

Only the object's **root** has an identity on the network. To sync a child entity (its
transform, its renderer), list it under **Synced children** below the table:

- Pick it from the **Add a child...** list. It's stored as its **name path** below the root,
  for example `Mech Visual/Legs/Torso`: one entity name per level.
- **Each name on the path must be unique among its siblings.** A path that matches no entity,
  or two entities, is shown in red and refused at Play: the engine never guesses which one
  you meant. Rename one of them.
- At most **16** synced children. The order matters (both machines use the same list).
- What uses the list: the **NetChildTransforms** row (`Children/Rotation` or
  `Children/Full`, section 8.16), the **NetRenderers** row (`Renderers`, section 8.18) and
  **Sync on/off: Synced children** (section 8.20).

---

## 2. The Network window

Open it from the menu bar: **Network > Network Window**.
Like the Animator, it opens as a tab beside the Scene and Game views, and you can drag it
anywhere else. The settings can only be changed in
**Edit mode**: they lock while playing, so press **Stop** to change them.

The **Network** menu also has **Start / Stop Connection Server**, **Connection Server...** and
**Debug...** (they open that tab).

**Session tab**

| Setting | Use |
|---|---|
| **Player name** | Shown to the other players and in lobby lists. |
| **Lobby discovery** | How lobby lists find sessions: **Auto**, **Server** or **LAN**, plus the connection server's address and port (section 8.11). |
| **Quick test (start at Play)** | **Offline** (the normal choice: the game's own lobby hosts and joins), or **Host** / **Client**, which start a session as soon as you press Play, to test a scene without the lobby. **Client** asks for the **Host IP**. **Game port** (default `27020`) is the port a host listens on and a client joins. |
| **Network simulation (testing)** | Adds latency and packet loss to every session of this Editor, lobby sessions included. |

Below the settings, the tab shows the status:
- Host: `Hosting on port 27020`, then the players who joined, with their IP and ping.
- Client: `Connecting...`, `Joining...`, then `Joined <ip>:<port> as player N` with the ping and
  everyone in the session.
- The connection server: `online`, `connecting...` or `unreachable`.
- Errors: `Could not start: ...` or `Last session ended: ...` (for example, the Host stopped or
  the content didn't match).

**Connection Server tab:** start and stop the lobby server on this PC (section 8.11).

**Debug tab:** while a session runs, each connection's ping, quality, KB/s and lifecycle
messages, the per-second rates, and **Counters** (malformed, refused, timeouts, commands...).

- The defaults come from `Assets/Config/EngineConfig.json` (the `"network"` section).
  Changes made in the window apply only to **that Editor**, are **not saved**, and are lost when
  you close it. Two Editors on the same PC can each use different settings. The listen address
  is only in `EngineConfig.json` (`"listenAddress"`; empty = every network card).

**Finding the Host IP on a LAN:** on the Host PC, open a terminal, run `ipconfig`, and use
the **IPv4 Address** of the network adapter you're connected through (e.g. `192.168.1.23`). On the same PC, use
`127.0.0.1`.

**Windows Firewall:** the first time you Host, Windows may ask whether to allow the Editor
on the network. Allow it on **Private networks**, or Clients on other PCs can't connect.

---

## 3. Testing: Host first, then Client

1. **Both machines must have the same project content:** the same commit, the same scene and
   the same prefabs. Network prefabs and scene objects are checked by hash, and a Client with
   different content is refused (look for *content* or *scene mismatch* in the panel).
2. Open the **same scene** in both Editors (e.g. `Scenes/network_tests/Network_Test.scene`).
3. **Editor 1 (Host):** in **Network Window > Session > Quick test**, select **Host**, keep
   game port `27020`, and press **Play**. The window should show `Hosting on port 27020`.
4. **Editor 2 (Client):** in the same place, select **Client**, enter the Host's IP
   (`127.0.0.1` on the same PC, the Host's LAN IP otherwise), keep `27020`, then press **Play**.
   The panel should show `Joined ... as player 1`, and the Host's list shows the new player.
5. Move in each window. Each player controls their own character and sees the others move.
   Objects that the Host simulates (e.g. the physics sphere) move the same on every screen.
6. Press **Stop** on a Client to leave. Press **Stop** on the Host to end the session for
   everyone.

**Always start the Host before the Client.** A Client that can't reach a Host gives up after
the join timeout and shows the reason in the panel.

---

## 4. Making a networked scene (like `Network_Test.scene`)

`Network_Test.scene` contains:

| Entity | Networked? | Settings |
|---|---|---|
| Ground, Directional Light | No | Ordinary scene entities: every machine loads them as they are. |
| Wall 1-4 | Yes, scene objects | Collider = **Host only**; Transform left **Local** because the walls never move. |
| Sphere (RigidBody) | Yes, scene object | Transform = **Replicated: Transform/Full**; RigidBody and Collider = **Host only**. The Host simulates it and Clients see it move. |
| Player Spawnpoint 1-3 | Instances of the player prefab | Mark where players appear (see section 5.2). |
| Cursor Lock | No | Has the `CursorLock` C# script. |

Steps to make your own:

1. **Create the scene** and add the static level: ground, lights, decoration. These stay
   ordinary entities with no network component.
2. **Create or reuse a player prefab** (section 5.1), e.g. `Assets/Prefabs/NetworkedPlayer.cprefab`.
3. **Drag the player prefab into the scene** once for each place a player should start
   (section 5.2).
4. **Make dynamic objects networked:** add `NetworkObjectAuthoring` to each object that the
   Host should simulate and the Clients should see. Set its `Transform` to **Replicated**
   and its physics components to **Host only**. Keep **Allow scene placement** on.
5. Objects that never move but should exist the same way on every machine (like the walls)
   can also be scene objects. Set the simulating components to **Host only** and leave the
   Transform **Local**.
6. **Save the scene.** A scene object needs a saved id: an entity added and never saved is
   not networked.
7. Commit the scene and its prefabs, pull them on the other machine, then test as in section 3.

---

## 5. Setting up the other components

### 5.1 The player prefab

Copy `Assets/Prefabs/NetworkedPlayer.cprefab`. It's set up like this:

```
Player (root)                Transform, CharacterController, PlayerController,
                             NetPlayerAnimation script, NetworkObjectAuthoring
├── Camera Pivot             Transform, CameraOrbit
│   └── Player Camera        Transform, Camera
└── Mech Visual              the game's mech, scaled to the 2 m capsule
    └── Legs                 Mech Legs controller (Pose, Speed)
        └── Torso            on the legs' Hip socket; head and sword below it
```

Its `NetworkObjectAuthoring`:

| Component | Network state | Runs on |
|---|---|---|
| Transform | Replicated: Transform/Full | Everywhere |
| NetAnimator | Replicated: Animator | Everywhere |
| CharacterController | Local, **Sync on/off** | **Host only** |
| PlayerController | Local | **Host only** |
| CameraOrbit | Local | **Owner only** |
| Camera | Local | **Owner only** |

- **Critical** = on, so players are updated in every snapshot.
- **Spawn per player** = on, so this prefab is the player.
- The Host moves every player from the input their owner sends. Clients only show the
  result, which is why the movement components are **Host only**.
- Each machine keeps only **its own** player's camera, which is why the camera is **Owner only**.
- **CharacterController: Host only + Sync on/off.** The Host switches it off for a dead player;
  Clients never switch theirs on, but the capsule their raycasts hit (section 8.13) follows that
  on/off, so nobody can aim at a dead player. This adds **EnabledMask** to the replicated list.
- **The body is the game's mech** (legs, torso, head and sword from the single-player
  player), without its single-player parts (`MechPart` scripts, part colliders, labels,
  sounds). `NetPlayerAnimation.cs` runs on the Host, which moves every player: from each
  one's speed and whether it is on the ground it sets the legs' `Pose` and `Speed`, as
  `MechPlayer` does, and the **Animator** row sends them to every Client (section 8.12).

### 5.2 Player spawn points

There's no separate spawn-point component: **an instance of the player prefab placed in the
scene is a spawn point.**

- When a session starts, every machine removes these placed players. The Host then spawns
  each real player at one of their positions and rotations.
- Player 0 (the Host) uses the first spawn point, player 1 the second, and so on. If there are
  more players than spawn points, they wrap around and are shifted 3 m along X so they
  don't overlap.
- With **no** spawn points in the scene, players spawn at a default position, as the default
  player (the first Spawn per player prefab by GUID, `NetworkedPlayer`).
- **The placed prefab is the scene's player.** Each scene can have its own: make a player
  prefab (Spawn per player on), place copies of it in that scene, and those copies are both
  the spawn points and the prefab every player is spawned as. Other scenes are not affected.
- All placed players in one scene must be copies of **the same** prefab. With a mix, the
  first one found wins and the log says *"a placed player of another player prefab"* for the
  rest (they are still removed, but not used).
- **Only in a session.** Placed players become spawn points when a session runs (Host or
  Client). In an **offline** Play nothing removes them: each one is a live player and they all
  follow your keys. To try a scene with several spawn points, host it (the lobby, or Quick
  test: Host).
- Name them clearly (e.g. `Player Spawnpoint 1`) so teammates know what they are.
- Don't change the network settings on these instances. Leave them as the prefab has them.

### 5.3 Other objects

| Object | Suggested settings |
|---|---|
| A physics prop (box, ball) | Transform **Replicated: Transform/Full**; RigidBody and Collider **Host only** |
| A character that only turns around the vertical axis (enemy, NPC) | Transform **Replicated: Transform/YawOnly**; AI, controllers and scripts **Host only** |
| A door or light switched on and off by gameplay | **Sync on/off** on the component that gets turned on and off |
| The local player's HUD or camera rig | **Owner only** |
| An important object everyone must see right away (boss, ball) | **Critical** on, or a higher **Send priority** |
| An enemy or projectile that gameplay spawns at runtime | Make it a **prefab** with `NetworkObjectAuthoring` (**Spawn per player** off); every machine finds it automatically, so there's nothing to register by hand. |

### 5.4 Things to remember

- Gameplay (health, damage, enemies) belongs in **C# scripts**. The network layer only syncs
  components and has no gameplay built in.
- Only the **Host** should run gameplay logic. Set scripts and AI that change the game state to **Host only**.
- After changing network settings on a prefab or scene object, **both machines need the
  new version**, or the Client will be refused when joining.
- If an object doesn't show up on the Client, check its Inspector for a red error and
  check that the scene was saved.

---

## 6. Step-by-step example: an arena with 2 spawn points, a ball and blocking boxes

This builds a small scene from nothing:

- a floor
- 4 boxes that block players (they never move)
- 1 ball the Host simulates with physics, which every player can push around
- 2 player spawn points

> **The player MUST be a prefab.** Networked players are spawned from a prefab, one for
> each player. A player made only in the scene (not a prefab) is never spawned or controlled
> over the network. You can **reuse** `Assets/Prefabs/NetworkedPlayer.cprefab` or make your
> own: the player prefab you place in this scene is the one this scene spawns (section 5.2),
> and other scenes keep theirs.

### Step 1: New scene

1. **Scene > New Scene** (Ctrl+N).
2. **Scene > Save Scene As...** and name it, e.g. `Scenes/Arena_Test.scene`.

### Step 2: Floor and light (not networked)

1. Hierarchy **+ > 3D Objects > Create Cube**. Rename it `Ground`.
2. Inspector, **Transform**: position `(0, 0, 0)`, scale `(30, 0.5, 30)`.
3. **Add Component > Collider**: type `Box`, layer `NonMoving`.
4. Hierarchy **+ > Light Objects > Create Directional Light**.

Don't add a network component to these. Every machine loads them from the scene as they are.

### Step 3: Blocking boxes (networked scene objects that never move)

For each box (make 4: `Box 1` to `Box 4`):

1. Hierarchy **+ > 3D Objects > Create Cube**. Rename it `Box 1`.
2. **Transform**: put it where it should block, e.g. position `(-5, 1, 0)`, scale `(1, 2, 6)`.
3. **Add Component > Collider**: type `Box`, layer `NonMoving`.
4. **Add Component > NetworkObjectAuthoring**, then set its table:

   | Component | Network state | Runs on |
   |---|---|---|
   | Transform | Local (it never moves, so there's nothing to send) | Everywhere |
   | MeshRenderer | Local | Everywhere |
   | Collider | Local | **Host only** |

5. Keep **Allow scene placement** on, and **Spawn per player** and **Critical** off.
6. Check that the component shows **"Network settings are valid"**.

Tip: set up `Box 1` completely, then **Duplicate** it (Ctrl+D) three times and move the copies.

### Step 4: The ball (a networked scene object the Host simulates)

1. Hierarchy **+ > 3D Objects > Create Sphere**. Rename it `Ball`.
2. **Transform**: position `(3, 1, 0)`.
3. **Add Component > RigidBody**: mass `1`, **Use Gravity** on, **Is Kinematic** off.
4. **Add Component > Collider**: type `Sphere`, radius `0.5`, layer `Moving`
   (set bounciness to `1` if you want it to bounce).
5. **Add Component > NetworkObjectAuthoring**, then set its table:

   | Component | Network state | Runs on |
   |---|---|---|
   | Transform | **Replicated: Transform/Full** | Everywhere |
   | MeshRenderer | Local | Everywhere |
   | RigidBody | Local | **Host only** |
   | Collider | Local | **Host only** |

6. Keep **Allow scene placement** on. Optional: raise **Send priority** (e.g. `2`) so the
   ball is updated more often than other objects.
7. Check for **"Network settings are valid"**.

Why: only the Host runs physics. Clients get the ball's position and rotation 30 times a
second and draw it moving smoothly.

### Step 5: The player prefab

**Option A: reuse the existing one (recommended).** Skip to Step 6 and use
`Assets/Prefabs/NetworkedPlayer.cprefab`.

**Option B: make your own** (`NetworkedPlayer.cprefab` can keep **Spawn per player** on: this
scene uses whichever player prefab you place in it):

1. Hierarchy **+ > Create Player**. This makes:
   ```
   Player                  MeshRenderer, CharacterController, PlayerController
   └── Camera Pivot        CameraOrbit
       └── Player Camera   Camera
   ```
2. Select the **`Player` root** and **Add Component > NetworkObjectAuthoring**. Set its table:

   | Component | Network state | Runs on |
   |---|---|---|
   | Transform | **Replicated: Transform/Full** | Everywhere |
   | MeshRenderer | Local | Everywhere |
   | CharacterController | Local | **Host only** |
   | PlayerController | Local | **Host only** |
   | CameraOrbit | Local | **Owner only** |
   | Camera | Local | **Owner only** |

3. Turn on **Critical** and **Spawn per player**. Keep **Allow scene placement** on.
4. Check for **"Network settings are valid"**.
5. **Turn it into a prefab:** right-click `Player` in the Hierarchy and choose
   **Create Prefab**. It's saved in `Assets/Prefabs`, and the `Player` in the scene becomes
   its first instance (shown in the prefab colour).

Why each setting:
- **Transform Replicated**: every machine sees every player where the Host says they are.
- **CharacterController and PlayerController Host only**: the Host moves every player using
  the input each player sends, so Clients can't cheat or drift.
- **Camera Owner only**: each machine keeps only the camera of **its own** player.

### Step 6: Two spawn points

A placed copy of the player prefab is a spawn point.

1. Drag the player `.cprefab` from the **Asset Browser** into the Scene view, **twice**
   (if you did Option B, the `Player` already in the scene counts as one of them).
2. Rename them `Player Spawnpoint 1` and `Player Spawnpoint 2`.
3. Move and rotate each one where a player should start, e.g. `(-8, 1, 0)` and `(8, 1, 0)`.
   Keep them above the floor.
4. Don't change their network settings. They must stay **exactly like the prefab**.

When Play starts in Host or Client mode, these placed players are removed. The Host then
spawns the real players there: the Host's player at one spawn point, the first Client at the
other. Which spawn point counts as "first" follows the scene's internal order, not the names. A
third or fourth player reuses a spawn point, shifted 3 m along X.

### Step 7: Mouse look (optional)

To capture the mouse for the camera, add an empty entity (**+ > Create Empty**, name it
`Cursor Lock`), then **Add Component > ScriptComponent** and choose the `CursorLock` script,
the same way `Network_Test.scene` does.

### Step 8: Save, share, test

1. **Scene > Save Scene** (Ctrl+S). Entities that were never saved aren't networked.
2. Commit the scene (and your player prefab, if you made one) and pull it on the other PC.
3. Test as in **section 3**: Editor 1 picks **Host** and presses Play, then Editor 2 picks
   **Client**, enters the Host's IP and presses Play.

**What you should see:**
- Two players, one at each spawn point, each controlled by its own Editor.
- The boxes block both players.
- When either player pushes the ball, it moves the same way on both screens.

### Quick checklist if something is wrong

| Problem | Check |
|---|---|
| Client is refused when joining | Both machines have the same scene and prefabs (same commit); the scene was saved. |
| "a placed player of another player prefab" | The scene has placed copies of two different player prefabs. Keep copies of one only (section 5.2). |
| Every placed player moves when you press a key | You pressed Play **offline**: placed players only become spawn points in a session. Host it instead. |
| No player appears | The player is a **prefab** with **Spawn per player** on and a valid component setup. |
| Ball moves on the Host but not on the Client | Ball's **Transform** is **Replicated: Transform/Full**. |
| Ball jitters or falls through the floor on the Client | RigidBody and Collider on the ball are **Host only**. |
| Both cameras show the same view | Camera and CameraOrbit on the player are **Owner only**. |
| An object is missing on the Client | Its Inspector shows **"Network settings are valid"**, and **Allow scene placement** is on. |

---

## 7. Current limitations

Read this before designing gameplay for multiplayer.

### What works

- **AI chases the nearest player, out of all players.** The flow-field navigation and the
  State Graph's "nearest player" target every player. Set the AI components
  (FlowFieldAgent, StateGraphAgent, AI scripts) to **Host only** and the enemy's Transform to
  **Replicated**. On a Client, the players' PlayerController is switched off, so AI running
  there finds **no players** and stands still.
- **Physics:** any player can push props, because the Host simulates everything.
- **Physics queries on Clients:** a Client's raycasts hit other players, props and walls
  where it draws them (section 8.13), for aim highlights, interaction prompts and the like.
- **Joining mid-game:** a Client can join while the game is running and receives the
  current world.
- **Generated levels:** every machine builds the same terrain from the scene seed
  (section 8.14).
- **Scale, character size, moving parts, attached parts and looks:** synced when the object
  asks for them (sections 8.15 to 8.18).

### What doesn't work yet

| # | Limitation | What it means for you |
|---|---|---|
| 1 | **Other Client actions must be sent as commands.** | Movement, turning and jump are sent automatically. Fire, interact, reload and the rest reach the Host only if your script sends them with `Network.SendCommand` (section 8). A Host-only C# script using `Input.IsPressed` still reads the **Host's own keyboard**, not the Client's. |
| 2 | **Script events are moments, not state.** | The Host tells every machine "this happened" with `Network.SendEvent` (section 8.4). A player who joins later never hears an old event, so lasting state must still be something that is synced. |
| 3 | **`Prefab.Instantiate` is still local.** | It creates the object only on the machine that runs the script. Use `Network.Spawn` on the Host for objects every player should see (section 8.5); the prefab must be a network prefab. |
| 4 | **Script values are synced only through network variables.** | The Transform (with scale if asked), component on/off state, an object's 64 network variables and its NetData bytes (sections 8.6 and 8.26), its Animator parameters (section 8.12), a CharacterController's size, synced children's transforms, attachments and renderers (sections 8.15 to 8.18) are sent. Ordinary script fields and other component values (lights, audio, particles, Collider sizes) are **not**: put health, ammo or score in a network variable. |
| 5 | **Animation is synced through Animator parameters only.** | Turn on **Animator** in the object's Network Object table (section 8.12) and the Host's `Animator.Set...` calls reach every Client. Up to 32 parameters per object; a trigger fired twice within one snapshot (about 33 ms) plays once on Clients; `ResetTrigger` stays on the Host. Without the row, a Host-only script's animation is seen only on the Host. |
| 6 | **Gameplay audio, particles and UI need a script event.** | Effects triggered by Host-only gameplay happen only on the Host unless the Host sends an event and a script that runs **Everywhere** plays them (section 8.4). |
| 7 | **Clients query physics; they don't simulate it.** | A Client's `Physics` queries (`Raycast`, `RaycastNonAlloc`, `SphereCast`, `CapsuleCast`, `CheckSphere`, the `Overlap...NonAlloc` ones) hit network objects where they are drawn, through query bodies the engine adds (section 8.13); the entity they return is the query body, so use `Network.ObjectOf`. They are for display and feel only: the Host's own queries decide hits and damage. When the Host switches a Host-only Collider or CharacterController off (a dead player), Clients' query bodies follow only if that component also has **Sync on/off** (section 8.13). Client triggers fire only for the Client's local objects. |
| 8 | **Only the Host's `Network.LoadScene` keeps the session across scenes.** | It moves everyone to the new scene (section 8.8). `Scene.Load`, or opening a scene in the Editor, during a session still ends it. Spawned network objects can come along: `Network.KeepAcrossScenes` (section 8.30). |
| 9 | **A Client's own player responds with a small delay.** | About 110–150 ms on a LAN, because the Client waits for the Host instead of predicting its own movement. The Host's player has no delay. |
| 10 | **LAN only, at most 4 players.** | The Host plus 3 Clients, connecting by IP address. A lobby can be built in C# (section 8.9 has one), but there is no internet play or server list: players type the Host's IP address. |
| 11 | **The Host leaving ends the game for everyone.** | There's no handing over to another player. A Client that leaves has its player removed. |
| 12 | **Both machines need exactly the same content.** | Any difference in a network prefab or scene object (or in the scene) means the Client is refused when joining. Both machines also need the same engine build: this version speaks **protocol 12**, and an older build is refused when joining. |

### In practice

- **Works today:** gameplay only the Host controls: AI chasing players, physics props,
  moving platforms, doors switched on and off with **Sync on/off**.
- **Works with scripts (section 8):** Host/Client checks, the player list, object
  ownership, Client actions sent to the Host as commands, Host events to every machine,
  spawning network objects from the Host, and animation the Host's scripts drive
  (section 8.12).

## 8. Networking from C# scripts

Everything is in the `Network` class and a few `EntityScript` callbacks. The full
comments are in `ManagedScripts/Runtime/Networking/` (`Network.cs`, `NetWriter.cs`, `NetReader.cs` and the rest) and
`EntityScript.cs`.

### 8.1 Who am I, who else is here

| Call | Meaning |
|---|---|
| `Network.IsAuthority` | This machine decides the game: the Host, or a single-player game. Guard every change to shared game state with it. |
| `Network.IsHost` / `IsClient` / `IsOffline` | The role. |
| `Network.IsReady` | This machine is in the game. On a Client: joined and the Host's world has arrived. |
| `Network.LocalPeerId` | This player's number: Host 0, Clients 1 to 3. |
| `Network.Peers`, `PlayerCount`, `GetPeerName(peer)`, `IsPeerReady(peer)` | Everyone in the session. The same on every machine. |
| `entity.IsNetworked`, `entity.NetworkId`, `entity.Owner`, `entity.IsMine` | Is it a network object, its number, whose it is, and whether it belongs to the player on this machine. |
| `Network.FindEntity(networkId)` | The local entity of a network object. |

Offline counts as a session of one: `IsAuthority` is true, `LocalPeerId` is 0.

### 8.2 Callbacks (override in an `EntityScript`)

| Callback | When |
|---|---|
| `OnNetworkStarted()` | Host: the session is up. Client: joined and the world has arrived. Not called offline. |
| `OnNetworkStopped(reason)` | The session ended, or a join failed. |
| `OnPeerJoined(peer)` / `OnPeerReady(peer)` / `OnPeerLeft(peer)` | Another player joined, finished loading, or left. On every machine, never for yourself. |
| `OnNetworkCommand(sender, kind, data)` | Authority only: somebody sent a command. |
| `OnNetworkEvent(kind, data)` | Every machine: the Host sent an event. |
| `OnNetVarChanged(slot)` | Every machine: one of this entity's network variables changed. |

They are called before `OnUpdate`, in the frame the event happened. A script that is
switched off (for example a **Host only** script on a Client) hears nothing.

### 8.3 Commands: a Client asks, the Host decides

```csharp
public sealed class Door : EntityScript
{
    const ushort Open = 1;                 // your own numbers, from 1

    public override void OnUpdate(float dt)
    {
        // any machine: the player on THIS machine pressed the key
        if (Input.IsPressed(KeyCode.E))
            Network.SendCommand(Open, new NetWriter().WriteBool(true), Entity);
    }

    public override void OnNetworkCommand(int sender, ushort kind, NetReader data)
    {
        // runs on the Host (and in a single-player game) only
        bool open = data.ReadBool();
        if (kind != Open || !data.Ok) return;
        // never trust a command: may this player do this? is it close enough?
        SetOpen(open);
    }
}
```

- The script that answers must run on the Host, so it must be a network object's script
  that is not switched off there. The door must be a network object for a Client to name it.
- A command with a target reaches only that entity's scripts; without a target, every
  script gets it.
- The Host's own `SendCommand`, and one in a single-player game, is delivered on the
  same machine next frame, so the same script works in every role.
- `sender` cannot be faked. Everything in `data` can: read it, check `data.Ok`, then check
  the values.
- At most 1024 bytes of data. Commands arrive in the order they were sent.
- What the Host decides reaches Clients through what is synced (Transform, **Sync
  on/off**) or through an event (8.4).

### 8.4 Events: the Host tells everyone

```csharp
const ushort Opened = 1;

void SetOpen(bool open)                    // on the Host, after it decided
{
    Network.SendEvent(Opened, new NetWriter().WriteBool(open), Entity);
}

public override void OnNetworkEvent(ushort kind, NetReader data)
{
    // runs on EVERY machine, the Host included (and in a single-player game)
    bool open = data.ReadBool();
    if (kind != Opened || !data.Ok) return;
    AudioSource.Play();                    // each player hears it
}
```

- Only the Host (or a single-player game) may send; a Client is refused and should send a
  command instead.
- The script that hears it must run on Clients too: its **Runs on** must be **Everywhere**.
- With a target, only that entity's scripts get it; the target must be a network object.
  Without a target, every script gets it.
- `Network.SendEvent(kind, data, target, toPeer)` sends to one player instead of everyone.
- `Network.SendEventExcept(peer, kind, data, target)` sends to everyone but one player,
  usually the one who caused it and already showed the effect.
- `Network.SendEventUnreliable(kind, data, target)` sends once with no guarantee: it may be
  lost or arrive early. Use it only for frequent effects where a missed one does not matter
  (footsteps, sparks), never for an object spawned in the same frame. At most 1000 bytes.
- Events arrive in order, after that frame's spawns and despawns.
- A player who joins later does not get earlier events.

### 8.5 Spawning: only the Host creates network objects

```csharp
public Prefab bolt;                        // a NETWORK prefab, assigned in the Inspector

void Fire(Vector3 from, Quaternion aim)
{
    if (!Network.IsAuthority) return;      // a Client asks with a command instead
    Entity made = Network.Spawn(bolt, from, aim);        // appears on every machine
    // Network.Spawn(prefab, position, rotation, owner) gives it to a player
}
```

- The prefab needs a **Network Object** component on its root (section 1), like the
  player prefab. Otherwise `Network.Spawn` logs why and returns null.
- The entity exists on the Host at once; Clients get it the same frame's send, and
  players who join later get it with the world.
- `Network.Despawn(entity)` (or `Entity.Destroy` on the Host) removes it everywhere.
  A Client must never destroy a network object itself.
- In a single-player game `Network.Spawn` is an ordinary `Instantiate`.
- `owner` sets `entity.Owner` / `IsMine` on every machine. It cannot be changed later.

### 8.6 Network variables: values every player sees

Every entity has 64 numbered slots (`Entity.NetVars`). The Host writes them; every
machine reads them and is told when one changes.

```csharp
const int Health = 0;                      // your own slot numbers, 0 to 63

public override void OnCreate()
{
    if (Network.IsAuthority) Entity.NetVars.SetInt(Health, 100);
}

public void TakeDamage(int amount)         // Host only
{
    if (!Network.IsAuthority) return;
    Entity.NetVars.SetInt(Health, Entity.NetVars.GetInt(Health) - amount);
}

public override void OnNetVarChanged(int slot)     // every machine
{
    if (slot == Health) healthBar.Value = Entity.NetVars.GetInt(Health);
}
```

- **Turn it on per object:** in the object's **Network Object** table, set the
  **NetVariables** row to **Replicated**. The row is always listed; the engine adds the
  component itself while a session runs, so there is nothing to add in the Inspector.
  Without it, a `Set` on a network object is refused and logs why.
- Types: `SetInt` / `SetFloat` / `SetBool` / `SetEntity` use one slot; `SetVector3` uses
  three in a row. Read a slot the way it was written.
- Every slot starts at 0. The Host sets the first values, in `OnCreate`.
- A player who joins later gets the current values with the object, and a lost packet is
  made up for by the next one. That is the difference from an event.
- A Client's `Set` on a network object is refused. On an entity that is not a network
  object, variables work but stay on that machine.
- The script that reads them on Clients must run **Everywhere**.
- Use them for values that change now and then. The whole object is resent whenever
  anything on it changes.

### 8.7 A complete example: the arena shooter (`network_gamplay.scene`)

A small player-versus-player shooter for 2 to 4 players, written only in C# on top of the
API above. Nothing in it is native code.

| File | Role |
|---|---|
| `Assets/Scripts/network_tests/ArenaRules.cs` | **The rules, decided by the Host.** Fire requests (**commands**) are checked: alive, cooldown, the shot starts at the shooter. Bolts are **spawned** owned by the shooter. Health, kills, deaths and alive/dead live in **network variables**. Hits, deaths and respawns are **reliable events**; "somebody fired" is **unreliable**. |
| `Assets/Scripts/network_tests/ArenaView.cs` | **What every machine shows**, read from those variables and events: nameplates with health bars, hiding dead bodies, the HUD, hit and kill messages, a kill feed. It never changes the game. |
| `Assets/Scripts/network_tests/NetPlayerAnimation.cs` | The players' mech legs: walk, stand and jump, set on the Host and synced (section 8.12). |
| `Assets/Scripts/network_tests/NetBolt.cs` | The projectile. Runs on the Host only: flies, raycasts against the players' capsules, reports hits to `ArenaRules`, despawns itself. |
| `Assets/Prefabs/network_tests/NetBolt.cprefab` | Network prefab: Transform replicated, script **Host only**, **critical** (fast, so in every snapshot). |
| `Assets/Prefabs/network_tests/NetNameplate.cprefab`, `NetHud.cprefab` | Local prefabs (not networked): each machine makes its own. |

**Settings** are on the **Arena Rules** entity's scripts in the Inspector: max health (100),
fire cooldown, respawn delay (3 s), self damage, and the bolt's speed, damage (25) and
lifetime on the NetBolt prefab. Respawn points are the entities named **Arena Respawn 1**
to **4**: move them, or add **Arena Respawn 5** and so on.

**Death:** the Host switches off the dead player's CharacterController and PlayerController
(no movement, no capsule to hit, fire refused), and every machine hides the body (its
**Mech Visual**). After the
delay the Host moves it to the respawn point farthest from everyone, switches them back on
and restores full health.

**Controls:** move as usual; **left mouse** or **F** fires where your camera looks.

**Play it:** open `network_tests/network_gamplay.scene` in 2 to 4 Editors; one **Host**, the others
**Client** (Network window > Session > Quick test), then Play in each. Offline nothing happens: there is no spawned
player.

### 8.8 Changing scenes without ending the session

```csharp
if (Network.IsHost) Network.LoadScene("Scenes/network_tests/network_gamplay.scene");
```

- **Only the Host** can do this. Every machine loads the same scene from its own project,
  so every machine must have it.
- **What happens:**
  1. The old scene, with every network object in it, goes on every machine - except the
     ones kept with `Network.KeepAcrossScenes` (section 8.30).
  2. The new scene's network objects are set up as when a session starts.
  3. Every player's character is spawned again, at the new scene's spawn points (or, with
     `Network.KeepPlayersAcrossScenes`, the same character is moved there).
- **Clients** get the new scene's world as if they had just joined, so `OnNetworkStarted`
  runs again on them.
- **The Host** gets `OnAllPeersReady()` once every client has loaded the scene, has its
  world, and has drawn 3 frames of it (so its first-frame stutter is over): the moment to
  start a round.
- `OnNetworkSceneLoading()` reaches only scripts that survive the load (DontDestroyOnLoad).
- A client that cannot load the scene leaves. A client that takes more than 30 s is dropped.
- Offline, `Network.LoadScene` is an ordinary `Scene.Load`.

### 8.9 Starting a session from a script, and the lobby example

```csharp
Network.StartHost();                 // port and player name from the Network window / EngineConfig.json
Network.Join("192.168.1.20");        // the Host's IP address
Network.Leave();                     // the Host leaving ends the session for everyone
```

- **The start happens next frame.** The result arrives as `OnNetworkStarted`, or as
  `OnNetworkStopped(reason)` if it failed: port in use, refused, version mismatch.
- **A session can't be started twice.** `StartHost` and `Join` return false (with the reason
  logged) while one runs.
- **The Network window:**
  - **Quick test: Offline** means "the game's own menu decides".
  - **Host** and **Client** are the quick test that starts at Play.
  - **Player name** is used when a script gives none.

**Finding sessions on the network (no IP address to type):**

```csharp
Network.StartBrowsing();                       // while a lobby list is shown
foreach (LobbyInfo lobby in Network.Lobbies)   // HostName, Players / MaxPlayers, Availability
    ...
Network.Join(chosenLobby);                     // refused if it closed, filled or stopped answering
Network.StopBrowsing();
```

- A host answers on the **discovery port** (UDP 27019, separate from the game port), and
  every browser lists the hosts that answered. It works on one PC and across a LAN.
  Windows may ask the host to allow the game through its firewall.
- **What a list shows:** each entry has the host's name, the player count, a status, and
  whether it can be joined: **Open**, **Full**, **In progress** (the host closed it with
  `Network.SetLobby(false, "In match")`), **Different version** or **Not responding**.
- **When a host goes away,** its entry disappears within about 3 seconds.
- **The host has the last word:** a full or closed session refuses a join itself, even one
  made with a typed IP address.

**Why a session ended:** read `Network.LastEndCode` (an `EndCode`) in `OnNetworkStopped`, or
let `Network.DescribeEnd(wasJoined)` give the sentence:

| Code | Message |
|---|---|
| `SessionEnded` | "The host left. The session has ended." |
| `ServerFull` | "That lobby is full." |
| `LobbyClosed` | "That game has already started." |
| Lost connection or timeout | "Could not reach that lobby" or "Lost the connection to the host" |

A session never outlives its host. When the host leaves, closes the game or stops
answering, every client's session ends.

**The lobby example: `network_lobby.scene`** (`NetLobby.cs`, `NetLobbyUi.cprefab`). Leave
the Network window's **Quick test** on **Offline** and press Play in every Editor. Each Editor's **Player name**
is shown in the lists (default "Player"; set a different name in each Editor to tell them apart).

| Screen | What it does |
|---|---|
| **Menu** | **Create Lobby** (you host) or **Find Lobbies**. |
| **Find Lobbies** | Sessions on your network, refreshed live. Pick one and **Join Lobby** (you are a client), or **Back**. |
| **Waiting** | "Creating your lobby..." / "Joining X's lobby...", with **Cancel**. |
| **Lobby** | Everyone and who is ready. **Ready** / **Not Ready**; the host's **Start Match** works once everyone is ready and there are at least 2 players; **Leave Lobby**. |

**Starting the match** closes the lobby to new players and takes everyone to the arena.
In the arena, **Backspace** (host) brings everyone back to the lobby, and the lobby opens
again. **L** leaves.

**Feedback:** whenever a session ends, every machine goes back to the lobby menu with a
sentence saying why: the host left, the connection was lost, the lobby was full, the game
had already started, or the join was cancelled. After a failed join it goes back to the list.

### 8.10 In the Network window

A Client lists every player in the session under its status. **Debug > Counters** shows
rosters and commands sent, received and refused.

### 8.11 The connection server: one place that lists every lobby

LAN broadcast (8.9) only reaches PCs on the same subnet, and some networks (university Wi-Fi,
some routers) block it. The **connection server** is a small program that runs on one PC.
Hosts tell it about their lobby, and lobby browsers ask it for the list. It only knows
**who is hosting**: the game itself still connects directly to the host, and **the server never
carries gameplay**. If it crashes or is closed, running matches carry on. In **Auto** mode a
lobby browser tries it 5 times, then says it is not accessible and offers to search the LAN
instead.

| Port (UDP) | What |
|---|---|
| `27018` | the connection server |
| `27019` | LAN discovery (broadcast) |
| `27020` | the game itself (Host port) |

**Running it from the Editor (easiest):** **Network > Start Connection Server**, or the
**Connection Server** tab of the Network window. Building the Editor also builds the server
(`ConnectionServer/` project). The tab shows:
- whether it runs, its uptime and how many lobbies it lists;
- **Addresses**: what other PCs should type, each with **Copy**;
- **Use this server**: sets this Editor's Lobby discovery to it;
- the server's **log**.

It runs as its own hidden program: **closing the Editor leaves it running**, and the next
Editor you open finds it again. Stop it with **Network > Stop Connection Server**. The tab
also sets its **UDP port** and **Lobby lifetime** before starting. If the server stops by
itself (for example, another server already uses the port), the tab says so and the log shows
why. Its files are next to the exe: `CrucibleConnectionServer.log`, and
`CrucibleConnectionServer.pid` while it runs.

**Running it by hand** (for example on a PC without the Editor): start
`Build/ConnectionServer/Release/CrucibleConnectionServer.exe` (or the Debug one). It prints the
addresses it can be reached at, for example `192.168.0.7:27018 (Wi-Fi)`, and logs lobbies as they
come and go. Press **Ctrl+C** to stop it. The Editor's tab can't stop a server started this way,
but the Session tab still shows whether it answers.

```text
CrucibleConnectionServer.exe [--port 27018] [--lifetime 7000]
```

- `--port`: the UDP port to listen on.
- `--lifetime`: milliseconds without a heartbeat before a lobby is dropped (3000 to 60000).
  Hosts send a heartbeat every 2 s.

Windows may ask to allow it through the firewall the first time it runs: allow it on
**Private networks**.

**Pointing the game at it:** in **Network Window > Session**, open **Lobby discovery** (or press
**Use this server** on the PC that runs it):

| Field | Meaning |
|---|---|
| **Auto** | Lobby lists come from the connection server. A browser sends it up to 5 list requests (about 5 s); if none is answered, the server is **not accessible**, the list stays empty, and the game can offer **LAN discovery** instead (the lobby example shows a **Search LAN instead** button). LAN is used only once the player chooses it, until the browser is opened again. With no address set, Auto uses LAN broadcast. A host registers with the server **and** answers LAN broadcasts, so it is found either way. |
| **Server** (default) | The connection server only: lobbies are listed only while it is running, so start it first (**Network > Start Connection Server**). |
| **LAN** | LAN broadcast only; the server is not used (testing). |
| **Server address** | The IPv4 address the server printed, e.g. `192.168.0.7`. Default `127.0.0.1`: a connection server on **this PC**. The Network window says so under the field: on another device, change it to the address of the PC running the server. Empty means no server. |
| **Server port** | Default `27018`. |

The same values can be set for everyone in `Assets/Config/EngineConfig.json`:

```json
"network": {
    "lobbyDiscovery": "server",
    "lobbyServerAddress": "192.168.0.7",
    "lobbyServerPort": 27018,
    "discoveryPort": 27019
}
```

Under its status, the Session tab shows `Connection server 192.168.0.7:27018: online`
(`(this session is listed)` on a host), `connecting (attempt 2 of 5)...` or `unreachable`. The
lobby example's list header shows where the list comes from: **Connecting to the connection
server...**, **Lobbies - connection server**, **Connection server not accessible** (with
**Search LAN instead**), **Lobbies on your network (LAN)**, or **Lobbies - showing 6 of 40**.

**From C#:**

```csharp
Network.LobbyServerState      // Off, Connecting, Online, Unreachable (LobbyServerStatus)
Network.LobbyServerAddress    // "192.168.0.7:27018", or ""
Network.LobbyDiscovery        // Auto, Server, Lan (LobbyDiscoveryMode)
Network.LobbyListFromServer   // true: Lobbies is the server's list; false: the LAN list
Network.LobbyListTotal        // how many lobbies the server knows of
Network.LobbyListTruncated    // it sent fewer than that (one reply holds about 25)
Network.IsLobbyRegistered     // host: the server lists this session
Network.LobbyServerFailedAttempts  // unanswered list requests in a row (LobbyServerMaxAttempts = 5: Unreachable)
Network.CanUseLanDiscovery    // Auto, server not chosen against yet: UseLanDiscovery would switch now
Network.UseLanDiscovery()     // the player agreed: list LAN lobbies until the next StartBrowsing
Network.IsUsingLanDiscovery   // LAN was chosen for this browse
lobby.FromServer, lobby.FromLan   // how a LobbyInfo was found (both = the same lobby seen both ways)
```

**How it behaves:**
- **A host appears** within about a second of starting, and **disappears** at once when it
  stops (it unregisters). If it crashes, it disappears after the lifetime (7 s by default).
- **Player counts** update with each heartbeat (every 2 s).
- **The server lists lobbies, not matches.** When the host closes its lobby (the match
  starts, `Network.SetLobby(false, "In match")`), it unregisters: it leaves the server's list
  and frees its place (see Limits) at once. The match and its players are not affected, and
  nothing waits for the server: if it is unreachable, the entry expires after the lifetime
  (7 s). When the session goes back to its lobby (`SetLobby(true, ...)`), it registers again.
  LAN lists still show a closed lobby, as **In progress**.
- **If the server restarts,** hosts register again by themselves within about 2 s.
- **Every lobby has a random 64-bit id and a secret owner token** (from Windows'
  `BCryptGenRandom`). Only the host holding the token can update or remove its lobby. This
  stops other players on the LAN from changing a lobby by mistake or as a prank. It is **not**
  strong security: anyone who can see the network traffic can read the token.
- **One lobby, two sources:** in Auto mode the same lobby found through the server and through
  LAN broadcast is listed once.
- **Which address you join:** the host reports its own network addresses, and each browser
  picks the one on its own subnet, so PCs with several network adapters (Wi-Fi + Ethernet,
  VirtualBox, VPN) get a reachable address. A host on the same PC as the browser is joined at
  `127.0.0.1` when LAN broadcast also found it.
- **Limits:** at most 256 lobbies on a server, 8 from one IP address (open lobbies only:
  matches in progress are not registered), and one list reply (about 25 lobbies) per request.
  The list says when it was cut short.

**Testing across PCs:**
1. On PC A: **Network > Start Connection Server**. Open **Network > Connection Server...**,
   note the address under **Addresses** (e.g. `192.168.0.7:27018`), and press **Use this server**.
2. In every other Editor (PC B, ...): **Network > Network Window > Session**, keep **Quick test**
   on **Offline**, then **Lobby discovery > Server** (the default), **Server address** `192.168.0.7`
   (in place of the default `127.0.0.1`),
   **Server port** `27018`.
3. Open `network_tests/network_lobby.scene` and press **Play** in every Editor. One player chooses
   **Create Lobby**, and the others choose **Find Lobbies**. The header should say
   **Lobbies - connection server**, and the lobby should be listed. Join it.
4. Start the match, then on PC A choose **Network > Stop Connection Server**. The match should
   keep going. In a browser (**Find Lobbies**), the header changes to **Connection server not
   accessible** after about 5 s, with **Search LAN instead**. Press it: the LAN lobbies appear.
5. Start the server again (**Network > Start Connection Server**). Within a couple of seconds
   the host is registered again, without doing anything. A browser that chose LAN keeps the LAN
   list; press **Back** and **Find Lobbies** to use the server again.
6. Optional: with the default **Lobby discovery > Server** the list can only come from the server;
   set **LAN** on a browser PC to check the broadcast path, or **Auto** to try the server with
   LAN as the fallback.

**Not covered:** the internet. The server only lists lobbies; it does not relay or punch
through routers (no NAT traversal, STUN, ICE or TURN). Players must be able to reach the
host's IP directly, as on a LAN.

### 8.12 Animation: the Host's Animator on every machine

A character's animation comes from its **Animator parameters** (`Speed`, `Attack`, ...),
which scripts set with `Animator.SetFloat`, `SetBool`, `SetInteger` and `SetTrigger`. When
those scripts run on the Host only (AI, game rules), turn on Animator syncing and the Host's
parameter values reach every Client. Each Client's own animator then plays the same states
from them, a snapshot behind the Host, like the movement it goes with.

- **Turn it on per object:** in the object's **Network Object** table, set the
  **NetAnimator** row (serializer **Animator**) to **Replicated**. Like NetVariables, the row
  is always listed and the engine adds the component itself during a session.
- **Nothing changes in your scripts.** Keep calling `Animator.Set...` on the Host; the engine
  notices the calls on a network object and sends them. Models below the root count too: the
  root and all its children, in hierarchy order, each with all its controller's parameters.
- **Up to 32 parameters per object.** Any more are not synced, and a warning names the
  object; they still work on each machine on its own.
- **Triggers** are sent as a count, so a Client fires each one once. A Client that joins
  later doesn't replay triggers fired before it arrived. Two fires within one snapshot
  (about 33 ms) play once. `ResetTrigger` is not sent.
- **On a Client** a synced parameter belongs to the Host: a script's `Set...` returns false.
  Parameters of objects without the row, and of local entities, work as usual.
- A parameter the Host has never set keeps its controller's initial value on Clients.
- **Both machines need the same controllers.** Every animator controller's parameter names
  and types are part of the content check, so a Client whose controllers differ is refused
  when joining.

**Example:** `network_gamplay.scene` has an **Animation Dummy** (script
`Assets/Scripts/network_tests/NetAnimDummy.cs`, with a **Body** child that uses the golem
model and the Enemy Fodder controller). The Host walks it back and forth, jogs it, and stops
it for a melee attack (the `Attack` trigger) and a flinch (`HitDirection` and the `Hit`
trigger). Its table replicates **Transform/YawOnly** and **Animator**. Play the arena as Host and
Client: on the Client the dummy should walk, jog, attack and flinch at the same moments as on
the Host. The players' mechs use the same mechanism: their legs walk when they move and
stand when they stop, on every machine (section 5.1). Set its **Animator** row back to **Local** to see the difference: on the Client it
slides along in its idle pose.

### 8.13 Physics queries on a Client

A network object's Collider, RigidBody and CharacterController run **Host only**, so a
Client has no physics of its own for it. So that a Client's physics queries still hit what the
player sees, the engine gives the Client a
**query body** for each of them: a hidden child named **Network Query Body** with a copy of
the Collider (same shape, size, layer and trigger setting), or a capsule the size of the
CharacterController. It follows the object as drawn and is never simulated, pushed or sent.

- **Nothing to set up.** Every network object gets them on Clients, for each Collider or
  CharacterController that is **Host only**. One that runs **Everywhere** has a real body
  already and gets none. The Host gets none: it has the real ones.
- **Not on the Client's own player**, so rays from its camera don't hit its own body.
- **Every query sees them:** `Physics.Raycast`, `RaycastNonAlloc`, `SphereCast`,
  `CapsuleCast`, `CheckSphere`, `OverlapSphereNonAlloc`, `OverlapBoxNonAlloc` and
  `OverlapCapsuleNonAlloc`.
- **A hit lands on the query body**, a child of the object, while on the Host it lands on the
  object (or one of its parts). The `Overlap...NonAlloc` results and `RaycastNonAlloc`'s hits
  are query bodies too. Use `Network.ObjectOf(hit.Entity)` (or on each overlap result) to get
  the network object on both:

  ```csharp
  RaycastHit hit;
  if (Physics.Raycast(origin, direction, out hit, 100f, Network.LocalPlayerEntity))
  {
      Entity target = Network.ObjectOf(hit.Entity);   // the player, prop or wall; null if not networked
  }
  ```

- **For display and feel only:** aim highlights, interaction prompts, "can I reach it" checks.
  The Host still decides every hit and its damage with its own raycast, as the arena does.
  A Client's query sees each object where it was drawn last frame, about 100 ms behind the
  Host, so the two can disagree near an edge. The Host's answer wins.
- **When the Host switches physics off** (a dead player's CharacterController, an open door's
  Collider): tick **Sync on/off** on that **Host only** component, on the object's root. The
  component stays off on Clients, and their query body switches off and on with the Host's.
  This combination is allowed only for Collider and CharacterController. Without it, a
  Client's query still sees the object: check the game's own state instead. The player prefab
  has it on its CharacterController.
- **Triggers** on query bodies only notice the Client's own local objects; gameplay triggers
  belong on the Host.
- **Terrain gets no query body.** A Terrain's HeightField Collider takes its shape from the
  Terrain on the same entity, so it can't be copied. Clients have their own terrain body
  instead: every machine generates the same ground from the scene seed (section 8.14).

**Example:** in `network_gamplay.scene` the crosshair turns red while your aim is on another
living player (`ArenaView.cs`, `UpdateAim`). On the Host the ray hits the player's
CharacterController; on a Client it hits that player's query body. It works the same on both.

### 8.14 Generated levels: the scene seed

A level generated from a seed (the `SubLevelGenerator` terrain) must use the **same seed on
every machine**, or each player stands on different ground. While a session runs, every scene
has one **scene seed**, the same number on every machine:

```csharp
int seed = Network.HasSceneSeed ? Network.SceneSeed : mySeed;   // offline: your own
```

- The Host picks it **at random** for every scene: the one the session starts in, and every
  `Network.LoadScene`. To choose it yourself: `Network.LoadScene("Scenes/Level.scene", 1234)`.
- A Client has it **before** the new scene's scripts start (`OnCreate`), because it comes with
  the Host's "change scene" message.
- A Client that **joins a scene already running** learns it only when it joins: regenerate in
  `OnNetworkStarted` if it differs from what you built.
- Offline, `HasSceneSeed` is false and `SceneSeed` is 0.
- Generate on every machine, but let only the **Host** do gameplay with it (placing players,
  spawning enemies): those reach Clients as network objects.

### 8.15 Scale and character size

- **Scale:** set the Transform to `Replicated: Transform/Full+Scale` (or
  `Transform/YawOnly+Scale`). The Host's scale reaches every Client as it is, without
  smoothing. The plain `Transform/Full` and `Transform/YawOnly` never send scale, exactly as
  before. 12 bytes more per update.
- **Character size** (crouching, growing): set the CharacterController row to
  `Replicated: CharacterController/Size` (height, radius and centre). Keep its **Runs on** at
  **Host only**: on Clients the controller stays off, and the capsule their raycasts hit
  (section 8.13) takes the new size, and undoes the object's scale as the Host's capsule does.

### 8.16 Moving parts: synced children

For parts that move on their own while the object stays put (a torso aiming apart from the
legs, a turret head, a door's hinge):

1. List them under **Synced children** (section 1.3).
2. Set the **NetChildTransforms** row to `Replicated: Children/Rotation` (rotations only, 4
   bytes per child) or `Replicated: Children/Full` (positions and rotations, 16 bytes per
   child).

- The Host sends each child's **local** transform (relative to its parent inside the object).
  Clients draw them on the same 100 ms delay as the object itself, so the parts and the
  object move together. A jump of more than 5 m snaps instead of sliding.
- A child the Host can't find (renamed, removed) isn't sent; the Client leaves its own copy
  alone and logs it once. The same happens on a Client that can't find it.
- Scripts on Clients shouldn't move synced children: the Host's values overwrite them every frame.

### 8.17 Attaching network objects to each other

A network object can hang under **another** network object: a mech part on a player's
mount, a held item, a passenger. Set the attached object's **NetAttachment** row to
`Replicated: Attachment`, and its Transform to `Replicated` as usual. Then on the Host:

```csharp
part.Transform.SetParent(mount, false);   // as MechPart.AttachTo does
part.Transform.SetParent(null, true);     // drop it
```

- Clients parent their copy to **the same mount**, found by its name path inside the other
  object (so the mount's name must be unique among its siblings), and use the same local
  offset. Changing mount and detaching work the same way.
- The attached object's motion **restarts** when its parent changes: it snaps to its new
  place instead of sliding through the world.
- **Joining mid-game:** an attached object waits until the object it hangs on has arrived,
  then attaches.
- **Hanging under a scaled object?** Give the attached object `Transform/Full+Scale`. Dropping it
  with its world pose kept (`SetParent(null, true)`) moves the parent's scale into its own
  Transform, which plain `Transform/Full` doesn't send.
- **Parent destroyed:** on the Host, destroying an object destroys what's attached to it, and
  every Client follows. To keep a part, detach it first.
- Attached to something that isn't a network object (a plain scene entity), or to a mount
  whose name isn't unique: Clients see it unattached, and the Host logs it once.

### 8.18 Looks: renderers and material parameters

Set the **NetRenderers** row to `Replicated: Renderers`. The Host's renderers then reach
every Client, at the start and whenever they change:

- On the **root** and the **first 7 synced children** (section 1.3): model, material, mesh,
  up to 4 material slots, colour, visible, and **MaterialParams** (emissive scale and tint).
  A renderer further down the list is refused at Play (move it up).
- Change them from Host scripts as usual (`mesh.Material = damagedLook`, `mesh.Model = ...`,
  `mesh.Visible = false`, `params.Tint = Color.Red`).
- A **shader graph's** own parameters belong to its material: swap the material to change
  them on every machine.
- Not synced: meshes and materials made at runtime (they have no asset file), more than 4
  material slot overrides (the first 4 are sent).

### 8.19 How much one object can send

Everything an object replicates travels together in **one record of at most 1024 bytes**,
with at most 16 rows replicated. The red message under the table says when the worst case
doesn't fit, with each row's share. The big ones are `Renderers` (95 bytes per renderer it
covers), `Animator` (132), `Children/Full` (16 per child) and `NetVariables` (66). An object
whose record is unchanged costs nothing; one that changes sends its whole record again.

### 8.20 Switching parts on and off: on/off below the root

To sync a component switched on or off on a **child** (a weapon model shown or hidden, a muzzle
light, a part's hitbox):

1. List the child under **Synced children** (section 1.3).
2. In that component's row, set **Sync on/off** to `Synced children` (or `Both`, to sync the
   root's too).

- Every synced child that has that component follows the Host: `gun.SetEnabled<MeshRenderer>(false)`
  on the Host hides the gun everywhere.
- At most 8 components, 2 bytes per synced child.
- A **Host only** Collider or CharacterController on a child works as on the root: it stays off
  on Clients, and its query body follows the Host's on/off (section 8.13). Other Host only or
  Owner only components can't be synced on/off.
- `Entity.SetActive` on a child isn't synced as such: it switches every component below it, and
  only the components you listed are sent. Sync the ones that matter (the MeshRenderer, for
  example).

### 8.21 Giving an object to a player: ownership

```csharp
Network.SetOwner(vehicle, driverPeerId);   // Host only; 0 = the Host
```

- It changes next frame **on every machine**: `Entity.Owner` and `IsMine` follow, the object's
  **Owner only** components switch on for the new owner and off for everyone else, and if it
  has a `PlayerInput`, the new owner's movement drives it.
- The object's own scripts hear it on every machine:

  ```csharp
  public override void OnOwnershipChanged(int previousOwner, int newOwner) { ... }
  ```

- **Players can't change owner**: a player always belongs to its own player (refused, logged).
- **A Client leaving:** its player is removed as before, and every other object it owned goes
  back to the Host.
- A Client joining later receives every object with its current owner.

### 8.22 Checking it all with two Editors: `network_sync_test.scene`

A test scene for sections 8.15 to 8.21. Open `Scenes/network_tests/network_sync_test.scene` in both Editors,
start the **Host** in one and the **Client** in the other, as in section 3. The **Sync Demo**
panel behind the mech (the middle one of three) says what each machine sees; it should read
the same on both after each key.
Press the keys in the **Host's** Game view:

| Key | What changes | Check on the Client |
|---|---|---|
| **1** | The mech's scale: 1, 1.5, 2 | Same size (`scale` matches) |
| **2** | The mech's capsule: height 2 and 1 | `height` matches (the capsule Clients' raycasts hit is resized) |
| **3** | The torso turns 30 degrees | Turns smoothly, same angle, together with the mech |
| **4** | The mech's material: EmberFlow and MagicOrb | Same material |
| **5** | The tint: none, red, green | Same tint |
| **6** | The gun: hidden and shown | Hidden and shown |
| **7** | The part: Mount L, Mount R, dropped, back | Snaps to each mount, no sliding; dropped in the same place |
| **8** | The crate's owner: Host, each Client, Host | The crate's light is on **only** on its owner's screen; the Console logs `SyncOwnerProbe` on every machine |
| **9** | The mech: hidden and shown | Hidden and shown |
| **0** | Everything back to the start | Back to the start |

Also check:
- **Joining late:** press some keys, then start a second Client. It shows the same text at once.
- **Leaving:** give the crate to a Client (key 8), then stop that Client. The crate goes back to
  the Host (its light comes on there).
- **Parent destroyed:** press 7 to put the part on a mount, then delete `Sync Mech` in the
  **Host's** Hierarchy. The part is destroyed everywhere with it.
- **Player input (section 8.23):** hold **J**, **K** or **L** in the **Client's** Game view. The
  Host's text shows them held next to the Client's number (`1: J--`); each tap, however short,
  adds exactly one to the count (`x3`). The Client's own text shows its own keys. Stop the
  Client: its entry disappears; freeze it (pause it in the debugger) and it reads `(none)`
  within about a quarter second.

### 8.23 Player input: every player's buttons on the Host

The Host runs the game, so it needs to know what **every** player presses - not only how they
move (the `PlayerController` already does that), but fire, use, reload, and where they aim.
Each machine gives its own input every frame; the Host reads everyone's:

```csharp
const int Fire = 0, Use = 1, Reload = 2;          // your game's actions: 0 to 31

// One script on every machine (the Host and offline too):
ActionMap actions = new ActionMap()
    .Bind(Fire, KeyCode.MouseLeft)
    .Bind(Use, KeyCode.E)
    .Bind(Reload, KeyCode.R);

public override void OnUpdate(float dt)
{
    actions.Send(cameraPitch);                     // actions and aim pitch
    // or Network.SetLocalInput(move, yaw, pitch, actions.Sample()) to send movement too
}

// Gameplay, on the player's own script - runs on the Host (and offline):
public override void OnUpdate(float dt)
{
    if (!Network.IsAuthority) return;
    NetInput input = PlayerActions.Of(Entity);      // whoever owns this player
    if (input.WasPressed(Fire)) Shoot(input.Yaw, input.Pitch);
    if (input.IsDown(Use)) Interact();
}
```

| Call | Meaning |
|---|---|
| `Network.SetLocalInput(move, yaw, pitch, actions)` | This machine's input this frame: actions (bit i = action i), aim pitch, move direction (world X/Z) and yaw. |
| `Network.SetLocalActions(actions, pitch)` | Only actions and pitch (players moved by the `PlayerController`). |
| `Network.GetInput(peer)` | What a player does this frame. The Host reads anyone (0 is itself); a Client only itself. |
| `Network.LocalInput` | This machine's own. |
| `PlayerActions.Of(entity)` | The input of whoever owns the entity. |
| `input.IsDown(a)`, `input.WasPressed(a)` | Held now; went down since last frame (true for exactly one frame). |
| `input.Move`, `Yaw`, `Pitch`, `Held`, `Pressed`, `IsFresh` | The rest. `IsFresh` is false before any input and when it stopped. |

- **Same code everywhere.** Offline and on the Host, `GetInput` reads this machine's input the
  same way, so single-player gameplay that uses it becomes multiplayer without changes.
- **Taps are never lost.** A Client sends its input 30 times a second, each command 3 times;
  a press shorter than that still shows as one `WasPressed` on the Host - never two.
- **A frame behind.** The Host sees its own input one frame after it was given, like everyone
  else's; a Client's arrives with its ping.
- **The `PlayerController` is unchanged.** A player with one keeps moving with its own keys;
  scripts only add actions and pitch. Without one, `SetLocalInput`'s move and yaw are sent.
- **Frozen or cut-off players stop.** No input for about a quarter second (16 steps): nothing
  held, no move; facing and pitch are kept.
- **The Host decides.** A Client reading another player's input gets nothing: what others do
  reaches it as their results (movement, events, network variables).

### 8.24 One clock for everyone: `Network.Time`

Every machine's `Time.ElapsedTime` is different, so a countdown or cooldown shown on a Client
would not match the Host's. `Network.Time` is the **Host's** game time, on every machine:

```csharp
const int RoundEnd = 0;                                   // a network variable slot

// Host: the round ends in 60 seconds. Stored once, as a tick of the shared clock.
if (Network.IsAuthority) Entity.NetVars.SetInt(RoundEnd, Network.Tick + 60 * Network.TickRate);

// Every machine: what is left - no message needed while it counts down.
double left = Entity.NetVars.GetInt(RoundEnd) / (double)Network.TickRate - Network.Time;
```

| Call | Meaning |
|---|---|
| `Network.Time` | Seconds of the Host's game time. A Client estimates it from the snapshots, corrected for half its ping: within about a frame. |
| `Network.Tick`, `Network.TickRate` | The same in whole ticks, 60 a second. Store ticks in network variables (an int), not seconds in a float. |
| `Network.HasSharedTime` | False offline and while joining: `Time` and `Tick` are then this machine's own (`Time.ElapsedTime`), so single-player code works unchanged. |

- It stops while the Host is paused; a Client's estimate stops within about 0.1 s.
- Decide on the Host (`Network.IsAuthority`) when something is over; Clients only show it.

### 8.25 Pings, and removing a player

```csharp
int mine = Network.Ping;                       // this player's ping to the Host, in ms
foreach (int peer in Network.Peers)
    Log.Info(Network.GetPeerName(peer) + ": " + Network.GetPing(peer) + " ms");

if (Network.IsHost) Network.Kick(peer, "AFK");  // Host only
```

- **`Network.GetPing(peer)`:** the Host measures every Client's ping (its own is 0) and sends
  everyone the list **once a second**. A Client's own ping is live. -1 means unknown or not in
  the session.
- **`Network.Kick(peer, reason)`:** the Client is removed at once. It hears
  `OnNetworkStopped("kicked by the host: AFK")` and `Network.LastEndCode` is `EndCode.Kicked`;
  everyone else hears `OnPeerLeft`. Its player and objects are handled as when a player leaves.
  Nothing stops it from joining again.

### 8.26 More synced state: 64 variables and `NetData`

- **Network variables now have 64 slots** (0 to 63, section 8.6). Nothing else changed:
  existing scripts work as before.
- **`Entity.NetData`** holds up to **256 bytes** per object for things numbered slots don't
  suit: a list of items, ammo per weapon, a loadout. Write it with a `NetWriter`, read it back
  with a `NetReader`, in the same order:

```csharp
// Host:
NetWriter w = new NetWriter();
w.WriteByte((byte)ammo.Length);
foreach (int a in ammo) w.WriteUShort((ushort)a);
Entity.NetData.Set(w);

// Every machine:
public override void OnNetDataChanged()
{
    NetReader r = Entity.NetData.Read();
    int count = r.ReadByte();
    for (int i = 0; i < count; ++i) ammo[i] = r.ReadUShort();
}
```

- Turn it on like the variables: in the object's **Network Object** table, set the **NetData**
  row to **Replicated**. A player who joins later gets the current bytes with the object.
- **Cost:** an object that hasn't changed costs nothing, but any change to it sends its whole
  record again, block included. Keep a big block on an object that doesn't move every frame
  (a game state or loadout object), or keep it small.
- **One record holds at most 1024 bytes.** The variables can take up to 266 and the block 258,
  so an object that also syncs renderers and children may not fit; the Inspector says so,
  listing what each part needs.

### 8.27 Checking 8.24 to 8.26 with two Editors: the Api Demo

`Scenes/network_tests/network_sync_test.scene` has an **Api Demo** panel left of the Sync
Demo's (light blue text). Start the Host and a Client as in section 8.22. Each machine shows: the shared
time and tick, a countdown, every player's ping, slot 63, and the NetData list.

Press the keys in the **Host's** Game view:

| Key | What it does | Check on the Client |
|---|---|---|
| **T** | Starts a 10 second countdown (its end tick in slot 0) | Counts down **together** with the Host, within about 0.1 s, and says `done` at the same moment |
| **V** | Adds 1 to slot 63 | `slot 63` follows |
| **B** | Adds an item to the list (8 at most, the oldest dropped) | The same list; `heard` goes up by one each time |
| **N** | Empties the list | `(empty)`, `0 bytes` |
| **X** | Kicks the newest Client | That Client's text: `last stop: kicked by the host: kicked from the Api Demo (key X) (code 1010)`; the Host's ping list loses it |

Also check:
- **Time:** the `time` line reads the same on both, to about a tenth of a second (compare a
  screenshot of both windows, or watch the countdown). Offline it says `(own clock)`.
- **Pings:** each line shows every player; the Client's own number updates live, the others
  about once a second. Two Editors on one PC show 0 to a few ms.
- **Joining late:** press B a few times and V, then start a second Client: it shows the same
  list and slot 63 at once.
- **Rejoin after a kick:** press Play again in the kicked Editor and join: it is let back in.

### 8.28 Helpers for gameplay scripts

All in `ManagedScripts/Runtime/Networking/`. They are plain C# on top of sections 8.3 to 8.26:
nothing new goes over the network.

**Where does this logic run?**

| Logic | Runs on | Guard |
|---|---|---|
| Damage, deaths, scores, spawning, waves, timers ending | The Host (and single player) | `if (!Network.RequireAuthority("TakeDamage")) return;` |
| Reading your own keys, your camera, your HUD | Each machine, for its own player | `if (!Network.IsLocalPlayer(Entity)) return;` (or `Entity.IsMine`) |
| Looks and sounds | Every machine | `NetEffect.Play` (below), or an event |
| Asking for something | A Client | `SendCommand` / a named command |

`Network.HasAuthority(entity)` says whether this machine decides about an entity (the Host,
or single player). `RequireAuthority` logs once per name on a Client, so a Host-only call left
in a Client's path shows up in the Console instead of silently doing nothing.

**Named messages (`NetMessage`)** - no more picking numbers that nobody else uses:

```csharp
static readonly NetMessage EnemyDied = NetMessage.Named("enemy.died");

EnemyDied.SendEvent(writer, enemy);                       // as Network.SendEvent
public override void OnNetworkEvent(ushort kind, NetReader data) { if (EnemyDied.Is(kind)) ... }
NetMessage.OnEvent(EnemyDied, (data, target) => ..., this);   // or once per machine, no override
```

- The number comes from the name, so it's the same on every machine. Two names that land on
  the same number are reported (naming both): rename one. Old hand-numbered kinds (1 to
  32767) still work; 0xFF00 and up belong to the helpers.
- A handler with an owner (`this`) is dropped once its entity is gone.

**Synced fields (`NetScript`)** - change the base class and the field types:

```csharp
public sealed class Mech : NetScript
{
    readonly NetInt health = new NetInt(slot: 0, initial: 100);
    readonly NetTimer dash = new NetTimer(slot: 1);

    public override void OnCreate() { health.Changed += (old, now) => bar.Value = now; }
    void TakeDamage(int n) { health.Value -= n; }                         // the Host
    void Dash() { if (!dash.IsRunning) { ...; dash.Start(2f); } }         // the Host
}
```

- Types: `NetInt`, `NetFloat`, `NetBool`, `NetVector3` (3 slots), `NetEntityRef`, `NetTimer`.
- The Host sets `initial` values when the object starts. `Changed(old, new)` fires on every
  machine. `NetTimer` uses the shared clock: `Start(seconds)`, `Remaining`, `IsRunning`,
  `JustFinished()` (true once per finish on each machine).
- Each field needs its own slot (clashes are reported); scripts on one entity share its 64.
  A `NetScript` that overrides `OnNetVarChanged` calls `base.OnNetVarChanged(slot)`. The object
  needs **NetVariables** replicated.

**Effects (`NetEffect`)** - the same line offline, on the Host and on a Client:

```csharp
static readonly NetEffect HitFlash = NetEffect.Register("fx.hitflash",
    (target, position, extra) => SpawnSparks(position));
HitFlash.Play(enemy, hitPoint);
```

- It plays here at once and on every other machine, once each. A Client's effects are passed
  on by the Host (at most 30 a second per Client). `unreliable: true` for frequent ones.
- Register it on every machine. Effects are looks and sounds only: a Client can start them.

**The game state (`NetGameState`)** - one per scene, the same shape in every game mode:

```csharp
public sealed class MatchState : NetGameState { }        // your two lines; on one scene object

NetGameState.Current.Phase = 2;                          // the Host
NetGameState.Current.MatchTimer.Start(180);
NetGameState.Current.AddScore(killer.Owner, 1);
NetGameState.RequestScore(1);                            // any machine: a Client asks the Host
```

- Phase, wave, a match timer and four scores, with `PhaseChanged`, `WaveChanged` and
  `ScoreChanged` on every machine. Slots 32 to 63 are free for your own fields.
- Override `AllowScoreRequest(peer, amount)` to decide what a Client may ask for.
- Its object needs **NetVariables** replicated.

### 8.29 Checking 8.28 with two Editors: the Kit Demo

`network_sync_test.scene` has a **Kit Demo** panel (light green text, right of the Sync Demo) and a
**Kit State** object (the game state). Start the Host and a Client as in section 8.22.

| Key | Where | What to check |
|---|---|---|
| **M** | Host | `message kitdemo.ping`: `handler` and `override` both go up by one on **every** machine |
| **M** | Client | The Host's `hello from` line counts it under that Client's number |
| **F** | Host | `health` drops by 10 on both, `changed` goes up on both; then `cooldown` counts down 3 s on both, and F does nothing until it ends (`over` goes up by one) |
| **F** | Client | Nothing changes; the Client's text says `refused here: Kit Demo damage (F)` and its Console has one warning |
| **R** | Either | The text flashes yellow on **both**, and `effects played here` goes up by exactly one on each, with `last from` the presser's number |
| **G** / **H** | Host | `phase` goes up on both / `timer` counts down from 30 together |
| **P** | Either | `scores`: +1 for whoever pressed it, on both |

Also check:
- `reserved heard by scripts` stays **0** everywhere (the helpers' own messages never reach
  scripts).
- `players`: each machine shows `mine` for its own number and `other` for the rest.
- **Joining late:** press F, G and P a few times, then start a second Client: it shows the same
  health, phase and scores at once.
- **Offline:** play the scene without a session: every key works on its own, nothing is refused.

### 8.30 Keeping objects across scene changes

A player's mech, its parts and its health can come with them to the next level:

```csharp
// Host, before Network.LoadScene:
Network.KeepPlayersAcrossScenes = true;   // every player's character
Network.KeepAcrossScenes(lootChest);      // any spawned network object (and what is attached to it)
Network.LoadScene("Scenes/level2.scene");
```

- **On every machine** the kept objects survive the load with the same NetworkId, the same
  network variables and NetData, the same owner - and their scripts keep running.
- **Kept players** aren't spawned again: each stands at the new scene's spawn point.
- **Scene objects** (placed in a scene) belong to it and can't be kept; spawn the object
  with `Network.Spawn` instead. An object attached under another is kept with that one.
- It lasts like `DontDestroyOnLoad`: to drop a kept object later, `Network.Despawn` it.
  `Network.IsKeptAcrossScenes(entity)` says whether one is kept. At most 64 per change.
- A player who joins later gets kept objects like any others.

**Check it with two Editors (the Travel Demo):** start the Host and a Client in
`network_sync_test.scene` as in section 8.22. The **Travel Demo** panel (purple text) is west of
the middle. In the **Host's** Game view:

| Key | What it does | Check on the Client |
|---|---|---|
| **Y** | Spawns the orange **Travel Case** in front of the panel | The case appears, labelled `Travel Case #<number>` |
| **I** | Adds an item to the case (its NetData) | The same items on its label |
| **U** | Travels to `network_travel_test.scene` (green floor), keeping both players and the case | Same scene, same case number and items, `scene changes: 1`; both players' numbers (`my player #`) unchanged; nothing appears twice |
| **U** again | Back to the arena, the same way | `scene changes: 2` |

The Host's text also shows how long the last travel took until every Client was ready.

### 8.31 Lag compensation: hitting what you saw

A Client draws other players about 100 ms in the past (to keep their motion smooth), plus
half its ping. Tested against where targets are *now*, its well-aimed shots miss. The Host
remembers where every network object's colliders were over the last second, and tests a
shot at the moment the shooter saw:

```csharp
static readonly NetMessage FireCmd = NetMessage.Named("weapon.fire");

// Client (and Host): fire
FireCmd.SendCommand(new NetWriter().WriteFloat((float)Network.ViewTick).WriteVector3(muzzle).WriteVector3(aim));

// Host
NetMessage.OnCommand(FireCmd, (sender, data, target) => {
    float viewTick = data.ReadFloat();
    Vector3 from = data.ReadVector3(), dir = data.ReadVector3();
    RaycastHit hit;
    if (Network.RaycastAt(viewTick, from, dir, out hit, 100f, Network.GetPlayerEntity(sender)))
        Damage(Network.ObjectOf(hit.Entity));
});
```

- **`Network.ViewTick`:** the moment this machine is showing (a Client: about 100 ms behind
  the shared clock; the Host and offline: now).
- **`Network.RaycastAt`:** network objects are tested where they were then; walls and
  anything else not networked are tested now (they don't move). On a Client and offline it
  is a plain `Physics.Raycast`.
- **`Network.MaxRewindSeconds`** (default 0.3, at most 1): a shot older than this is tested
  at the limit, so a very laggy player can't hit what was long gone.
- Boxes, spheres, capsules and CharacterController capsules are remembered; a mesh
  collider isn't (give a target a box or capsule too).

**Check it with two Editors (the Lag Demo):** in the Network window of the **Client**, set a
simulated latency of +200 ms, then start the Host and the Client. East of the middle, the
red **Lag Target** slides across the yellow **firing line**. On the **Client**, press **Z**
each time the target crosses the line *on your screen*:

- `rewound hits` should go up with nearly every good shot; `present-time hits` mostly not.
- `last shot` shows how far back the Host went (about 150 to 250 ms at +200 ms latency).
- Without simulated latency, both counts go up together. Shots fired by the Host count the
  same both ways.

### 8.32 The instruction boards in the test scenes

`network_sync_test.scene` has a dark board on top of each wall listing the keys of the
demos near it: **Sync + Input**, **Api + Kit**, **Lag + Travel**, and one with how to start.
`network_travel_test.scene` has its own board. They are ordinary scene entities (a flat
box and a `WorldText` with its billboard off): move them or edit their text freely.
