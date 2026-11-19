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

    public enum MechAttackType { None, Melee, Projectile, Splash }

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
        public float AbilityCooldown;   // head / torso / legs
        public MechAbilityKind Ability; // None = the part has no ability (no HUD pod, its key does nothing)
        public float AbilityPower;      // meaning depends on Ability (see MechAbilityKind)
        public float AbilityDuration;   // seconds; 0 = instant (Jump)

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
            new MechPartDef { Id = "large_crossbow", Name = "LARGE CROSSBOW", Kind = MechPartKind.Shoulder, Attack = MechAttackType.Splash,
                              Damage = 100f, SplashDamage = 10f, SplashRadius = .5f, Ammo = 30, Magazine = 1, FireRate = 1f, Reload = 4f,
                              RangeGolems = 20f, Speed = -5f, ProjectileSpeed = 160f, ProjectileSize = .07f },
            new MechPartDef { Id = "cannon", Name = "CANNON", Kind = MechPartKind.Shoulder, Attack = MechAttackType.Splash,
                              Damage = 100f, SplashDamage = 50f, SplashRadius = 3f, Ammo = 30, Magazine = 1, FireRate = 1f, Reload = 3f,
                              RangeGolems = 10f, Speed = -4f, ProjectileSpeed = 100f, ProjectileSize = .3f },
            // Abilities from Unity main's PartData_Head / _Torso / _Legs_Fast. Golem Legs and Heavy Legs
            // have a cooldown stat there but no ability, so Space does nothing with them.
            new MechPartDef { Id = "golem_head", Name = "GOLEM HEAD", Kind = MechPartKind.Head, Health = 50f, AbilityCooldown = 10f,
                              Ability = MechAbilityKind.Heal, AbilityPower = 5f, AbilityDuration = 10f },
            new MechPartDef { Id = "golem_torso", Name = "GOLEM TORSO", Kind = MechPartKind.Torso, Health = 150f, AbilityCooldown = 30f,
                              Ability = MechAbilityKind.DamageAmp, AbilityPower = 50f, AbilityDuration = 20f },
            new MechPartDef { Id = "golem_legs", Name = "GOLEM LEGS", Kind = MechPartKind.Legs, Health = 50f, AbilityCooldown = 6f },
            new MechPartDef { Id = "fast_legs", Name = "FAST LEGS", Kind = MechPartKind.Legs, Health = 50f, Speed = 10f, AbilityCooldown = 4f,
                              Ability = MechAbilityKind.Jump, AbilityPower = 2f },
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

    /// <summary>One physical weapon part's live state. Unity: Part (ammo, ReadyAt, reload).</summary>
    public sealed class MechWeapon
    {
        public readonly MechPartDef Def;
        public MechSocket Socket;               // set when equipped
        public string Name { get { return Def.Name; } }
        public string KeyLabel { get { return KeyLabelFor(Socket); } }
        public bool UsesAmmo { get { return Def.Ammo > 0 || Def.Magazine > 0; } }
        public float ShotInterval { get { return Def.FireRate > 0f ? 1f / Def.FireRate : 1f; } }
        public bool RepeatWhileHeld { get { return Socket == MechSocket.ArmL || Socket == MechSocket.ArmR; } }
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
            if (IsCoolingDown(now) || IsEmpty) return false;
            readyStartedAt = now;
            readyAt = now + ShotInterval;
            if (!UsesAmmo) return true;
            RemainingAmmo = Math.Max(0, RemainingAmmo - 1);
            MagazineRounds = Math.Max(0, MagazineRounds - 1);
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
    }

    /// <summary>Socket groups, in HUD order. The starting parts are MechPlayer's start* fields.</summary>
    public static class MechLoadout
    {
        public static readonly MechSocket[] WeaponSockets = { MechSocket.ShoulderL, MechSocket.ShoulderR, MechSocket.ArmL, MechSocket.ArmR };
        public static readonly MechSocket[] AbilitySockets = { MechSocket.Head, MechSocket.Torso, MechSocket.Legs };
    }
}
