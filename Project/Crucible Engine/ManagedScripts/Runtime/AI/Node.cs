using System.Collections.Generic;

namespace Crucible.AI
{
    /// <summary>A behaviour tree node (Unity's Node): OnStart, OnUpdate, OnEnd.</summary>
    /// <remarks>
    /// OnStart runs once when the node begins; if it returns Running, OnUpdate runs the same
    /// frame and then every frame until it returns Success or Failure; OnEnd runs once when it
    /// finishes or is cut short. Derive from Action (a leaf), Modifier (one child) or Composite
    /// (many children) and add a [NodeDescription] - WaitAction is the shortest example. Public
    /// fields (float, int, bool, string, an enum, Vector3, Entity, BehaviorGraphAsset, or a
    /// BlackboardVariable of those) show in the graph window. Every enemy gets its own copy of
    /// every node.
    /// </remarks>
    public abstract class Node
    {
        /// <summary>What a step of a node reports. Numbers are shared with the engine: don't change them.</summary>
        public enum Status { Running = 0, Success = 1, Failure = 2 }

        /// <summary>The enemy running this tree (Unity's GameObject).</summary>
        public Entity Self { get; internal set; }

        /// <summary>
        /// Seconds this update covers. 0 for a node a Sequence started mid-frame,
        /// after an earlier child finished - that frame's time was already used.
        /// </summary>
        protected float DeltaTime { get; private set; }

        /// <summary>True between OnStart and OnEnd.</summary>
        public bool IsRunning { get; private set; }

        /// <summary>What its last step reported.</summary>
        public Status CurrentStatus { get; private set; } = Status.Running;

        // The three steps a node writes. The defaults: start running, finish on the first update.
        protected virtual Status OnStart() { return Status.Running; }
        protected virtual Status OnUpdate() { return Status.Success; }
        protected virtual void OnEnd() {}

        /// <summary>
        /// Runs the node for one frame: starts it if it isn't running, updates it,
        /// and ends it when it finishes. Called by its parent (and the engine, for the root).
        /// </summary>
        public Status Tick(float deltaTime)
        {
            DeltaTime = deltaTime;
            if (!IsRunning)
            {
                // A fresh run: start it, and if it didn't finish on the spot,
                // give it its first update this same frame.
                IsRunning = true;
                CurrentStatus = OnStart();
                if (CurrentStatus == Status.Running) CurrentStatus = OnUpdate();
            }
            else
            {
                CurrentStatus = OnUpdate();
            }

            // Finished either way: end the run, so the next Tick starts a new one.
            if (CurrentStatus != Status.Running)
            {
                IsRunning = false;
                OnEnd();
            }
            return CurrentStatus;
        }

        /// <summary>
        /// Cuts the node short (the state graph left its state): its running children
        /// first - they started after it, so they end before it - then its own OnEnd.
        /// </summary>
        public void Abort()
        {
            if (!IsRunning) return;
            AbortChildren();
            IsRunning = false;
            CurrentStatus = Status.Failure;   // cut short counts as not having worked
            OnEnd();
        }

        // --- the tree's shape: used by the engine when it builds a tree ---------- //

        internal virtual int MaxChildren { get { return 0; } }
        internal virtual bool AddChild(Node child) { return false; }
        internal virtual void AbortChildren() {}
        internal virtual void CollectChildren(List<Node> into) {}
    }

    /// <summary>A leaf node that does something: Wait, Move To, Log (Unity's Action).</summary>
    public abstract class Action : Node {}

    /// <summary>A node with one child that changes how it runs: On Start (Unity's Modifier).</summary>
    public abstract class Modifier : Node
    {
        /// <summary>Its child, or null if nothing is connected under it.</summary>
        protected Node Child { get; private set; }

        internal override int MaxChildren { get { return 1; } }

        internal override bool AddChild(Node child)
        {
            if (Child != null || child == null) return false;   // one child only
            Child = child;
            return true;
        }

        internal override void AbortChildren() { if (Child != null) Child.Abort(); }
        internal override void CollectChildren(List<Node> into) { if (Child != null) into.Add(Child); }
    }

    /// <summary>A node with many children that decides their order: Sequence (Unity's Composite).</summary>
    public abstract class Composite : Node
    {
        private readonly List<Node> children = new List<Node>();

        /// <summary>Its children, left to right on the canvas: the order they run in.</summary>
        protected IReadOnlyList<Node> Children { get { return children; } }

        internal override int MaxChildren { get { return int.MaxValue; } }

        internal override bool AddChild(Node child)
        {
            if (child == null) return false;
            children.Add(child);
            return true;
        }

        internal override void AbortChildren() { foreach (Node child in children) child.Abort(); }
        internal override void CollectChildren(List<Node> into) { into.AddRange(children); }
    }
}
