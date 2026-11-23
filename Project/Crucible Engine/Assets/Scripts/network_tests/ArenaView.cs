/*************************************************************************
* \file:         ArenaView.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          20 November 2026
* \version       1.1
* \brief         Networking sample: everything the arena shooter SHOWS, on
*                every machine. It never changes the game; it reads
*                ArenaRules' network variables and listens to its events.
*
* \details
*    Next to ArenaRules on the "Arena Rules" entity, running Everywhere.
*
*      Nameplates   one local (not networked) NetNameplate per player,
*                   following its character: name, health bar, "down"
*                   while dead, a red flash when hit. Built from the
*                   network variables, so a late joiner sees the same.
*      Bodies       a dead player's body is hidden here, on each machine
*                   (the renderers are not replicated: every machine draws
*                   what the shared state says): its "Mech Visual" child
*                   (SetActive), and a MeshRenderer on the player itself
*                   if it has one.
*      HUD          a local NetHud canvas: this player's health bar and
*                   number, everyone's kills and deaths, a crosshair, a
*                   centre message (hit, killed, respawning in N) and a
*                   kill feed.
*      Events       HitEvent, DeathEvent, RespawnEvent (reliable) and
*                   ShotEvent (unreliable: a muzzle flash on a nameplate).
*
* \history
*    -v1.0       arena shooter (Networking C# Phase 6)
*    -v1.1       the player is the game's mech: a dead player's whole Mech
*                Visual is hidden, not only a MeshRenderer on the root
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class ArenaView : EntityScript
    {
        /// <summary>A local text above each player (NetNameplate prefab).</summary>
        public Prefab nameplatePrefab;
        /// <summary>The local HUD canvas (NetHud prefab).</summary>
        public Prefab hudPrefab;
        /// <summary>Show this machine's own nameplate too.</summary>
        public bool showOwnNameplate = false;
        /// <summary>Height of a nameplate above its character's origin (metres).</summary>
        public float nameplateHeight = 1.7f;
        /// <summary>Seconds a kill-feed line stays.</summary>
        public float feedSeconds = 5f;

        private static readonly Color[] PeerColors =
        {
            new Color(0.35f, 0.75f, 1f), new Color(1f, 0.6f, 0.2f), new Color(0.5f, 1f, 0.45f), new Color(0.95f, 0.45f, 1f)
        };

        private ArenaRules rules;
        private readonly Entity[] nameplates = new Entity[Network.MaxPlayers];
        private readonly float[] hitFlash = new float[Network.MaxPlayers];
        private readonly float[] shotFlash = new float[Network.MaxPlayers];
        private readonly float[] deadSince = new float[Network.MaxPlayers];
        private readonly List<string> feed = new List<string>();
        private readonly List<float> feedAge = new List<float>();
        private UIText hudHealth, hudScores, hudFeed, hudMessage;
        private UISlider hudBar;
        private string message = "";
        private float messageTime;
        private float clock;

        public override void OnCreate()
        {
            rules = GetComponent<ArenaRules>();
            if (hudPrefab == null)
                return;
            Entity hud = hudPrefab.Instantiate();   // local: this machine's screen only
            if (hud == null)
                return;
            hudHealth = Text(hud, "Health");
            hudScores = Text(hud, "Scores");
            hudFeed = Text(hud, "Feed");
            hudMessage = Text(hud, "Message");
            Entity bar = hud.FindChild("Health Bar");
            hudBar = bar != null ? bar.GetComponent<UISlider>() : null;
        }

        private static UIText Text(Entity hud, string child)
        {
            Entity e = hud.FindChild(child);
            return e != null ? e.GetComponent<UIText>() : null;
        }

        public override void OnUpdate(float deltaTime)
        {
            if (rules == null)
                return;
            clock += deltaTime;
            for (int peer = 0; peer < Network.MaxPlayers; ++peer)
                UpdatePlayer(peer, deltaTime);
            UpdateHud(deltaTime);
        }

        // --- per player: body and nameplate ------------------------------------------------ //

        private void UpdatePlayer(int peer, float deltaTime)
        {
            hitFlash[peer] = Math.Max(0f, hitFlash[peer] - deltaTime);
            shotFlash[peer] = Math.Max(0f, shotFlash[peer] - deltaTime);
            int state = rules.StateOf(peer);
            Entity player = state != ArenaRules.StateNone ? Network.GetPlayerEntity(peer) : null;
            if (player == null)
            {
                RemoveNameplate(peer);
                return;
            }

            // The body: hidden while dead, on every machine.
            bool visible = state == ArenaRules.StateAlive;
            if (player.HasComponent<MeshRenderer>() && player.IsEnabled<MeshRenderer>() != visible)
                player.SetEnabled<MeshRenderer>(visible);
            Entity body = player.FindChild("Mech Visual");
            if (body != null && body.IsActiveSelf != visible)
                body.SetActive(visible);

            // The nameplate.
            if (peer == Network.LocalPeerId && !showOwnNameplate)
            {
                RemoveNameplate(peer);
                return;
            }
            if (nameplates[peer] == null || !nameplates[peer].IsAlive)
            {
                nameplates[peer] = nameplatePrefab != null ? nameplatePrefab.Instantiate() : null;
                if (nameplates[peer] == null)
                    return;
            }
            Entity plate = nameplates[peer];
            plate.Transform.WorldPosition = player.Transform.WorldPosition + new Vector3(0f, nameplateHeight, 0f);
            WorldText text = plate.WorldText;
            string name = Network.GetPeerName(peer);
            if (state == ArenaRules.StateDead)
            {
                text.Text = name + "\n(down)";
                text.Color = Color.Gray;
            }
            else
            {
                text.Text = name + "\n" + Bar(rules.HealthOf(peer), rules.maxHealth, 10);
                text.Color = hitFlash[peer] > 0f ? Color.Red : shotFlash[peer] > 0f ? Color.Yellow : PeerColors[peer];
            }
        }

        private void RemoveNameplate(int peer)
        {
            if (nameplates[peer] != null && nameplates[peer].IsAlive)
                Entity.Destroy(nameplates[peer]);
            nameplates[peer] = null;
        }

        private static string Bar(int value, int max, int cells)
        {
            int filled = max > 0 ? (int)Math.Round((double)Math.Max(0, Math.Min(value, max)) / max * cells) : 0;
            return "[" + new string('#', filled) + new string('-', cells - filled) + "] " + value;
        }

        // --- the HUD ------------------------------------------------------------------------ //

        private void UpdateHud(float deltaTime)
        {
            int self = Network.LocalPeerId;
            if (hudHealth != null)
            {
                if (!Network.IsOnline)
                    hudHealth.Text = "Offline - start a Host or Client session (Network tab) to play";
                else if (!Network.IsReady || self < 0)
                    hudHealth.Text = "Joining...";
                else
                    hudHealth.Text = Network.GetPeerName(self) + " (player " + self + ")   Health " + rules.HealthOf(self) + " / " + rules.maxHealth;
            }
            if (hudBar != null)
            {
                hudBar.MaxValue = Math.Max(1, rules.maxHealth);
                hudBar.Value = self >= 0 ? rules.HealthOf(self) : 0;
            }
            if (hudScores != null)
            {
                string scores = (Network.IsHost ? "Backspace: lobby   " : "") + "L: leave\nKills / Deaths";
                foreach (int peer in Network.Peers)
                    if (rules.StateOf(peer) != ArenaRules.StateNone)
                        scores += "\n" + Network.GetPeerName(peer) + (peer == self ? " (you)" : "") + "   " + rules.KillsOf(peer) + " / " + rules.DeathsOf(peer);
                hudScores.Text = scores;
            }

            for (int i = feedAge.Count - 1; i >= 0; --i)
            {
                feedAge[i] += deltaTime;
                if (feedAge[i] < feedSeconds)
                    continue;
                feedAge.RemoveAt(i);
                feed.RemoveAt(i);
            }
            if (hudFeed != null)
                hudFeed.Text = string.Join("\n", feed);

            if (hudMessage != null)
            {
                if (self >= 0 && rules.StateOf(self) == ArenaRules.StateDead)
                {
                    float left = Math.Max(0f, rules.respawnDelay - (clock - deadSince[self]));
                    hudMessage.Text = message + "\nRespawning in " + Math.Ceiling(left);
                    hudMessage.Color = Color.Red;
                }
                else
                {
                    messageTime = Math.Max(0f, messageTime - deltaTime);
                    hudMessage.Text = messageTime > 0f ? message : "";
                    hudMessage.Color = Color.White;
                }
            }
        }

        private void Say(string text, float seconds)
        {
            message = text;
            messageTime = seconds;
        }

        private void AddFeed(string line)
        {
            feed.Add(line);
            feedAge.Add(0f);
            while (feed.Count > 5)
            {
                feed.RemoveAt(0);
                feedAge.RemoveAt(0);
            }
        }

        private static string NameOf(int peer)
        {
            return peer >= 0 && peer < Network.MaxPlayers ? Network.GetPeerName(peer) : "someone";
        }

        // --- what the host announces -------------------------------------------------------- //

        public override void OnNetworkEvent(ushort kind, NetReader data)
        {
            int self = Network.LocalPeerId;
            if (kind == ArenaRules.HitEvent)
            {
                int victim = data.ReadByte(), shooter = data.ReadByte();
                int damage = data.ReadInt(), health = data.ReadInt();
                data.ReadVector3();
                if (!data.Ok || victim >= Network.MaxPlayers)
                    return;
                hitFlash[victim] = 0.25f;
                if (victim == self)
                    Say("Hit by " + NameOf(shooter) + "  -" + damage, 0.8f);
                else if (shooter == self)
                    Say("Hit " + NameOf(victim) + "  -" + damage + "  (" + health + " left)", 0.6f);
            }
            else if (kind == ArenaRules.DeathEvent)
            {
                int victim = data.ReadByte(), killer = data.ReadByte();
                if (!data.Ok || victim >= Network.MaxPlayers)
                    return;
                deadSince[victim] = clock;
                AddFeed(killer == victim || killer >= Network.MaxPlayers ? NameOf(victim) + " died"
                                                                         : NameOf(killer) + " eliminated " + NameOf(victim));
                if (victim == self)
                    Say("You were eliminated by " + NameOf(killer), rules != null ? rules.respawnDelay : 3f);
                else if (killer == self)
                    Say("You eliminated " + NameOf(victim), 1.5f);
            }
            else if (kind == ArenaRules.RespawnEvent)
            {
                int peer = data.ReadByte();
                if (!data.Ok || peer >= Network.MaxPlayers)
                    return;
                AddFeed(NameOf(peer) + " respawned");
                if (peer == self)
                    Say("Respawned", 1f);
            }
            else if (kind == ArenaRules.ShotEvent)
            {
                int shooter = data.ReadByte();
                if (data.Ok && shooter < Network.MaxPlayers)
                    shotFlash[shooter] = 0.1f;
            }
        }

        public override void OnPeerJoined(int peer)
        {
            AddFeed(NameOf(peer) + " joined");
        }

        public override void OnPeerLeft(int peer)
        {
            AddFeed("Player " + peer + " left");
            if (peer >= 0 && peer < Network.MaxPlayers)
                RemoveNameplate(peer);
        }
    }
}
