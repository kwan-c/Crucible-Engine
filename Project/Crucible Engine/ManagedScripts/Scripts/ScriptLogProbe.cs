namespace Crucible.Scripts
{
    // Attach through Add Component, then watch the Editor Console while playing.
    public sealed class ScriptLogProbe : global::Crucible.EntityScript
    {
        public bool logMessages = true;
        public float messageIntervalSeconds = 5.0f;
        public string message = "Hello from C#";

        private double elapsed;
        private double sinceLastMessage;
        private int intervals;
        private bool loggedFirstUpdate;

        public override void OnCreate()
        {
            elapsed = 0;
            sinceLastMessage = 0;
            intervals = 0;
            loggedFirstUpdate = false;
            global::Crucible.Log.Info("[ScriptLogProbe] OnCreate: entity " + this.Entity +
                ", Log Messages=" + logMessages + ", Interval=" + messageIntervalSeconds + ", Message=" + message);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (!loggedFirstUpdate)
            {
                global::Crucible.Log.Info("[ScriptLogProbe] OnUpdate: first frame for entity " + this.Entity);
                loggedFirstUpdate = true;
            }

            elapsed += deltaTime;
            if (!logMessages) { sinceLastMessage = 0; return; }
            sinceLastMessage += deltaTime;
            // Inspector edits can change the interval live; guard zero/non-finite input.
            double interval = float.IsNaN(messageIntervalSeconds) || float.IsInfinity(messageIntervalSeconds)
                ? 5.0 : global::System.Math.Max(0.1, messageIntervalSeconds);
            while (sinceLastMessage >= interval)
            {
                sinceLastMessage -= interval;
                ++intervals;
                global::Crucible.Log.Info("[ScriptLogProbe] OnUpdate: " + message +
                    " | Play time=" + elapsed.ToString("F2") + "s, entity " + this.Entity);
            }
        }

        public override void OnDestroy()
        {
            global::Crucible.Log.Info("[ScriptLogProbe] OnDestroy: entity " + this.Entity +
                ", periodic messages sent: " + intervals);
        }
    }
}
