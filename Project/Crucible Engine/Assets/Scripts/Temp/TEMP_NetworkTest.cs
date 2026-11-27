// ============================================================================
// TEMPORARY - DELETE AFTER THE TWO-EDITOR NETWORKING CHECK. NOT GAMEPLAY.
// Do not commit. (Delete this file, its .meta if the Editor made one, and the
// Temp folder; then rebuild the script DLL.)
// ============================================================================
// Verifies Networking C# Phases 1 and 2 by hand, in two Editors:
//   - logs every network callback with the role and peer id of the machine,
//   - T sends command kind 1 with the integer 5, aimed at this script's entity,
//   - the authority (the Host) logs who sent it, its kind and the integer.
// Phases 5A and 3 (press these on the HOST; on a Client they log the refusal):
//   - Y sends event kind 2 with the integer 7, aimed at this entity: EVERY machine logs it,
//   - U spawns Spawn Prefab (assign a NETWORK prefab in the Inspector) 2 m above this entity,
//   - I despawns the newest one spawned with U.
// Phases 4 and 5B (press on the HOST; this entity's Network Object table needs NetVariables
// set to Replicated, or H logs why not):
//   - H adds 10 to network variable 0 (int) and flips variable 1 (bool): EVERY machine logs
//     OnNetVarChanged with the new values,
//   - J sends unreliable event kind 3 (value 9) to every machine,
//   - K sends event kind 4 (value 4) to everyone EXCEPT peer 1.
//
// Attach it to a NETWORK OBJECT and leave its Script component "Local" in the
// Network Object's table, so it runs on the Host AND on every Client.
using Crucible;

namespace Crucible.Scripts
{
    public sealed class TEMP_NetworkTest : EntityScript
    {
        private const ushort TestCommand = 1;
        private const int TestValue = 5;
        private const KeyCode SendKey = KeyCode.T;
        private const ushort TestEvent = 2;
        private const int TestEventValue = 7;
        private const KeyCode EventKey = KeyCode.Y;
        private const KeyCode SpawnKey = KeyCode.U;
        private const KeyCode DespawnKey = KeyCode.I;
        private const KeyCode VariableKey = KeyCode.H;
        private const KeyCode UnreliableKey = KeyCode.J;
        private const KeyCode ExceptKey = KeyCode.K;
        private const int CounterSlot = 0, FlagSlot = 1;

        /// <summary>A network prefab (Network Object on its root) for the U key. Optional.</summary>
        public Prefab spawnPrefab;

        private readonly NetWriter writer = new NetWriter();
        private readonly System.Collections.Generic.List<Entity> spawned = new System.Collections.Generic.List<Entity>();

        // "[NetTest Host peer 0]" in front of every line, so two consoles are easy to compare.
        private static string Tag()
        {
            string role = Network.IsHost ? "Host" : Network.IsClient ? "Client" : "Offline";
            return "[NetTest " + role + " peer " + Network.LocalPeerId + "] ";
        }

        private static string Players()
        {
            int[] peers = Network.Peers;
            string list = "";
            for (int i = 0; i < peers.Length; ++i)
                list += (i > 0 ? ", " : "") + peers[i] + "='" + Network.GetPeerName(peers[i]) + "'" +
                        (Network.IsPeerReady(peers[i]) ? "" : " (loading)");
            return Network.PlayerCount + " player(s): " + list;
        }

        public override void OnCreate()
        {
            Log.Info(Tag() + "OnCreate on " + Entity + ": IsNetworked=" + Entity.IsNetworked + ", NetworkId=" + Entity.NetworkId +
                     ", Owner=" + Entity.Owner + ", IsMine=" + Entity.IsMine + ", IsReady=" + Network.IsReady +
                     ". Press " + SendKey + " to send the test command.");
        }

        public override void OnUpdate(float deltaTime)
        {
            if (Input.IsPressed(SendKey))
            {
                writer.Clear().WriteInt(TestValue);
                bool sent = Network.SendCommand(TestCommand, writer, Entity);
                Log.Info(Tag() + "SendCommand(kind " + TestCommand + ", value " + TestValue + ", target NetworkId " +
                         Entity.NetworkId + ") returned " + sent);
            }
            if (Input.IsPressed(EventKey))
            {
                writer.Clear().WriteInt(TestEventValue);
                bool sent = Network.SendEvent(TestEvent, writer, Entity);
                Log.Info(Tag() + "SendEvent(kind " + TestEvent + ", value " + TestEventValue + ") returned " + sent);
            }
            if (Input.IsPressed(SpawnKey))
            {
                Vector3 where = Transform.Position + new Vector3(0f, 2f + spawned.Count, 0f);
                Entity made = Network.Spawn(spawnPrefab, where, Quaternion.Identity);
                if (made != null)
                    spawned.Add(made);
                Log.Info(Tag() + "Spawn returned " + (made == null ? "null" : made + ", NetworkId " + made.NetworkId +
                         ", Owner " + made.Owner + ", IsNetworked " + made.IsNetworked));
            }
            TestVariablesAndEventOptions();
            if (Input.IsPressed(DespawnKey) && spawned.Count > 0)
            {
                Entity last = spawned[spawned.Count - 1];
                spawned.RemoveAt(spawned.Count - 1);
                Log.Info(Tag() + "Despawn(" + last + ") returned " + Network.Despawn(last));
            }
        }

        private void TestVariablesAndEventOptions()
        {
            if (Input.IsPressed(VariableKey))
            {
                bool a = Entity.NetVars.SetInt(CounterSlot, Entity.NetVars.GetInt(CounterSlot) + 10);
                bool b = Entity.NetVars.SetBool(FlagSlot, !Entity.NetVars.GetBool(FlagSlot));
                Log.Info(Tag() + "NetVars set returned " + a + ", " + b + ": counter " + Entity.NetVars.GetInt(CounterSlot) +
                         ", flag " + Entity.NetVars.GetBool(FlagSlot));
            }
            if (Input.IsPressed(UnreliableKey))
                Log.Info(Tag() + "SendEventUnreliable(kind 3) returned " +
                         Network.SendEventUnreliable(3, writer.Clear().WriteInt(9), Entity));
            if (Input.IsPressed(ExceptKey))
                Log.Info(Tag() + "SendEventExcept(peer 1, kind 4) returned " +
                         Network.SendEventExcept(1, 4, writer.Clear().WriteInt(4), Entity));
        }

        public override void OnNetVarChanged(int slot)
        {
            Log.Info(Tag() + "OnNetVarChanged(" + slot + "): counter " + Entity.NetVars.GetInt(CounterSlot) + ", flag " +
                     Entity.NetVars.GetBool(FlagSlot));
        }

        public override void OnNetworkEvent(ushort kind, NetReader data)
        {
            int value = data.ReadInt();
            Log.Info(Tag() + "OnNetworkEvent: kind " + kind + ", value " + value + ", data ok " + data.Ok);
        }

        public override void OnNetworkStarted()
        {
            Log.Info(Tag() + "OnNetworkStarted: IsAuthority=" + Network.IsAuthority + ", IsReady=" + Network.IsReady + ", " + Players());
        }

        public override void OnNetworkStopped(string reason)
        {
            Log.Info(Tag() + "OnNetworkStopped: " + reason);
        }

        public override void OnPeerJoined(int peer)
        {
            Log.Info(Tag() + "OnPeerJoined(" + peer + ") '" + Network.GetPeerName(peer) + "'. Now " + Players());
        }

        public override void OnPeerReady(int peer)
        {
            Log.Info(Tag() + "OnPeerReady(" + peer + ") '" + Network.GetPeerName(peer) + "'. Now " + Players());
        }

        public override void OnPeerLeft(int peer)
        {
            Log.Info(Tag() + "OnPeerLeft(" + peer + "). Now " + Players());
        }

        public override void OnNetworkCommand(int sender, ushort kind, NetReader data)
        {
            int value = data.ReadInt();
            if (!data.Ok)
            {
                Log.Warning(Tag() + "OnNetworkCommand: sender " + sender + ", kind " + kind + ", but the data could not be read");
                return;
            }
            Log.Info(Tag() + "OnNetworkCommand: sender " + sender + ", kind " + kind + ", value " + value +
                     (data.AtEnd ? "" : " (" + data.Remaining + " unread bytes)"));
        }
    }
}
