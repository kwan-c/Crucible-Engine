using System;

namespace Crucible.Scripts
{
    // Tests Entity.Find, HasComponent and GetComponent (components and other scripts).
    //   1. Put EntityRefTest on some entity (it bobs its Target).
    //   2. Put this script on another entity and type that entity's Hierarchy name into "Find Name".
    //   3. Press Play. OnCreate logs what it found - including the other entity's script, which
    //      works even if that script hasn't run its own OnCreate yet.
    //   4. Press B to speed up the other script's bobbing, N to slow it down.
    public sealed class FindComponentTest : EntityScript
    {
        public string findName = "";

        Entity other;
        EntityRefTest otherScript;

        public override void OnCreate()
        {
            Log.Info("[FindComponentTest] Me: " + Entity + " - Transform " + HasComponent<Transform>() +
                     ", RigidBody " + HasComponent<RigidBody>() + ", Animator " + HasComponent<Animator>() +
                     ", my script found via GetComponent: " + (GetComponent<FindComponentTest>() == this));

            other = Entity.Find(findName);
            if (other == null)
            {
                Log.Warning("[FindComponentTest] No entity named '" + findName + "'. Set Find Name in the Inspector.");
                return;
            }

            Log.Info("[FindComponentTest] Found " + other + " - Transform " + other.HasComponent<Transform>() +
                     ", RigidBody " + other.HasComponent<RigidBody>() + ", Animator " + other.HasComponent<Animator>());

            EntityScript anyScript = other.GetComponent<EntityScript>();
            Log.Info("[FindComponentTest] Its script: " + (anyScript != null ? anyScript.GetType().Name : "none"));

            otherScript = other.GetComponent<EntityRefTest>();
            if (otherScript != null)
                Log.Info("[FindComponentTest] It's an EntityRefTest moving " + otherScript.target +
                         " at speed " + otherScript.bobSpeed + ". Press B / N to change it.");
        }

        public override void OnUpdate(float deltaTime)
        {
            if (otherScript == null)
                return;

            if (Input.IsPressed(KeyCode.B))
            {
                otherScript.bobSpeed *= 2f;
                Log.Info("[FindComponentTest] Other script's bob speed is now " + otherScript.bobSpeed);
            }
            if (Input.IsPressed(KeyCode.N))
            {
                otherScript.bobSpeed *= 0.5f;
                Log.Info("[FindComponentTest] Other script's bob speed is now " + otherScript.bobSpeed);
            }
        }
    }
}
