using System.Collections.Generic;

namespace Crucible.AI
{
    /// <summary>
    /// Runs another behaviour graph in this spot, as if its tree were drawn here (Unity's Run
    /// Subgraph). It finishes when that tree finishes, with its result.
    /// </summary>
    /// <remarks>
    /// An Action in the editor (no children to connect), but at Play the engine builds the
    /// chosen graph's tree and hands it over as a hidden child - minus its On Start, which
    /// would repeat forever and never let this node finish.
    /// </remarks>
    [NodeDescription(
        name: "Run Subgraph",
        description: "Runs another behaviour graph (.behaviorgraph) here and finishes when it does.",
        story: "Run [Graph]",
        category: "Flow",
        id: "run_subgraph")]
    public sealed class RunSubgraphNode : Action
    {
        public BehaviorGraphAsset Graph = new BehaviorGraphAsset();

        private Node inner;   // the subgraph's tree, added by the engine

        protected override Status OnStart()
        {
            return inner == null ? Status.Failure : Status.Running;   // no graph chosen, or it couldn't be built
        }

        protected override Status OnUpdate()
        {
            return inner.Tick(DeltaTime);
        }

        // The hidden child, from the engine. Not a Modifier on purpose: the editor
        // must not offer an output dot to connect children to.
        internal override int MaxChildren { get { return 1; } }

        internal override bool AddChild(Node child)
        {
            if (inner != null || child == null) return false;
            inner = child;
            return true;
        }

        internal override void AbortChildren() { if (inner != null) inner.Abort(); }
        internal override void CollectChildren(List<Node> into) { if (inner != null) into.Add(inner); }
    }
}
