using System;

namespace Crucible.Scripts
{
    // Tests the C# side of the AI: StateGraphAgent (brain variables), FlowFieldAgent
    // (movement orders) and FlowFieldSurface. Everything it does is logged with
    // [AITest], and OnCreate logs what you should expect to see.
    //
    //   1. Attach to an enemy with a FlowFieldAgent. For the brain part, it also needs
    //      a StateGraphAgent using PatrolChaseTest.stategraph (or any graph with a
    //      Vector3 variable "PatrolA"). The scene needs a player named "Player"
    //      (Hierarchy > + > Create Player), or drag one onto Target.
    //      Optional: drag the ground (the FlowFieldSurface entity) onto Ground.
    //   2. Press Play, then:
    //        1   Stop()
    //        2   MoveTo(a point 10 m in front of where it spawned)
    //        3   Arrive(Target)
    //        4   FollowField()
    //        K   halve Speed / put it back
    //        L   log its status: state, HasArrived, RemainingDistance, Velocity, Speed, Target
    //        G   Ground's FloodRadius: 0 (whole surface) / back to what it was
    //   The movement keys fight the state graph if one is running (it gives its own
    //   orders every few seconds). To test movement alone, clear the StateGraphAgent's
    //   graph in the Inspector, or use an enemy without one.
    public sealed class StateGraphAgentTest : EntityScript
    {
        public Entity target;   // empty = the entity named "Player"
        public Entity ground;   // optional: the FlowFieldSurface entity, for G

        FlowFieldAgent agent;
        StateGraphAgent brain;
        FlowFieldSurface surface;
        Vector3 spawn;
        string lastState = "";
        bool lastArrived;
        bool slow;
        float normalSpeed;
        float normalFlood;
        bool flooded;
        int frames;

        public override void OnCreate()
        {
            agent = GetComponent<FlowFieldAgent>();
            brain = GetComponent<StateGraphAgent>();
            surface = ground != null ? ground.GetComponent<FlowFieldSurface>() : null;
            if (target == null) target = Entity.Find("Player");
            spawn = Transform.Position;

            Log.Info("[AITest] Expect: (a) 'PatrolA set: True', then 'PatrolA reads back' as this enemy's spawn + (8, 0, 0) - " +
                     "so it patrols to a point 8 m right of where it starts; (b) 'Misspelled set: False' and ONE engine warning about " +
                     "\"PatrolAA\" (not one per frame; only on the first Play since the Editor started); (c) 'Wrong type set: False' " +
                     "and one warning that PatrolA holds a different type; (d) a line each time the state changes; (e) keys 1-4 change " +
                     "how it moves, and 'HasArrived -> True' when it gets there.");

            if (agent == null) Log.Info("[AITest] No FlowFieldAgent on " + Entity + ": keys 1-4, K and L won't do anything.");
            if (target == null) Log.Info("[AITest] No Target and no entity named \"Player\": key 3 will make it stand still.");
            if (ground != null && surface == null) Log.Info("[AITest] Ground has no FlowFieldSurface: G won't do anything.");

            if (brain == null)
            {
                Log.Info("[AITest] No StateGraphAgent on " + Entity + ": skipping the variable tests.");
                return;
            }

            // (a) Set from OnCreate - before the graph's first frame - and read it back.
            Vector3 patrolA = spawn + new Vector3(8f, 0f, 0f);
            Log.Info("[AITest] PatrolA set: " + brain.SetVariableValue("PatrolA", patrolA));
            Vector3 readBack;
            if (brain.GetVariableValue("PatrolA", out readBack))
                Log.Info("[AITest] PatrolA reads back " + readBack + " (expected " + patrolA + ")");
            else
                Log.Info("[AITest] PatrolA could not be read back - does the graph have a Vector3 variable \"PatrolA\"?");

            // (c) Right name, wrong type: the float 1 into a Vector3 variable.
            Log.Info("[AITest] Wrong type set: " + brain.SetVariableValue("PatrolA", 1f));
        }

        public override void OnUpdate(float deltaTime)
        {
            ++frames;

            if (brain != null)
            {
                // (b) A typo, every frame for the first 2 seconds or so: the engine must warn once, not 120 times.
                if (frames <= 120)
                {
                    bool set = brain.SetVariableValue("PatrolAA", 1f);
                    if (frames == 1) Log.Info("[AITest] Misspelled set: " + set + " (repeating it for 120 frames)");
                }

                // (d) The state it's in.
                string state = brain.CurrentState;
                if (state != lastState)
                {
                    Log.Info("[AITest] State: \"" + lastState + "\" -> \"" + state + "\"");
                    lastState = state;
                }
            }

            if (agent == null)
                return;

            // (e) Orders.
            if (Input.IsPressed(KeyCode.Num1)) { agent.Stop(); Log.Info("[AITest] Stop()"); }
            if (Input.IsPressed(KeyCode.Num2))
            {
                Vector3 point = spawn + new Vector3(0f, 0f, 10f);
                agent.MoveTo(point);
                Log.Info("[AITest] MoveTo(" + point + ")");
            }
            if (Input.IsPressed(KeyCode.Num3)) { agent.Arrive(target); Log.Info("[AITest] Arrive(" + (target != null ? target.ToString() : "null") + ")"); }
            if (Input.IsPressed(KeyCode.Num4)) { agent.FollowField(); Log.Info("[AITest] FollowField()"); }

            // A setting, written and read back.
            if (Input.IsPressed(KeyCode.K))
            {
                if (!slow) normalSpeed = agent.Speed;
                slow = !slow;
                agent.Speed = slow ? normalSpeed * 0.5f : normalSpeed;
                Log.Info("[AITest] Speed is now " + agent.Speed.ToString("0.0") + (slow ? " (halved)" : " (back to normal)"));
            }

            // Arrival, logged the moment it changes.
            bool arrived = agent.HasArrived;
            if (arrived != lastArrived)
            {
                Log.Info("[AITest] HasArrived -> " + arrived + " (RemainingDistance " + agent.RemainingDistance.ToString("0.0") + ")");
                lastArrived = arrived;
            }

            if (Input.IsPressed(KeyCode.L))
            {
                string targetText = "-";
                if (brain != null)
                {
                    Entity t;
                    targetText = brain.GetVariableValue("Target", out t) ? (t != null ? t.ToString() : "null") : "no Target variable";
                }
                Log.Info("[AITest] State \"" + (brain != null ? brain.CurrentState : "-") + "\", HasArrived " + agent.HasArrived +
                         ", RemainingDistance " + agent.RemainingDistance.ToString("0.0") + ", Velocity " + agent.Velocity +
                         ", Speed " + agent.Speed.ToString("0.0") + ", Target variable " + targetText);
            }

            if (Input.IsPressed(KeyCode.G) && surface != null)
            {
                if (!flooded) normalFlood = surface.FloodRadius;
                flooded = !flooded;
                surface.FloodRadius = flooded ? 0f : normalFlood;
                Log.Info("[AITest] FloodRadius " + surface.FloodRadius.ToString("0") + (flooded ? " (whole surface" : " (back") +
                         ") - shows once the player has moved " + surface.RebuildDistance.ToString("0.0") + " m");
            }
        }
    }
}
