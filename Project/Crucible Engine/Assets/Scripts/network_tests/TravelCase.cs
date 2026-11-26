/*************************************************************************
* \file:         TravelCase.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Networking sample: the Travel Case, a network prefab the host keeps
*                across scene changes (Network.KeepAcrossScenes).
*
* \details
*    On Prefabs/network_tests/TravelCase.cprefab. Its label shows its
*    NetworkId, the items in its NetData and how many scene changes it has
*    come through (variable slot 0, counted by the host) - the same on every
*    machine, before and after a travel.
*
* \history
*    -v1.0       the kept object of the travel demo
*
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class TravelCase : EntityScript
    {
        /// <summary>The case in the session (every machine has its copy), or null.</summary>
        public static TravelCase Current { get; private set; }

        private WorldText label;

        public override void OnCreate()
        {
            Current = this;
            Entity child = Entity.FindChild("Case Label");
            label = child != null ? child.GetComponent<WorldText>() : null;
        }

        public override void OnDestroy()
        {
            if (ReferenceEquals(Current, this)) Current = null;
        }

        public override void OnNetworkSceneLoading()
        {
            if (Network.IsAuthority)
                Entity.NetVars.SetInt(0, Entity.NetVars.GetInt(0) + 1);   // one more scene change survived
        }

        public override void OnUpdate(float deltaTime)
        {
            if (label != null)
                label.Text = "Travel Case #" + Entity.NetworkId + "\n" + Items() + "\nscene changes: " + Entity.NetVars.GetInt(0);
        }

        /// <summary>The items in its NetData: a count, then that many strings.</summary>
        public string Items()
        {
            NetData data = Entity.NetData;
            if (data.Length == 0) return "(empty)";
            NetReader reader = data.Read();
            int count = reader.ReadByte();
            string list = string.Empty;
            for (int i = 0; i < count; ++i)
                list += (i > 0 ? ", " : "") + reader.ReadString();
            return list;
        }

        /// <summary>The host: adds an item (at most 6, the oldest dropped).</summary>
        public void AddItem(string item)
        {
            System.Collections.Generic.List<string> items = new System.Collections.Generic.List<string>();
            NetData data = Entity.NetData;
            if (data.Length > 0)
            {
                NetReader reader = data.Read();
                int count = reader.ReadByte();
                for (int i = 0; i < count; ++i) items.Add(reader.ReadString());
            }
            if (items.Count == 6) items.RemoveAt(0);
            items.Add(item);
            NetWriter writer = new NetWriter().WriteByte((byte)items.Count);
            foreach (string s in items) writer.WriteString(s);
            data.Set(writer);
        }
    }
}
