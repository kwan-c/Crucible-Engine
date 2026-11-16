/*!
* @file			MaterialShaderUI.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		The Inspector's controls for a material's shader, and for that shader's own
*				parameters and textures.
*
* One function, called from the Inspector's material section with the copy of the material it
* is editing:
*
*     changed |= DrawMaterialShaderParams(m_renderer, data);
*
* It draws:
*   - "Shader": the built-in shader, or one of the mesh shaders in Assets/Shaders (a surface
*     function followed by SurfaceTemplate.slang - MagicOrb.slang, later every shader graph);
*   - one control per entry of the shader's parameter list (<ShaderName>.params.json, see
*     Graphics/MaterialShaderParams.h): a slider for a number, drag boxes for a vector, a colour
*     picker for a colour, and a drop target for each texture, taking a texture dragged from
*     the Asset Browser.
* Nothing below "Shader" is drawn for a shader without a list (the built-in one).
*
* The controls are built from the list each frame, so they follow it when the shader is edited
* while the Editor runs. Right-click a control to put it back to the list's default.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

namespace Crucible {

	class Renderer;
	struct MaterialData;

	/*********************************************************************
	 \brief  Draws the controls into the current ImGui window.
	 \param  renderer  Where the shaders and their lists are.
	 \param  data      The material being edited: its shader, parameter
	                   values and shader textures are changed in place.
	 \return true if `data` was changed this frame (the caller then hands
	         it to Renderer::UpdateMaterial, as for its own controls).
	 *********************************************************************/
	bool DrawMaterialShaderParams(Renderer& renderer, MaterialData& data);

}
