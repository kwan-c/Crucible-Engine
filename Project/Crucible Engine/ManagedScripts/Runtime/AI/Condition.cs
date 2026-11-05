namespace Crucible.AI
{
    /// <summary>One check on a state graph arrow (Unity's Condition). Override IsTrue.</summary>
    public abstract class Condition
    {
        /// <summary>The enemy whose arrow this is.</summary>
        public Entity Self { get; internal set; }

        /// <summary>Seconds since the enemy entered its current state.</summary>
        public float TimeInState { get; internal set; }

        /// <summary>True once the state's tree or node has finished at least once since it was entered.</summary>
        public bool StateFinished { get; internal set; }

        /// <summary>How it last finished (Success or Failure), if StateFinished.</summary>
        public Node.Status StateFinishedWith { get; internal set; }

        /// <summary>Does the check pass right now?</summary>
        public abstract bool IsTrue();
    }
}
