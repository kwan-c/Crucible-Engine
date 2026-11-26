/*************************************************************************
* \file:         SyncDemo.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          26 November 2026
* \version       1.1
* \brief         Networking sample: a key-driven check, in two Editors, of
*                scale, character size, synced children, attachments,
*                renderers, on/off below the root and ownership.
*
* \details
*    On "Sync Demo" in network_sync_test.scene. The HOST changes things with
*    the number keys (above the letters, or the number pad); every machine
*    prints what it sees on the floating text, so Host and Client can be
*    compared at a glance (README section 8.22 has the checklist):
*
*        1  the mech's scale        1 -> 1.5 -> 2
*        2  the mech's capsule      height 2 <-> 1
*        3  the torso's aim         +30 degrees
*        4  the mech's material     A <-> B
*        5  the mech's tint         none -> red -> green    (MaterialParams)
*        6  the gun                 shown <-> hidden        (Children/OnOff)
*        7  the part                none -> Mount L -> Mount R -> dropped
*        8  the crate's owner       host -> each client -> host
*        9  the mech                visible <-> hidden
*        0  everything back to the start
*
*    Nothing here sends anything: the script only changes components as any
*    gameplay would, and the objects' Network Object settings replicate them.
*    On a client the keys do nothing (the host decides). Offline it runs as
*    on a host.
*
*    Player input (every machine): J, K and L are three game actions in an
*    ActionMap, sent each frame. The host's text shows every player's held
*    actions and how many presses it counted (one per press, however short
*    the tap); a client's shows its own.
*
* \history
*    -v1.0       sample for scale, character size, synced children, attachments,
*                renderers, on/off below the root and ownership
*    -v1.1       player input: J, K, L as game actions on every machine; the host shows each
*                player's held actions and press count
*
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class SyncDemo : EntityScript
    {
        /// <summary>The mech's two materials (key 4).</summary>
        public Material materialA;
        public Material materialB;

        private Entity mech, torso, gun, mountL, mountR, part, crate;
        private WorldText text;
        private int scaleStep, tintStep, partStep;
        private float torsoYaw;
        private bool crouched;
        private bool useB;

        // Player input: three game actions on every machine; the host counts every player's presses.
        private const int ActJ = 0, ActK = 1, ActL = 2;
        private readonly ActionMap actions = new ActionMap().Bind(ActJ, KeyCode.J).Bind(ActK, KeyCode.K).Bind(ActL, KeyCode.L);
        private readonly System.Collections.Generic.Dictionary<int, int> presses = new System.Collections.Generic.Dictionary<int, int>();

        public override void OnCreate()
        {
            mech = Entity.Find("Sync Mech");
            part = Entity.Find("Sync Part");
            crate = Entity.Find("Sync Crate");
            Entity body = mech != null ? mech.FindChild("Body") : null;
            torso = body != null ? body.FindChild("Torso") : null;
            gun = torso != null ? torso.FindChild("Gun") : null;
            mountL = body != null ? body.FindChild("Mount L") : null;
            mountR = body != null ? body.FindChild("Mount R") : null;
            text = Entity.GetComponent<WorldText>();
            if (mech == null || torso == null || gun == null || mountL == null || mountR == null || part == null || crate == null)
                Log.Error("SyncDemo: the scene is missing one of Sync Mech (Body/Torso/Gun, Body/Mount L, Body/Mount R), Sync Part, Sync Crate");
        }

        public override void OnUpdate(float deltaTime)
        {
            if (mech == null || torso == null || gun == null || part == null || crate == null)
                return;
            actions.Send();
            if (Network.IsAuthority)
            {
                HandleKeys();
                CountPresses();
            }
            if (text != null)
                text.Text = Describe();
        }

        private static bool Key(KeyCode row, KeyCode pad) { return Input.IsPressed(row) || Input.IsPressed(pad); }

        private void HandleKeys()
        {
            if (Key(KeyCode.Num1, KeyCode.KP1))
            {
                scaleStep = (scaleStep + 1) % 3;
                float s = 1f + 0.5f * scaleStep;
                mech.Transform.Scale = new Vector3(s, s, s);
            }
            if (Key(KeyCode.Num2, KeyCode.KP2))
            {
                crouched = !crouched;
                CharacterController controller = mech.GetComponent<CharacterController>();
                if (controller != null) controller.Height = crouched ? 1f : 2f;
            }
            if (Key(KeyCode.Num3, KeyCode.KP3))
            {
                torsoYaw = (torsoYaw + 30f) % 360f;
                torso.Transform.Rotation = Quaternion.AngleAxis(torsoYaw, Vector3.Up);
            }
            if (Key(KeyCode.Num4, KeyCode.KP4))
            {
                useB = !useB;
                MeshRenderer renderer = mech.GetComponent<MeshRenderer>();
                if (renderer != null) renderer.Material = useB ? materialB : materialA;
            }
            if (Key(KeyCode.Num5, KeyCode.KP5))
            {
                tintStep = (tintStep + 1) % 3;
                MaterialParams tint = mech.GetComponent<MaterialParams>();
                if (tint != null)
                {
                    if (tintStep == 0) tint.ClearTint();
                    else tint.Tint = tintStep == 1 ? new Color(1f, 0.2f, 0.2f) : new Color(0.2f, 1f, 0.2f);
                }
            }
            if (Key(KeyCode.Num6, KeyCode.KP6))
                gun.SetEnabled<MeshRenderer>(!gun.IsEnabled<MeshRenderer>());
            if (Key(KeyCode.Num7, KeyCode.KP7))
            {
                partStep = (partStep + 1) % 4;
                if (partStep == 1 || partStep == 2)
                {
                    part.Transform.SetParent(partStep == 1 ? mountL : mountR, false);
                    part.Transform.Position = new Vector3(0f, 0f, 0.6f);
                    part.Transform.Rotation = Quaternion.Identity;
                }
                else if (partStep == 3)
                {
                    part.Transform.SetParent(null, true);   // dropped where it is
                }
                else
                {
                    part.Transform.Position = new Vector3(3f, 1f, 6f);   // back on its own spot
                }
            }
            if (Key(KeyCode.Num8, KeyCode.KP8))
                Network.SetOwner(crate, NextOwner(crate.Owner));
            if (Key(KeyCode.Num9, KeyCode.KP9))
            {
                MeshRenderer renderer = mech.GetComponent<MeshRenderer>();
                if (renderer != null) renderer.Visible = !renderer.Visible;
            }
            if (Key(KeyCode.Num0, KeyCode.KP0))
                ResetAll();
        }

        // Whose input this machine can read: every player on the host, only its own elsewhere.
        private static int[] InputPeers()
        {
            return Network.IsHost ? Network.Peers : new int[] { Network.LocalPeerId };
        }

        private void CountPresses()
        {
            foreach (int peer in InputPeers())
            {
                NetInput input = Network.GetInput(peer);
                int n = (input.WasPressed(ActJ) ? 1 : 0) + (input.WasPressed(ActK) ? 1 : 0) + (input.WasPressed(ActL) ? 1 : 0);
                int before;
                presses.TryGetValue(peer, out before);
                presses[peer] = before + n;
            }
        }

        private string DescribeInput()
        {
            string line = "\ninput (J K L)";
            foreach (int peer in InputPeers())
            {
                NetInput input = Network.GetInput(peer);
                int n;
                presses.TryGetValue(peer, out n);
                line += "   " + peer + ": " + (input.IsDown(ActJ) ? "J" : "-") + (input.IsDown(ActK) ? "K" : "-") + (input.IsDown(ActL) ? "L" : "-")
                     + (Network.IsAuthority ? " x" + n : "") + (input.IsFresh ? "" : " (none)");
            }
            return line;
        }

        // The host, then each player in the session in turn, then the host again.
        private static int NextOwner(int current)
        {
            int[] peers = Network.Peers;
            for (int i = 0; i < peers.Length; ++i)
                if (peers[i] == current)
                    return peers[(i + 1) % peers.Length];
            return Network.HostPeerId;
        }

        private void ResetAll()
        {
            scaleStep = 0; tintStep = 0; partStep = 0; torsoYaw = 0f; crouched = false; useB = false;
            mech.Transform.Scale = new Vector3(1f, 1f, 1f);
            CharacterController controller = mech.GetComponent<CharacterController>();
            if (controller != null) controller.Height = 2f;
            torso.Transform.Rotation = Quaternion.Identity;
            MeshRenderer renderer = mech.GetComponent<MeshRenderer>();
            if (renderer != null) { renderer.Material = materialA; renderer.Visible = true; }
            MaterialParams tint = mech.GetComponent<MaterialParams>();
            if (tint != null) tint.ClearTint();
            gun.SetEnabled<MeshRenderer>(true);
            part.Transform.SetParent(null, false);
            part.Transform.Position = new Vector3(3f, 1f, 6f);
            part.Transform.Rotation = Quaternion.Identity;
            if (Network.IsHost) Network.SetOwner(crate, Network.HostPeerId);
        }

        // What this machine sees: the same lines on the Host and every Client when the sync works.
        private string Describe()
        {
            CharacterController controller = mech.GetComponent<CharacterController>();
            MeshRenderer renderer = mech.GetComponent<MeshRenderer>();
            MaterialParams tint = mech.GetComponent<MaterialParams>();
            Quaternion q = torso.Transform.Rotation;
            double yaw = 2.0 * System.Math.Atan2(q.Y, q.W) * 180.0 / System.Math.PI;
            Entity parent = part.Transform.Parent;
            string role = !Network.IsOnline ? "offline" : Network.IsHost ? "HOST (keys 0-9)" : "CLIENT " + Network.LocalPeerId;
            Color t = tint != null ? tint.Tint : new Color(0f, 0f, 0f);
            return role
                + "\nscale " + mech.Transform.Scale.X.ToString("0.0")
                + "   height " + (controller != null ? controller.Height.ToString("0.0") : "-")
                + "   torso " + ((int)System.Math.Round(yaw)).ToString() + " deg"
                + "\nmaterial " + (renderer != null && renderer.Material != null ? renderer.Material.Name : "-")
                + "   tint " + t.R.ToString("0.0") + " " + t.G.ToString("0.0") + " " + t.B.ToString("0.0")
                + "   mech " + (renderer != null && renderer.Visible ? "shown" : "hidden")
                + "\ngun " + (gun.IsEnabled<MeshRenderer>() ? "on" : "off")
                + "   part on " + (parent != null ? parent.Name : "nothing")
                + "\ncrate owner " + crate.Owner + (crate.IsMine ? " (mine: light on)" : "")
                + DescribeInput();
        }
    }
}
