/*************************************************************************
* \file:         FpsCounter.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          27 November 2026
* \version       1.1
* \brief         On-screen frames-per-second readout in the top-right
*                corner that stays up through every scene change.
*
* \details
*    Goes on a ROOT ScreenSpace UICanvas entity of its own (e.g. "FPS
*    Canvas"). On Play it marks the canvas DontDestroyOnLoad, so the counter
*    survives every Scene.Load / Reload, and raises the canvas' SortOrder so
*    it draws over every other UI. Put the canvas in the first scene that
*    loads; it can also sit in other scenes - a second copy destroys itself.
*
*    The text is a child of the canvas named textChildName (default
*    "FPS Text") with a UIText; if there is none, a UIText on the canvas
*    itself is used. Scripts cannot set anchors, so lay the child out once
*    in the Inspector:
*
*        UIRect   Anchor Min / Max = (1, 0)     top-right of the canvas
*                 Pivot            = (1, 0)     box grows left and down
*                 Position         = (-20, 20)  margin from the corner
*                 Size             = (220, 40)
*                 Raycast Target   = off        never blocks clicks
*        UIText   Horizontal Align = Right, Vertical Align = Top
*
*    The value is averaged over updateInterval seconds so it is readable,
*    and is coloured green / yellow / red by goodFps and okFps.
*    toggleKey shows / hides it.
*
*    v1.1  Lives on the canvas and persists across scenes.
*
**************************************************************************/
namespace Crucible.Scripts
{
    public sealed class FpsCounter : EntityScript
    {
        public string textChildName = "FPS Text";
        public bool persistAcrossScenes = true;
        public int sortOrder = 1000;          // canvas draw order: higher is on top of other UI

        public float updateInterval = 0.5f;   // seconds between refreshes (the average window)
        public bool showFrameTime = true;     // also show milliseconds per frame
        public bool visible = true;
        public KeyCode toggleKey = KeyCode.F3;

        public float goodFps = 55f;           // at or above: goodColor
        public float okFps = 30f;             // at or above: okColor, below: badColor
        public Color goodColor = new Color(0.3f, 0.95f, 0.5f);
        public Color okColor = new Color(1f, 0.85f, 0.3f);
        public Color badColor = new Color(1f, 0.3f, 0.25f);

        // The canvas that survives scene loads. A copy that a later scene brings in removes itself.
        static Entity instance;

        UIText text;
        float accumulated;
        int frames;

        public override void OnCreate()
        {
            if (persistAcrossScenes)
            {
                if (instance != null && instance.IsAlive && instance != Entity)
                {
                    Destroy(Entity);
                    return;
                }
                instance = Entity;
                DontDestroyOnLoad(Entity);   // logs why if this is not a root entity
            }

            UICanvas canvas = GetComponent<UICanvas>();
            if (canvas != null) canvas.SortOrder = sortOrder;
            else Log.Warning("FpsCounter on " + Entity.Name + " should be on a UICanvas entity.");

            Entity child = string.IsNullOrEmpty(textChildName) ? null : Entity.FindChild(textChildName);
            text = child != null ? child.GetComponent<UIText>() : null;
            if (text == null) text = GetComponent<UIText>();
            if (text == null)
            {
                Log.Warning("FpsCounter on " + Entity.Name + " found no UIText (child \"" + textChildName + "\" or on the canvas).");
                return;
            }
            text.Text = visible ? "FPS: --" : string.Empty;
        }

        public override void OnDestroy()
        {
            if (instance == Entity) instance = null;
        }

        public override void OnUpdate(float deltaTime)
        {
            if (text == null) return;

            if (Input.IsPressed(toggleKey))
            {
                visible = !visible;
                if (!visible) text.Text = string.Empty;
                accumulated = 0f;
                frames = 0;
            }
            if (!visible) return;

            accumulated += deltaTime;
            ++frames;
            if (accumulated < updateInterval) return;

            float fps = frames / accumulated;
            float ms = accumulated * 1000f / frames;
            accumulated = 0f;
            frames = 0;

            text.Text = showFrameTime
                ? "FPS: " + fps.ToString("0") + "  (" + ms.ToString("0.0") + " ms)"
                : "FPS: " + fps.ToString("0");
            text.Color = fps >= goodFps ? goodColor : fps >= okFps ? okColor : badColor;
        }
    }
}
