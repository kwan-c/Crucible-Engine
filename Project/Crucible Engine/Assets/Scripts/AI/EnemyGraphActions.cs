using Crucible;
using Crucible.AI;
using Action = Crucible.AI.Action;   // System.Action has the same name

namespace Crucible.Scripts
{
    // Port of Gam3333's EnemyGraphActions.cs (Unity Behavior nodes, namespace
    // Lithomachia.EnemyBehavior). The [NodeDescription]s are Unity's, ids included, so the
    // graphs line up node for node. The bodies call the Enemy script (EnemyController.cs
    // and EnemyWeapon.cs), as Unity's called EnemyGraphController.
    //
    // Used by the .behaviorgraph files in Assets/BehaviorGraphs, which mirror Unity's
    // Assets/AI/Behavior/Graphs: four shared subgraphs ("Acquire and Approach", "Telegraphed
    // Weapon Attack", "Recover", "Wander Until Detection") and one main graph per enemy type.

    // Unity's EnemyGraphAction: finds the Enemy, fails if it isn't ready, and splits the
    // node into Begin (once) and Tick (every frame, including the first).
    public abstract class EnemyGraphAction : Action
    {
        protected Enemy Controller;

        protected sealed override Status OnStart()
        {
            Controller = Self.GetComponent<Enemy>();
            if (Controller == null || !Controller.IsReady) return Status.Failure;
            Begin();
            return Step();
        }

        protected sealed override Status OnUpdate() { return Step(); }

        Status Step()
        {
            if (Controller == null || !Controller.IsReady) return Status.Failure;
            return DeltaTime <= 0f ? Status.Running : Tick();
        }

        protected virtual void Begin() { }
        protected abstract Status Tick();
    }

    [NodeDescription(name: "Detect Player", story: "Detect player", category: "Lithomachia/Enemy", id: "822bf5976a9c49efb19522b3ca0668c0",
                     description: "Succeeds if the player is within the enemy's detect radius (lose radius once detected); fails otherwise.")]
    public sealed class DetectEnemyPlayerAction : EnemyGraphAction
    {
        protected override Status Tick() { return Controller.SensePlayer() ? Status.Success : Status.Failure; }
    }

    [NodeDescription(name: "Wander Until Player Detected", story: "Wander until player detected", category: "Lithomachia/Enemy", id: "42ba3aeae12b425da949ccda1961a38f9",
                     description: "Drifts about slowly until it detects the player, then stops and succeeds.")]
    public sealed class WanderEnemyAction : EnemyGraphAction
    {
        protected override Status Tick()
        {
            if (Controller.SensePlayer()) { Controller.Hold("Detected player"); return Status.Success; }
            Controller.Wander();
            return Status.Running;
        }
    }

    [NodeDescription(name: "Approach Attack Range", story: "Approach attack range", category: "Lithomachia/Enemy", id: "f104b8619db84315999a6013ed279084",
                     description: "Moves to where it can attack (melee: in range and facing; ranged: a firing spot with a clear shot). Fails if it loses the player.")]
    public sealed class ApproachEnemyAction : EnemyGraphAction
    {
        protected override Status Tick()
        {
            if (!Controller.SensePlayer()) { Controller.Hold("Lost player"); return Status.Failure; }
            return Controller.Approach() ? Status.Success : Status.Running;
        }
    }

    [NodeDescription(name: "Request Weapon Attack", story: "Request weapon attack", category: "Lithomachia/Enemy", id: "1fbd2471ec2a41b88bad9cc8e502427e",
                     description: "Keeps in position until its weapon is off cooldown, then starts an attack and succeeds.")]
    public sealed class RequestEnemyAttackAction : EnemyGraphAction
    {
        protected override Status Tick()
        {
            Controller.Hold("Request attack");
            if (!Controller.SensePlayer()) return Status.Failure;

            // Melee pressure and ranged shooters stay in position while they wait.
            if (Controller.ContinuousMeleePressure || Controller.UsesRangedPositioning)
            {
                bool inRange = Controller.Approach();
                if (!inRange || Controller.CooldownRemaining > 0f) return Status.Running;
                return Controller.RequestAttack() ? Status.Success : Status.Running;
            }

            if (!Controller.TargetInReach()) return Status.Failure;
            if (Controller.CooldownRemaining > 0f) return Status.Running;
            return Controller.RequestAttack() ? Status.Success : Status.Failure;
        }
    }

    [NodeDescription(name: "Wait For Weapon Attack", story: "Wait for weapon attack", category: "Lithomachia/Enemy", id: "48e8f62d4ecb4d8d8b3334a212a34449",
                     description: "Holds still (keeping the swing's facing) until the attack has played out. Cancels it if the player is lost.")]
    public sealed class WaitEnemyAttackAction : EnemyGraphAction
    {
        protected override Status Tick()
        {
            Controller.Hold("Attack windup / release");
            if (!Controller.SensePlayer()) { Controller.CancelAttack(); return Status.Failure; }
            return Controller.AttackInProgress ? Status.Running : Status.Success;
        }

        // Cut short (the state ended, the tree was aborted): the hit must not land later.
        protected override void OnEnd() { if (Controller != null) Controller.CancelAttack(); }
    }

    [NodeDescription(name: "Recover Weapon Cooldown", story: "Recover weapon cooldown", category: "Lithomachia/Enemy", id: "a8110da821b546f0a5d10af059770ce7",
                     description: "Waits out the weapon's cooldown: melee keeps pursuing, ranged keeps or improves its firing spot.")]
    public sealed class RecoverEnemyAction : EnemyGraphAction
    {
        bool completed;

        protected override void Begin() { completed = false; }

        protected override Status Tick()
        {
            Controller.Hold("Recover");
            if (!Controller.SensePlayer()) return Status.Failure;
            completed = Controller.Recover();
            return completed ? Status.Success : Status.Running;
        }

        protected override void OnEnd() { if (!completed && Controller != null) Controller.CancelAttack(); }
    }
}
