/*************************************************************************
* \file:         ArenaRules.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          20 November 2026
* \version       1.1
* \brief         Networking sample: the rules of a small multiplayer arena
*                shooter - health, damage, death, respawn and firing -
*                decided by the host alone. One per scene, on a network
*                scene object, next to ArenaView (which only shows them).
*
* \details
*    Who decides what (the host is the authority, Network.IsAuthority):
*
*      a player presses Fire   any machine   -> COMMAND FireCommand (aim)
*      may it fire? from where? host          checks alive, cooldown, muzzle
*      the bolt                 host          Network.Spawn, owned by the shooter
*      did it hit?              host          NetBolt raycasts on the host
*      damage, death, respawn   host          ApplyHit / Kill / Respawn
*      health, kills, deaths    host writes   NETWORK VARIABLES on this entity;
*                                             every machine reads them
*      hit / death / respawn    host          reliable EVENTS (feedback)
*      "somebody fired"         host          unreliable EVENT (cosmetic)
*
*    A client never changes the game: it asks with a command, and shows
*    what the variables and events say. The engine refuses a client's
*    NetVars write to a network object, so it could not change health even
*    if a script tried.
*
*    State per player, in this entity's 16 network variables (PeerId p):
*      HealthSlot + p   current health (0..maxHealth)
*      KillsSlot + p    kills
*      DeathsSlot + p   deaths
*      StateSlot + p    StateNone (no player), StateAlive, StateDead
*    A player who joins late gets all of it with this object.
*
*    Death, on the host: the character's CharacterController and
*    PlayerController are switched off - it cannot move, its capsule is
*    gone so bolts fly through, and FireCommand is refused. Every machine
*    hides the body (ArenaView, from StateSlot). Respawn: after
*    respawnDelay the host moves the character to a respawn point (an
*    entity named "Arena Respawn 1", "Arena Respawn 2", ...), switches the
*    two components back on and restores full health.
*
*    Setup (network_gamplay.scene): this entity has a Network Object
*    component with NetVariables Replicated; ArenaRules and ArenaView run
*    Everywhere; Bolt Prefab = Prefabs/network_tests/NetBolt.
*    Controls: left mouse button or F = fire where your camera looks;
*    Backspace (host) = everyone back to the lobby; L = leave the session.
*    Reached from network_lobby.scene (NetLobby), the session carries on
*    into this scene and back. When it ends here (left, or the host is
*    gone), this machine goes back to lobbyScene on its own.
*
*    Needs a session: offline there is no spawned player.
*
* \history
*    -v1.0       the arena shooter (Networking C# Phase 6)
*    -v1.1       the network scenes moved to Scenes/network_tests (lobbyScene's default)
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class ArenaRules : EntityScript
    {
        /// <summary>The name NetBolt and ArenaView find this entity by.</summary>
        public const string EntityName = "Arena Rules";
        /// <summary>Respawn points are entities named this plus 1, 2, 3...</summary>
        public const string RespawnPointName = "Arena Respawn ";

        // Commands (a player asks the host).
        public const ushort FireCommand = 1;

        // Events (the host tells everyone), all about this entity.
        public const ushort HitEvent = 1;       // byte victim, byte shooter, int damage, int health left, Vector3 point
        public const ushort DeathEvent = 2;     // byte victim, byte killer
        public const ushort RespawnEvent = 3;   // byte peer
        public const ushort ShotEvent = 4;      // byte shooter (unreliable)

        // This entity's network variables.
        public const int HealthSlot = 0, KillsSlot = 4, DeathsSlot = 8, StateSlot = 12;
        public const int StateNone = 0, StateAlive = 1, StateDead = 2;

        /// <summary>The projectile (a network prefab with the NetBolt script).</summary>
        public Prefab boltPrefab;
        /// <summary>Health a player starts and respawns with.</summary>
        public int maxHealth = 100;
        /// <summary>Seconds between two shots of one player.</summary>
        public float fireCooldown = 0.3f;
        /// <summary>Seconds a dead player waits before respawning.</summary>
        public float respawnDelay = 3f;
        /// <summary>Can a player's own bolts hurt it?</summary>
        public bool allowSelfDamage = false;
        /// <summary>How far from the shooter a shot may start (metres): the host's check on a command.</summary>
        public float maxMuzzleOffset = 4f;
        /// <summary>Used when the scene has no respawn points: a random spot within this distance of the centre.</summary>
        public float arenaHalfSize = 10f;
        /// <summary>The lobby (a path inside Assets): Backspace on the host takes everyone there; empty = none.</summary>
        public string lobbyScene = "Scenes/network_tests/network_lobby.scene";

        private readonly NetWriter writer = new NetWriter();
        private readonly double[] lastShot = new double[Network.MaxPlayers];   // host
        private readonly float[] respawnIn = new float[Network.MaxPlayers];    // host: seconds left, 0 = none
        private readonly List<Entity> respawnPoints = new List<Entity>();
        private readonly Random random = new Random();
        private double localLastShot = -1.0;
        private bool hostStarted;

        // --- shared helpers (every machine) ---------------------------------------------- //

        /// <summary>The scene's ArenaRules, or null.</summary>
        public static ArenaRules Find()
        {
            Entity holder = Entity.Find(EntityName);
            return holder != null ? holder.GetComponent<ArenaRules>() : null;
        }

        /// <summary>The PeerId whose character <paramref name="entity"/> is, or -1.</summary>
        public static int PeerOfPlayer(Entity entity)
        {
            if (entity == null || !entity.IsNetworked)
                return -1;
            int owner = entity.Owner;
            return Network.GetPlayerEntity(owner) == entity ? owner : -1;
        }

        public int StateOf(int peer) { return Valid(peer) ? Entity.NetVars.GetInt(StateSlot + peer) : StateNone; }
        public int HealthOf(int peer) { return Valid(peer) ? Entity.NetVars.GetInt(HealthSlot + peer) : 0; }
        public int KillsOf(int peer) { return Valid(peer) ? Entity.NetVars.GetInt(KillsSlot + peer) : 0; }
        public int DeathsOf(int peer) { return Valid(peer) ? Entity.NetVars.GetInt(DeathsSlot + peer) : 0; }
        private static bool Valid(int peer) { return peer >= 0 && peer < Network.MaxPlayers; }

        public override void OnCreate()
        {
            for (int i = 1; i <= 16; ++i)
            {
                Entity point = Entity.Find(RespawnPointName + i);
                if (point == null)
                    break;
                respawnPoints.Add(point);
            }
        }

        public override void OnUpdate(float deltaTime)
        {
            if (Network.IsHost)
                RunHost(deltaTime);

            // The local player's input. It never changes the game: it asks.
            if (Network.IsReady && (Input.IsPressed(KeyCode.MouseLeft) || Input.IsPressed(KeyCode.F)))
                AskToFire();
            if (Network.IsHost && Input.IsPressed(KeyCode.Backspace) && !string.IsNullOrEmpty(lobbyScene))
                Network.LoadScene(lobbyScene);   // the whole session goes back to the lobby
            if (Network.IsOnline && Input.IsPressed(KeyCode.L))
                Network.Leave();
        }

        /// <summary>
        /// The session ended here - this player left, or the host left, closed the game or
        /// stopped answering: back to the lobby's menu, offline, with a sentence saying why.
        /// </summary>
        public override void OnNetworkStopped(string reason)
        {
            if (string.IsNullOrEmpty(lobbyScene))
                return;
            NetLobby.ArrivalMessage = Network.DescribeEnd(true);
            Scene.Load(lobbyScene);
        }

        private void AskToFire()
        {
            int self = Network.LocalPeerId;
            Entity player = Network.LocalPlayerEntity;
            // Checked here only to save pointless commands: the host checks again.
            if (player == null || StateOf(self) != StateAlive || Time.ElapsedTimeAsDouble - localLastShot < fireCooldown)
                return;
            localLastShot = Time.ElapsedTimeAsDouble;
            // Aim where this player's own camera looks; start a little in front of the chest.
            Entity pivot = player.FindChild("Camera Pivot");
            Vector3 direction = (pivot != null ? pivot.Transform.Forward : player.Transform.Forward).Normalized;
            Vector3 origin = player.Transform.WorldPosition + new Vector3(0f, 0.6f, 0f) + direction * 0.8f;
            writer.Clear().WriteVector3(origin).WriteVector3(direction);
            Network.SendCommand(FireCommand, writer, Entity);
        }

        // --- the host only ------------------------------------------------------------------ //

        private void RunHost(float deltaTime)
        {
            if (!hostStarted)
            {
                hostStarted = true;
                ResetPlayer(Network.HostPeerId);   // the host plays too; clients start in OnPeerReady
            }
            for (int peer = 0; peer < Network.MaxPlayers; ++peer)
            {
                if (respawnIn[peer] <= 0f)
                    continue;
                respawnIn[peer] -= deltaTime;
                if (respawnIn[peer] <= 0f)
                    Respawn(peer);
            }
        }

        public override void OnPeerReady(int peer)
        {
            if (Network.IsAuthority)
                ResetPlayer(peer);
        }

        /// <summary>A player left: its character is removed by the engine; its row is cleared.</summary>
        public override void OnPeerLeft(int peer)
        {
            if (!Network.IsAuthority || !Valid(peer))
                return;
            respawnIn[peer] = 0f;
            Entity.NetVars.SetInt(StateSlot + peer, StateNone);
            Entity.NetVars.SetInt(HealthSlot + peer, 0);
            Entity.NetVars.SetInt(KillsSlot + peer, 0);
            Entity.NetVars.SetInt(DeathsSlot + peer, 0);
        }

        private void ResetPlayer(int peer)
        {
            if (!Valid(peer))
                return;
            respawnIn[peer] = 0f;
            lastShot[peer] = -1.0;
            Entity.NetVars.SetInt(HealthSlot + peer, maxHealth);
            Entity.NetVars.SetInt(KillsSlot + peer, 0);
            Entity.NetVars.SetInt(DeathsSlot + peer, 0);
            Entity.NetVars.SetInt(StateSlot + peer, StateAlive);
        }

        /// <summary>The host answers what players ask. Nothing in a command is trusted.</summary>
        public override void OnNetworkCommand(int sender, ushort kind, NetReader data)
        {
            if (kind != FireCommand || !Valid(sender))
                return;
            Vector3 origin = data.ReadVector3();
            Vector3 direction = data.ReadVector3();
            if (!data.Ok || !data.AtEnd || direction.Length < 0.5f)
                return;   // damaged or dishonest

            // May this player fire, from there, now?
            Entity player = Network.GetPlayerEntity(sender);
            if (player == null || StateOf(sender) != StateAlive)
                return;   // dead players cannot shoot
            if ((origin - player.Transform.WorldPosition).Length > maxMuzzleOffset)
                return;   // a shot must start at the shooter
            double now = Time.ElapsedTimeAsDouble;
            if (now - lastShot[sender] < fireCooldown)
                return;
            lastShot[sender] = now;

            // Owned by the shooter: the bolt knows whose shot it is (Entity.Owner).
            Network.Spawn(boltPrefab, origin, Quaternion.LookRotation(direction.Normalized), sender);
            Network.SendEventUnreliable(ShotEvent, writer.Clear().WriteByte((byte)sender), Entity);
        }

        /// <summary>
        /// Host: a bolt of <paramref name="shooter"/> hit <paramref name="victim"/>'s character.
        /// False if the hit does not count (victim not alive, or its own bolt).
        /// </summary>
        public bool ApplyHit(int victim, int shooter, int damage, Vector3 point)
        {
            if (!Network.IsAuthority || !Valid(victim) || damage <= 0 || StateOf(victim) != StateAlive)
                return false;
            if (victim == shooter && !allowSelfDamage)
                return false;
            int health = Math.Max(0, HealthOf(victim) - damage);
            Entity.NetVars.SetInt(HealthSlot + victim, health);
            writer.Clear().WriteByte((byte)victim).WriteByte((byte)(Valid(shooter) ? shooter : 255)).WriteInt(damage)
                  .WriteInt(health).WriteVector3(point);
            Network.SendEvent(HitEvent, writer, Entity);
            if (health == 0)
                Kill(victim, shooter);
            return true;
        }

        private void Kill(int victim, int killer)
        {
            Entity.NetVars.SetInt(StateSlot + victim, StateDead);
            Entity.NetVars.SetInt(DeathsSlot + victim, DeathsOf(victim) + 1);
            if (killer != victim && StateOf(killer) != StateNone)
                Entity.NetVars.SetInt(KillsSlot + killer, KillsOf(killer) + 1);
            SetCharacterActive(Network.GetPlayerEntity(victim), false);
            respawnIn[victim] = Math.Max(0.01f, respawnDelay);
            Network.SendEvent(DeathEvent, writer.Clear().WriteByte((byte)victim).WriteByte((byte)(Valid(killer) ? killer : 255)), Entity);
        }

        private void Respawn(int peer)
        {
            respawnIn[peer] = 0f;
            Entity player = Network.GetPlayerEntity(peer);
            if (player == null || StateOf(peer) != StateDead)
                return;   // left meanwhile
            // Moving the Transform of a character whose controller is off, then switching it on,
            // creates its capsule at the new place (the physics system treats it as a teleport).
            player.Transform.WorldPosition = PickRespawnPoint();
            SetCharacterActive(player, true);
            Entity.NetVars.SetInt(HealthSlot + peer, maxHealth);
            Entity.NetVars.SetInt(StateSlot + peer, StateAlive);
            Network.SendEvent(RespawnEvent, writer.Clear().WriteByte((byte)peer), Entity);
        }

        private static void SetCharacterActive(Entity player, bool on)
        {
            if (player == null)
                return;
            player.SetEnabled<CharacterController>(on);
            player.SetEnabled<PlayerController>(on);
        }

        // The respawn point farthest from every living player, or a random spot.
        private Vector3 PickRespawnPoint()
        {
            Vector3 best = Vector3.Zero;
            float bestDistance = -1f;
            foreach (Entity point in respawnPoints)
            {
                if (point == null || !point.IsAlive)
                    continue;
                Vector3 position = point.Transform.WorldPosition;
                float nearest = float.MaxValue;
                for (int peer = 0; peer < Network.MaxPlayers; ++peer)
                {
                    Entity other = StateOf(peer) == StateAlive ? Network.GetPlayerEntity(peer) : null;
                    if (other != null)
                        nearest = Math.Min(nearest, (other.Transform.WorldPosition - position).Length);
                }
                if (nearest > bestDistance)
                {
                    bestDistance = nearest;
                    best = position;
                }
            }
            if (bestDistance >= 0f)
                return best;
            float x = (float)(random.NextDouble() * 2.0 - 1.0) * arenaHalfSize;
            float z = (float)(random.NextDouble() * 2.0 - 1.0) * arenaHalfSize;
            return new Vector3(x, 1.5f, z);
        }
    }
}
