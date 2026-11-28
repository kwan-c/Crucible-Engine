/*************************************************************************
* \file:         MechLoadout.cs
* \author:       Tok Duan Jie Nelson
* \par:          Email: t.duanjienelson@digipen.edu
* \author:       Hugo Low Ren Hao (M3: sync with Unity main)
* \par:          Email: low.h@digipen.edu
* \date          8 November 2026
* \version       1.2
* \brief         The part catalogue (Unity's Assets/PartCatalogue PartData
*                stats), the weapon rules (Unity's Part.cs Consume /
*                TryReload) and ability cooldowns (PartAbilityCooldown).
*
* \details
*    MechPartDef is the shared, read-only data of a part type (Unity's
*    PartData asset). MechWeapon is the live state of ONE physical part -
*    ammo and cooldowns stay with the part when it is dropped or swapped, as
*    in Unity. MechAbility is simpler: it belongs to the part TYPE in a
*    socket, so swapping in another part of the same type keeps its cooldown
*    and running effect (Unity would end them); a different type starts fresh.
*
*    Range is in golem lengths in Unity (MechRulesConfig: 4 m per golem).
*    A shot does full damage up to Range and disappears there; there is no
*    falloff (Unity main removed it).
*
*    Times use Time.ElapsedTime (Play time, pause excluded) as Unity used
*    Time.time.
*
*    v1.1  stats come from MechPartDef; damage / range / splash / melee added
*          for hits; parts can be swapped (MechPart, MechPlayer.Equip).
*    v1.2  Unity main: one Range with no falloff; abilities (Heal, Damage Amp,
*          Jump) with power, duration and an effect window on MechAbility.
*
**************************************************************************/
using System;

namespace Crucible.Scripts
{
    public enum MechSocket { ShoulderL, ShoulderR, ArmL, ArmR, Head, Torso, Legs }

    /// <summary>Which sockets a part fits. Unity: PartCategory / PartSlots.</summary>
    public enum MechPartKind { Arm, Shoulder, Head, Torso, Legs }

    public enum MechAttackType { None, Melee, Projectile, Splash, Special }
    public enum MechWeaponMechanic { None, Hook, Shotgun, Scythe, PunchyArm, Shield, RockRing, Mortar, Katyusha, Tentacles, Tesla }

    /// <summary>
    /// What a head, torso or legs part does on its key (Unity: PartAbility, same order).
    /// Heal: AbilityPower % of maximum health per second for AbilityDuration.
    /// DamageAmp: weapon damage x (1 + AbilityPower %) for AbilityDuration.
    /// Jump: an instant leap of AbilityPower golem lengths, only from the ground.
    /// </summary>
    public enum MechAbilityKind { None, DamageAmp, Heal, Jump }

    /// <summary>One part type's stats. Unity: a PartData asset.</summary>
    public sealed class MechPartDef
    {
        public string Id, Name;
        public MechPartKind Kind;
        public MechAttackType Attack;
        public float Health;            // added to the mech's maximum health
        public float Speed;             // legs: m/s (0 = mech default); other parts: percent (-20 = x0.8)
        public float Damage, SplashDamage, SplashRadius;
        public int Ammo, Magazine;
        public float FireRate = 1f;     // attacks per second
        public float Reload;            // seconds
        public float RangeGolems;       // Unity's Range stat
        public float ProjectileSpeed;   // m/s (Unity: ProjectileSpeed)
        public float ProjectileSize;    // sphere radius of the shot, m (Unity: ProjectileSize)
        public bool Piercing;
        public float MeleeWidth;        // greybox box width in metres; zero uses the mech's settings
        public MechWeaponMechanic Mechanic;
        public float AbilityCooldown;   // head / torso / legs
        public MechAbilityKind Ability; // None = the part has no ability (no HUD pod, its key does nothing)
        public float AbilityPower;      // meaning depends on Ability (see MechAbilityKind)
        public float AbilityDuration;   // seconds; 0 = instant (Jump)

        // Loot: set on a rolled copy (MechPartStatRoll) - the catalogue entry it was rolled from
        // and the seed. Null / 0 on the catalogue's own, authored entries.
        public MechPartDef Base;
        public int RollSeed;
        public bool IsRolled { get { return Base != null; } }

        public MechPartDef Copy() { return (MechPartDef)MemberwiseClone(); }

        public bool IsWeapon { get { return Attack != MechAttackType.None; } }
        public const float MetresPerGolem = 4f;
        /// <summary>Metres. A shot does full damage up to here and disappears at it (Unity: MechCombat's ranged fire).</summary>
        public float Range { get { return RangeGolems * MetresPerGolem; } }

        public static bool Fits(MechPartKind kind, MechSocket socket)
        {
            switch (kind)
            {
                case MechPartKind.Arm:      return socket == MechSocket.ArmL || socket == MechSocket.ArmR;
                case MechPartKind.Shoulder: return socket == MechSocket.ShoulderL || socket == MechSocket.ShoulderR;
                case MechPartKind.Head:     return socket == MechSocket.Head;
                case MechPartKind.Torso:    return socket == MechSocket.Torso;
                default:                    return socket == MechSocket.Legs;
            }
        }
    }

    /// <summary>Unity's Assets/PartCatalogue, ids are this port's names.</summary>
    public static class MechPartCatalogue
    {
        static readonly MechPartDef[] parts =
        {
            new MechPartDef { Id = "sword", Name = "SWORD", Kind = MechPartKind.Arm, Attack = MechAttackType.Melee,
                              Damage = 100f, FireRate = .7f, RangeGolems = 1f, Speed = -2f },
            new MechPartDef { Id = "mace", Name = "MACE", Kind = MechPartKind.Arm, Attack = MechAttackType.Melee,
                              Damage = 70f, FireRate = 1f, RangeGolems = 1f, Speed = -1f },
            new MechPartDef { Id = "throwing_axe", Name = "THROWING AXE", Kind = MechPartKind.Arm, Attack = MechAttackType.Projectile,
                              Damage = 50f, Ammo = 50, Magazine = 1, FireRate = .5f, Reload = 1f, RangeGolems = 8f, Speed = -2.5f,
                              ProjectileSpeed = 100f, ProjectileSize = .18f },
            new MechPartDef { Id = "small_crossbow", Name = "CROSSBOW ARM", Kind = MechPartKind.Arm, Attack = MechAttackType.Projectile,
                              Damage = 70f, Ammo = 80, Magazine = 1, FireRate = .6f, Reload = 1.2f, RangeGolems = 10f, Speed = -1f,
                              ProjectileSpeed = 160f, ProjectileSize = .07f },
            // Unity's PartData_Arm_Blaster has no stats yet (0 damage, no ammo); these are placeholders.
            new MechPartDef { Id = "blaster", Name = "BLASTER", Kind = MechPartKind.Arm, Attack = MechAttackType.Projectile,
                              Damage = 35f, FireRate = 2f, RangeGolems = 8f, ProjectileSpeed = 100f, ProjectileSize = .18f },
            new MechPartDef { Id = "large_crossbow", Name = "LARGE CROSSBOW", Kind = MechPartKind.Shoulder, Attack = MechAttackType.Projectile,
                              Damage = 180f, Ammo = 30, Magazine = 1, FireRate = 1f, Reload = 2.5f,
                              RangeGolems = 20f, Speed = -5f, ProjectileSpeed = 160f, ProjectileSize = .07f, Piercing = true },
            new MechPartDef { Id = "cannon", Name = "CANNON", Kind = MechPartKind.Shoulder, Attack = MechAttackType.Splash,
                              Damage = 200f, SplashDamage = 120f, SplashRadius = 4f, Ammo = 30, Magazine = 1, FireRate = 1f, Reload = 5f,
                              RangeGolems = 10f, Speed = -4f, ProjectileSpeed = 100f, ProjectileSize = .3f },
            // Unity Hung-GreyBox PartCatalogue/GreyboxArms and GreyboxShoulders.
            new MechPartDef { Id = "minigun", Name = "MINIGUN", Kind = MechPartKind.Arm, Attack = MechAttackType.Projectile,
                              Damage = 9f, Ammo = 400, Magazine = 50, FireRate = 10f, Reload = 2f,
                              RangeGolems = 4.5f, Speed = -5f, ProjectileSpeed = 80f, ProjectileSize = .06f },
            new MechPartDef { Id = "hook", Name = "HOOK", Kind = MechPartKind.Arm, Attack = MechAttackType.Projectile,
                              Mechanic = MechWeaponMechanic.Hook, Damage = 30f, Ammo = 30, Magazine = 1,
                              FireRate = .2f, Reload = .7f, RangeGolems = 8f, Speed = -3f, ProjectileSpeed = 60f, ProjectileSize = .15f },
            new MechPartDef { Id = "hammer", Name = "HAMMER", Kind = MechPartKind.Arm, Attack = MechAttackType.Melee,
                              Damage = 100f, SplashDamage = 50f, SplashRadius = 3f, FireRate = .7f, RangeGolems = 1f, Speed = -3.5f, MeleeWidth = 1.8f },
            new MechPartDef { Id = "spear", Name = "SPEAR", Kind = MechPartKind.Arm, Attack = MechAttackType.Melee,
                              Damage = 90f, SplashDamage = 90f, SplashRadius = .5f, FireRate = 1f, RangeGolems = 3f, Speed = -4f, MeleeWidth = .7f },
            new MechPartDef { Id = "shotgun", Name = "SHOTGUN", Kind = MechPartKind.Arm, Attack = MechAttackType.Projectile,
                              Mechanic = MechWeaponMechanic.Shotgun, Damage = 150f, Ammo = 50, Magazine = 5, FireRate = .8f,
                              Reload = 3f, RangeGolems = 6f, Speed = -3f, ProjectileSpeed = 75f, ProjectileSize = .09f },
            new MechPartDef { Id = "supersword", Name = "SUPERSWORD", Kind = MechPartKind.Arm, Attack = MechAttackType.Melee,
                              Damage = 250f, SplashDamage = 250f, SplashRadius = 2.5f, FireRate = .35f, RangeGolems = 2.5f, Speed = -7f, MeleeWidth = 4.5f },
            new MechPartDef { Id = "scythe", Name = "SCYTHE", Kind = MechPartKind.Arm, Attack = MechAttackType.Melee,
                              Mechanic = MechWeaponMechanic.Scythe, Damage = 100f, SplashDamage = 100f, SplashRadius = 1.5f,
                              FireRate = .65f, RangeGolems = 1.5f, Speed = -4f, MeleeWidth = 3.5f },
            new MechPartDef { Id = "punchy_arm", Name = "PUNCHY ARM", Kind = MechPartKind.Arm, Attack = MechAttackType.Melee,
                              Mechanic = MechWeaponMechanic.PunchyArm, Damage = 30f, SplashDamage = 30f, SplashRadius = .5f,
                              FireRate = 2f, RangeGolems = .7f, Speed = -1.5f, MeleeWidth = 1f },
            new MechPartDef { Id = "metal_blades", Name = "METAL BLADES", Kind = MechPartKind.Arm, Attack = MechAttackType.Splash,
                              Damage = 60f, SplashDamage = 30f, SplashRadius = .5f, Ammo = 40, Magazine = 3, FireRate = 1f,
                              Reload = 2f, RangeGolems = 8f, Speed = -3f, ProjectileSpeed = 65f, ProjectileSize = .12f },
            new MechPartDef { Id = "shield", Name = "SHIELD", Kind = MechPartKind.Shoulder, Attack = MechAttackType.Special,
                              Mechanic = MechWeaponMechanic.Shield, FireRate = .5f, Speed = -4f },
            new MechPartDef { Id = "giant_rock", Name = "GIANT ROCK ON A ROPE", Kind = MechPartKind.Shoulder, Attack = MechAttackType.Special,
                              Mechanic = MechWeaponMechanic.RockRing, Damage = 210f, SplashRadius = 1f,
                              FireRate = .33333334f, RangeGolems = 7f, Speed = -6f },
            new MechPartDef { Id = "fishing_line", Name = "FISHING LINE", Kind = MechPartKind.Shoulder, Attack = MechAttackType.Projectile,
                              Damage = 10f, Ammo = 50, Magazine = 10, FireRate = .7f, Reload = 2f, RangeGolems = 7f,
                              Speed = -2f, ProjectileSpeed = 120f, ProjectileSize = .025f },
            new MechPartDef { Id = "mortar", Name = "MORTAR", Kind = MechPartKind.Shoulder, Attack = MechAttackType.Splash,
                              Mechanic = MechWeaponMechanic.Mortar, Damage = 100f, SplashDamage = 100f, SplashRadius = 5f,
                              Ammo = 25, Magazine = 1, FireRate = 1f, Reload = 5f, RangeGolems = 20f, Speed = -3f, ProjectileSpeed = 40f, ProjectileSize = .2f },
            new MechPartDef { Id = "katyusha", Name = "KATYUSHA", Kind = MechPartKind.Shoulder, Attack = MechAttackType.Splash,
                              Mechanic = MechWeaponMechanic.Katyusha, Damage = 20f, SplashDamage = 20f, SplashRadius = 2f,
                              Ammo = 100, Magazine = 10, FireRate = .2f, Reload = 5f, RangeGolems = 15f, Speed = -7f, ProjectileSpeed = 65f, ProjectileSize = .12f },
            new MechPartDef { Id = "tentacles", Name = "TENTACLES", Kind = MechPartKind.Shoulder, Attack = MechAttackType.Special,
                              Mechanic = MechWeaponMechanic.Tentacles, Damage = 50f, FireRate = 1.5f, RangeGolems = 4f, Speed = -3f },
            new MechPartDef { Id = "tesla_coil", Name = "TESLA COIL", Kind = MechPartKind.Shoulder, Attack = MechAttackType.Special,
                              Mechanic = MechWeaponMechanic.Tesla, Damage = 4f, Ammo = 70, Magazine = 35,
                              FireRate = 5f, Reload = 3f, RangeGolems = 7f },
            // Abilities from Unity main's PartData_Head / _Torso / _Legs_Fast. Golem Legs and Heavy Legs
            // have a cooldown stat there but no ability, so Space does nothing with them.
            new MechPartDef { Id = "golem_head", Name = "GOLEM HEAD", Kind = MechPartKind.Head, Health = 50f, AbilityCooldown = 10f,
                              Ability = MechAbilityKind.Heal, AbilityPower = 5f, AbilityDuration = 10f },
            new MechPartDef { Id = "golem_torso", Name = "GOLEM TORSO", Kind = MechPartKind.Torso, Health = 150f, AbilityCooldown = 30f,
                              Ability = MechAbilityKind.DamageAmp, AbilityPower = 50f, AbilityDuration = 20f },
            new MechPartDef { Id = "golem_legs", Name = "GOLEM LEGS", Kind = MechPartKind.Legs, Health = 50f, AbilityCooldown = 6f },
            new MechPartDef { Id = "fast_legs", Name = "FAST LEGS", Kind = MechPartKind.Legs, Health = 50f, Speed = 10f, AbilityCooldown = 4f,
                              Ability = MechAbilityKind.Jump, AbilityPower = 1f },
            new MechPartDef { Id = "heavy_legs", Name = "HEAVY LEGS", Kind = MechPartKind.Legs, Health = 50f, Speed = 5f, AbilityCooldown = 5f },
        };

        /// <summary>The part with this id, or null (logs an error).</summary>
        public static MechPartDef Find(string id)
        {
            foreach (var part in parts)
                if (string.Equals(part.Id, id, StringComparison.OrdinalIgnoreCase)) return part;
            Log.Error("MechPartCatalogue: no part called '" + id + "'.");
            return null;
        }
    }

    /// <summary>
    /// Loot stat variation. Unity: PartStatRoll. Every numeric stat Unity rolls is scaled by a
    /// random factor in [1 - Variation, 1 + Variation]; identity (id, name, kind, attack type,
    /// mechanic, ability) is untouched. Ammo and magazine stay whole numbers inside the rolled
    /// bounds, so a one-round magazine stays one round. MeleeWidth is this port's greybox
    /// setting, not a Unity stat, so it is not rolled.
    /// </summary>
    public static class MechPartStatRoll
    {
        public const double Variation = .10d;
        static readonly Random seeds = new Random();

        public static int NewSeed() { lock (seeds) return seeds.Next(1, int.MaxValue); }

        public static MechPartDef Create(MechPartDef definition, int seed)
        {
            if (definition == null) return null;
            MechPartDef source = definition.Base ?? definition;   // never roll a roll
            MechPartDef result = source.Copy();
            result.Base = source;
            result.RollSeed = seed;
            var random = new Random(seed);
            // Unity rolls its stats list in authored order; the order here is fixed, so a seed
            // always gives the same part.
            result.Health          = Roll(source.Health, random);
            result.Speed           = Roll(source.Speed, random);
            result.Damage          = Roll(source.Damage, random);
            result.Magazine        = RollCount(source.Magazine, random);
            result.Ammo            = RollCount(source.Ammo, random);
            result.Reload          = Roll(source.Reload, random);
            result.RangeGolems     = Roll(source.RangeGolems, random);
            result.ProjectileSpeed = Roll(source.ProjectileSpeed, random);
            result.ProjectileSize  = Roll(source.ProjectileSize, random);
            result.SplashRadius    = Roll(source.SplashRadius, random);
            result.FireRate        = Roll(source.FireRate, random);
            result.SplashDamage    = Roll(source.SplashDamage, random);
            result.AbilityCooldown = Roll(source.AbilityCooldown, random);
            result.AbilityDuration = Roll(source.AbilityDuration, random);
            result.AbilityPower    = Roll(source.AbilityPower, random);
            return result;
        }

        static double Factor(Random random) { return 1d - Variation + random.NextDouble() * Variation * 2d; }

        static float Roll(float value, Random random)
        {
            double factor = Factor(random);   // drawn for zeros too, so every stat's draw is stable
            if (value == 0f || float.IsNaN(value) || float.IsInfinity(value)) return value;
            return (float)(value * factor);
        }

        static int RollCount(int value, Random random)
        {
            double factor = Factor(random);
            if (value == 0) return value;
            double first = value * (1d - Variation), second = value * (1d + Variation);
            double minimum = Math.Ceiling(Math.Min(first, second)), maximum = Math.Floor(Math.Max(first, second));
            if (minimum > maximum) return value;
            return (int)Math.Max(minimum, Math.Min(maximum, Math.Round(value * factor)));
        }

        /// <summary>"DMG 50 > 53.1 (+6%), RATE ..." - every stat this roll changed, for the log.</summary>
        public static string Describe(MechPartDef rolled)
        {
            MechPartDef b = rolled != null ? rolled.Base : null;
            if (b == null) return rolled != null ? rolled.Name + " (authored stats)" : "";
            var text = new System.Text.StringBuilder(rolled.Name);
            text.Append(" [seed ").Append(rolled.RollSeed).Append("]");
            Add(text, "HP", b.Health, rolled.Health);
            Add(text, "SPD%", b.Speed, rolled.Speed);
            Add(text, "DMG", b.Damage, rolled.Damage);
            Add(text, "MAG", b.Magazine, rolled.Magazine);
            Add(text, "AMMO", b.Ammo, rolled.Ammo);
            Add(text, "RELOAD", b.Reload, rolled.Reload);
            Add(text, "RANGE", b.RangeGolems, rolled.RangeGolems);
            Add(text, "PROJ SPD", b.ProjectileSpeed, rolled.ProjectileSpeed);
            Add(text, "PROJ SIZE", b.ProjectileSize, rolled.ProjectileSize);
            Add(text, "SPLASH R", b.SplashRadius, rolled.SplashRadius);
            Add(text, "RATE", b.FireRate, rolled.FireRate);
            Add(text, "SPLASH DMG", b.SplashDamage, rolled.SplashDamage);
            Add(text, "ABIL CD", b.AbilityCooldown, rolled.AbilityCooldown);
            Add(text, "ABIL DUR", b.AbilityDuration, rolled.AbilityDuration);
            Add(text, "ABIL PWR", b.AbilityPower, rolled.AbilityPower);
            return text.ToString();
        }

        static void Add(System.Text.StringBuilder text, string label, float from, float to)
        {
            if (from == 0f) return;
            var c = System.Globalization.CultureInfo.InvariantCulture;
            float percent = (to / from - 1f) * 100f;
            text.Append("\n  ").Append(label).Append(' ').Append(from.ToString("0.###", c)).Append(" > ")
                .Append(to.ToString("0.###", c)).Append(" (").Append(percent >= 0f ? "+" : "")
                .Append(percent.ToString("0.0", c)).Append("%)");
        }
    }

    /// <summary>One physical weapon part's live state. Unity: Part (ammo, ReadyAt, reload).</summary>
    public sealed class MechWeapon
    {
        public readonly MechPartDef Def;
        public MechSocket Socket;               // set when equipped
        public string Name { get { return Def.Name; } }
        public string KeyLabel { get { return KeyLabelFor(Socket); } }
        public bool UsesAmmo { get { return Def.Ammo > 0 || Def.Magazine > 0; } }
        public float ShotInterval { get { return Def.FireRate > 0f ? 1f / Def.FireRate : 1f; } }
        public bool RepeatWhileHeld { get { return Socket == MechSocket.ArmL || Socket == MechSocket.ArmR || Def.Mechanic == MechWeaponMechanic.Tesla; } }
        public bool IsMelee { get { return Def.Attack == MechAttackType.Melee; } }

        public int RemainingAmmo { get; private set; }   // total shots left, loaded rounds included
        public int MagazineRounds { get; private set; }  // shots left before the next reload
        float readyAt, readyStartedAt, reloadEndsAt;

        public MechWeapon(MechPartDef def)
        {
            Def = def;
            RemainingAmmo = UsesAmmo ? Math.Max(0, def.Ammo) : 0;
            MagazineRounds = def.Magazine > 0 ? Math.Min(def.Magazine, RemainingAmmo) : RemainingAmmo;
        }

        public static string KeyLabelFor(MechSocket socket)
        {
            switch (socket)
            {
                case MechSocket.ShoulderL: return "Q";
                case MechSocket.ShoulderR: return "E";
                case MechSocket.ArmL:      return "LMB";
                case MechSocket.ArmR:      return "RMB";
                default:                   return string.Empty;
            }
        }

        public float CooldownRemaining(float now) { return Math.Max(0f, readyAt - now); }
        public float CooldownProgress(float now)
        {
            float duration = Math.Max(0f, readyAt - readyStartedAt);
            float remaining = CooldownRemaining(now);
            return remaining <= 0f || duration <= 0f ? 1f : MechMath.Clamp01(1f - remaining / duration);
        }
        public bool IsCoolingDown(float now) { return CooldownRemaining(now) > 0f; }
        public bool IsReloading(float now) { return reloadEndsAt > now; }
        public bool IsEmpty { get { return UsesAmmo && RemainingAmmo <= 0; } }

        /// <summary>
        /// One shot or swing, if ready. A shot uses a round; emptying the magazine with ammo left
        /// starts the reload by itself, and the next shot waits for the interval and the reload.
        /// </summary>
        public bool TryAttack(float now)
        {
            return TryAttack(now, 1);
        }

        public bool TryAttack(float now, int ammunitionCost)
        {
            if (IsCoolingDown(now) || (UsesAmmo && (ammunitionCost < 1 || ammunitionCost > MagazineRounds))) return false;
            readyStartedAt = now;
            readyAt = now + ShotInterval;
            if (!UsesAmmo) return true;
            RemainingAmmo -= ammunitionCost;
            MagazineRounds -= ammunitionCost;
            if (RemainingAmmo <= 0) { reloadEndsAt = 0f; return true; }
            if (MagazineRounds > 0) return true;
            MagazineRounds = Def.Magazine > 0 ? Math.Min(Def.Magazine, RemainingAmmo) : RemainingAmmo;
            if (Def.Magazine <= 0) return true;
            reloadEndsAt = now + Def.Reload;
            readyAt = Math.Max(readyAt, reloadEndsAt);
            return true;
        }

        /// <summary>R: refill a partly empty magazine from the reserve (moves rounds, never creates them).</summary>
        public bool TryReload(float now)
        {
            if (!UsesAmmo || IsReloading(now)) return false;
            int loaded = Math.Min(Def.Magazine, RemainingAmmo);
            if (Def.Magazine <= 0 || MagazineRounds >= loaded) return false;
            MagazineRounds = loaded;
            readyStartedAt = now;
            reloadEndsAt = now + Def.Reload;
            readyAt = Math.Max(readyAt, reloadEndsAt);
            return true;
        }

        /// <summary>
        /// Zoo respawn: ready to fire again with a full magazine and reserve - no cooldown or
        /// reload left running. The part keeps its identity and socket.
        /// </summary>
        public void Rearm()
        {
            readyAt = readyStartedAt = reloadEndsAt = 0f;
            RemainingAmmo = UsesAmmo ? Math.Max(0, Def.Ammo) : 0;
            MagazineRounds = Def.Magazine > 0 ? Math.Min(Def.Magazine, RemainingAmmo) : RemainingAmmo;
        }

        /// <summary>"[in the magazine / left in total]", or N/A without ammo. Unity: WeaponsHudPod.AmmoText.</summary>
        public string AmmoText { get { return UsesAmmo ? MagazineRounds + " / " + RemainingAmmo : "N/A"; } }
    }

    /// <summary>A head / torso / legs part's ability cooldown. Unity: PartAbilityCooldown.</summary>
    public sealed class MechAbility
    {
        public readonly MechPartDef Def;
        public MechSocket Socket;
        public string Name { get { return Def.Name; } }
        public string KeyLabel { get { return KeyLabelFor(Socket); } }
        public float Cooldown { get { return Math.Max(0f, Def.AbilityCooldown); } }
        public MechAbilityKind Kind { get { return Def.Ability; } }
        public float Power { get { return Math.Max(0f, Def.AbilityPower); } }
        float readyAt, duration, activeUntil;

        public MechAbility(MechPartDef def, MechSocket socket) { Def = def; Socket = socket; }

        public static string KeyLabelFor(MechSocket socket)
        {
            return socket == MechSocket.Head ? "Tab" : socket == MechSocket.Torso ? "Ctrl" : socket == MechSocket.Legs ? "Spc" : string.Empty;
        }

        public float Remaining(float now) { return Math.Max(0f, readyAt - now); }
        public bool IsCoolingDown(float now) { return Remaining(now) > 0f; }
        public float Progress(float now) { return duration > 0f ? MechMath.Clamp01(1f - Remaining(now) / duration) : 1f; }

        /// <summary>Its effect is running (Heal, Damage Amp). Always false for an instant ability.</summary>
        public bool IsActive(float now) { return now < activeUntil; }

        /// <summary>
        /// Starts the effect window and the cooldown together, as Unity does (the cooldown runs
        /// while the effect lasts). Refused while either is still running.
        /// </summary>
        public bool ConfirmActivation(float now)
        {
            if (IsCoolingDown(now) || IsActive(now)) return false;
            activeUntil = now + Math.Max(0f, Def.AbilityDuration);
            duration = Cooldown;
            readyAt = now + Cooldown;
            return true;
        }

        /// <summary>Zoo respawn: ends a running effect (Heal, Damage Amp) and clears the cooldown.</summary>
        public void Reset()
        {
            readyAt = duration = activeUntil = 0f;
        }
    }

    /// <summary>Socket groups, in HUD order. The starting parts are MechPlayer's start* fields.</summary>
    public static class MechLoadout
    {
        public static readonly MechSocket[] WeaponSockets = { MechSocket.ShoulderL, MechSocket.ShoulderR, MechSocket.ArmL, MechSocket.ArmR };
        public static readonly MechSocket[] AbilitySockets = { MechSocket.Head, MechSocket.Torso, MechSocket.Legs };
    }
}
