/*!
* @file			ImageDecoders.cpp
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		The installed image decoders (see ImageDecoders.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "Resources/ImageDecoders.h"

namespace Crucible {

	namespace {
		ImageDecoders g_decoders;   // set once at startup, before anything loads
	}

	void SetImageDecoders(const ImageDecoders& decoders) { g_decoders = decoders; }

	const ImageDecoders& GetImageDecoders() { return g_decoders; }

}
