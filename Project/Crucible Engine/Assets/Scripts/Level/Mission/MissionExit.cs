/*************************************************************************
* \file:         MissionExit.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.0
* \brief         Ends a mission level: back to the menu once the mission is
*                complete, or failed because every player is down.
*
* \details
*    On the Sub Level Generator entity, next to MissionController.
*      Complete   MissionController.MissionCompleted: the HUD keeps
*                 "Mission complete", returnDelay seconds later menuScene
*                 loads.
*      Failed     every player (the generator's players: the "Player" tag)
*                 has a MechPlayer whose Health is dead: MissionController
*                 .Fail() (HUD "Mission failed", areas stop), then the same
*                 wait and load. A player entity without a MechPlayer counts
*                 as alive, so test scenes with a stand-in never fail.
*    Under the outcome the HUD shows "Returning to the main menu"
*    (MissionController.ReturnNote). The load goes through Scene.Load, so
*    the loading screen shows as usual.
*    Multiplayer: the mission fails only once all players are down.
*
* \history
*    -v1.0       new: complete / all players dead -> main menu
*
**************************************************************************/
using System.Collections.Generic;

namespace Crucible.@Scripts
{
    public sealed class MissionExit : EntityScript
    {
        public string menuScene = "Scenes/Game/Main Menu Scene V2.scene";   // inside Assets
        public float returnDelay = 4f;                                       // seconds the outcome shows first
        public bool returnOnComplete = true;
        public bool returnOnDeath = true;

        private const string kReturnNote = "Returning to the main menu";

        private MissionController controller;
        private SubLevelGenerator generator;
        private float returnIn = -1f;          // < 0: not leaving yet
        private bool loadRequested;

        public override void OnUpdate(float deltaTime)
        {
            if (controller == null)
            {
                controller = Entity.GetComponent<MissionController>();
                generator = Entity.GetComponent<SubLevelGenerator>();
                if (controller == null) return;
                controller.MissionCompleted += OnCompleted;
            }

            if (returnIn < 0f)
            {
                if (returnOnDeath && AllPlayersDown())
                {
                    controller.Fail();
                    BeginReturn("every player is down");
                }
                return;
            }

            if (loadRequested) return;
            returnIn -= deltaTime;
            if (returnIn > 0f) return;
            loadRequested = true;
            if (!Scene.Load(menuScene)) Log.Warning("MissionExit: could not load '" + menuScene + "' - check the path (inside Assets)");
        }

        public override void OnDestroy()
        {
            if (controller != null) controller.MissionCompleted -= OnCompleted;
        }

        private void OnCompleted()
        {
            if (returnOnComplete) BeginReturn("mission complete");
        }

        private void BeginReturn(string why)
        {
            if (returnIn >= 0f) return;
            returnIn = returnDelay > 0f ? returnDelay : 0.0001f;
            controller.ReturnNote = kReturnNote;
            Log.Info("MissionExit: " + why + " - back to " + menuScene + " in " + returnDelay + " s");
        }

        // True when there is at least one player and every one with a MechPlayer is dead.
        private bool AllPlayersDown()
        {
            if (generator == null || controller.State == MissionState.Complete || controller.Failed) return false;
            List<Entity> players = generator.Players();
            if (players.Count == 0) return false;
            foreach (Entity p in players)
            {
                MechPlayer mech = p != null && p.IsAlive ? p.GetComponent<MechPlayer>() : null;
                if (mech == null || mech.Health == null || !mech.Health.IsDead) return false;
            }
            return true;
        }
    }
}
