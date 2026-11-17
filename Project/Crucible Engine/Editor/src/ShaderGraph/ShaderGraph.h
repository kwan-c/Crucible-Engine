/*!
* @file			ShaderGraph.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		A shader graph as data: nodes, the links between their pins, and the
*				parameters a material using the graph can set. Plus its file (.shadergraph).
*
* A graph describes one thing: a SURFACE function - what a mesh looks like at a pixel (albedo,
* normal, metallic, roughness, emissive, occlusion, alpha). ShaderGraphCompiler turns it into
* Slang source that ends with the engine's mesh shader (SurfaceTemplate.slang), so lighting,
* shadows, fog and outlines are never the graph's business.
*
*   - A NODE is one box: an instance of a row of the node table (ShaderGraphNodes.h - "Lerp",
*     "SampleTexture", "Output"). It has an id that is never reused, a place on the canvas, a
*     constant for each input pin that has nothing plugged in, and for a few node types a
*     setting (a Parameter node's "parameter").
*   - A LINK joins an output pin of one node to an input pin of another. Pins are named, so a
*     file keeps loading when the table's rows gain pins.
*   - A PARAMETER is a value a material sets: a number, a vector, a colour or a texture. The
*     graph's parameters become the shader's parameter list (the engine's
*     Graphics/MaterialShaderParams.h), which is what the Inspector and .mat files use.
*
* This file knows nothing about Vulkan, the renderer or ImGui: it is plain data and JSON, so
* it can be tested alone and used by tools (ShaderGraphTests, later Cook).
*
* The .shadergraph file (JSON):
*
*     {
*       "version": 1,
*       "domain": "surface",
*       "nextId": 4,
*       "parameters": [
*         { "name": "tint", "type": "colour", "default": [1.0, 0.5, 0.2] },
*         { "name": "mask", "type": "texture", "colour": false, "default": "white" }
*       ],
*       "nodes": [
*         { "id": 1, "type": "Output", "x": 400.0, "y": 0.0, "inputs": { "roughness": 0.4 } },
*         { "id": 2, "type": "Parameter", "x": 0.0, "y": 0.0, "settings": { "parameter": "tint" } }
*       ],
*       "links": [ { "from": [2, "out"], "to": [1, "albedo"] } ],
*       "comments": [ { "id": 3, "x": -20.0, "y": -40.0, "w": 480.0, "h": 200.0, "text": "Tint", "colour": [0.3, 0.4, 0.6] } ]
*     }
*
* "comments" are the boxes drawn behind groups of nodes on the canvas, with a title. They are
* only for the people reading the graph: the shader does not change with them. Written only when
* a graph has some, so a graph without any is the same file as before they existed.
*
* Every key but "version" is optional, and unknown keys are ignored, so files written by an
* older Editor keep loading. A file with a higher version than this code knows is refused.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Crucible::ShaderGraph {

	/// The .shadergraph format this code writes (and the newest it reads).
	inline constexpr int kFormatVersion = 1;

	/// What a graph is for. "surface": a mesh surface, a shader (see the file comment).
	inline constexpr const char* kSurfaceDomain = "surface";
	/// "subgraph": a group of nodes saved as a file of its own (.shadersubgraph) and used in other
	/// graphs as one node. It has "inputs" and "outputs" instead of parameters and an Output
	/// node - see ShaderGraphSubgraphs.h.
	inline constexpr const char* kSubgraphDomain = "subgraph";

	/// What a parameter is. The first five are the "type" values of a .params.json file too.
	enum class ParameterType : uint8_t { Float, Vec2, Vec3, Vec4, Colour, Texture };

	/// "float", "vec2", "vec3", "vec4", "colour", "texture".
	const char* ParameterTypeName(ParameterType type);

	struct Parameter {
		std::string          name;                        ///< a letter or '_', then letters, digits, '_'; unique in the graph
		ParameterType        type = ParameterType::Float;
		std::array<float, 4> defaultValue{};              ///< numbers, vectors and colours
		float                minValue = 0.0f;             ///< slider range in the Inspector (numbers and vectors)
		float                maxValue = 1.0f;
		bool                 textureIsColour = true;      ///< textures: colour data (sRGB) or plain numbers
		std::string          textureDefault = "white";    ///< textures: what an empty slot shows - "white", "black" or "normal"
	};

	struct Node {
		uint32_t    id = 0;      ///< unique in the graph, never 0, never reused
		std::string type;        ///< a row of the node table (ShaderGraphNodes.h)
		float       x = 0.0f;    ///< where it sits on the canvas
		float       y = 0.0f;
		/// Constants for input pins with nothing plugged in, by pin name. A pin that is not here
		/// uses the table's default. Four numbers whatever the pin's type; it uses as many as it has.
		std::map<std::string, std::array<float, 4>> inputs;
		/// The node's own choices, by name - a Parameter node's "parameter".
		std::map<std::string, std::string> settings;
	};

	struct Link {
		uint32_t    fromNode = 0;   ///< the node whose output pin it leaves
		std::string fromPin;
		uint32_t    toNode = 0;     ///< the node whose input pin it enters
		std::string toPin;
	};

	/// The smallest a comment box can be, either way (canvas units).
	inline constexpr float kMinCommentSize = 60.0f;

	/// A box drawn behind nodes on the canvas, with a title: notes for whoever reads the graph.
	/// Changes nothing in the shader.
	struct Comment {
		uint32_t             id = 0;                  ///< unique among the graph's nodes AND comments, never 0
		float                x = 0.0f, y = 0.0f;      ///< top-left corner, canvas units
		float                width = 300.0f, height = 200.0f;
		std::string          text = "Comment";
		std::array<float, 3> colour{ 0.30f, 0.40f, 0.60f };   ///< 0..1
	};

	struct Graph {
		std::string            domain = kSurfaceDomain;
		uint32_t               nextId = 1;   ///< the id the next new node or comment gets
		std::vector<Parameter> parameters;   ///< in the order a material's Inspector shows them
		/// A sub-graph's pins, as the node that uses it shows them (ShaderGraphSubgraphs.h). Written like
		/// parameters: name, type, default. Empty, and not in the file, for a shader graph.
		std::vector<Parameter> inputs, outputs;
		std::vector<Node>      nodes;
		std::vector<Link>      links;
		std::vector<Comment>   comments;     ///< in drawing order: later ones on top

		/// Adds a node of `type` and returns its id (a new one, never one a deleted node had).
		uint32_t AddNode(std::string type, float x = 0.0f, float y = 0.0f);

		/// The node with this id, or null.
		Node*       FindNode(uint32_t id);
		const Node* FindNode(uint32_t id) const;

		/// The parameter called `name`, or null.
		const Parameter* FindParameter(std::string_view name) const;

		/// The link into this input pin, or null. (A valid graph has at most one.)
		const Link* FindLinkInto(uint32_t node, std::string_view pin) const;

		/// Adds a comment box and returns its id (from the same counter as nodes).
		uint32_t AddComment(float x, float y, float width, float height, std::string text);

		/// The comment with this id, or null.
		Comment*       FindComment(uint32_t id);
		const Comment* FindComment(uint32_t id) const;
	};

	/*********************************************************************
	 \brief  Reads a graph from the text of a .shadergraph file.
	 \param  json   The file's text.
	 \param  out    The graph (untouched on failure).
	 \param  error  Why it could not be read.
	 \return false for text that is not such a file: bad JSON, no version, a
	         newer version than kFormatVersion, another domain, a node without
	         an id or a type. Whether the graph makes SENSE (links that fit,
	         no loops) is ShaderGraphCompiler's question, not this one: a
	         graph being edited is often not finished.
	 *********************************************************************/
	bool Load(std::string_view json, Graph& out, std::string& error);

	/// The graph as the text of a .shadergraph file (what Load reads).
	std::string Save(const Graph& graph);

	/// A float as text that reads back as exactly the same float, in as few digits as that takes:
	/// "0.5", "1.0", "-2.0", "1e-05". Good as JSON and as Slang. (Not a number, or infinite: "0.0".)
	std::string NumberText(float value);

	/// Whether `name` can be a parameter's name: a letter or '_', then letters, digits and '_'.
	bool IsIdentifier(std::string_view name);

}
