/*************************************************************************
* \file:         QualitySettings.cs
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          25 November 2026
* \version       1.0
* \brief         The player's graphics quality: preset, anti-aliasing,
*                shadows, ambient occlusion, bloom, volumetric fog. Unity's
*                QualitySettings. Part of Crucible.ScriptCore.dll.
*
* \details
*    These are the PLAYER'S settings (UserSettings.json, next to the .exe),
*    the same for every scene, kept between launches. Build the options
*    menu's Graphics page on them:
*
*        QualitySettings.Level = QualityLevel.Medium;           // every setting at once
*        QualitySettings.Shadows = ShadowQuality.Low;           // Level becomes Custom
*        QualitySettings.AntiAliasing = AntiAliasing.FXAA;
*        QualitySettings.Save();                                // on "Apply"
*        QualitySettings.Revert();                              // on "Cancel"
*
*    A change applies at once (shows next frame); Save() writes it to the
*    file so the next launch starts with it (it saves every player setting,
*    like InputSettings.Save). Level follows the settings: changing one so it
*    no longer matches the preset makes it Custom, and matching one again
*    gives that preset's name back.
*
*    Presets:
*                  AntiAliasing  Shadows  AO   Bloom  VolumetricFog
*        Low       FXAA          Low      off  on     off
*        Medium    MSAA2x        Medium   on   on     off
*        High      MSAA4x        High     on   on     on     (default)
*        Ultra     MSAA4x        Ultra    on   on     on
*
*    VSync, FrameRateLimit, Brightness, FieldOfViewOffset, Vignette,
*    FilmGrain and ChromaticAberration are preferences outside the presets:
*    picking a Level keeps them, and changing them never makes it Custom.
*
*    IN THE EDITOR changes apply too, so a menu can be tested in Play, but
*    Stop puts the Scene View panel's renderer settings back. The editor
*    never starts with the saved settings; the game build does.
*
*    C++ side: Engine/src/Scripting/Bindings/QualitySettingsBindings.cpp.
*
**************************************************************************/
using System.Runtime.CompilerServices;

namespace Crucible
{
    /// <summary>A graphics preset. Unity's quality levels.</summary>
    public enum QualityLevel
    {
        Low = 0,
        Medium = 1,
        High = 2,
        Ultra = 3,
        /// <summary>The settings match no preset. Cannot be set; it is what Level reads then.</summary>
        Custom = 4,
    }

    /// <summary>Edge smoothing. The MSAA levels also keep FXAA on (it catches the edges shading makes).</summary>
    public enum AntiAliasing
    {
        Off = 0,
        /// <summary>Fast screen-space smoothing; nearly free, slightly soft.</summary>
        FXAA = 1,
        /// <summary>2 samples per pixel + FXAA.</summary>
        MSAA2x = 2,
        /// <summary>4 samples per pixel + FXAA.</summary>
        MSAA4x = 3,
    }

    /// <summary>The sun's shadows: more cascades cost more, reach further and stay sharper.</summary>
    public enum ShadowQuality
    {
        Off = 0,
        /// <summary>1 cascade, 40 m.</summary>
        Low = 1,
        /// <summary>2 cascades, 60 m.</summary>
        Medium = 2,
        /// <summary>3 cascades, 80 m.</summary>
        High = 3,
        /// <summary>4 cascades, 120 m.</summary>
        Ultra = 4,
    }

    /// <summary>The player's graphics quality (UserSettings.json). Unity's QualitySettings.</summary>
    public static class QualitySettings
    {
        /// <summary>
        /// The preset the settings match, or Custom. Setting it changes every setting to that
        /// preset; setting Custom does nothing.
        /// </summary>
        public static QualityLevel Level
        {
            get { int v; return GetLevelInternal(out v) != 0 ? (QualityLevel)v : QualityLevel.High; }
            set { SetLevelInternal((int)value); }
        }

        /// <summary>Edge smoothing. Default MSAA4x.</summary>
        public static AntiAliasing AntiAliasing
        {
            get { int v; return GetAntiAliasingInternal(out v) != 0 ? (AntiAliasing)v : AntiAliasing.MSAA4x; }
            set { SetAntiAliasingInternal((int)value); }
        }

        /// <summary>The sun's shadows. Default High.</summary>
        public static ShadowQuality Shadows
        {
            get { int v; return GetShadowQualityInternal(out v) != 0 ? (ShadowQuality)v : ShadowQuality.High; }
            set { SetShadowQualityInternal((int)value); }
        }

        /// <summary>Darkens creases, corners and the ground under objects. Default on.</summary>
        public static bool AmbientOcclusion
        {
            get { int v; return GetAmbientOcclusionInternal(out v) != 0 && v != 0; }
            set { SetAmbientOcclusionInternal(value ? 1 : 0); }
        }

        /// <summary>Glow around bright lights and surfaces. Default on.</summary>
        public static bool Bloom
        {
            get { int v; return GetBloomInternal(out v) != 0 && v != 0; }
            set { SetBloomInternal(value ? 1 : 0); }
        }

        /// <summary>
        /// Light shafts and glow in fog, where the scene's Skybox asks for them. Off = the
        /// cheaper flat fog. Default on.
        /// </summary>
        public static bool VolumetricFog
        {
            get { int v; return GetVolumetricFogInternal(out v) != 0 && v != 0; }
            set { SetVolumetricFogInternal(value ? 1 : 0); }
        }

        /// <summary>
        /// VSync on: no tearing, the frame rate follows the monitor. Off: frames go out as soon
        /// as they are ready. Not part of the presets. Default on.
        /// </summary>
        public static bool VSync
        {
            get { int v; return GetVSyncInternal(out v) == 0 || v != 0; }
            set { SetVSyncInternal(value ? 1 : 0); }
        }

        /// <summary>FrameRateLimit value: the game's own frame rate (EngineConfig targetFPS).</summary>
        public const int FrameRateDefault = -1;
        /// <summary>FrameRateLimit value: no limit (VSync, when on, still holds it to the monitor).</summary>
        public const int FrameRateUnlimited = 0;

        /// <summary>
        /// Frames per second at most: FrameRateDefault, FrameRateUnlimited, or a number
        /// (clamped to 15..1000). Not part of the presets. Default FrameRateDefault.
        /// </summary>
        public static int FrameRateLimit
        {
            get { int v; return GetFrameRateLimitInternal(out v) != 0 ? v : FrameRateDefault; }
            set { SetFrameRateLimitInternal(value); }
        }

        /// <summary>
        /// Brightness: multiplies each scene's own exposure, so dark levels stay darker than
        /// bright ones. 0.5 .. 1.5 (clamped); default 1 = as designed. Not part of the presets.
        /// </summary>
        public static float Brightness
        {
            get { float v; return GetBrightnessInternal(out v) != 0 ? v : 1.0f; }
            set { SetBrightnessInternal(value); }
        }

        /// <summary>
        /// Degrees added to every camera's vertical field of view: -10 .. +30 (clamped);
        /// default 0 = as designed. Camera.FieldOfView still reads and sets the designer's
        /// value; screen-to-world rays include this offset. Not part of the presets.
        /// </summary>
        public static float FieldOfViewOffset
        {
            get { float v; return GetFieldOfViewOffsetInternal(out v) != 0 ? v : 0.0f; }
            set { SetFieldOfViewOffsetInternal(value); }
        }

        /// <summary>Darkened screen corners, where a scene's Skybox uses them. Off = never. Default on.</summary>
        public static bool Vignette
        {
            get { int v; return GetVignetteInternal(out v) == 0 || v != 0; }
            set { SetVignetteInternal(value ? 1 : 0); }
        }

        /// <summary>Film grain noise, where a scene's Skybox uses it. Off = never. Default on.</summary>
        public static bool FilmGrain
        {
            get { int v; return GetFilmGrainInternal(out v) == 0 || v != 0; }
            set { SetFilmGrainInternal(value ? 1 : 0); }
        }

        /// <summary>Colour fringes toward the screen edges, where a scene's Skybox uses them. Off = never. Default on.</summary>
        public static bool ChromaticAberration
        {
            get { int v; return GetChromaticAberrationInternal(out v) == 0 || v != 0; }
            set { SetChromaticAberrationInternal(value ? 1 : 0); }
        }

        /// <summary>Writes every player setting to UserSettings.json. False if it could not.</summary>
        public static bool Save() { return SaveInternal() != 0; }

        /// <summary>Back to the graphics settings last saved (the options menu's Cancel).</summary>
        public static void Revert() { RevertInternal(); }

        /// <summary>Back to the defaults (High). Not saved until Save().</summary>
        public static void ResetToDefaults() { ResetToDefaultsInternal(); }

        // --- internal calls (QualitySettingsBindings.cpp) ------------------------ //

        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetLevelInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetLevelInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetAntiAliasingInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetAntiAliasingInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetShadowQualityInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetShadowQualityInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetAmbientOcclusionInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetAmbientOcclusionInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetBloomInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetBloomInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetVolumetricFogInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetVolumetricFogInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetVSyncInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetVSyncInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetFrameRateLimitInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetFrameRateLimitInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetBrightnessInternal(out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetBrightnessInternal(float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetFieldOfViewOffsetInternal(out float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetFieldOfViewOffsetInternal(float value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetVignetteInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetVignetteInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetFilmGrainInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetFilmGrainInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int GetChromaticAberrationInternal(out int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SetChromaticAberrationInternal(int value);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int SaveInternal();
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int RevertInternal();
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int ResetToDefaultsInternal();
    }
}
