using System;

namespace Crucible.AI
{
    /// <summary>The part of every BlackboardVariable that doesn't depend on T (for the engine).</summary>
    public abstract class BlackboardVariable
    {
        // The enemy whose variables a link reads. Set by the engine when it builds the tree.
        internal Entity Owner;

        /// <summary>The graph variable it's linked to, or null if it holds its own value.</summary>
        public string LinkedVariable { get; internal set; }

        /// <summary>True if it reads a graph variable instead of its own value.</summary>
        public bool IsLinked { get { return !string.IsNullOrEmpty(LinkedVariable); } }

        internal abstract Type ValueType { get; }
        internal abstract object BoxedValue { get; set; }   // its OWN value, linked or not
    }

    /// <summary>A node field holding a value or linked to a graph variable (Unity's BlackboardVariable).</summary>
    public sealed class BlackboardVariable<T> : BlackboardVariable
    {
        private T value;

        public BlackboardVariable() {}
        public BlackboardVariable(T value) { this.value = value; }

        /// <summary>One that starts linked to the graph variable <paramref name="variable"/>.</summary>
        public static BlackboardVariable<T> Link(string variable)
        {
            return new BlackboardVariable<T> { LinkedVariable = variable };
        }

        /// <summary>
        /// The value: its own, or the linked variable's. A link to a variable the graph doesn't
        /// have (or of another type) reads as default(T), and the engine warns once.
        /// </summary>
        public T Value
        {
            get
            {
                if (!IsLinked) return value;
                if (ReferenceEquals(Owner, null)) return default(T);
                T read;
                return Owner.StateGraphAgent.GetVariableValue(LinkedVariable, out read) ? read : default(T);
            }
            set
            {
                if (!IsLinked) { this.value = value; return; }
                if (!ReferenceEquals(Owner, null)) Owner.StateGraphAgent.SetVariableValue(LinkedVariable, value);
            }
        }

        public static implicit operator T(BlackboardVariable<T> variable) { return variable != null ? variable.Value : default(T); }

        internal override Type ValueType { get { return typeof(T); } }
        internal override object BoxedValue { get { return value; } set { this.value = (T)value; } }

        public override string ToString() { return IsLinked ? "-> " + LinkedVariable : (value != null ? value.ToString() : "null"); }
    }
}
