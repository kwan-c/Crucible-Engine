/*************************************************************************
* \file:         MechInputClaims.cs
* \brief         Lets a world interaction (a Zoo floor pad) claim a key while
*                the player stands on it, so the same press does not also
*                drive a gameplay action; the pad wins while occupied. The Zoo
*                pads use G, so F stays the aim / lock-on mode key; the claim
*                still guards whatever key a pad is given.
*
* \details
*    A claim names its owner script. A claim whose owner was destroyed or
*    switched off is ignored and pruned, so a scene change can never leave
*    a key blocked.
*
*        MechInputClaims.Claim(this, KeyCode.F);     // player stepped on
*        MechInputClaims.Release(this, KeyCode.F);   // player stepped off
*        if (!MechInputClaims.IsClaimed(KeyCode.F)) ...
*
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.Scripts
{
    public static class MechInputClaims
    {
        sealed class Entry { public EntityScript Owner; public KeyCode Key; }
        static readonly List<Entry> claims = new List<Entry>();

        public static void Claim(EntityScript owner, KeyCode key)
        {
            if (owner == null || IsClaimedBy(owner, key)) return;
            claims.Add(new Entry { Owner = owner, Key = key });
        }

        public static void Release(EntityScript owner, KeyCode key)
        {
            for (int i = claims.Count - 1; i >= 0; i--)
                if (claims[i].Owner == owner && claims[i].Key == key) claims.RemoveAt(i);
        }

        /// <summary>A live, switched-on owner currently claims this key.</summary>
        public static bool IsClaimed(KeyCode key)
        {
            for (int i = claims.Count - 1; i >= 0; i--)
            {
                Entry claim = claims[i];
                Entity entity = claim.Owner != null ? claim.Owner.Entity : null;
                if (entity == null || !entity.IsAlive || !entity.IsActive) { claims.RemoveAt(i); continue; }
                if (claim.Key == key) return true;
            }
            return false;
        }

        static bool IsClaimedBy(EntityScript owner, KeyCode key)
        {
            foreach (Entry claim in claims) if (claim.Owner == owner && claim.Key == key) return true;
            return false;
        }
    }
}
