/*************************************************************************
* \file:         NetFields.cs
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Synced fields for a NetScript: NetInt, NetFloat, NetBool,
*                NetVector3, NetEntityRef and NetTimer.
*                Part of Crucible.ScriptCore.dll.
*
* \details
*    Each one stores its value in the entity's network variables (slots 0
*    to 63, Entity.NetVars): every machine reads it, the authority (the
*    Host, or single player) writes it - a Client's write is refused and
*    logged, like NetVars. Changed(old, new) fires on every machine.
*
*    NetTimer stores when it ends, as a tick of the shared clock
*    (Network.Tick), so every machine counts down from the same moment
*    without any further message:
*
*        readonly NetTimer cooldown = new NetTimer(slot: 5);
*        if (Network.IsAuthority && !cooldown.IsRunning) { Fire(); cooldown.Start(3f); }
*        text = cooldown.Remaining.ToString("0.0");
*
* \history
*    -v1.0       NetField, NetInt, NetFloat, NetBool, NetVector3, NetEntityRef, NetTimer
*
**************************************************************************/
using System;

namespace Crucible
{
    /// <summary>A NetScript field kept in step on every machine (see NetScript).</summary>
    public abstract class NetField
    {
        private NetScript owner;

        /// <summary>The first slot it uses.</summary>
        public int Slot { get; private set; }
        /// <summary>How many slots it uses.</summary>
        public int SlotCount { get; private set; }
        /// <summary>Its field name, once bound.</summary>
        public string Name { get; private set; }

        internal bool Valid { get; private set; }

        protected NetField(int slot, int slotCount)
        {
            Slot = slot;
            SlotCount = slotCount;
            Valid = slot >= 0 && slot + slotCount <= NetVars.SlotCount;
            if (!Valid)
                Log.Error(GetType().Name + ": slot " + slot + " is outside 0 to " + (NetVars.SlotCount - slotCount));
        }

        internal ulong Mask
        {
            get { ulong bits = SlotCount >= 64 ? ulong.MaxValue : (1ul << SlotCount) - 1ul; return bits << Slot; }
        }

        internal bool Covers(int slot) { return slot >= Slot && slot < Slot + SlotCount; }

        internal void Bind(NetScript script, string name)
        {
            owner = script;
            Name = name;
        }

        /// <summary>The entity's variables, or null before the script is bound.</summary>
        protected NetVars Vars
        {
            get
            {
                if (ReferenceEquals(owner, null) || ReferenceEquals(owner.Entity, null))
                {
                    Log.Error(GetType().Name + " (slot " + Slot + "): used before its NetScript was created - use it from OnCreate on");
                    return null;
                }
                return owner.Entity.NetVars;
            }
        }

        internal abstract void ApplyInitial();   // the authority, at creation
        internal abstract void Capture();        // remember the current value, no event
        internal abstract void Refresh();        // a slot changed: raise Changed if the value differs
    }

    /// <summary>An int in one slot.</summary>
    public sealed class NetInt : NetField
    {
        private readonly int initial;
        private int last;

        public event Action<int, int> Changed;

        public NetInt(int slot, int initial = 0) : base(slot, 1) { this.initial = initial; }

        public int Value
        {
            get { NetVars v = Vars; return v != null ? v.GetInt(Slot) : 0; }
            set { NetVars v = Vars; if (v != null) v.SetInt(Slot, value); }
        }

        public static implicit operator int(NetInt field) { return ReferenceEquals(field, null) ? 0 : field.Value; }

        internal override void ApplyInitial() { if (initial != 0) Value = initial; }
        internal override void Capture() { last = Value; }
        internal override void Refresh()
        {
            int now = Value;
            if (now == last) return;
            int old = last;
            last = now;
            if (Changed != null) Changed(old, now);
        }

        public override string ToString() { return Value.ToString(); }
    }

    /// <summary>A float in one slot. NaN and infinity are refused.</summary>
    public sealed class NetFloat : NetField
    {
        private readonly float initial;
        private float last;

        public event Action<float, float> Changed;

        public NetFloat(int slot, float initial = 0f) : base(slot, 1) { this.initial = initial; }

        public float Value
        {
            get { NetVars v = Vars; return v != null ? v.GetFloat(Slot) : 0f; }
            set { NetVars v = Vars; if (v != null) v.SetFloat(Slot, value); }
        }

        public static implicit operator float(NetFloat field) { return ReferenceEquals(field, null) ? 0f : field.Value; }

        internal override void ApplyInitial() { if (initial != 0f) Value = initial; }
        internal override void Capture() { last = Value; }
        internal override void Refresh()
        {
            float now = Value;
            if (now.Equals(last)) return;
            float old = last;
            last = now;
            if (Changed != null) Changed(old, now);
        }

        public override string ToString() { return Value.ToString(); }
    }

    /// <summary>A bool in one slot.</summary>
    public sealed class NetBool : NetField
    {
        private readonly bool initial;
        private bool last;

        public event Action<bool, bool> Changed;

        public NetBool(int slot, bool initial = false) : base(slot, 1) { this.initial = initial; }

        public bool Value
        {
            get { NetVars v = Vars; return v != null && v.GetBool(Slot); }
            set { NetVars v = Vars; if (v != null) v.SetBool(Slot, value); }
        }

        public static implicit operator bool(NetBool field) { return !ReferenceEquals(field, null) && field.Value; }

        internal override void ApplyInitial() { if (initial) Value = true; }
        internal override void Capture() { last = Value; }
        internal override void Refresh()
        {
            bool now = Value;
            if (now == last) return;
            bool old = last;
            last = now;
            if (Changed != null) Changed(old, now);
        }

        public override string ToString() { return Value.ToString(); }
    }

    /// <summary>A Vector3 in three slots in a row (slot, slot + 1, slot + 2).</summary>
    public sealed class NetVector3 : NetField
    {
        private readonly Vector3 initial;
        private Vector3 last;

        public event Action<Vector3, Vector3> Changed;

        public NetVector3(int slot) : base(slot, 3) { }
        public NetVector3(int slot, Vector3 initial) : base(slot, 3) { this.initial = initial; }

        public Vector3 Value
        {
            get { NetVars v = Vars; return v != null ? v.GetVector3(Slot) : new Vector3(0f, 0f, 0f); }
            set { NetVars v = Vars; if (v != null) v.SetVector3(Slot, value); }
        }

        internal override void ApplyInitial() { if (initial.X != 0f || initial.Y != 0f || initial.Z != 0f) Value = initial; }
        internal override void Capture() { last = Value; }
        internal override void Refresh()
        {
            Vector3 now = Value;   // all three slots arrive in one record: one event, not three
            if (now.X == last.X && now.Y == last.Y && now.Z == last.Z) return;
            Vector3 old = last;
            last = now;
            if (Changed != null) Changed(old, now);
        }

        public override string ToString() { Vector3 v = Value; return "(" + v.X + ", " + v.Y + ", " + v.Z + ")"; }
    }

    /// <summary>A reference to a network object in one slot (null: none).</summary>
    public sealed class NetEntityRef : NetField
    {
        private uint last;

        public event Action<Entity, Entity> Changed;

        public NetEntityRef(int slot) : base(slot, 1) { }

        public Entity Value
        {
            get { NetVars v = Vars; return v != null ? v.GetEntity(Slot) : null; }
            set { NetVars v = Vars; if (v != null) v.SetEntity(Slot, value); }
        }

        private uint Raw { get { NetVars v = Vars; return v != null ? unchecked((uint)v.GetInt(Slot)) : 0u; } }

        internal override void ApplyInitial() { }
        internal override void Capture() { last = Raw; }
        internal override void Refresh()
        {
            uint now = Raw;
            if (now == last) return;
            uint old = last;
            last = now;
            if (Changed != null) Changed(Network.FindEntity(old), Network.FindEntity(now));
        }
    }

    /// <summary>
    /// A countdown in one slot: the tick of the shared clock (Network.Tick) it ends at, 0 when
    /// stopped. The authority starts and stops it; every machine reads Remaining.
    /// </summary>
    public sealed class NetTimer : NetField
    {
        private int last;
        private int finishedEnd;   // this machine: the end JustFinished already reported

        /// <summary>Started, restarted or stopped (on every machine).</summary>
        public event Action Changed;

        public NetTimer(int slot) : base(slot, 1) { }

        /// <summary>The shared-clock tick it ends at; 0 when stopped.</summary>
        public int EndTick { get { NetVars v = Vars; return v != null ? v.GetInt(Slot) : 0; } }

        /// <summary>Seconds left (0 when stopped or over).</summary>
        public double Remaining { get { return RemainingAt(EndTick, Network.Time); } }

        /// <summary>Running and not over yet.</summary>
        public bool IsRunning { get { return Remaining > 0.0; } }

        /// <summary>The authority: runs for <paramref name="seconds"/> from now (restarts it if running).</summary>
        public bool Start(double seconds)
        {
            NetVars v = Vars;
            if (v == null) return false;
            int ticks = Math.Max(1, (int)Math.Ceiling(seconds * Network.TickRate));
            return v.SetInt(Slot, Network.Tick + ticks);
        }

        /// <summary>The authority: stops it (Remaining 0, JustFinished never fires for it).</summary>
        public bool Stop()
        {
            NetVars v = Vars;
            return v != null && v.SetInt(Slot, 0);
        }

        /// <summary>
        /// True once on this machine when the timer is over: the first call after it ends.
        /// Call it every frame (e.g. in OnUpdate) where you react to the end.
        /// </summary>
        public bool JustFinished()
        {
            int end = EndTick;
            if (end == 0 || end == finishedEnd || Remaining > 0.0) return false;
            finishedEnd = end;
            return true;
        }

        /// <summary>Seconds left of a timer ending at <paramref name="endTick"/>, at shared time <paramref name="now"/>.</summary>
        public static double RemainingAt(int endTick, double now)
        {
            if (endTick == 0) return 0.0;
            double left = endTick / (double)Network.TickRate - now;
            return left > 0.0 ? left : 0.0;
        }

        internal override void ApplyInitial() { }
        internal override void Capture() { last = EndTick; finishedEnd = Remaining > 0.0 ? 0 : last; }
        internal override void Refresh()
        {
            int now = EndTick;
            if (now == last) return;
            last = now;
            if (Changed != null) Changed();
        }

        public override string ToString() { return Remaining.ToString("0.0"); }
    }
}
