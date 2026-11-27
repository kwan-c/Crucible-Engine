/*************************************************************************
* \file:         QuestBoard.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          26 November 2026
* \version       1.0
* \brief         The quests the main menu's quest table offers, and the one
*                the player accepted. The meeting point between the menu and
*                level generation.
*
* \details
*    PLACEHOLDER DATA: Unity's six prototype side quests (titles, types and
*    rewards from Gam3333's PrototypeQuest_2..7), each with a one-line
*    stand-in briefing, and all of them load TutorialMap for now.
*
*    How the menu uses it:
*        QuestBoard.Offers(6)    a fresh board: one QuestInfo per slab;
*        QuestBoard.Accept(q)    Accept on the quest tablet: remembers q and
*                                loads q.ScenePath.
*
*    How level generation plugs in later (nothing in the menu changes):
*        - the generator, once its level loads, reads QuestBoard.Accepted:
*          .Type for what to build and .Seed for which variant (the same seed
*          gives the same level, handy for testing);
*        - a level per quest type: change ScenePath in Placeholders below.
*    Accepted is a static, so it survives Scene.Load in the Game, like
*    TorsoSelector's torso choice that MechPlayer reads. In the Editor it
*    also survives from one Play to the next, so a level should treat it as
*    "maybe left over" and Offers() clears it when the board is rebuilt.
*
*    Unity: QuestDefinition assets + QuestProgressionRuntime's run state.
*
**************************************************************************/
using System;
using System.Collections.Generic;

namespace Crucible.Scripts
{
    /// <summary>The three kinds of quest, in QuestSlab.Types' order (its logo and label follow it).</summary>
    public enum QuestType { Koth, Elimination, Escort }

    /// <summary>One quest on the board.</summary>
    public sealed class QuestInfo
    {
        public string Title;
        public QuestType Type;
        public string Briefing;
        public int Reward;          // Gni
        public string ScenePath;    // inside Assets, for Scene.Load
        public int Seed;            // for level generation: same seed, same level

        /// <summary>"KOTH", "ELIMINATION" or "ESCORT": the label the slab and tablet show.</summary>
        public string TypeName { get { return QuestSlab.Types[(int)Type]; } }

        public QuestInfo Copy() { return (QuestInfo)MemberwiseClone(); }
    }

    public static class QuestBoard
    {
        /// <summary>Where every placeholder quest goes for now.</summary>
        public const string PlaceholderScene = "Scenes/Game/TestFolder/TutorialMap.scene";

        // Unity's prototype side quests (Gam3333 PrototypeQuest_2..7: title, type, reward).
        // The briefings are stand-ins, one per type: Unity's own text was a generic placeholder
        // that talks about picking a torso after Accept, which our menu does on its own table.
        private static readonly QuestInfo[] Placeholders =
        {
            Quest("Escort the Core Carrier", QuestType.Escort, 125),
            Quest("Clear the Old Road", QuestType.Elimination, 150),
            Quest("Hold the Crossing", QuestType.Koth, 175),
            Quest("Escort the Stonekeeper", QuestType.Escort, 200),
            Quest("Capture the Workshop", QuestType.Koth, 225),
            Quest("Clear the Quarry", QuestType.Elimination, 250),
        };

        /// <summary>The quest the player accepted, or null (none yet, or a new board since).</summary>
        public static QuestInfo Accepted { get; private set; }

        // Seeded from the clock: a different board, and different level seeds, every run.
        private static readonly Random random = new Random();

        /*********************************************************************
         \brief  A fresh board of `count` quests, in random order, each with
                 a new Seed. No quest repeats until all of them are used.
                 Clears Accepted: a new board means nothing is accepted yet.
         *********************************************************************/
        public static List<QuestInfo> Offers(int count)
        {
            Accepted = null;
            List<QuestInfo> offers = new List<QuestInfo>();
            List<QuestInfo> deck = new List<QuestInfo>();
            while (offers.Count < count)
            {
                if (deck.Count == 0) deck.AddRange(Placeholders);   // (re)fill: all used up
                int pick = random.Next(deck.Count);
                QuestInfo quest = deck[pick].Copy();   // a copy: each offer has its own seed
                deck.RemoveAt(pick);
                quest.Seed = random.Next();
                offers.Add(quest);
            }
            return offers;
        }   // QuestBoard::Offers()

        /*********************************************************************
         \brief  The player accepted `quest`: remember it for the level, then
                 load its scene (at the start of the next frame). False, and
                 nothing remembered, when the scene can't be loaded.
         *********************************************************************/
        public static bool Accept(QuestInfo quest)
        {
            if (quest == null) return false;
            Accepted = quest;
            if (Scene.Load(quest.ScenePath)) return true;
            Log.Warning("QuestBoard: '" + quest.Title + "' could not load " + quest.ScenePath + ".");
            Accepted = null;
            return false;
        }

        private static QuestInfo Quest(string title, QuestType type, int reward)
        {
            string briefing =
                type == QuestType.Koth ? "Take the marked ground and hold it until the time runs out." :
                type == QuestType.Elimination ? "Clear every enemy from the area." :
                                                "Keep the carrier safe until it reaches the end of the route.";
            return new QuestInfo { Title = title, Type = type, Briefing = briefing, Reward = reward, ScenePath = PlaceholderScene };
        }

    }   // QUESTBOARD CLASS

}   // NAMESPACE CRUCIBLE.SCRIPTS
