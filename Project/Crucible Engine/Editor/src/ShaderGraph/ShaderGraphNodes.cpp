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
#include <string>

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
				{ kOutputNode, "Output", "The surface: what the mesh looks like at this pixel. Lighting, shadows and fog are added by the engine.",
				  { { "albedo",    Float3, { 0.8f, 0.8f, 0.8f }, nullptr, true },
				    { "normal",    Float3, { 0.0f, 0.0f, 1.0f } },   // tangent space; (0, 0, 1) = the mesh's own normal
				    { "metallic",  Float,  { 0.0f } },
				    { "roughness", Float,  { 0.5f } },
				    { "emissive",  Float3, { 0.0f, 0.0f, 0.0f }, nullptr, true },
				    { "occlusion", Float,  { 1.0f } },
				    { "alpha",     Float,  { 1.0f } } },
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

				// ---- Texture -------------------------------------------------------------------
				{ "SampleTexture", "Texture", "Reads a texture parameter at UV.",
				  { { "texture", Texture }, { "uv", Float2, {}, "s.uv" } },
				  { { "rgba", Float4, "{texture}.Sample({uv})" },
				    { "rgb",  Float3, "{rgba}.rgb" },
				    { "r",    Float,  "{rgba}.r" },
				    { "g",    Float,  "{rgba}.g" },
				    { "b",    Float,  "{rgba}.b" },
				    { "a",    Float,  "{rgba}.a" } } },
				{ "NormalMap", "Texture", "Reads a normal map (a texture parameter that is not a colour) for the Output's normal.",
				  { { "texture", Texture }, { "uv", Float2, {}, "s.uv" }, { "strength", Float, { 1.0f } } },
				  { { "out", Float3, "GraphUnpackNormal({texture}.Sample({uv}).xy, {strength})" } } },

				// ---- UV ------------------------------------------------------------------------
				{ "TilingOffset", "UV", "Repeats (Tiling) and shifts (Offset) texture coordinates.",
				  { { "uv", Float2, {}, "s.uv" }, { "tiling", Float2, { 1.0f, 1.0f } }, { "offset", Float2, { 0.0f, 0.0f } } },
				  { { "out", Float2, "{uv} * {tiling} + {offset}" } } },
				{ "Panner", "UV", "Slides texture coordinates over time: Speed is in repeats per second.",
				  { { "uv", Float2, {}, "s.uv" }, { "speed", Float2, { 0.1f, 0.0f } }, { "time", Float, {}, "s.time" } },
				  { { "out", Float2, "{uv} + {speed} * {time}" } } },

				// ---- Effect --------------------------------------------------------------------
				{ "Fresnel", "Effect", "0 where the surface faces the camera, 1 at its edges: rim glow. A higher Power keeps it nearer the edge.",
				  { { "normal", Float3, {}, "s.normalWS" }, { "power", Float, { 5.0f } } },
				  { { "out", Float, "GraphFresnel({normal}, s.viewDir, {power})" } } },
				{ "Noise", "Effect", "Soft random blotches between 0 and 1. A higher Scale makes them smaller.",
				  { { "uv", Float2, {}, "s.uv" }, { "scale", Float, { 8.0f } } },
				  { { "out", Float, "GraphNoise({uv} * {scale})" } } },
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
