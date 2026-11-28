/*************************************************************************
* \file:         PartSwapWindow.cs
* \author:       <your name>
* \par:          Email: <your email>
* \date          28 November 2026
* \version       1.0
* \brief         One part card of the part swap UI. Goes on the root entity of
*                the PartSwapWindow prefab (the two screen-space windows under
*                PartSwapHUD/RAnchor and /LAnchor) and of the PartSwapWorldSpace
*                prefab (the world-space card over a dropped part).
*
* \details
*    Expected children (found by name, anywhere under the root):
*
*        PartName, PartType, PartStatName, PartStat   UIText
*        PartIcon                                     UIImage
*        INTERACTABLE Background                      UIButton (optional)
*
*    Two states, both about the root's UIRect:
*
*        Active     the UIRect is switched on: the card is drawn (with all of its
*                   children, which the layout skips while the root is off) and
*                   a click on the card swaps.
*        Inactive   the UIRect is switched off: nothing is drawn, nothing is
*                   clicked. This is the starting state (startActive = false).
*
*    Everything below is public so a script on another entity can drive the
*    card (MechPlayer does):
*
*        window.SetActive(true / false);
*        window.SetPartName("throwing_axe");     // shows the part id: THROWING AXE
*        window.SetPartType(MechPartKind.Arm);   // or any string
*        window.SetPartStatNames("Damage\nRange");
*        window.SetPartStats("50\n32m");
*        window.SetPartIcon(texture);            // null = the prefab's own artwork
*
*    or in one go:
*
*        window.ShowPart(def);        // name, type, stat names, stats and icon from a part
*        window.ShowEmpty();          // "-EMPTY-", icon alpha 0, no stat text
*
*    Clicking: the card listens to its own INTERACTABLE Background button
*    (OnButtonClicked), whatever the button's Action Id is, so the left and
*    right cards stay apart without sharing one id. A click calls
*    MechPlayer.CompletePartSwap(Socket), which swaps the highlighted dropped
*    part into the socket this card shows (Bind sets it). Only the screen-space
*    cards can be clicked: the engine's world-space canvases ignore the mouse.
*
*    Icons come from the MechHud on the "Player HUD" entity (its public icon
*    textures), so they are assigned in one place. A part with no icon there
*    keeps the prefab's own icon.
*
**************************************************************************/
using System;
using System.Text;

namespace Crucible.Scripts
{
    public sealed class PartSwapWindow : EntityScript
    {
        /// <summary>Tick to start Active; the scene starts every card Inactive.</summary>
        public bool startActive = false;
        /// <summary>The entity carrying MechHud, where the part icon textures are assigned.</summary>
        public string hudEntityName = "Player HUD";

        /// <summary>What PartName reads for a socket with nothing in it.</summary>
        public const string EmptyName = "-EMPTY-";

        const string NameChild = "PartName";
        const string TypeChild = "PartType";
        const string StatNameChild = "PartStatName";
        const string StatChild = "PartStat";
        const string IconChild = "PartIcon";
        const string ButtonChild = "INTERACTABLE Background";

        /// <summary>Active: the root UIRect is on and a click swaps.</summary>
        public bool IsActive { get; private set; }
        /// <summary>The socket this card shows and swaps into (set by Bind).</summary>
        public MechSocket Socket { get; private set; }

        /// <summary>Raised when the card is clicked while Active (after the swap was asked for).</summary>
        public event Action<PartSwapWindow> Pressed;

        MechPlayer player;
        bool ready;
        UIText nameText, typeText, statNameText, statText;
        UIImage iconImage;
        Entity button;
        Texture defaultIcon;
        Color defaultIconColour = Color.White;
        MechHud hud;
        int hudSearches;

        public override void OnCreate()
        {
            EnsureReady();
            SetActive(startActive);
        }

        // Lazily too: another script may drive the card before this one's OnCreate has run.
        void EnsureReady()
        {
            if (ready) return;
            ready = true;
            nameText = TextOf(NameChild);
            typeText = TextOf(TypeChild);
            statNameText = TextOf(StatNameChild);
            statText = TextOf(StatChild);
            Entity icon = Entity.FindChild(IconChild);
            iconImage = icon != null ? icon.GetComponent<UIImage>() : null;
            if (iconImage != null)
            {
                defaultIcon = iconImage.Texture;
                defaultIconColour = iconImage.Color;
            }
            button = Entity.FindChild(ButtonChild);
        }

        UIText TextOf(string childName)
        {
            Entity child = Entity.FindChild(childName);
            return child != null ? child.GetComponent<UIText>() : null;
        }

        // ---------------------------------------------------------------------------- //
        // Active / Inactive
        // ---------------------------------------------------------------------------- //

        /// <summary>Active switches the root UIRect on (the card and its children show); Inactive switches it off.</summary>
        public void SetActive(bool active)
        {
            EnsureReady();
            IsActive = active;
            Entity.SetEnabled<UIRect>(active);
        }

        /// <summary>Which socket this card represents, and who to ask when it is clicked.</summary>
        public void Bind(MechPlayer owner, MechSocket socket)
        {
            player = owner;
            Socket = socket;
        }

        // ---------------------------------------------------------------------------- //
        // Content
        // ---------------------------------------------------------------------------- //

        /// <summary>PartName takes after the part id: "throwing_axe" reads THROWING AXE. Null or empty reads -EMPTY-.</summary>
        public void SetPartName(string partId)
        {
            EnsureReady();
            if (nameText != null) nameText.Text = string.IsNullOrEmpty(partId) ? EmptyName : partId.Replace('_', ' ').ToUpperInvariant();
        }

        public void SetPartName(MechPartDef def) { SetPartName(def != null ? def.Id : null); }

        /// <summary>PartType: any text, e.g. "SHOULDER".</summary>
        public void SetPartType(string type)
        {
            EnsureReady();
            if (typeText != null) typeText.Text = type ?? string.Empty;
        }

        public void SetPartType(MechPartKind kind) { SetPartType(kind.ToString().ToUpperInvariant()); }

        /// <summary>PartStatName: one stat name per line ("Damage\nMag size\n..."), lined up with PartStat.</summary>
        public void SetPartStatNames(string names)
        {
            EnsureReady();
            if (statNameText != null) statNameText.Text = names ?? string.Empty;
        }

        /// <summary>The stat names of a part (Damage, Mag size, Fire rate, Reload, Range ...; a head / torso / legs shows Health, Speed, Ability ...).</summary>
        public void SetPartStatNames(MechPartDef def)
        {
            string names, values;
            BuildStats(def, out names, out values);
            SetPartStatNames(names);
        }

        /// <summary>PartStat: one value per line, lined up with PartStatName.</summary>
        public void SetPartStats(string values)
        {
            EnsureReady();
            if (statText != null) statText.Text = values ?? string.Empty;
        }

        public void SetPartStats(MechPartDef def)
        {
            string names, values;
            BuildStats(def, out names, out values);
            SetPartStats(values);
        }

        /// <summary>PartIcon: this texture, fully opaque. Null puts the prefab's own artwork back.</summary>
        public void SetPartIcon(Texture icon)
        {
            EnsureReady();
            if (iconImage == null) return;
            if (icon != null)
            {
                iconImage.Texture = icon;
                iconImage.Color = Color.White;
            }
            else
            {
                iconImage.Texture = defaultIcon;
                iconImage.Color = defaultIconColour;
            }
        }

        /// <summary>The part's icon, from MechHud's textures.</summary>
        public void SetPartIcon(MechPartDef def) { SetPartIcon(IconFor(def)); }

        /// <summary>PartIcon invisible (alpha 0), keeping its texture and tint.</summary>
        public void HidePartIcon()
        {
            EnsureReady();
            if (iconImage == null) return;
            Color c = iconImage.Color;
            iconImage.Color = new Color(c.R, c.G, c.B, 0f);
        }

        /// <summary>Everything from one part: name, type, stat names, stats and icon. Null shows the empty card.</summary>
        public void ShowPart(MechPartDef def)
        {
            if (def == null) { ShowEmpty(); return; }
            SetPartName(def);
            SetPartType(def.Kind);
            SetPartStatNames(def);
            SetPartStats(def);
            SetPartIcon(def);
        }

        /// <summary>A socket with no part: "-EMPTY-", the icon at alpha 0, no stat text (and no type).</summary>
        public void ShowEmpty() { ShowEmptyCard(null); }

        /// <summary>As ShowEmpty, with the socket's kind in PartType ("SHOULDER") so the player sees what the slot is.</summary>
        public void ShowEmpty(MechPartKind kind) { ShowEmptyCard(kind.ToString().ToUpperInvariant()); }

        void ShowEmptyCard(string type)
        {
            SetPartName((string)null);
            SetPartType(type);
            SetPartStatNames(string.Empty);
            SetPartStats(string.Empty);
            HidePartIcon();
        }

        // ---------------------------------------------------------------------------- //
        // Click: the swap
        // ---------------------------------------------------------------------------- //

        public override void OnButtonClicked(Entity clicked)
        {
            EnsureReady();
            if (!IsActive || button == null || clicked != button) return;
            if (player != null) player.CompletePartSwap(Socket);
            if (Pressed != null) Pressed(this);
        }

        // ---------------------------------------------------------------------------- //
        // Icons and stats
        // ---------------------------------------------------------------------------- //

        // MechHud's icon textures (assigned once, on the Player HUD). Same mapping as MechHud.
        Texture IconFor(MechPartDef def)
        {
            if (def == null) return null;
            if (hud == null && hudSearches < 3)
            {
                hudSearches++;
                Entity hudEntity = Entity.Find(hudEntityName);
                hud = hudEntity != null ? hudEntity.GetComponent<MechHud>() : null;
            }
            if (hud == null) return null;
            if (def.IsWeapon)
            {
                switch (def.Id)
                {
                    case "sword":          return hud.iconSword;
                    case "mace":           return hud.iconMace;
                    case "throwing_axe":   return hud.iconThrowingAxe;
                    case "small_crossbow": return hud.iconCrossbowArm;
                    case "large_crossbow": return hud.iconLargeCrossbow;
                    case "cannon":         return hud.iconCannon;
                    default:               return null;
                }
            }
            switch (def.Id)
            {
                case "golem_head":  return hud.abilityIconHead;
                case "golem_torso": return hud.abilityIconTorso;
                case "golem_legs":
                case "fast_legs":   return hud.abilityIconLegs;
                default:            return null;
            }
        }

        // Stat names and values, one per line and lined up. Short on purpose: the value column is narrow.
        // A line only appears when the part has that stat. The PartStatName box fits about seven lines.
        static void BuildStats(MechPartDef def, out string names, out string values)
        {
            names = values = string.Empty;
            if (def == null) return;
            var n = new StringBuilder();
            var v = new StringBuilder();
            Action<string, string> add = (label, value) =>
            {
                if (n.Length > 0) { n.Append('\n'); v.Append('\n'); }
                n.Append(label);
                v.Append(value);
            };

            if (def.IsWeapon)
            {
                if (def.Damage > 0f)       add("Damage", Num(def.Damage));
                if (def.SplashDamage > 0f) add("Splash", Num(def.SplashDamage));
                if (def.Magazine > 0)      add("Mag size", def.Magazine.ToString());
                if (def.FireRate > 0f)     add("Fire rate", Num(def.FireRate) + "/s");
                if (def.Reload > 0f)       add("Reload", Num(def.Reload) + "s");
                if (def.RangeGolems > 0f)  add("Range", Num(def.Range) + "m");
                if (def.Speed != 0f)       add("Move", Signed(def.Speed) + "%");
            }
            else
            {
                if (def.Health > 0f) add("Health", "+" + Num(def.Health));
                if (def.Speed != 0f)
                {
                    // Legs set a speed in m/s; every other part is a percentage on the mech's speed.
                    if (def.Kind == MechPartKind.Legs) add("Speed", Num(def.Speed) + " m/s");
                    else add("Move", Signed(def.Speed) + "%");
                }
                if (def.Ability != MechAbilityKind.None)
                {
                    add("Ability", AbilityName(def.Ability));
                    if (def.AbilityPower > 0f) add("Power", AbilityPower(def));
                    if (def.AbilityDuration > 0f) add("Duration", Num(def.AbilityDuration) + "s");
                    if (def.AbilityCooldown > 0f) add("Cooldown", Num(def.AbilityCooldown) + "s");
                }
            }
            names = n.ToString();
            values = v.ToString();
        }

        static string AbilityName(MechAbilityKind kind)
        {
            switch (kind)
            {
                case MechAbilityKind.Heal:      return "Heal";
                case MechAbilityKind.DamageAmp: return "Dmg Amp";
                case MechAbilityKind.Jump:      return "Jump";
                default:                        return string.Empty;
            }
        }

        // What AbilityPower means depends on the ability (see MechAbilityKind).
        static string AbilityPower(MechPartDef def)
        {
            switch (def.Ability)
            {
                case MechAbilityKind.Heal:      return Num(def.AbilityPower) + "%/s";
                case MechAbilityKind.DamageAmp: return "+" + Num(def.AbilityPower) + "%";
                case MechAbilityKind.Jump:      return Num(def.AbilityPower) + " golem";
                default:                        return Num(def.AbilityPower);
            }
        }

        static string Num(float value) { return value.ToString("0.##"); }
        static string Signed(float value) { return (value > 0f ? "+" : string.Empty) + Num(value); }
    }
}
