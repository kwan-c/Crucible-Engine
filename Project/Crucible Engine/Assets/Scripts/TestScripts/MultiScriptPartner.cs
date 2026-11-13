using System;

namespace Crucible.Scripts
{
    // The second script for MultiScriptTest (see that file): goes on the same entity.
    public sealed class MultiScriptPartner : EntityScript
    {
        public  int count;
        int logged;

        /// <summary>Called by MultiScriptTest on the same entity.</summary>
        public void Bump() { ++count; }

        public override void OnCreate()
        {
            Log.Info("[MultiScriptPartner] OnCreate on " + Entity + ", MultiScriptTest here too: " +
                     (GetComponent<MultiScriptTest>() != null));
        }

        public override void OnUpdate(float deltaTime)
        {
            if (count != logged)
            {
                logged = count;
                Log.Info("[MultiScriptPartner] OnUpdate: count is now " + count);
            }
        }
    }
}
