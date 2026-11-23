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
2. Open the **same scene** in both Editors (e.g. `Scenes/Network_Test.scene`).
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
| 1 | **Other Client actions must be sent as commands.** | Movement, turning and jump are sent automatically. Fire, interact, reload and the rest reach the Host only if your script sends them with `Network.SendCommand` (section 8). A Host-only C# script using `Input.IsPressed` still reads the **Host's own keyboard**, not the Client's. |
| 2 | **Script events are moments, not state.** | The Host tells every machine "this happened" with `Network.SendEvent` (section 8.4). A player who joins later never hears an old event, so lasting state must still be something that is synced. |
| 3 | **`Prefab.Instantiate` is still local.** | It creates the object only on the machine that runs the script. Use `Network.Spawn` on the Host for objects every player should see (section 8.5); the prefab must be a network prefab. |
| 4 | **Script values are synced only through network variables.** | The Transform, component on/off state and an object's 16 network variables are sent (section 8.6). Ordinary script fields and other component values are **not**: put health, ammo or score in a network variable. |
| 5 | **Animation isn't synced.** | If a Host-only script sets Animator parameters, remote characters slide around without animating on Clients. |
| 6 | **Gameplay audio, particles and UI need a script event.** | Effects triggered by Host-only gameplay happen only on the Host unless the Host sends an event and a script that runs **Everywhere** plays them (section 8.4). |
| 7 | **Clients can't use physics.** | Colliders are Host only, so a Client-side raycast or trigger doesn't hit anything networked. |
| 8 | **Only the Host's `Network.LoadScene` keeps the session across scenes.** | It moves everyone to the new scene (section 8.8). `Scene.Load`, or opening a scene in the Editor, during a session still ends it. Network objects marked DontDestroyOnLoad do not stay networked in the next scene. |
| 9 | **A Client's own player responds with a small delay.** | About 110–150 ms on a LAN, because the Client waits for the Host instead of predicting its own movement. The Host's player has no delay. |
| 10 | **LAN only, at most 4 players.** | The Host plus 3 Clients, connecting by IP address. A lobby can be built in C# (section 8.9 has one), but there is no internet play or server list: players type the Host's IP address. |
| 11 | **The Host leaving ends the game for everyone.** | There's no handing over to another player. A Client that leaves has its player removed. |
| 12 | **Both machines need exactly the same content.** | Any difference in a network prefab or scene object (or in the scene) means the Client is refused when joining. |

### In practice

- **Works today:** gameplay only the Host controls: AI chasing players, physics props,
  moving platforms, doors switched on and off with **Sync on/off**.
- **Works with scripts (section 8):** Host/Client checks, the player list, object
  ownership, Client actions sent to the Host as commands, Host events to every machine,
  and spawning network objects from the Host.
- **Needs more networking work first:** synced animation.

## 8. Networking from C# scripts

Everything is in the `Network` class and a few `EntityScript` callbacks. The full
comments are in `ManagedScripts/Runtime/Network.cs`, `NetWriter.cs`, `NetReader.cs` and
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

Every entity has 16 numbered slots (`Entity.NetVars`). The Host writes them; every
machine reads them and is told when one changes.

```csharp
const int Health = 0;                      // your own slot numbers, 0 to 15

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
| `Assets/Scripts/network_tests/NetBolt.cs` | The projectile. Runs on the Host only: flies, raycasts against the players' capsules, reports hits to `ArenaRules`, despawns itself. |
| `Assets/Prefabs/network_tests/NetBolt.cprefab` | Network prefab: Transform replicated, script **Host only**, **critical** (fast, so in every snapshot). |
| `Assets/Prefabs/network_tests/NetNameplate.cprefab`, `NetHud.cprefab` | Local prefabs (not networked): each machine makes its own. |

**Settings** are on the **Arena Rules** entity's scripts in the Inspector: max health (100),
fire cooldown, respawn delay (3 s), self damage, and the bolt's speed, damage (25) and
lifetime on the NetBolt prefab. Respawn points are the entities named **Arena Respawn 1**
to **4**: move them, or add **Arena Respawn 5** and so on.

**Death:** the Host switches off the dead player's CharacterController and PlayerController
(no movement, no capsule to hit, fire refused), and every machine hides the body. After the
delay the Host moves it to the respawn point farthest from everyone, switches them back on
and restores full health.

**Controls:** move as usual; **left mouse** or **F** fires where your camera looks.

**Play it:** open `network_gamplay.scene` in 2 to 4 Editors; one **Host**, the others
**Client** (Network window > Session > Quick test), then Play in each. Offline nothing happens: there is no spawned
player.

### 8.8 Changing scenes without ending the session

```csharp
if (Network.IsHost) Network.LoadScene("Scenes/network_gamplay.scene");
```

- **Only the Host** can do this. Every machine loads the same scene from its own project,
  so every machine must have it.
- **What happens:**
  1. The old scene, with every network object in it, goes on every machine.
  2. The new scene's network objects are set up as when a session starts.
  3. Every player's character is spawned again, at the new scene's spawn points.
- **Clients** get the new scene's world as if they had just joined, so `OnNetworkStarted`
  runs again on them.
- **The Host** gets `OnAllPeersReady()` once every client has loaded the scene and has its
  world: the moment to start a round.
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
| **Auto** (default) | Lobby lists come from the connection server. A browser sends it up to 5 list requests (about 5 s); if none is answered, the server is **not accessible**, the list stays empty, and the game can offer **LAN discovery** instead (the lobby example shows a **Search LAN instead** button). LAN is used only once the player chooses it, until the browser is opened again. With no address set, Auto uses LAN broadcast. A host registers with the server **and** answers LAN broadcasts, so it is found either way. |
| **Server** | The connection server only (testing). |
| **LAN** | LAN broadcast only; the server is not used (testing). |
| **Server address** | The IPv4 address the server printed, e.g. `192.168.0.7`. Empty means no server. There is no built-in address. |
| **Server port** | Default `27018`. |

The same values can be set for everyone in `Assets/Config/EngineConfig.json`:

```json
"network": {
    "lobbyDiscovery": "auto",
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
   on **Offline**, then **Lobby discovery > Auto**, **Server address** `192.168.0.7`,
   **Server port** `27018`.
3. Open `network_lobby.scene` and press **Play** in every Editor. One player chooses
   **Create Lobby**, and the others choose **Find Lobbies**. The header should say
   **Lobbies - connection server**, and the lobby should be listed. Join it.
4. Start the match, then on PC A choose **Network > Stop Connection Server**. The match should
   keep going. In a browser (**Find Lobbies**), the header changes to **Connection server not
   accessible** after about 5 s, with **Search LAN instead**. Press it: the LAN lobbies appear.
5. Start the server again (**Network > Start Connection Server**). Within a couple of seconds
   the host is registered again, without doing anything. A browser that chose LAN keeps the LAN
   list; press **Back** and **Find Lobbies** to use the server again.
6. Optional: set **Lobby discovery > Server** on a browser PC to check that the list really comes
   from the server, or **LAN** to check the broadcast path.

**Not covered:** the internet. The server only lists lobbies; it does not relay or punch
through routers (no NAT traversal, STUN, ICE or TURN). Players must be able to reach the
host's IP directly, as on a LAN.
