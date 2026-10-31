/*!
* @file			EditorImageDecoders.h
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		Installs the editor's stb_image decoders into the engine (Resources/ImageDecoders.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

namespace Crucible {

	/// Call once at startup, before Core::Init loads anything.
	void InstallEditorImageDecoders();

}
