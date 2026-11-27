/*************************************************************************
* \file:         ScreenBindings.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          2 November 2026
* \version       1.0
* \brief         Internal calls behind the static C# Screen class
*                (ManagedScripts/Runtime/Screen.cs): the window mode.
*
* \details
*    Goes through Core (SetFullscreen / MaximizeWindow / RestoreWindow),
*    which also saves the mode to UserSettings.json for the next launch -
*    the same path F11 and the editor's menu use.
*
*    In the EDITOR (EngineConfig::app == AppKind::Editor) every change is
*    ignored, as in Unity: the window there is the editor itself, and a
*    script would otherwise leave it fullscreen after Stop and save that as
*    the editor's mode. The first ignored call logs once. Reading the mode
*    works in both apps.
*
*    Also the windowed size and the fullscreen resolution (Core::
*    SetWindowedSize / SetFullscreenResolution, saved the same way), and the
*    monitor's resolution list for an options menu.
*
*    Mode numbers (C# WindowMode): 0 Windowed, 1 Maximized, 2 Fullscreen.
*
**************************************************************************/
#include "Scripting/ScriptBindings.h"
#include "Core.h"
#include "Configuration/EngineConfig.h"
#include "Diagnostics/Logger.h"
#if CRUCIBLE_WITH_MONO
#include <vector>
#include <mono/metadata/loader.h>

namespace Crucible::ScriptBindings
{
	namespace
	{
		thread_local Core* core = nullptr;
		bool loggedEditorIgnore = false;

		enum class WindowMode : int32_t
		{
			Windowed = 0,
			Maximized = 1,
			Fullscreen = 2,
		};

		bool IsEditor()
		{
			return core && core->GetConfig().app == AppKind::Editor;
		}

		/*********************************************************************
		 \brief  True if a change may be made now; in the editor logs once
		         and returns false.
		 *********************************************************************/
		bool MayChange()
		{
			if (!core)
				return false;
			if (!IsEditor())
				return true;
			if (!loggedEditorIgnore)
			{
				loggedEditorIgnore = true;
				CRU_INFO("[Scripting] Screen: window changes are ignored in the editor - they apply in the game build");
			}
			return false;
		}

		/*********************************************************************
		 \brief  Puts the window in `mode`. Leaves fullscreen first, so
		         Maximized / Windowed act on the real window, not the
		         remembered state. Callers check MayChange.
		 *********************************************************************/
		void ApplyMode(WindowMode mode)
		{
			switch (mode)
			{
			case WindowMode::Fullscreen:
				core->SetFullscreen(true);
				break;
			case WindowMode::Maximized:
				core->SetFullscreen(false);
				if (!core->IsWindowMaximized())
					core->MaximizeWindow();
				break;
			case WindowMode::Windowed:
				core->SetFullscreen(false);
				if (core->IsWindowMaximized())
					core->RestoreWindow();
				break;
			}
		}

		/*********************************************************************
		 \brief  C# Screen.Mode (get). Fullscreen wins over maximized (a
		         fullscreen window remembers whether to come back maximized).
		 *********************************************************************/
		int32_t ManagedScreenGetMode(int32_t* out) noexcept
		{
			try
			{
				if (!out || !core)
					return 0;
				WindowMode mode = WindowMode::Windowed;
				if (core->IsFullscreen())
					mode = WindowMode::Fullscreen;
				else if (core->IsWindowMaximized())
					mode = WindowMode::Maximized;
				*out = static_cast<int32_t>(mode);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Screen.Mode (set). Returns 0 in the editor (ignored) or
		         for an unknown mode. Leaves fullscreen first, so Maximized /
		         Windowed act on the real window, not the remembered state.
		 *********************************************************************/
		int32_t ManagedScreenSetMode(int32_t value) noexcept
		{
			try
			{
				if (value < 0 || value > static_cast<int32_t>(WindowMode::Fullscreen))
					return 0;
				if (!MayChange())
					return 0;
				ApplyMode(static_cast<WindowMode>(value));
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Screen.SetResolution(width, height, mode, refreshRate).
		         Fullscreen: switches the monitor to width x height (it must
		         be one of Screen.Resolutions). Windowed / Maximized: sets the
		         windowed size (Maximized uses it when restored). Returns 0 in
		         the editor, or for a size the monitor does not offer.
		 *********************************************************************/
		int32_t ManagedScreenSetResolution(int32_t width, int32_t height, int32_t mode, int32_t refreshRate) noexcept
		{
			try
			{
				if (width <= 0 || height <= 0 || mode < 0 || mode > static_cast<int32_t>(WindowMode::Fullscreen))
					return 0;
				if (!MayChange())
					return 0;
				switch (static_cast<WindowMode>(mode))
				{
				case WindowMode::Fullscreen:
					if (!core->SetFullscreenResolution(width, height, refreshRate))
						return 0;
					core->SetFullscreen(true);
					break;
				case WindowMode::Maximized:
					core->SetWindowedSize(width, height);   // remembered while maximized
					ApplyMode(WindowMode::Maximized);
					break;
				case WindowMode::Windowed:
					ApplyMode(WindowMode::Windowed);         // back to a normal window first,
					core->SetWindowedSize(width, height);   // so the size applies now
					break;
				}
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Screen.SetBorderlessFullscreen: fullscreen at the
		         monitor's own resolution (forgets a chosen one).
		 *********************************************************************/
		int32_t ManagedScreenSetBorderless() noexcept
		{
			try
			{
				if (!MayChange())
					return 0;
				core->SetFullscreenResolution(0, 0);
				core->SetFullscreen(true);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Screen.WindowedWidth / Height, FullscreenWidth / Height /
		         RefreshRate (0 = borderless), in one call.
		 *********************************************************************/
		int32_t ManagedScreenGetSizes(int32_t* windowedW, int32_t* windowedH, int32_t* fullW, int32_t* fullH,
		                              int32_t* fullRefresh) noexcept
		{
			try
			{
				if (!windowedW || !windowedH || !fullW || !fullH || !fullRefresh || !core)
					return 0;
				*windowedW = core->GetWindowedWidth();
				*windowedH = core->GetWindowedHeight();
				*fullW = core->GetFullscreenWidth();
				*fullH = core->GetFullscreenHeight();
				*fullRefresh = core->GetFullscreenRefreshRate();
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		// Screen.Resolutions: the count call fills this, the index calls read it.
		std::vector<WindowResolution> resolutions;

		int32_t ManagedScreenGetResolutionCount(int32_t* out) noexcept
		{
			try
			{
				if (!out || !core)
					return 0;
				resolutions = core->GetScreenResolutions();
				*out = static_cast<int32_t>(resolutions.size());
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedScreenGetResolution(int32_t index, int32_t* width, int32_t* height, int32_t* refreshRate) noexcept
		{
			try
			{
				if (!width || !height || !refreshRate || index < 0 || index >= static_cast<int32_t>(resolutions.size()))
					return 0;
				const WindowResolution& r = resolutions[static_cast<std::size_t>(index)];
				*width = r.width;
				*height = r.height;
				*refreshRate = r.refreshRate;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Screen.FullScreen (set): only fullscreen on / off;
		         leaving it goes back to maximized or windowed, whichever the
		         window was before.
		 *********************************************************************/
		int32_t ManagedScreenSetFullScreen(int32_t value) noexcept
		{
			try
			{
				if (!MayChange())
					return 0;
				core->SetFullscreen(value != 0);
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		int32_t ManagedScreenIsEditor(int32_t* out) noexcept
		{
			try
			{
				if (!out || !core)
					return 0;
				*out = IsEditor() ? 1 : 0;
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		/*********************************************************************
		 \brief  C# Screen.AspectRatioLock: width / height of the locked
		         window shape (the game locks it to the project's), or 0.
		 *********************************************************************/
		int32_t ManagedScreenGetAspectLock(float* out) noexcept
		{
			try
			{
				if (!out || !core)
					return 0;
				*out = core->GetWindowAspectRatioLock();
				return 1;
			}
			catch (...)
			{
				return 0;
			}
		}

		template <typename Fn>
		void Add(const char* name, Fn* function)
		{
			mono_add_internal_call(name, reinterpret_cast<const void*>(function));
		}
	}

	void SetCore(Core* engine)
	{
		core = engine;
		resolutions.clear();
		loggedEditorIgnore = false;
	}

	Core* CurrentCore()
	{
		return core;
	}

	void RegisterScreenBindings()
	{
		Add("Crucible.Screen::GetModeInternal", &ManagedScreenGetMode);
		Add("Crucible.Screen::SetModeInternal", &ManagedScreenSetMode);
		Add("Crucible.Screen::SetFullScreenInternal", &ManagedScreenSetFullScreen);
		Add("Crucible.Screen::IsEditorInternal", &ManagedScreenIsEditor);
		Add("Crucible.Screen::GetAspectLockInternal", &ManagedScreenGetAspectLock);
		Add("Crucible.Screen::SetResolutionInternal", &ManagedScreenSetResolution);
		Add("Crucible.Screen::SetBorderlessInternal", &ManagedScreenSetBorderless);
		Add("Crucible.Screen::GetSizesInternal", &ManagedScreenGetSizes);
		Add("Crucible.Screen::GetResolutionCountInternal", &ManagedScreenGetResolutionCount);
		Add("Crucible.Screen::GetResolutionInternal", &ManagedScreenGetResolution);
	}
}
#endif
