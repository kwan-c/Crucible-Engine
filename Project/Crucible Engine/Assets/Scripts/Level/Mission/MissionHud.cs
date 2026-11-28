/*************************************************************************
* \file:         MissionHud.cs
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          27 November 2026
* \version       1.1
* \brief         Placeholder mission read-out (the GDD's quest box): what
*                to do, the timer, and a hold bar. Goes on the Mission HUD
*                canvas (Prefabs/UI/Mission HUD).
*
* \details
*    Port of Unity's MissionHud. The layout lives in the prefab, so it can
*    be changed in the UI Editor; this script finds its pieces by name under
*    the canvas and only changes text, colours and the bar's width:
*
*        Quest Box/ Title, Detail, Bar Back/ Bar Fill
*
*    A missing piece is skipped. Reads MissionController (on the entity
*    named controllerName): Title, Detail, and the bar (ShowBar,
*    BarProgress, BarState: a capture area's hold, or an objective
*    script's own bar, e.g. the escort's way to the next checkpoint). Bar colours follow the zone outlines (Unity's): yellow
*    waiting, blue holding, yellow warning, red failed, green complete.
*    The whole box hides when the level has no mission.
*
*    Later: the GDD puts the quest box in the combat HUD (MechHud's canvas,
*    Nelson's); the same named pieces can move there and this script can
*    point at them.
*
* \history
*    -v1.0       quest box: title, detail, hold bar
*    -v1.1       shows for any mission (HasMission); the bar from the
*                controller's ShowBar / BarProgress / BarState (Escort too)
*
**************************************************************************/
namespace Crucible.@Scripts
{
    public sealed class MissionHud : EntityScript
    {
        public string controllerName = "Sub Level Generator";  // the entity with the MissionController

        private MissionController controller;
        private Entity box, barBack;
        private UIText title, detail;
        private UIRect fillRect;
        private UIImage fillImage;
        private float fullWidth;
        private bool found;
        private float searchTimer;

        public override void OnUpdate(float deltaTime)
        {
            if (!found && !FindPieces(deltaTime)) return;
            if (controller == null || !controller.Entity.IsAlive) { found = false; return; }

            bool any = controller.HasMission;
            if (box != null) box.SetActive(any);
            if (!any) return;
            if (title != null) title.Text = controller.Title;
            if (detail != null) detail.Text = controller.Detail;

            bool bar = controller.ShowBar;
            if (barBack != null) barBack.SetActive(bar);
            if (!bar || fillRect == null) return;
            Vector2 size = fillRect.Size;
            fillRect.Size = new Vector2(fullWidth * System.Math.Max(0f, System.Math.Min(1f, controller.BarProgress)), size.Y);
            if (fillImage != null) fillImage.Color = ColourFor(controller.BarState);
        }

        // The controller and the box's pieces; looked for twice a second until found.
        private bool FindPieces(float deltaTime)
        {
            searchTimer -= deltaTime;
            if (searchTimer > 0f) return false;
            searchTimer = 0.5f;
            Entity holder = Entity.Find(controllerName);
            controller = holder != null ? holder.GetComponent<MissionController>() : null;
            if (controller == null) return false;
            box = Entity.FindChild("Quest Box");
            if (box == null) { Log.Warning("MissionHud: no 'Quest Box' under the canvas"); found = true; return true; }
            Entity t = box.FindChild("Title"), d = box.FindChild("Detail");
            barBack = box.FindChild("Bar Back");
            Entity fill = barBack != null ? barBack.FindChild("Bar Fill") : null;
            title = t != null ? t.GetComponent<UIText>() : null;
            detail = d != null ? d.GetComponent<UIText>() : null;
            fillRect = fill != null ? fill.GetComponent<UIRect>() : null;
            fillImage = fill != null ? fill.GetComponent<UIImage>() : null;
            UIRect back = barBack != null ? barBack.GetComponent<UIRect>() : null;
            fullWidth = back != null ? back.Size.X : (fillRect != null ? fillRect.Size.X : 0f);
            found = true;
            return true;
        }

        // Unity's zone colours.
        private static Color ColourFor(HoldZoneState state)
        {
            switch (state)
            {
                case HoldZoneState.Holding: return new Color(0.25f, 0.55f, 1f);
                case HoldZoneState.Failed: return new Color(1f, 0.25f, 0.2f);
                case HoldZoneState.Complete: return new Color(0.3f, 0.95f, 0.45f);
                default: return new Color(1f, 0.85f, 0.2f);
            }
        }
    }
}
