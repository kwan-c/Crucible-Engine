using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    // A ticket exists only while an eligible graph keeps requesting. A grant and the
    // beginning of the attack are synchronous. Start spacing survives an early release.
    internal sealed class EnemyAttackReservations
    {
        sealed class Entry
        {
            public Enemy Owner;
            public Entity Target;
            public Vector3 Start;
            public long Ticket;
            public float LastRequest, SpacedUntil;
            public bool Active;
        }

        readonly List<Entry> entries = new List<Entry>();
        long nextTicket;
        readonly Func<Enemy, bool> participates;
        public EnemyAttackReservations(Func<Enemy, bool> participates) { this.participates = participates; }

        static bool Near(Vector3 a, Vector3 b, float radius)
        {
            float x = a.X - b.X, z = a.Z - b.Z;
            return x*x + z*z <= radius*radius;
        }

        internal void Prune(float now)
        {
            for (int i = entries.Count - 1; i >= 0; i--)
            {
                Entry e = entries[i];
                if (e.Target == null || !e.Target.IsActive || !e.Target.IsAlive) { entries.RemoveAt(i); continue; }
                bool live = e.Owner.IsReady && e.Owner.Entity.IsEnabled<StateGraphAgent>() && participates(e.Owner)
                    && e.Owner.Target == e.Target;
                if (!live || !e.Owner.AttackInProgress) e.Active = false;
                if (!live || now - e.LastRequest > .65f) e.Ticket = 0;
                if (!e.Active && e.Ticket == 0 && now >= e.SpacedUntil) entries.RemoveAt(i);
            }
        }

        public bool TryAcquire(Enemy owner, int limit, float spacing, float radius)
        {
            float now = Time.ElapsedTime;
            Prune(now);
            Entry mine = null;
            int active = 0;
            long first = long.MaxValue;
            bool spaced = true;
            Vector3 here = owner.Position;
            foreach (Entry e in entries)
            {
                if (e.Target != owner.Target) continue;
                if (now < e.SpacedUntil && Near(e.Start, here, radius)) spaced = false;
                if (e.Owner == owner) { mine = e; continue; }
                if ((!e.Active && e.Ticket == 0) || !Near(e.Owner.Position, here, radius)) continue;
                if (e.Active) active++;
                if (e.Ticket > 0) first = Math.Min(first, e.Ticket);
            }
            if (mine == null) { mine = new Entry { Owner = owner, Target = owner.Target }; entries.Add(mine); }
            if (mine.Ticket == 0) mine.Ticket = ++nextTicket;
            mine.LastRequest = now;
            if (!spaced || active >= Math.Max(1, limit) || first < mine.Ticket) return false;
            mine.Active = true;
            mine.Ticket = 0;
            mine.Start = here;
            mine.SpacedUntil = now + Math.Max(0f, spacing);
            return true;
        }

        public bool AtCapacity(Enemy owner, int limit)
        {
            int count = 0;
            foreach (Entry e in entries)
                if (e.Active && e.Owner != owner && e.Target == owner.Target && ++count >= Math.Max(1, limit)) return true;
            return false;
        }

        public void Release(Enemy owner)
        {
            foreach (Entry e in entries)
                if (e.Owner == owner) { e.Active = false; e.Ticket = 0; }
        }

        public void Clear() { entries.Clear(); }
    }
}
