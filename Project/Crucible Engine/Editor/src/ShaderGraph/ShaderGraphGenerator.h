/*!
* @file			ShaderGraphGenerator.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Turns a shader graph into Slang source, after checking that it makes sense.
*
* Generate() does three things:
*
*   1. CHECKS the graph and lists every problem it finds, each with the node (and pin) it is
*      on, so the editor can show it there: a node type that does not exist, a link to a pin
*      that is not there, two links into one pin, a loop, types that do not fit (see
*      ShaderGraphNodes.h for the rules), a texture pin with nothing plugged in, a Parameter
*      node whose parameter is gone, no Output node or more than one.
*      A problem on a node the Output does not use is still listed, but does not stop the
*      shader being made (Problem::stops is false): half-built side branches are normal while
*      a graph is being worked on.
*
*   2. WRITES THE SHADER, if nothing stops it. Only nodes the Output uses are written, each
*      after the nodes it needs, one local variable per output pin that is used:
*
*          // node 3: Panner
*          float2 n3_out = s.uv + P.scrollSpeed * s.time;
*          // node 5: SampleTexture
*          float4 n5_rgba = gTex_pattern.Sample(n3_out);
*          float3 n5_rgb = n5_rgba.rgb;
*          // node 1: Output
*          o.albedo = n5_rgb;
*
*      The result is a whole mesh shader: the surface function above, between
*      #include "Surface.slang" and #include "SurfaceTemplate.slang" (Assets/Shaders), to be
*      compiled with the entry points vertMain and fragMain like any other.
*
*   3. WRITES THE PARAMETER LIST: the graph's parameters as the text of a .params.json file
*      (the engine's Graphics/MaterialShaderParams.h), in the graph's order. Numbers, vectors
*      and colours take the list's "params" slots, textures its "textures" slots.
*
* No Vulkan, no engine, no files: text in, text out.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "ShaderGraph/ShaderGraph.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Crucible::ShaderGraph {

	/// The most parameters of each kind a graph can have (the engine's limits:
	/// MaterialShaderParams::kMaxParams and kMaxTextures).
	inline constexpr size_t kMaxNumberParameters  = 256;
	inline constexpr size_t kMaxTextureParameters = 8;

	struct Problem {
		uint32_t    node = 0;       ///< the node it is on; 0 = the graph as a whole
		std::string pin;            ///< the pin it is on, if it is about one
		std::string message;
		bool        stops = true;   ///< false: on a node the Output does not use - shown, but the shader is still made
	};

	struct Generated {
		bool                  ok = false;    ///< no problem stops it: `source` and `paramsJson` are filled
		std::string           source;        ///< the whole shader, Slang
		std::string           paramsJson;    ///< its parameter list, the text of a .params.json
		std::vector<Problem>  problems;      ///< everything found, stopping or not
		/// For each line of `source` (first line = index 0) the node it was written for, or 0.
		/// A compiler error on a line can be shown on that node.
		std::vector<uint32_t> lineNodes;
	};

	/// Checks `graph` and writes its shader. See the file comment.
	Generated Generate(const Graph& graph);

	/// "node 7 (Lerp), pin t: a float3 cannot go into ..." - a problem as one line for a log.
	std::string Describe(const Graph& graph, const Problem& problem);

}
