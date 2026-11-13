using System;

namespace Crucible.Scripts
{
    // Tests several scripts on ONE entity: GetComponent<T> / GetComponents<T> over scripts,
    // OnCreate / OnUpdate for each, and scripts talking to each other.
    //   1. Put MultiScriptTest AND MultiScriptPartner on the same entity (Add Component twice).
    //   2. Press Play. Both OnCreates log. MultiScriptTest lists every script on the entity and
    //      finds its partner - even if the partner hasn't run its own OnCreate yet.
    //   3. Press Y: MultiScriptTest calls the partner, whose OnUpdate then logs the new count.
    //   4. Press U: switch ONLY the partner off (partner.Enabled). Y still counts, but the
    //      partner's OnUpdate stops logging until U switches it back on - then it logs the
    //      count it reached. This script keeps running throughout.
    public sealed class MultiScriptTest : EntityScript
    {
        MultiScriptPartner partner;

        public override void OnCreate()
        {
            EntityScript[] all = GetComponents<EntityScript>();
            string names = "";
            foreach (EntityScript script in all)
                names += (names.Length > 0 ? ", " : "") + script.GetType().Name;
            Log.Info("[MultiScriptTest] " + all.Length + " script(s) on " + Entity + ": " + names);

            partner = GetComponent<MultiScriptPartner>();
            Log.Info("[MultiScriptTest] GetComponent<MultiScriptTest>() is me: " + (GetComponent<MultiScriptTest>() == this) +
                     ", partner " + (partner != null ? "found" : "NOT found - add MultiScriptPartner to this entity") +
                     ", GetComponents<MultiScriptPartner>(): " + GetComponents<MultiScriptPartner>().Length +
                     ", GetComponents<Transform>(): " + GetComponents<Transform>().Length);
        }

        public override void OnUpdate(float deltaTime)
        {
            if (Input.IsPressed(KeyCode.Y) && partner != null)
            {
                partner.Bump();
                Log.Info("[MultiScriptTest] Y: asked the partner to count");
            }
            if (Input.IsPressed(KeyCode.U) && partner != null)
            {
                partner.Enabled = !partner.Enabled;
                Log.Info("[MultiScriptTest] U: partner Enabled " + partner.Enabled + " (IsEnabled<MultiScriptPartner> " +
                         Entity.IsEnabled<MultiScriptPartner>() + "), mine still " + Enabled +
                         ", all scripts switch " + Entity.IsEnabled<EntityScript>());
            }
        }
    }
}
