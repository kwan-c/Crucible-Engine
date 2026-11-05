namespace Crucible.AI
{
    /// <summary>Base of the movement nodes: Agent, and "stop when this node ends".</summary>
    public abstract class NavigationAction : Action
    {
        private FlowFieldAgent agent;
        private bool warned;

        /// <summary>The enemy's FlowFieldAgent, or null (warned once) if it has none.</summary>
        protected FlowFieldAgent Agent
        {
            get
            {
                if (agent != null || Self == null) return agent;
                agent = Self.GetComponent<FlowFieldAgent>();
                if (agent == null && !warned)
                {
                    Log.Warning("Behavior: '" + GetType().Name + "' on entity " + Self.Index + " needs a FlowFieldAgent to move it - add one");
                    warned = true;
                }
                return agent;
            }
        }

        /// <summary>Is <paramref name="entity"/> set and still in the scene?</summary>
        protected static bool IsThere(Entity entity) { return !ReferenceEquals(entity, null) && entity.IsAlive; }

        protected override void OnEnd()
        {
            // Finished or cut short, the order is over: don't leave the enemy
            // walking somewhere nobody wants it to go any more.
            if (agent != null) agent.Stop();
        }
    }
}
