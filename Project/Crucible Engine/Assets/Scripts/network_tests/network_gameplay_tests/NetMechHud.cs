/*************************************************************************
* \file:         NetMechHud.cs
* \author:       Tok Duan Jie Nelson (MechHud, the original);
*                Abhishek Akilandeswari Patchamuthu (this networked copy)
* \par:          Email: a.patchamuthu@digipen.edu
* \date          28 November 2026
* \version       1.0
* \brief         Networked gameplay test: a copy of MechHud that shows this
*                machine's own mech (NetMechPlayer.Local) on the host and on
*                a client. MechHud itself is unchanged (its logic is still
*                being updated, so this is a copy).
*
* \details
*    On the "Player HUD" canvas of net_gameplay_test.scene (the Zoo's
*    layout, copied). Changes from MechHud:
*      - the player is NetMechPlayer.Local, not one found by name
*      - the pods read NetMechPlayer.HudWeapons / HudAbilities: the real
*        weapons on the host, the host's synced ammo and cooldowns on a
*        client (health is mirrored into the client's MechHealth)
*      - while the mech is destroyed the movement readout shows the
*        respawn countdown
*    The lock-on cursor stays on the screen centre (no lock-on yet).
*
* \history
*    -v1.0       copy of MechHud v1.1: the local networked mech, synced pods;
*                the loadout panel ("Loadout Panel" UIText, L toggles it)
*
* The original's notes follow.
*
* \brief         The player's combat HUD. Port of the Unity HUD scripts:
*                MechHealthHud (health bar, lost-health white bar, low tint),
*                MechWeaponsHud + WeaponsHudPod (ammo, key, cooldown sweep,
*                dimming), MechAbilitiesHud + AbilitiesHudPod (cooldown
*                counter and sweep), PlayerDamageVignette (low-health edges)
*                and MechLocomotionDebug (the ` toggled movement readout).
*
* \details
*    Goes on the ScreenSpace UICanvas entity. The layout lives in the scene
*    (Tools/SceneGen builds Unity's Combat HUD prefab layout and sprites);
*    this script finds its pieces by name under the canvas and only changes
*    sizes, colours, textures and text:
*
*        Health HUD/ Glow, Bar Back, Bar White, Bar Front, Health Label (optional)
*        Weapon Q|E|LMB|RMB/ Name, Key, Icon (+ child Sweep Line), Ammo
*        Ability Tab|Ctrl|Spc/ Key, Icon (+ child Sweep Line), Cooldown
*        Vignette Top|Bottom|Left|Right
*        Movement Debug
*
*    Icons: the part / ability Texture fields below (Unity: PartData.icon and
*    abilityIcon). A missing piece is skipped, so the layout can be edited.
*    The player is found by name (playerName) and must have a MechPlayer.
*
*    v1.1  Unity sprites and icons, glow, rising sweep line (HudCooldownSweep).
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts.NetGameplay
{
    public sealed class NetMechHud : EntityScript
    {

        // Health meter (MechHealthHud)
        public float whiteBarDelay = 1f;            // seconds without new damage before the white bar drains
        public float whiteBarDrainDuration = 0.65f;
        public float lowHealthThreshold = 0.25f;
        public Color healthyColor = new Color(0.3f, 0.95f, 0.5f);
        public Color lowHealthColor = new Color(1f, 0.3f, 0.25f);

        // Weapon / ability pods
        public float reloadDim = 0.75f;              // sprite and ammo multiplier while unavailable
        public float abilityCooldownAlpha = 0.5f;
        public Color cooldownLineColor = new Color(1f, 0.93f, 0.6f, 1f);

        // Low-health vignette (PlayerDamageVignette)
        public float vignetteStartsAtHealth = 0.5f;
        public float vignetteFullAtHealth = 0.01f;
        public float vignetteMaximumAlpha = 0.8f;
        public Color vignetteTint = new Color(0.65f, 0.015f, 0.015f);

        public bool showMovementDebug = false;       // ` (backquote) toggles it

        // Unity's PartData icons (Assets/Textures/HUD/PartIcons) and ability icons (AbilityIcons).
        public Texture iconSword;
        public Texture iconMace;
        public Texture iconThrowingAxe;
        public Texture iconCrossbowArm;
        public Texture iconLargeCrossbow;
        public Texture iconCannon;
        public Texture abilityIconHead;     // Golem Head: Heal
        public Texture abilityIconTorso;    // Golem Torso: Damage Amp
        public Texture abilityIconLegs;     // Golem / Fast Legs: Jump (Heavy Legs has none in Unity)

        // Unity's LockOnCursor sprites: free camera, an automatically locked enemy, camera lock (F).
        public Texture cursorFree;
        public Texture cursorLockOn;
        public Texture cursorCamLock;
        public bool showCursorWhenUnlocked = true;   // LockOnCursor.showAtScreenCenterWhenUnlocked

        UIRect cursorRect;
        UIImage cursorImage;
        Color cursorBase;

        // LockOnCursor.LateUpdate: on the selected enemy's aim point while it is on screen, else the
        // screen centre. Camera lock outranks the automatic lock for the sprite.
        void RefreshCursor()
        {
            if (cursorRect == null) return;
            MechTargetLock targeting = player.Targeting;
            Camera camera = Camera.Main;
            Vector2 size = Camera.ScreenSize;
            Vector2 position = Vector2.Zero;
            bool visibleTarget = false;
            if (targeting != null && targeting.CurrentTarget != null && camera != null && size.X > 0f && size.Y > 0f)
            {
                Vector2 screen; float depth;
                if (camera.WorldToScreenPoint(targeting.CurrentTarget.AimPosition, out screen, out depth)
                    && depth >= camera.NearClip && screen.X >= 0f && screen.Y >= 0f && screen.X <= size.X && screen.Y <= size.Y)
                {
                    // Screen pixels -> this canvas's reference pixels (UI::CanvasScale), relative to the centre.
                    float scale = Math.Min(size.X / 1920f, size.Y / 1080f);
                    if (scale > 0f) position = (screen - size * 0.5f) * (1f / scale);
                    visibleTarget = true;
                }
            }
            cursorRect.Position = position;
            if (cursorImage == null) return;
            cursorImage.Color = WithAlpha(cursorBase, visibleTarget || showCursorWhenUnlocked ? cursorBase.A : 0f);
            Texture wanted = targeting != null && targeting.IsHardLockMode ? cursorCamLock
                           : visibleTarget ? cursorLockOn : cursorFree;
            if (wanted != null && cursorImage.Texture != wanted) cursorImage.Texture = wanted;
        }

        Texture WeaponIcon(string partId)
        {
            switch (partId)
            {
                case "sword":          return iconSword;
                case "mace":           return iconMace;
                case "throwing_axe":   return iconThrowingAxe;
                case "small_crossbow": return iconCrossbowArm;
                case "large_crossbow": return iconLargeCrossbow;
                case "cannon":         return iconCannon;
                default:               return null;
            }
        }

        Texture AbilityIcon(string partId)
        {
            switch (partId)
            {
                case "golem_head":  return abilityIconHead;
                case "golem_torso": return abilityIconTorso;
                case "golem_legs":
                case "fast_legs":   return abilityIconLegs;
                default:            return null;
            }
        }

        sealed class Bar
        {
            public UIRect Rect; public UIImage Image; public Vector2 FullSize;
            public static Bar Find(Entity parent, string name)
            {
                Entity e = parent != null ? parent.FindChild(name) : null;
                if (e == null) return null;
                var bar = new Bar { Rect = e.GetComponent<UIRect>(), Image = e.GetComponent<UIImage>() };
                if (bar.Rect == null) return null;
                bar.FullSize = bar.Rect.Size;
                return bar;
            }
            public void SetFill(float amount) { Rect.Size = new Vector2(FullSize.X * MechMath.Clamp01(amount), FullSize.Y); }
        }

        sealed class Pod
        {
            public Entity Root;
            public UIText Name, Key, Ammo, Cooldown;
            public UIImage Icon;
            public Color IconBase, IconPlaceholder, AmmoBase, CooldownBase;
            public Bar Sweep, SweepLine;
            public float SweepTop;
            public float IconHeight;
            public Color LineBase;
            public NetMechPlayer.HudWeapon Weapon;
            public NetMechPlayer.HudAbility Ability;
        }

        NetMechPlayer player;
        Entity healthGroup;
        Bar barFront, barWhite, barGlow;
        UIText healthLabel;
        UIText debugText;
        UIText loadoutText;   // "Loadout Panel": what is in each socket, while NetMechPlayer.LoadoutOpen
        readonly List<Pod> pods = new List<Pod>();
        readonly Dictionary<string, Pod> podCache = new Dictionary<string, Pod>();
        int boundLoadout = -1;
        readonly List<UIImage> vignette = new List<UIImage>();

        // Lost-health state (ratios, so both bars work whatever their size).
        float frontFill = 1f, lostFill = 1f;
        float lastDamageTime = float.NegativeInfinity, drainFrom, drainAge;
        float boundMaximum;
        bool healthSnapshot;
        bool healthVisible = true;

        public override void OnCreate()
        {
            healthGroup = Entity.FindChild("Health HUD");
            barFront = Bar.Find(healthGroup, "Bar Front");
            barWhite = Bar.Find(healthGroup, "Bar White");
            barGlow = Bar.Find(healthGroup, "Glow");
            healthLabel = TextOf(healthGroup, "Health Label");
            foreach (var side in new[] { "Top", "Bottom", "Left", "Right" })
            {
                Entity e = Entity.FindChild("Vignette " + side);
                UIImage image = e != null ? e.GetComponent<UIImage>() : null;
                if (image != null) vignette.Add(image);
            }
            debugText = TextOf(Entity, "Movement Debug");
            loadoutText = TextOf(Entity, "Loadout Panel");
            Entity cursor = Entity.FindChild("Cursor");
            cursorRect = cursor != null ? cursor.GetComponent<UIRect>() : null;
            cursorImage = cursor != null ? cursor.GetComponent<UIImage>() : null;
            cursorBase = cursorImage != null ? cursorImage.Color : Color.White;
            FindPlayer();
        }

        public override void OnUpdate(float deltaTime)
        {
            if (Input.IsPressed(KeyCode.GraveAccent)) showMovementDebug = !showMovementDebug;
            // This machine's own mech (it is spawned after the HUD, and comes back after a scene change).
            if (player != NetMechPlayer.Local) FindPlayer();
            if (player == null)
            {
                SetHealthVisible(false);
                if (debugText != null && debugText.Text.Length > 0) debugText.Text = string.Empty;
                return;
            }
            float now = Time.ElapsedTime;
            if (player.LoadoutVersion != boundLoadout) BindPods();
            RefreshHealth(now, deltaTime);
            foreach (var pod in pods) RefreshPod(pod, now);
            RefreshVignette();
            RefreshCursor();
            RefreshDebug();
            if (loadoutText != null)
            {
                string wanted = NetMechPlayer.LoadoutOpen ? player.LoadoutText() : string.Empty;
                if (loadoutText.Text != wanted) loadoutText.Text = wanted;
            }
        }

        void FindPlayer()
        {
            player = NetMechPlayer.Local;
            if (player == null || player.Health == null || player.HudWeapons == null) { player = null; return; }   // not created yet
            boundLoadout = -1;   // bound on the next update, once the player has equipped its parts
            healthSnapshot = false;
        }

        // ---------------------------------------------------------------------------- //
        // Health: MechHealthHud.Refresh / TickLostHealth
        // ---------------------------------------------------------------------------- //

        void RefreshHealth(float now, float deltaTime)
        {
            MechHealth health = player.Health;
            SetHealthVisible(!health.IsDead);
            float ratio = health.Normalized;

            // Damage: the white bar keeps the ratio the front had before the hit. Healing snaps both up.
            bool reset = !healthSnapshot || Math.Abs(boundMaximum - health.Maximum) > 0.001f || health.IsDead;
            if (reset) lostFill = ratio;
            else if (ratio < frontFill - 1e-5f) { lastDamageTime = now; drainFrom = lostFill; drainAge = 0f; }
            else if (ratio > frontFill + 1e-5f) lostFill = ratio;
            boundMaximum = health.Maximum; healthSnapshot = true;
            frontFill = ratio;
            if (lostFill < frontFill) lostFill = frontFill;

            // After a moment without damage the white bar drains into the front over a set time.
            if (lostFill > frontFill && now - lastDamageTime >= whiteBarDelay)
            {
                drainAge += deltaTime;
                lostFill = MechMath.Lerp(drainFrom, frontFill, drainAge / Math.Max(0.01f, whiteBarDrainDuration));
            }

            if (barFront != null)
            {
                barFront.SetFill(frontFill);
                if (barFront.Image != null) barFront.Image.Color = frontFill <= lowHealthThreshold ? lowHealthColor : healthyColor;
            }
            if (barWhite != null) barWhite.SetFill(lostFill);
            if (barGlow != null) barGlow.SetFill(frontFill);   // Unity stretches the glow with health
            if (healthLabel != null)
                healthLabel.Text = "HEALTH  " + (int)Math.Ceiling(health.Current) + " / " + (int)Math.Ceiling(health.Maximum);
        }

        void SetHealthVisible(bool visible)
        {
            if (healthGroup == null || visible == healthVisible) return;
            healthVisible = visible;
            healthGroup.SetActive(visible);
        }

        // ---------------------------------------------------------------------------- //
        // Weapon and ability pods
        // ---------------------------------------------------------------------------- //

        // One pod per socket, found by its key ("Weapon Q", "Ability Tab"). Rebuilt when the
        // loadout changes (MechWeaponsHud / MechAbilitiesHud rebuild on EquipmentChanged).
        void BindPods()
        {
            pods.Clear();
            boundLoadout = player.LoadoutVersion;
            for (int i = 0; i < MechLoadout.WeaponSockets.Length; i++)
            {
                MechSocket socket = MechLoadout.WeaponSockets[i];
                Pod pod = FindPod("Weapon " + MechWeapon.KeyLabelFor(socket));
                if (pod == null) continue;
                pod.Weapon = player.HudWeapons[i];
                // Unity's MechWeaponsHud only builds rows for equipped weapons: an empty socket has no pod.
                pod.Root.SetActive(pod.Weapon != null);
                if (pod.Name != null) pod.Name.Text = pod.Weapon != null ? pod.Weapon.Name : "EMPTY";
                if (pod.Key != null) pod.Key.Text = MechWeapon.KeyLabelFor(socket);
                SetIcon(pod, pod.Weapon != null ? WeaponIcon(pod.Weapon.Def.Id) : null);
                pods.Add(pod);
            }
            for (int i = 0; i < MechLoadout.AbilitySockets.Length; i++)
            {
                MechSocket socket = MechLoadout.AbilitySockets[i];
                Pod pod = FindPod("Ability " + MechAbility.KeyLabelFor(socket));
                if (pod == null) continue;
                pod.Ability = player.HudAbilities[i];
                pod.Root.SetActive(pod.Ability != null);
                if (pod.Name != null) pod.Name.Text = pod.Ability != null ? pod.Ability.Name : string.Empty;
                if (pod.Key != null) pod.Key.Text = MechAbility.KeyLabelFor(socket);
                SetIcon(pod, pod.Ability != null ? AbilityIcon(pod.Ability.Def.Id) : null);
                pods.Add(pod);
            }
        }

        // Unity: the part's sprite at full white; without art the faint empty slot the prefab was
        // authored with (its Image colour) stays.
        void SetIcon(Pod pod, Texture icon)
        {
            if (pod.Icon == null) return;
            pod.Icon.Texture = icon;
            pod.IconBase = icon != null ? Color.White : pod.IconPlaceholder;
            pod.Icon.Color = pod.IconBase;
        }

        // Cached: a pod's base colours are read once, before any dimming.
        Pod FindPod(string name)
        {
            Pod cached;
            if (podCache.TryGetValue(name, out cached)) return cached;
            Entity root = Entity.FindChild(name);
            if (root == null) { podCache[name] = null; return null; }
            var pod = new Pod
            {
                Root = root,
                Name = TextOf(root, "Name"), Key = TextOf(root, "Key"),
                Ammo = TextOf(root, "Ammo"), Cooldown = TextOf(root, "Cooldown"),
                Sweep = Bar.Find(root, "Sweep"), SweepLine = Bar.Find(root, "Sweep Line"),
            };
            Entity icon = root.FindChild("Icon");
            pod.Icon = icon != null ? icon.GetComponent<UIImage>() : null;
            pod.IconBase = pod.IconPlaceholder = pod.Icon != null ? pod.Icon.Color : Color.White;
            UIRect iconRect = icon != null ? icon.GetComponent<UIRect>() : null;
            pod.IconHeight = iconRect != null ? iconRect.Size.Y : 0f;
            pod.AmmoBase = pod.Ammo != null ? pod.Ammo.Color : Color.White;
            pod.CooldownBase = pod.Cooldown != null ? pod.Cooldown.Color : Color.White;
            if (pod.Sweep != null) pod.SweepTop = pod.Sweep.Rect.Position.Y;
            pod.LineBase = cooldownLineColor;
            podCache[name] = pod;
            return pod;
        }

        void RefreshPod(Pod pod, float now)
        {
            bool cooling;
            float progress;
            if (pod.Weapon == null && pod.Ability == null)
            {
                // Hidden (see BindPods); kept faint in case a layout shows it anyway.
                // Empty socket: a faint pod with no ammo.
                if (pod.Ammo != null) { pod.Ammo.Text = "-"; pod.Ammo.Color = Dim(pod.AmmoBase, 0.4f); }
                if (pod.Icon != null) pod.Icon.Color = Dim(pod.IconBase, 0.3f);
                if (pod.Cooldown != null) pod.Cooldown.Text = string.Empty;
                SetSweep(pod, false, 1f);
                return;
            }
            if (pod.Weapon != null)
            {
                NetMechPlayer.HudWeapon weapon = pod.Weapon;
                cooling = weapon.IsCoolingDown;
                progress = weapon.CooldownProgress;
                if (pod.Ammo != null)
                {
                    pod.Ammo.Text = weapon.IsReloading ? "RELOAD" : weapon.AmmoText;
                    pod.Ammo.Color = Dim(pod.AmmoBase, cooling || weapon.IsEmpty ? reloadDim : 1f);
                }
                if (pod.Icon != null) pod.Icon.Color = Dim(pod.IconBase, cooling || weapon.IsEmpty ? reloadDim : 1f);
            }
            else
            {
                NetMechPlayer.HudAbility ability = pod.Ability;
                float remaining = ability.Remaining;
                cooling = remaining > 0f;
                progress = ability.Progress;
                if (pod.Icon != null) pod.Icon.Color = WithAlpha(pod.IconBase, pod.IconBase.A * (cooling ? abilityCooldownAlpha : 1f));
                if (pod.Cooldown != null)
                {
                    pod.Cooldown.Text = cooling ? ((int)Math.Ceiling(remaining)).ToString() : string.Empty;
                    pod.Cooldown.Color = WithAlpha(pod.CooldownBase, cooling ? 1f : 0f);
                }
            }
            SetSweep(pod, cooling, progress);
        }

        // Unity's HudCooldownSweep: a thin line that rises from the bottom of the icon to its top as
        // the cooldown runs out, hidden while ready. "Sweep Line" is a child of "Icon" anchored to the
        // icon's top edge (so +y down from there). An optional "Sweep" shade covers what is left.
        static void SetSweep(Pod pod, bool cooling, float progress)
        {
            float remaining = cooling ? 1f - MechMath.Clamp01(progress) : 0f;
            if (pod.SweepLine != null)
            {
                pod.SweepLine.Rect.Position = new Vector2(pod.SweepLine.Rect.Position.X, pod.IconHeight * remaining);
                if (pod.SweepLine.Image != null) pod.SweepLine.Image.Color = WithAlpha(pod.LineBase, cooling ? pod.LineBase.A : 0f);
            }
            if (pod.Sweep != null)
                pod.Sweep.Rect.Size = new Vector2(pod.Sweep.FullSize.X, pod.Sweep.FullSize.Y * remaining);
        }

        // ---------------------------------------------------------------------------- //
        // Vignette and debug readout
        // ---------------------------------------------------------------------------- //

        void RefreshVignette()
        {
            MechHealth health = player.Health;
            float alpha = health.IsDead ? 0f
                : MechMath.Clamp01((vignetteStartsAtHealth - health.Normalized) / Math.Max(0.001f, vignetteStartsAtHealth - vignetteFullAtHealth))
                  * MechMath.Clamp01(vignetteMaximumAlpha);
            foreach (var image in vignette) image.Color = WithAlpha(vignetteTint, alpha);
        }

        void RefreshDebug()
        {
            if (debugText == null) return;
            // Destroyed: the countdown to the respawn, in place of the movement readout.
            if (player.Health.IsDead)
            {
                debugText.Text = "DESTROYED\nBack at your spawn point in " + (int)Math.Ceiling(player.RespawnRemaining);
                return;
            }
            if (!showMovementDebug) { if (debugText.Text.Length > 0) debugText.Text = string.Empty; return; }
            Vector3 v = player.Velocity;
            float planar = (float)Math.Sqrt(v.X * v.X + v.Z * v.Z);
            debugText.Text =
                "MOVEMENT  (` to hide)\n" +
                "State        " + player.State + "\n" +
                "Grounded     " + (player.IsGrounded ? "yes" : "no") + "\n" +
                "Speed        " + planar.ToString("0.0") + " m/s  (walk " + player.WalkSpeed.ToString("0.0") +
                ", run " + player.RunSpeed.ToString("0.0") + ")\n" +
                "Auto run     " + (player.IsAutoRunning ? "RUNNING" : ((int)(player.AutoRunProgress * 100f)) + "%") + "\n" +
                "Quick boost  " + (player.IsQuickBoosting ? "BOOSTING " + ((int)(player.QuickBoostProgress * 100f)) + "%"
                                  : player.QuickBoostCooldownRemaining > 0f ? "cooldown " + player.QuickBoostCooldownRemaining.ToString("0.00") + "s"
                                  : "ready") + "\n" +
                "Input        " + (player.GameplayInputActive ? "captured" : "released (click the game view)");
        }

        // ---------------------------------------------------------------------------- //

        static UIText TextOf(Entity parent, string name)
        {
            Entity e = parent != null ? parent.FindChild(name) : null;
            return e != null ? e.GetComponent<UIText>() : null;
        }

        static Color Dim(Color c, float dim) { return new Color(c.R * dim, c.G * dim, c.B * dim, c.A * dim); }
        static Color WithAlpha(Color c, float a) { return new Color(c.R, c.G, c.B, a); }
    }
}
