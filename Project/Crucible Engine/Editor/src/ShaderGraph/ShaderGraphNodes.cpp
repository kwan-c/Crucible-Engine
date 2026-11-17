/*!
* @file			ShaderGraphNodes.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		The node table - see ShaderGraphNodes.h.
*
* HOW A ROW READS
*
*     { "Lerp", "Math", "Blends from A to B as T goes from 0 to 1.",
*       { { "a", Any, { 0.0f } }, { "b", Any, { 1.0f } }, { "t", Any, { 0.5f } } },
*       { { "out", Any, "lerp({a}, {b}, {t})" } } },
*
*   name, menu, tooltip,
*   the input pins:  { name, type, default }  (or { name, type, {}, "slang expression" } for a
*                    pin that is something of the pixel when nothing is plugged in),
*   the output pins: { name, type, "slang expression" }.
*
* In an expression, {a} is replaced by the value going into pin "a" - a variable, a constant
* or a parameter, never a longer expression, so no extra brackets are needed around it - and
* {T} by the type an "any" node works in. An output may use an output listed before it.
* Functions too long for one line live in Assets/Shaders/GraphNodes.slang (names start with
* "Graph"), which every generated shader includes.
*
* A node's name and its pins' names are what .shadergraph files store. Add rows and pins
* freely; do not rename or remove ones that files already use.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "ShaderGraph/ShaderGraphNodes.h"

#include <set>

namespace Crucible::ShaderGraph {

	namespace {

		constexpr ValueType Float   = ValueType::Float;
		constexpr ValueType Float2  = ValueType::Float2;
		constexpr ValueType Float3  = ValueType::Float3;
		constexpr ValueType Float4  = ValueType::Float4;
		constexpr ValueType Texture = ValueType::Texture;
		constexpr ValueType Any     = ValueType::Any;

		// One input, one output, both "any": out = function(in).
		NodeType Function(const char* name, const char* summary, const char* code) {
			return { name, "Math", summary, { { "in", Any, { 0.0f } } }, { { "out", Any, code } } };
		}

		std::vector<NodeType> MakeTable() {
			return {
				// ---- Output --------------------------------------------------------------------
				{ kOutputNode, "Output", "The surface: what the mesh looks like at this pixel. Lighting, shadows and fog are added by the engine. "
				  "The last pin, vertexOffset, is different: it moves the mesh's vertices (metres, in the world), and its shadow with them.",
				  { { "albedo",    Float3, { 0.8f, 0.8f, 0.8f }, nullptr, true },
				    { "normal",    Float3, { 0.0f, 0.0f, 1.0f } },   // tangent space; (0, 0, 1) = the mesh's own normal
				    { "metallic",  Float,  { 0.0f } },
				    { "roughness", Float,  { 0.5f } },
				    { "emissive",  Float3, { 0.0f, 0.0f, 0.0f }, nullptr, true },
				    { "occlusion", Float,  { 1.0f } },
				    { "alpha",     Float,  { 1.0f } },
				    // Not part of the surface, and no constant to type: it only does something
				    // when a node is plugged in (ShaderGraphGenerator.h, Generated::movesVertices).
				    { kVertexOffsetPin, Float3, { 0.0f, 0.0f, 0.0f }, "float3(0.0, 0.0, 0.0)" } },
				  {} },

				// ---- Input: what the engine knows about the pixel ------------------------------
				{ "UV", "Input", "The mesh's texture coordinates.",
				  {}, { { "out", Float2, "s.uv" } } },
				{ "WorldPosition", "Input", "Where the pixel is in the world.",
				  {}, { { "out", Float3, "s.worldPos" } } },
				{ "WorldNormal", "Input", "The mesh's own normal, in the world (length 1).",
				  {}, { { "out", Float3, "s.normalWS" } } },
				{ "ViewDirection", "Input", "From the pixel towards the camera (length 1).",
				  {}, { { "out", Float3, "s.viewDir" } } },
				{ "VertexColour", "Input", "The colour painted on the mesh's vertices (white if it has none).",
				  {}, { { "out", Float3, "s.vertexColour" } } },
				{ "Time", "Input", "Seconds since the renderer started.",
				  {}, { { "out", Float, "s.time" } } },
				{ "ObjectCustom", "Input", "The object's own three numbers (its MaterialParams component); all zero by default.",
				  {}, { { "out", Float3, "s.custom" } } },
				{ "WorldTangent", "Input", "The direction along the surface in which the texture's U grows, in the world (length 1).",
				  {}, { { "out", Float3, "s.tangentWS" } } },
				{ "WorldBitangent", "Input", "The direction along the surface in which the texture's V grows, in the world (length 1).",
				  {}, { { "out", Float3, "s.bitangentWS" } } },
				// The material's built-in controls (IsMaterialFactorNode). The Inspector greys those
				// out on a graph material - unless the graph has one of these nodes to read them.
				{ "MaterialTint", "Input", "The material's Albedo Tint, set in the Inspector: its colour and its alpha.",
				  {}, { { "rgb", Float3, "pc.albedoTint.rgb" }, { "a", Float, "pc.albedoTint.a" } } },
				{ "MaterialMetallic", "Input", "The material's Metallic slider, set in the Inspector.",
				  {}, { { "out", Float, "pc.metallicFactor" } } },
				{ "MaterialRoughness", "Input", "The material's Roughness slider, set in the Inspector.",
				  {}, { { "out", Float, "pc.roughnessFactor" } } },
				{ "MaterialEmissive", "Input", "The material's Emissive Factor colour, set in the Inspector.",
				  {}, { { "out", Float3, "pc.emissiveFactor" } } },
				{ kParameterNode, "Input", "One of the graph's parameters: a value each material sets for itself.",
				  {}, { { "out", Any, "" } } },   // special: its type and code come from the parameter

				// ---- Constant ------------------------------------------------------------------
				{ "Float", "Constant", "A number.",
				  { { "value", Float, { 0.0f } } }, { { "out", Float, "{value}" } } },
				{ "Vector2", "Constant", "Two numbers as a float2. Also joins two floats into one.",
				  { { "x", Float, { 0.0f } }, { "y", Float, { 0.0f } } },
				  { { "out", Float2, "float2({x}, {y})" } } },
				{ "Vector3", "Constant", "Three numbers as a float3. Also joins three floats into one.",
				  { { "x", Float, { 0.0f } }, { "y", Float, { 0.0f } }, { "z", Float, { 0.0f } } },
				  { { "out", Float3, "float3({x}, {y}, {z})" } } },
				{ "Vector4", "Constant", "Four numbers as a float4. Also joins four floats into one.",
				  { { "x", Float, { 0.0f } }, { "y", Float, { 0.0f } }, { "z", Float, { 0.0f } }, { "w", Float, { 0.0f } } },
				  { { "out", Float4, "float4({x}, {y}, {z}, {w})" } } },
				{ "Colour", "Constant", "A colour.",
				  { { "colour", Float3, { 1.0f, 1.0f, 1.0f }, nullptr, true } }, { { "out", Float3, "{colour}" } } },

				// ---- Math: these work on whatever is plugged in ---------------------------------
				{ "Add", "Math", "A + B.",
				  { { "a", Any, { 0.0f } }, { "b", Any, { 0.0f } } }, { { "out", Any, "{a} + {b}" } } },
				{ "Subtract", "Math", "A - B.",
				  { { "a", Any, { 0.0f } }, { "b", Any, { 0.0f } } }, { { "out", Any, "{a} - {b}" } } },
				{ "Multiply", "Math", "A x B.",
				  { { "a", Any, { 1.0f } }, { "b", Any, { 1.0f } } }, { { "out", Any, "{a} * {b}" } } },
				{ "Divide", "Math", "A / B.",
				  { { "a", Any, { 1.0f } }, { "b", Any, { 1.0f } } }, { { "out", Any, "{a} / {b}" } } },
				{ "Power", "Math", "Base to the power of Exponent. A base below 0 counts as 0.",
				  { { "base", Any, { 0.0f } }, { "exponent", Any, { 2.0f } } },
				  { { "out", Any, "pow(max({base}, {T}(0.0)), {exponent})" } } },
				Function("SquareRoot", "The square root. A value below 0 counts as 0.", "sqrt(max({in}, {T}(0.0)))"),
				Function("Abs", "Drops the sign: -2 becomes 2.", "abs({in})"),
				Function("Negate", "Flips the sign.", "-{in}"),
				Function("OneMinus", "1 - the value: turns a 0..1 mask inside out.", "1.0 - {in}"),
				{ "Min", "Math", "The smaller of A and B.",
				  { { "a", Any, { 0.0f } }, { "b", Any, { 0.0f } } }, { { "out", Any, "min({a}, {b})" } } },
				{ "Max", "Math", "The larger of A and B.",
				  { { "a", Any, { 0.0f } }, { "b", Any, { 0.0f } } }, { { "out", Any, "max({a}, {b})" } } },
				{ "Clamp", "Math", "Keeps the value between Min and Max.",
				  { { "in", Any, { 0.0f } }, { "min", Any, { 0.0f } }, { "max", Any, { 1.0f } } },
				  { { "out", Any, "clamp({in}, {min}, {max})" } } },
				Function("Saturate", "Keeps the value between 0 and 1.", "saturate({in})"),
				{ "Lerp", "Math", "Blends from A to B as T goes from 0 to 1.",
				  { { "a", Any, { 0.0f } }, { "b", Any, { 1.0f } }, { "t", Any, { 0.5f } } },
				  { { "out", Any, "lerp({a}, {b}, {t})" } } },
				{ "Step", "Math", "0 below Edge, 1 from Edge up: a hard cut.",
				  { { "edge", Any, { 0.5f } }, { "in", Any, { 0.0f } } }, { { "out", Any, "step({edge}, {in})" } } },
				{ "Smoothstep", "Math", "0 below Min, 1 above Max, a smooth ramp between: a soft cut.",
				  { { "min", Any, { 0.0f } }, { "max", Any, { 1.0f } }, { "in", Any, { 0.0f } } },
				  { { "out", Any, "smoothstep({min}, {max}, {in})" } } },
				{ "Remap", "Math", "Moves the value from the range In Min..In Max to the range Out Min..Out Max.",
				  { { "in", Any, { 0.0f } }, { "inMin", Any, { 0.0f } }, { "inMax", Any, { 1.0f } },
				    { "outMin", Any, { 0.0f } }, { "outMax", Any, { 1.0f } } },
				  { { "out", Any, "{outMin} + ({in} - {inMin}) * ({outMax} - {outMin}) / ({inMax} - {inMin})" } } },
				Function("Floor", "Rounds down to a whole number.", "floor({in})"),
				Function("Fraction", "What is left after the whole number: 2.7 gives 0.7. Makes a value repeat.", "frac({in})"),
				Function("Sine", "The sine (the value is in radians): swings between -1 and 1.", "sin({in})"),
				Function("Cosine", "The cosine (the value is in radians): swings between -1 and 1.", "cos({in})"),
				Function("Ceiling", "Rounds up to a whole number.", "ceil({in})"),
				Function("Round", "Rounds to the nearest whole number.", "round({in})"),
				Function("Sign", "-1 below zero, 1 above zero, 0 at zero.", "sign({in})"),
				Function("Exp", "e to the power of the value.", "exp({in})"),
				Function("Log", "The natural logarithm. A value at or below 0 counts as a very small one.", "log(max({in}, {T}(0.000001)))"),
				{ "Modulo", "Math", "What is left of A after taking out whole Bs: makes a value repeat every B.",
				  { { "a", Any, { 0.0f } }, { "b", Any, { 1.0f } } }, { { "out", Any, "{a} - {b} * floor({a} / {b})" } } },
				{ "InverseLerp", "Math", "Where the value sits between A and B: 0 at A, 1 at B. The opposite of Lerp.",
				  { { "a", Any, { 0.0f } }, { "b", Any, { 1.0f } }, { "in", Any, { 0.0f } } },
				  { { "out", Any, "({in} - {a}) / ({b} - {a})" } } },

				// ---- Colour --------------------------------------------------------------------
				{ "Luminance", "Colour", "How bright a colour looks, as one number.",
				  { { "in", Float3, { 1.0f, 1.0f, 1.0f }, nullptr, true } }, { { "out", Float, "GraphLuminance({in})" } } },
				{ "Saturation", "Colour", "0 makes the colour grey, 1 leaves it as it is, above 1 makes it more vivid.",
				  { { "in", Float3, { 1.0f, 1.0f, 1.0f }, nullptr, true }, { "saturation", Float, { 1.0f } } },
				  { { "out", Float3, "lerp(float3(GraphLuminance({in})), {in}, float3({saturation}))" } } },
				{ "HueShift", "Colour", "Turns the colour round the colour wheel: 0.5 is the opposite colour, 1 is back where it started.",
				  { { "in", Float3, { 1.0f, 1.0f, 1.0f }, nullptr, true }, { "shift", Float, { 0.0f } } },
				  { { "out", Float3, "GraphHueShift({in}, {shift})" } } },
				{ "Contrast", "Colour", "Pushes values away from the middle (above 1) or towards it (below 1).",
				  { { "in", Any, { 0.5f } }, { "contrast", Any, { 1.0f } } }, { { "out", Any, "({in} - 0.5) * {contrast} + 0.5" } } },
				{ "Posterize", "Colour", "Rounds the value down to a number of even steps: bands instead of a smooth ramp.",
				  { { "in", Any, { 0.0f } }, { "steps", Any, { 4.0f } } }, { { "out", Any, "floor({in} * {steps}) / {steps}" } } },

				// ---- Vector --------------------------------------------------------------------
				{ "Split", "Vector", "Takes a vector apart: single numbers, or its first two or three.",
				  { { "in", Any, { 0.0f } } },
				  { { "x",   Float,  "({in}).x",   1 },
				    { "y",   Float,  "({in}).y",   2 },
				    { "z",   Float,  "({in}).z",   3 },
				    { "w",   Float,  "({in}).w",   4 },
				    { "xy",  Float2, "({in}).xy",  2 },
				    { "xyz", Float3, "({in}).xyz", 3 } } },
				{ "Dot", "Vector", "The dot product: how much two directions agree (1 = the same, 0 = at right angles, for directions of length 1).",
				  { { "a", Any, { 0.0f } }, { "b", Any, { 0.0f } } }, { { "out", Float, "dot({a}, {b})" } }, 2 },
				{ "Cross", "Vector", "The cross product: a direction at right angles to both.",
				  { { "a", Float3, { 1.0f, 0.0f, 0.0f } }, { "b", Float3, { 0.0f, 1.0f, 0.0f } } },
				  { { "out", Float3, "cross({a}, {b})" } } },
				{ "Normalize", "Vector", "The same direction with length 1.",
				  { { "in", Any, { 0.0f } } }, { { "out", Any, "normalize({in})" } }, 2 },
				{ "Length", "Vector", "How long a vector is.",
				  { { "in", Any, { 0.0f } } }, { { "out", Float, "length({in})" } }, 2 },
				{ "Distance", "Vector", "How far apart two points are.",
				  { { "a", Any, { 0.0f } }, { "b", Any, { 0.0f } } }, { { "out", Float, "distance({a}, {b})" } }, 2 },
				{ "AppendZ", "Vector", "A float2 and one more number make a float3.",
				  { { "xy", Float2, { 0.0f, 0.0f } }, { "z", Float, { 0.0f } } }, { { "out", Float3, "float3({xy}, {z})" } } },
				{ "AppendW", "Vector", "A float3 and one more number make a float4.",
				  { { "xyz", Float3, { 0.0f, 0.0f, 0.0f } }, { "w", Float, { 1.0f } } }, { { "out", Float4, "float4({xyz}, {w})" } } },
				{ "Reflect", "Vector", "A direction bounced off a surface with the given normal.",
				  { { "in", Float3, { 0.0f, -1.0f, 0.0f } }, { "normal", Float3, {}, "s.normalWS" } },
				  { { "out", Float3, "reflect({in}, {normal})" } } },
				{ "NormalBlend", "Vector", "Lays one tangent-space normal over another (detail over base), for the Output's normal.",
				  { { "base", Float3, { 0.0f, 0.0f, 1.0f } }, { "detail", Float3, { 0.0f, 0.0f, 1.0f } } },
				  { { "out", Float3, "GraphBlendNormals({base}, {detail})" } } },

				// ---- Texture -------------------------------------------------------------------
				{ "SampleTexture", "Texture", "Reads a texture parameter at UV.",
				  { { "texture", Texture }, { "uv", Float2, {}, "s.uv" } },
				  { { "rgba", Float4, "{texture}.Sample({uv})", 0, "{texture}.SampleLevel({uv}, 0.0)" },
				    { "rgb",  Float3, "{rgba}.rgb" },
				    { "r",    Float,  "{rgba}.r" },
				    { "g",    Float,  "{rgba}.g" },
				    { "b",    Float,  "{rgba}.b" },
				    { "a",    Float,  "{rgba}.a" } } },
				{ "NormalMap", "Texture", "Reads a normal map (a texture parameter that is not a colour) for the Output's normal.",
				  { { "texture", Texture }, { "uv", Float2, {}, "s.uv" }, { "strength", Float, { 1.0f } } },
				  { { "out", Float3, "GraphUnpackNormal({texture}.Sample({uv}).xy, {strength})", 0,
				      "GraphUnpackNormal({texture}.SampleLevel({uv}, 0.0).xy, {strength})" } } },
				{ "TriplanarTexture", "Texture", "Reads a texture by world position from three sides, blended by the normal: needs no UVs and does not stretch.",
				  { { "texture", Texture }, { "scale", Float, { 1.0f } }, { "sharpness", Float, { 4.0f } } },
				  { { "rgba", Float4, "GraphTriplanar({texture}, s.worldPos, s.normalWS, {scale}, {sharpness})", 0,
				      "GraphTriplanarVertex({texture}, s.worldPos, s.normalWS, {scale}, {sharpness})" },
				    { "rgb",  Float3, "{rgba}.rgb" },
				    { "a",    Float,  "{rgba}.a" } } },

				// ---- UV ------------------------------------------------------------------------
				{ "TilingOffset", "UV", "Repeats (Tiling) and shifts (Offset) texture coordinates.",
				  { { "uv", Float2, {}, "s.uv" }, { "tiling", Float2, { 1.0f, 1.0f } }, { "offset", Float2, { 0.0f, 0.0f } } },
				  { { "out", Float2, "{uv} * {tiling} + {offset}" } } },
				{ "Panner", "UV", "Slides texture coordinates over time: Speed is in repeats per second.",
				  { { "uv", Float2, {}, "s.uv" }, { "speed", Float2, { 0.1f, 0.0f } }, { "time", Float, {}, "s.time" } },
				  { { "out", Float2, "{uv} + {speed} * {time}" } } },
				{ "RotateUV", "UV", "Turns texture coordinates round a centre. The angle is in turns: 0.25 is a quarter turn.",
				  { { "uv", Float2, {}, "s.uv" }, { "centre", Float2, { 0.5f, 0.5f } }, { "turns", Float, { 0.0f } } },
				  { { "out", Float2, "GraphRotate({uv}, {centre}, {turns})" } } },
				{ "PolarUV", "UV", "Texture coordinates as an angle (0..1 round the centre) and a distance from it: rings, swirls, radial sweeps.",
				  { { "uv", Float2, {}, "s.uv" }, { "centre", Float2, { 0.5f, 0.5f } } },
				  { { "angle",    Float, "GraphPolar({uv}, {centre}).x" },
				    { "distance", Float, "GraphPolar({uv}, {centre}).y" } } },

				// ---- Effect --------------------------------------------------------------------
				{ "Fresnel", "Effect", "0 where the surface faces the camera, 1 at its edges: rim glow. A higher Power keeps it nearer the edge.",
				  { { "normal", Float3, {}, "s.normalWS" }, { "power", Float, { 5.0f } } },
				  { { "out", Float, "GraphFresnel({normal}, s.viewDir, {power})" } } },
				{ "Noise", "Effect", "Soft random blotches between 0 and 1. A higher Scale makes them smaller.",
				  { { "uv", Float2, {}, "s.uv" }, { "scale", Float, { 8.0f } } },
				  { { "out", Float, "GraphNoise({uv} * {scale})" } } },
				{ "GradientNoise", "Effect", "Smooth noise between 0 and 1 without the Noise node's blocky look. A higher Scale makes it finer.",
				  { { "uv", Float2, {}, "s.uv" }, { "scale", Float, { 8.0f } } },
				  { { "out", Float, "GraphGradientNoise({uv} * {scale})" } } },
				{ "Clouds", "Effect", "Four layers of noise, each finer and fainter than the last: clouds, smoke, marble. Between 0 and 1.",
				  { { "uv", Float2, {}, "s.uv" }, { "scale", Float, { 4.0f } } },
				  { { "out", Float, "GraphClouds({uv} * {scale})" } } },
				{ "Voronoi", "Effect", "Cells: the distance to the nearest cell's centre, and a random number (0..1) for each cell.",
				  { { "uv", Float2, {}, "s.uv" }, { "scale", Float, { 6.0f } } },
				  { { "distance", Float, "GraphVoronoi({uv} * {scale}).x" },
				    { "cell",     Float, "GraphVoronoi({uv} * {scale}).y" } } },
				{ "Checker", "Effect", "A checkerboard of 0 and 1.",
				  { { "uv", Float2, {}, "s.uv" }, { "scale", Float, { 8.0f } } },
				  { { "out", Float, "GraphChecker({uv} * {scale})" } } },
				{ "Pulse", "Effect", "Swings smoothly between 0 and 1 over time, Speed times a second.",
				  { { "speed", Float, { 1.0f } }, { "time", Float, {}, "s.time" } },
				  { { "out", Float, "0.5 + 0.5 * sin({time} * {speed} * 6.2831853)" } } },

				// ---- Utility ----
				// A point a link passes through, to route it tidily: what goes in comes out. Drawn as a
				// dot (ShaderGraphView). Numbers and vectors only - not a texture.
				{ kRerouteNode, "Utility", "A point to route a link through. What goes in comes out (numbers and vectors).",
				  { { "in", Any, { 0.0f } } }, { { "out", Any, "{in}" } } },
			};
		}

	}   // anonymous namespace

	const char* ValueTypeName(ValueType type) {
		switch (type) {
		case ValueType::Float:   return "float";
		case ValueType::Float2:  return "float2";
		case ValueType::Float3:  return "float3";
		case ValueType::Float4:  return "float4";
		case ValueType::Texture: return "texture";
		default:                 return "any";
		}
	}

	uint32_t ComponentCount(ValueType type) {
		switch (type) {
		case ValueType::Float:  return 1;
		case ValueType::Float2: return 2;
		case ValueType::Float3: return 3;
		case ValueType::Float4: return 4;
		default:                return 0;
		}
	}

	ValueType NumericType(uint32_t components) {
		switch (components) {
		case 2:  return ValueType::Float2;
		case 3:  return ValueType::Float3;
		case 4:  return ValueType::Float4;
		default: return ValueType::Float;
		}
	}

	int NodeType::FindInput(std::string_view pin) const {
		for (size_t i = 0; i < inputs.size(); ++i) {
			if (pin == inputs[i].name) { return static_cast<int>(i); }
		}
		return -1;
	}

	int NodeType::FindOutput(std::string_view pin) const {
		for (size_t i = 0; i < outputs.size(); ++i) {
			if (pin == outputs[i].name) { return static_cast<int>(i); }
		}
		return -1;
	}

	bool IsMaterialFactorNode(std::string_view type) {
		return type == "MaterialTint" || type == "MaterialMetallic" || type == "MaterialRoughness" || type == "MaterialEmissive";
	}

	const std::vector<NodeType>& NodeTypes() {
		static const std::vector<NodeType> table = MakeTable();
		return table;
	}

	const NodeType* FindNodeType(std::string_view name) {
		for (const NodeType& type : NodeTypes()) {
			if (name == type.name) { return &type; }
		}
		return nullptr;
	}

	bool CheckNodeTable(std::string& error) {
		std::set<std::string> names;
		for (const NodeType& type : NodeTypes()) {
			const std::string name = type.name;
			if (!names.insert(name).second) {
				error = "two rows are called " + name;
				return false;
			}

			std::set<std::string> pins;
			bool anyInput = false;
			for (const InputPin& pin : type.inputs) {
				if (!pins.insert(pin.name).second || std::string_view(pin.name) == "T") {
					error = name + ": the pin name \"" + pin.name + "\" is used twice, or is T";
					return false;
				}
				anyInput = anyInput || pin.type == ValueType::Any;
				if (pin.type == ValueType::Texture && pin.defaultCode) {
					error = name + "." + pin.name + ": a texture pin cannot have a default";
					return false;
				}
			}

			for (size_t i = 0; i < type.outputs.size(); ++i) {
				const OutputPin& pin = type.outputs[i];
				if (!pins.insert(pin.name).second || std::string_view(pin.name) == "T") {
					error = name + ": the pin name \"" + pin.name + "\" is used twice, or is T";
					return false;
				}
				if (name == kParameterNode) { continue; }   // its type and code come from the parameter
				if ((pin.type == ValueType::Any || pin.needs != 0) && !anyInput) {
					error = name + "." + pin.name + ": its type depends on \"any\" inputs, and the row has none";
					return false;
				}

				// The code for the vertex offset names nothing its usual code does not: what a
				// node reads is worked out from the usual code.
				if (pin.vertexCode) {
					const std::string_view usual = pin.code, other = pin.vertexCode;
					for (size_t open = other.find('{'); open != std::string_view::npos; open = other.find('{', open + 1)) {
						const size_t close = other.find('}', open);
						if (close == std::string_view::npos || usual.find(other.substr(open, close - open + 1)) == std::string_view::npos) {
							error = name + "." + pin.name + ": its vertex code uses a {placeholder} its code does not";
							return false;
						}
					}
				}

				// Every {placeholder} is an input, an earlier output, or T.
				const std::string_view code = pin.code;
				if (code.empty()) {
					error = name + "." + pin.name + " has no code";
					return false;
				}
				for (size_t open = code.find('{'); open != std::string_view::npos; open = code.find('{', open + 1)) {
					const size_t close = code.find('}', open);
					if (close == std::string_view::npos) {
						error = name + "." + pin.name + ": a { is never closed";
						return false;
					}
					const std::string_view word = code.substr(open + 1, close - open - 1);
					if (word == "T") {
						if (!anyInput) {
							error = name + "." + pin.name + " uses {T}, and the row has no \"any\" input";
							return false;
						}
						continue;
					}
					const int output = type.FindOutput(word);
					if (type.FindInput(word) < 0 && (output < 0 || static_cast<size_t>(output) >= i)) {
						error = name + "." + pin.name + ": {" + std::string(word) + "} is not an input or an earlier output of the row";
						return false;
					}
				}
			}
		}
		if (!FindNodeType(kOutputNode) || !FindNodeType(kParameterNode)) {
			error = "the Output or the Parameter row is missing";
			return false;
		}
		return true;
	}

}
