/*************************************************************************
* \file:         QuestSlab.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          26 November 2026
* \version       1.0
* \brief         One quest stone on the main menu's quest table (the root of
*                the "Quest Slab" prefab): shows its quest type and logo, and
*                plays its landing effect when QuestStoneDrop drops it.
*
* \details
*    The prefab (Assets/Prefabs/Quests/Quest Slab.cprefab) is Xavier's level
*    select slab layout with the side quest text:
*        Quest Slab     the Slab1 model + this script
*        |- Quest Text  sideQuestText ("SIDE QUEST")
*        |- Quest Type  a WorldText: the quest type
*        '- Logo        the type's logo (Logo1 / Logo2 / Logo3)
*
*    The types follow the Unity prototype's board: KOTH has the flag
*    (Logo1), ELIMINATION the target (Logo3), ESCORT Logo2.
*
*    landingVfx is the "something plays when it lands" input: a prefab
*    spawned where the slab lands and removed vfxSeconds later.
*
**************************************************************************/
namespace Crucible.Scripts
{
    public sealed class QuestSlab : EntityScript
    {
        public Model kothLogo;               // Logo1 (flag)
        public Model eliminationLogo;        // Logo3 (target)
        public Model escortLogo;             // Logo2
        public Prefab landingVfx;            // spawned where the slab lands; empty = nothing
        public float vfxSeconds = 2f;        // the effect is removed after this long

        /// <summary>The quest types a slab can show, in the order SetQuest takes them.</summary>
        public static readonly string[] Types = { "KOTH", "ELIMINATION", "ESCORT" };

        /// <summary>This slab's quest type (one of Types), once SetQuest has run.</summary>
        public string QuestType { get; private set; }

        // One generator for every slab, seeded from the clock: each slab a different pick, each run new.
        private static readonly System.Random random = new System.Random();

        // A random quest for now (no quest list yet). Done here, not by QuestStoneDrop: a slab
        // spawned hidden only gets its script once it is shown, and this runs right then. The
        // prefab's own look (ELIMINATION + target) is a matching pair for that first frame.
        public override void OnCreate()
        {
            SetQuest(random.Next(Types.Length));
        }

        /*********************************************************************
         \brief  Shows quest type Types[type]: its name on the Quest Type text
                 and its logo on the Logo child.
         *********************************************************************/
        public void SetQuest(int type)
        {
            if (type < 0 || type >= Types.Length) type = 0;
            QuestType = Types[type];

            Entity label = Entity.FindChild("Quest Type");
            WorldText text = label != null ? label.GetComponent<WorldText>() : null;
            if (text != null) text.Text = QuestType;

            Model logo = type == 0 ? kothLogo : type == 1 ? eliminationLogo : escortLogo;
            Entity logoEntity = Entity.FindChild("Logo");
            MeshRenderer renderer = logoEntity != null ? logoEntity.GetComponent<MeshRenderer>() : null;
            if (renderer != null && logo != null) renderer.Model = logo;
        }   // QuestSlab::SetQuest()

        /// <summary>Called by QuestStoneDrop the moment the slab hits the table.</summary>
        public void Landed()
        {
            if (landingVfx == null) return;
            Entity vfx = landingVfx.Instantiate(Entity.Transform.WorldPosition, Quaternion.Identity);
            if (vfx != null) Destroy(vfx, vfxSeconds);   // one-shot: gone once it has played
        }

    }   // QUESTSLAB CLASS

}   // NAMESPACE CRUCIBLE.SCRIPTS
