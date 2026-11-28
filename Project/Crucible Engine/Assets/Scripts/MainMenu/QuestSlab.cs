/*************************************************************************
* \file:         QuestSlab.cs
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          26 November 2026
* \version       1.1   shows a QuestBoard quest; hover brighten
* \brief         One quest stone on the main menu's quest table (the root of
*                the "Quest Slab" prefab): shows its quest's type and logo,
*                brightens while hovered, and plays its landing effect when
*                QuestStoneDrop drops it.
*
* \details
*    The prefab (Assets/Prefabs/Quests/Quest Slab.cprefab) is Xavier's level
*    select slab layout with the side quest text:
*        Quest Slab     the Slab1 model + MaterialParams + this script
*        |- Quest Text  sideQuestText ("SIDE QUEST")
*        |- Quest Type  a WorldText: the quest type
*        '- Logo        the type's logo (Logo1 / Logo2 / Logo3)
*
*    QuestStoneDrop gives each slab its quest (SetQuest) from QuestBoard
*    while the slab is still hidden: the engine makes a hidden entity's
*    scripts straight away (only OnCreate waits until it is shown), so this
*    script is there to take it. Until then the prefab's own look (an
*    ELIMINATION label with the target logo) shows.
*
*    The logos follow the Unity prototype's board: KOTH has the flag
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
        public float hoverBrighten = 0.25f;  // how much brighter the stone gets at full highlight

        /// <summary>The labels SetQuest shows, indexed by QuestType (Koth, Elimination, Escort).</summary>
        public static readonly string[] Types = { "CAPTURE", "ELIMINATION", "ESCORT" };   // Koth shows as CAPTURE (M3 name)

        /// <summary>This slab's quest, once QuestStoneDrop has given it one (else null).</summary>
        public QuestInfo Quest { get; private set; }

        /*********************************************************************
         \brief  Shows `quest` on this slab: its type's name on the Quest Type
                 text and its type's logo on the Logo child.
         *********************************************************************/
        public void SetQuest(QuestInfo quest)
        {
            if (quest == null) return;
            Quest = quest;

            Entity label = Entity.FindChild("Quest Type");
            WorldText text = label != null ? label.GetComponent<WorldText>() : null;
            if (text != null) text.Text = quest.TypeName;

            Model logo = quest.Type == QuestType.Koth ? kothLogo
                       : quest.Type == QuestType.Elimination ? eliminationLogo : escortLogo;
            Entity logoEntity = Entity.FindChild("Logo");
            MeshRenderer renderer = logoEntity != null ? logoEntity.GetComponent<MeshRenderer>() : null;
            if (renderer != null && logo != null) renderer.Model = logo;
        }   // QuestSlab::SetQuest()

        /*********************************************************************
         \brief  How lit up the stone looks: 0 = as modelled, 1 = hoverBrighten
                 brighter. QuestPicker eases it in and out while hovered. Uses
                 the root's MaterialParams (the stone body; the logo and text
                 keep their colour).
         *********************************************************************/
        public void SetHighlight(float amount)
        {
            MaterialParams look = Entity.GetComponent<MaterialParams>();
            if (look == null) return;
            if (amount <= 0f) { look.ClearTint(); return; }   // exactly the modelled colour at rest
            float k = 1f + hoverBrighten * amount;
            look.Tint = new Color(k, k, k);
        }

        /// <summary>Called by QuestStoneDrop the moment the slab hits the table.</summary>
        public void Landed()
        {
            if (landingVfx == null) return;
            Entity vfx = landingVfx.Instantiate(Entity.Transform.WorldPosition, Quaternion.Identity);
            if (vfx != null) Destroy(vfx, vfxSeconds);   // one-shot: gone once it has played
        }

    }   // QUESTSLAB CLASS

}   // NAMESPACE CRUCIBLE.SCRIPTS
