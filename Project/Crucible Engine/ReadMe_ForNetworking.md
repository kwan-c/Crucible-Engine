# Crucible Networking: How to Use It

This guide explains how to make objects networked, how to start a Host and a Client
from the Editor, and how to build a scene like `Assets/Scenes/Network_Test.scene`.

> **LAN only.** Networking currently works only on a local network: two Editors on the
> same PC, or PCs on the same LAN or Wi-Fi. Playing over the internet (NAT traversal,
> relay servers, matchmaking) is not supported yet.

At most 4 players can play together: the Host plus up to 3 Clients.

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
| **Network state** | `Local` / `Replicated: <serializer>` | **Local** (the default): each machine keeps the value it loaded from the scene or prefab, and the Host never sends it. **Replicated**: the Host sends this component's value to every Client. For example, `Transform` offers `Replicated: Transform/Full` (position, rotation and scale) and `Replicated: Transform/YawOnly` (position and yaw only, which is cheaper). |
| **Runs on** | `Everywhere` / `Host only` / `Owner only` | **Everywhere**: the component runs on every machine. **Host only**: switched off on Clients, used for physics, AI and gameplay logic, since the Host has authority. **Owner only**: switched off everywhere except on the machine that owns the object, used for the player's camera or HUD. |
| **Sync on/off** | checkbox | Sends whether this root component is enabled or disabled to the Clients. |

Rule of thumb:
- Things that **move** → `Transform` set to **Replicated**.
- Things that **simulate** (RigidBody, Collider, CharacterController, PlayerController, AI) → **Host only**.
- Things only **the player who owns the object** should have (Camera, CameraOrbit) → **Owner only**.
- Everything else (MeshRenderer, lights, names) → leave it **Local**.

### 1.2 The other settings

| Setting | Default | What it does |
|---|---|---|
| **Send priority** | 1.0 | How strongly this object competes for each Client's bandwidth. A higher value means it's updated more often. Only the Host uses it. |
| **Critical** | off | Sent in **every** snapshot, ahead of the bandwidth budget. Use it for players or a boss. Use it sparingly. |
| **Allow scene placement** | on | When this object is saved in a scene, it becomes a **networked scene object**: the Host and every Client load their own copy and link them up instead of spawning one. |
| **Spawn per player** | off | Marks this prefab as **the player**. The Host spawns one for itself and one for each Client that joins, and that player owns it. **Only one prefab in the project** may have this turned on, and only a prefab can be the player. |

At the bottom of the component, **"Network settings are valid"** (green) means the object
is ready. If something is wrong, a red message says what. Leftover settings that refer to a
component the object no longer has show a **Remove** button.

---

## 2. The Network tab

Open the **Network** panel. It's a tab next to the Inspector. You can only change it in
**Edit mode**: it locks while playing, so press **Stop** to change it.

| Mode | Fields | Use |
|---|---|---|
| **Offline** | — | Plain single-player. Networking is off. |
| **Host** | **Port** (default `27020`); *Advanced > Listen address* (leave empty = all network cards) | This Editor runs the game and others join it. |
| **Client** | **Host IP** (default `127.0.0.1`), **Host port** (default `27020`) | Joins a Host. |

- The defaults come from `Assets/Config/EngineConfig.json` (the `"network"` section).
  Changes made in the panel apply only to **that Editor window**, are **not saved**, and are
  lost when you close it. Two Editors on the same PC can each use different settings.
- Under the settings, the panel shows the status:
  - Host: `Hosting on port 27020`, plus the players who joined.
  - Client: `Connecting...`, `Joining...`, then `Joined <ip>:<port> as player N`.
  - Errors: `Could not start: ...` or `Last session ended: ...` (for example, the Host
    stopped or the content didn't match).

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
2. Open the **same scene** in both Editors (e.g. `Scenes/Network_Test.scene`).
3. **Editor 1 (Host):** in the Network tab, select **Host**, keep port `27020`, and press **Play**.
   The panel should show `Hosting on port 27020`.
4. **Editor 2 (Client):** in the Network tab, select **Client**, enter the Host's IP
   (`127.0.0.1` on the same PC, the Host's LAN IP otherwise) and port `27020`, then press **Play**.
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
Player (root)                Transform, MeshRenderer, CharacterController,
                             PlayerController, NetworkObjectAuthoring
└── Camera Pivot             Transform, CameraOrbit
    └── Player Camera        Transform, Camera
```

Its `NetworkObjectAuthoring`:

| Component | Network state | Runs on |
|---|---|---|
| Transform | Replicated: Transform/Full | Everywhere |
| CharacterController | Local | **Host only** |
| PlayerController | Local | **Host only** |
| CameraOrbit | Local | **Owner only** |
| Camera | Local | **Owner only** |
| MeshRenderer | Local | Everywhere |

- **Critical** = on, so players are updated in every snapshot.
- **Spawn per player** = on, so this prefab is the player.
- The Host moves every player from the input their owner sends. Clients only show the
  result, which is why the movement components are **Host only**.
- Each machine keeps only **its own** player's camera, which is why the camera is **Owner only**.

### 5.2 Player spawn points

There's no separate spawn-point component: **an instance of the player prefab placed in the
scene is a spawn point.**

- When a session starts, every machine removes these placed players. The Host then spawns
  each real player at one of their positions and rotations.
- Player 0 (the Host) uses the first spawn point, player 1 the second, and so on. If there are
  more players than spawn points, they wrap around and are shifted 3 m along X so they
  don't overlap.
- With **no** spawn points in the scene, players spawn at a default position.
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
> over the network. Also, **only one prefab in the whole project** may have
> **Spawn per player** turned on. `Assets/Prefabs/NetworkedPlayer.cprefab` already does, so
> either **reuse it** (recommended) or turn it off there before making your own. Otherwise
> the session refuses to start with *"... both have spawnPerPlayer: only one prefab can be
> the player"*.

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

**Option B: make your own** (first turn off **Spawn per player** on `NetworkedPlayer.cprefab`):

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
| "only one prefab can be the player" | Two prefabs have **Spawn per player** on. Turn it off on one. |
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
- **Joining mid-game:** a Client can join while the game is running and receives the
  current world.

### What doesn't work yet

| # | Limitation | What it means for you |
|---|---|---|
| 1 | **Only movement, turning and jump are sent from Clients.** | Fire, interact, reload and other actions from a Client never reach the Host. A Host-only C# script using `Input.IsPressed` reads the **Host's own keyboard**, not the Client's. |
| 2 | **C# has no networking API.** | Scripts can't check whether they run on the Host or a Client, can't send network events, and can't tell which player owns an object. |
| 3 | **Objects spawned by scripts aren't networked.** | `Prefab.Instantiate` in C# creates the object only on the machine that runs the script. Only players are spawned by the network. Enemy waves, projectiles and pickups spawned at runtime won't appear on Clients. |
| 4 | **Only the Transform and component on/off state are synced.** | Health, ammo, score, script variables and other component values are **not** sent. A Client's HUD can't show the real health. |
| 5 | **Animation isn't synced.** | If a Host-only script sets Animator parameters, remote characters slide around without animating on Clients. |
| 6 | **Gameplay audio, particles and UI only happen on the Host.** | Hit effects, death sounds and a "Game Over" screen triggered by Host-only gameplay aren't seen on Clients. |
| 7 | **Clients can't use physics.** | Colliders are Host only, so a Client-side raycast or trigger doesn't hit anything networked. |
| 8 | **Changing scenes ends the session.** | Loading another scene (e.g. main menu → game) disconnects everyone. Start the session from the scene you'll actually play in. |
| 9 | **A Client's own player responds with a small delay.** | About 110–150 ms on a LAN, because the Client waits for the Host instead of predicting its own movement. The Host's player has no delay. |
| 10 | **LAN only, at most 4 players.** | The Host plus 3 Clients, connecting by IP address. There's no internet play, lobby or server list. |
| 11 | **The Host leaving ends the game for everyone.** | There's no handing over to another player. A Client that leaves has its player removed. |
| 12 | **Both machines need exactly the same content.** | Any difference in a network prefab or scene object (or in the scene) means the Client is refused when joining. |

### In practice

- **Works today:** gameplay only the Host controls: AI chasing players, physics props,
  moving platforms, doors switched on and off with **Sync on/off**.
- **Needs more networking work first:** Client actions beyond moving, runtime spawns from
  scripts, synced values (health, score), synced animation, and effects triggered by
  gameplay. The planned fix is C# networking bindings (events, spawning, ownership,
  Host/Client checks) and more replicated component types.
