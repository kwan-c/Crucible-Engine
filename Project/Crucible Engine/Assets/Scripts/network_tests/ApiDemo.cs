/*************************************************************************
* \file:         ApiDemo.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Networking sample: the shared clock, pings, kicking, the
*                high network variable slots and NetData, checked in two
*                Editors.
*
* \details
*    On "Api Demo" in network_sync_test.scene, a scene network object that
*    replicates NetVariables and NetData. Every machine prints what it sees
*    on the floating text; the HOST changes things with these keys
*    (README section 8.27 has the checklist):
*
*        T  a 10 second countdown     the end tick in variable slot 0
*        V  a counter                 variable slot 63 (the highest)
*        B  add an item to a list     NetData (up to 8 items, oldest dropped)
*        N  empty the list            NetData.Clear
*        X  kick the newest client    Network.Kick
*
*    The countdown is stored once, as a tick of the shared clock; every
*    machine works out what is left from Network.Time, so all screens count
*    down together without any further message.
*
* \history
*    -v1.0       sample for the shared clock, pings, Kick, slot 63 and NetData
*
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class ApiDemo : EntityScript
    {
        private const int EndTickSlot = 0;    // the countdown's end, in shared clock ticks (0 = none)
        private const int CounterSlot = 63;   // the highest slot there is
        private const int MaxItems = 8;

        private static readonly string[] Names = { "Rifle", "Rocket", "Shield", "Medkit", "Ammo", "Grenade", "Boots", "Visor" };

        private WorldText text;
        private readonly List<string> items = new List<string>();   // the host's list, sent as NetData
        private int nextName;
        private int dataHeard;                                      // OnNetDataChanged calls on this machine
        private string lastStop = string.Empty;

        public override void OnCreate()
        {
            text = Entity.GetComponent<WorldText>();
        }

        public override void OnUpdate(float deltaTime)
        {
            if (Network.IsAuthority)
                HandleKeys();
            if (text != null)
                text.Text = Describe();
        }

        public override void OnNetDataChanged()
        {
            ++dataHeard;
        }

        public override void OnNetworkStopped(string reason)
        {
            lastStop = reason + " (code " + Network.LastEndCode + ")";
        }

        private void HandleKeys()
        {
            if (Input.IsPressed(KeyCode.T))
                Entity.NetVars.SetInt(EndTickSlot, Network.Tick + 10 * Network.TickRate);
            if (Input.IsPressed(KeyCode.V))
                Entity.NetVars.SetInt(CounterSlot, Entity.NetVars.GetInt(CounterSlot) + 1);
            if (Input.IsPressed(KeyCode.B))
            {
                if (items.Count == MaxItems)
                    items.RemoveAt(0);
                items.Add(Names[nextName++ % Names.Length]);
                SendItems();
            }
            if (Input.IsPressed(KeyCode.N))
            {
                items.Clear();
                Entity.NetData.Clear();
            }
            if (Input.IsPressed(KeyCode.X) && Network.IsHost)
            {
                int newest = 0;
                foreach (int peer in Network.Peers)
                    if (peer > newest)
                        newest = peer;
                if (newest > 0)
                    Network.Kick(newest, "kicked from the Api Demo (key X)");
            }
        }

        private void SendItems()
        {
            NetWriter writer = new NetWriter();
            writer.WriteByte((byte)items.Count);
            foreach (string item in items)
                writer.WriteString(item);
            Entity.NetData.Set(writer);
        }

        // Every machine reads the list back from NetData, so the host shows exactly what it sent.
        private string ReadItems()
        {
            NetData data = Entity.NetData;
            if (data.Length == 0)
                return "(empty)";
            NetReader reader = data.Read();
            int count = reader.ReadByte();
            string list = string.Empty;
            for (int i = 0; i < count; ++i)
                list += (i > 0 ? ", " : "") + reader.ReadString();
            return list;
        }

        private string Describe()
        {
            string role = !Network.IsOnline ? "offline" : Network.IsHost ? "HOST (keys T V B N X)" : "CLIENT " + Network.LocalPeerId;

            int endTick = Entity.NetVars.GetInt(EndTickSlot);
            string countdown = "-";
            if (endTick != 0)
            {
                double left = endTick / (double)Network.TickRate - Network.Time;
                countdown = left > 0.0 ? left.ToString("0.0") + " s" : "done";
            }

            string pings = string.Empty;
            foreach (int peer in Network.IsOnline ? Network.Peers : new int[] { 0 })
                pings += "  " + peer + ": " + Network.GetPing(peer);

            NetData data = Entity.NetData;
            return role
                + "\ntime " + Network.Time.ToString("0.00") + " s   tick " + Network.Tick + (Network.HasSharedTime ? "" : " (own clock)")
                + "\ncountdown " + countdown
                + "\nping (ms) you " + Network.Ping + "  |" + pings
                + "\nslot 63 = " + Entity.NetVars.GetInt(CounterSlot)
                + "\ndata " + data.Length + " bytes, heard " + dataHeard + ": " + ReadItems()
                + (lastStop.Length > 0 ? "\nlast stop: " + lastStop : "");
        }
    }
}
