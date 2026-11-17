/*!
* @file			main.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Tests for the shader graph's data, file, generator, edits and undo
*				(Editor/src/ShaderGraph - everything there that needs no window).
*
* A small program of its own, because that code needs neither the engine nor a window:
*
*     ShaderGraphTests                 runs the tests; prints what failed; returns 1 if any did
*     ShaderGraphTests <folder>        also writes the shaders it generated into <folder>, as
*                                      AllNodes_<type>.slang, to be compiled with slangc by hand
*
* What is tested: the node table agrees with itself; a graph saved and loaded is the same
* graph; old and odd files load (or are refused, saying why); the generator finds loops, links
* that do not fit, missing pieces and bad parameters, and tells problems that stop the shader
* from ones on nodes the Output does not use; the generated text and parameter list are what
* they should be; one graph per input type uses every row of the table; the editor's edits
* keep their rules (one Output, one link per input, no loops, types that fit); and undo and
* redo bring back exactly what was there.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "ShaderGraph/ShaderGraph.h"
#include "ShaderGraph/ShaderGraphDocument.h"
#include "ShaderGraph/ShaderGraphEdit.h"
#include "ShaderGraph/ShaderGraphGenerator.h"
#include "ShaderGraph/ShaderGraphNodes.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using namespace Crucible::ShaderGraph;

namespace {

	int g_checks = 0;
	int g_failed = 0;

#define CHECK(condition)                                                                        \
	do {                                                                                        \
		++g_checks;                                                                             \
		if (!(condition)) {                                                                     \
			++g_failed;                                                                         \
			std::printf("  FAILED line %d: %s\n", __LINE__, #condition);                        \
		}                                                                                       \
	} while (false)

	bool Contains(std::string_view text, std::string_view part) {
		return text.find(part) != std::string_view::npos;
	}

	void Connect(Graph& graph, uint32_t fromNode, const char* fromPin, uint32_t toNode, const char* toPin) {
		graph.links.push_back({ fromNode, fromPin, toNode, toPin });
	}

	// A problem on `node` (and `pin`, unless empty) whose message contains `part`.
	const Problem* FindProblem(const Generated& result, uint32_t node, std::string_view pin, std::string_view part) {
		for (const Problem& problem : result.problems) {
			if (problem.node == node && (pin.empty() || problem.pin == pin) && Contains(problem.message, part)) { return &problem; }
		}
		return nullptr;
	}

	void PrintProblems(const Graph& graph, const Generated& result) {
		for (const Problem& problem : result.problems) {
			std::printf("    %s%s\n", Describe(graph, problem).c_str(), problem.stops ? "" : "  (does not stop the shader)");
		}
	}

	// ---- The table ---------------------------------------------------------------------------------

	void TestTable() {
		std::printf("node table\n");
		std::string error;
		const bool fine = CheckNodeTable(error);
		if (!fine) { std::printf("    %s\n", error.c_str()); }
		CHECK(fine);
		CHECK(FindNodeType("Lerp") != nullptr);
		CHECK(FindNodeType("NoSuchNode") == nullptr);
		CHECK(FindNodeType("Split")->FindOutput("xyz") == 5);
		CHECK(FindNodeType("Split")->FindInput("xyz") == -1);
		CHECK(NodeTypes().size() == 77);
		CHECK(IsMaterialFactorNode("MaterialTint") && IsMaterialFactorNode("MaterialEmissive") && !IsMaterialFactorNode("Parameter") && !IsMaterialFactorNode("Material"));
		for (const NodeType& type : NodeTypes()) { CHECK(IsMaterialFactorNode(type.name) == (std::string_view(type.name).substr(0, 8) == "Material")); }
	}

	// ---- The file ----------------------------------------------------------------------------------

	Graph SmallGraph() {
		Graph graph;
		graph.parameters.push_back({ "tint", ParameterType::Colour, { 1.0f, 0.5f, 0.2f, 1.0f } });
		graph.parameters.push_back({ "speed", ParameterType::Float, { 0.6f }, -4.0f, 4.0f });
		graph.parameters.push_back({ "pattern", ParameterType::Texture, {}, 0.0f, 1.0f, false, "black" });
		const uint32_t output = graph.AddNode("Output", 400.0f, 0.0f);
		const uint32_t tint = graph.AddNode("Parameter", 0.0f, -50.5f);
		graph.FindNode(tint)->settings["parameter"] = "tint";
		graph.FindNode(output)->inputs["roughness"] = { 0.4f, 0.0f, 0.0f, 0.0f };
		graph.FindNode(output)->inputs["emissive"] = { 0.0f, 0.1f, -2.5f, 0.0f };
		Connect(graph, tint, "out", output, "albedo");
		return graph;
	}

	void TestSaveLoad() {
		std::printf("save and load\n");
		const Graph graph = SmallGraph();
		const std::string text = Save(graph);

		Graph loaded;
		std::string error;
		CHECK(Load(text, loaded, error));
		CHECK(Save(loaded) == text);   // nothing lost, nothing drifting
		CHECK(loaded.nextId == 3);
		CHECK(loaded.nodes.size() == 2 && loaded.links.size() == 1 && loaded.parameters.size() == 3);
		CHECK(loaded.parameters[0].type == ParameterType::Colour && loaded.parameters[0].defaultValue[1] == 0.5f);
		CHECK(loaded.parameters[1].minValue == -4.0f && loaded.parameters[1].maxValue == 4.0f);
		CHECK(loaded.parameters[2].textureDefault == "black" && !loaded.parameters[2].textureIsColour);
		CHECK(loaded.FindNode(1)->inputs.at("roughness")[0] == 0.4f);
		CHECK(loaded.FindNode(1)->inputs.at("emissive")[2] == -2.5f);
		CHECK(loaded.FindNode(2)->settings.at("parameter") == "tint");
		CHECK(loaded.FindNode(2)->y == -50.5f);
		CHECK(loaded.FindLinkInto(1, "albedo") != nullptr && loaded.FindLinkInto(1, "normal") == nullptr);

		// Numbers are written short (0.4, not 0.4000000059604645), and each node is one line.
		CHECK(Contains(text, "    { \"id\": 1, \"type\": \"Output\", \"x\": 400.0, \"y\": 0.0, "
		                     "\"inputs\": { \"emissive\": [0.0, 0.1, -2.5], \"roughness\": 0.4 } },\n"));
		CHECK(Contains(text, "    { \"from\": [2, \"out\"], \"to\": [1, \"albedo\"] }\n"));

		// Names with characters JSON must escape survive.
		Graph odd;
		odd.AddNode("Type \"quoted\" \\ back\tslash");
		odd.nodes[0].settings["key\n"] = "value\x01";
		Graph oddLoaded;
		CHECK(Load(Save(odd), oddLoaded, error));
		CHECK(oddLoaded.nodes.size() == 1 && oddLoaded.nodes[0].type == odd.nodes[0].type && oddLoaded.nodes[0].settings == odd.nodes[0].settings);

		// An empty graph.
		CHECK(Load(Save(Graph{}), oddLoaded, error) && oddLoaded.nodes.empty() && oddLoaded.nextId == 1);

		// Ids are never handed out twice, even after a node is deleted.
		Graph edited = loaded;
		edited.nodes.pop_back();
		CHECK(edited.AddNode("Time") == 3);
		CHECK(edited.AddNode("UV") == 4);
	}

	void TestOldAndOddFiles() {
		std::printf("old and odd files\n");
		Graph graph;
		std::string error;

		// The least a file can be.
		CHECK(Load(R"({ "version": 1 })", graph, error));
		CHECK(graph.nodes.empty() && graph.nextId == 1 && graph.domain == "surface");
		CHECK(Load("\xEF\xBB\xBF{ \"version\": 1 }", graph, error));   // saved by an editor that adds a UTF-8 mark

		// Written by hand: comments, a comma too many, keys this code does not know, keys left out.
		CHECK(Load(R"({
			// a comment
			"version": 1,
			"somethingFromLater": { "a": [1, 2, 3] },
			"parameters": [ { "name": "glow" }, { "name": "mask", "type": "texture" }, ],
			"nodes": [
				{ "id": 7, "type": "Output", "futureKey": true, "inputs": { "albedo": [1, 0, 0], "metallic": 1 } },
				{ "id": 3, "type": "Time" },
			],
			"links": [ { "from": [3, "out"], "to": [7, "roughness"], "label": "x" } ],
		})", graph, error));
		CHECK(graph.nextId == 8);   // above every id in the file, though the file did not say
		CHECK(graph.parameters.size() == 2 && graph.parameters[0].type == ParameterType::Float);
		CHECK(graph.parameters[1].textureIsColour && graph.parameters[1].textureDefault == "white");
		CHECK(graph.FindNode(7)->inputs.at("albedo")[0] == 1.0f && graph.FindNode(7)->inputs.at("metallic")[0] == 1.0f);
		CHECK(Generate(graph).ok);

		// A nextId lower than an id in the file is not believed.
		CHECK(Load(R"({ "version": 1, "nextId": 2, "nodes": [ { "id": 9, "type": "Output" } ] })", graph, error));
		CHECK(graph.nextId == 10);

		// Refused, and saying why. A failed load leaves the graph alone.
		const Graph before = graph;
		CHECK(!Load(R"({ "version": 2 })", graph, error) && Contains(error, "newer Editor"));
		CHECK(!Load(R"({ "nodes": [] })", graph, error) && Contains(error, "version"));
		CHECK(!Load("{ \"version\": 1,\n\n \"nodes\": [ }", graph, error) && Contains(error, "line 3"));
		CHECK(!Load(R"([1, 2])", graph, error));
		CHECK(!Load(R"({ "version": 1, "domain": "postprocess" })", graph, error) && Contains(error, "domain"));
		CHECK(!Load(R"({ "version": 1, "nodes": [ { "id": 1, "type": "UV" }, { "id": 1, "type": "Time" } ] })", graph, error) && Contains(error, "id 1"));
		CHECK(!Load(R"({ "version": 1, "nodes": [ { "type": "UV" } ] })", graph, error) && Contains(error, "id"));
		CHECK(!Load(R"({ "version": 1, "nodes": [ { "id": 4 } ] })", graph, error) && Contains(error, "type"));
		CHECK(!Load(R"({ "version": 1, "links": [ { "from": [1, "out"] } ] })", graph, error) && Contains(error, "link 1"));
		CHECK(!Load(R"({ "version": 1, "parameters": [ { "name": "a", "type": "matrix" } ] })", graph, error) && Contains(error, "matrix"));
		CHECK(!Load(R"({ "version": 1, "nodes": [ { "id": 1, "type": "Float", "inputs": { "value": "big" } } ] })", graph, error) && Contains(error, "value"));
		CHECK(graph.nextId == before.nextId && graph.nodes.size() == before.nodes.size());
	}

	// ---- The generator: what it writes ---------------------------------------------------------------

	void TestGeneratedText() {
		std::printf("generated shader\n");
		Graph graph;
		graph.parameters.push_back({ "pattern", ParameterType::Texture, {}, 0.0f, 1.0f, true, "white" });
		graph.parameters.push_back({ "tint", ParameterType::Colour, { 1.0f, 0.5f, 0.2f, 1.0f } });
		graph.parameters.push_back({ "scroll", ParameterType::Vec2, { 0.1f, 0.0f }, -2.0f, 2.0f });
		graph.parameters.push_back({ "mask", ParameterType::Texture, {}, 0.0f, 1.0f, false, "black" });
		graph.parameters.push_back({ "glow", ParameterType::Float, { 1.5f }, 0.0f, 8.0f });

		const uint32_t output = graph.AddNode("Output");
		const uint32_t scroll = graph.AddNode("Parameter");
		const uint32_t panner = graph.AddNode("Panner");
		const uint32_t pattern = graph.AddNode("Parameter");
		const uint32_t sample = graph.AddNode("SampleTexture");
		const uint32_t tint = graph.AddNode("Parameter");
		const uint32_t lerp = graph.AddNode("Lerp");
		const uint32_t unused = graph.AddNode("Noise");   // nothing reads it: it is not written
		const uint32_t glow = graph.AddNode("Parameter");
		const uint32_t emissive = graph.AddNode("Multiply");
		graph.FindNode(scroll)->settings["parameter"] = "scroll";
		graph.FindNode(pattern)->settings["parameter"] = "pattern";
		graph.FindNode(tint)->settings["parameter"] = "tint";
		graph.FindNode(glow)->settings["parameter"] = "glow";
		graph.FindNode(output)->inputs["roughness"] = { 0.25f };
		graph.FindNode(emissive)->inputs["b"] = { -0.5f };
		Connect(graph, scroll, "out", panner, "speed");
		Connect(graph, panner, "out", sample, "uv");
		Connect(graph, pattern, "out", sample, "texture");
		Connect(graph, sample, "rgb", lerp, "a");
		Connect(graph, tint, "out", lerp, "b");
		Connect(graph, sample, "a", lerp, "t");          // a float into a node working in float3
		Connect(graph, lerp, "out", output, "albedo");
		Connect(graph, glow, "out", emissive, "a");
		Connect(graph, emissive, "out", output, "emissive");   // a float into a float3 input

		const Generated result = Generate(graph);
		if (!result.ok) { PrintProblems(graph, result); }
		CHECK(result.ok && result.problems.empty());
		const std::string& source = result.source;

		// A whole mesh shader: the surface between the two includes.
		CHECK(source.find("#include \"Surface.slang\"") < source.find("SurfaceData EvaluateSurface(SurfaceInput s)"));
		CHECK(source.find("SurfaceData EvaluateSurface(SurfaceInput s)") < source.find("#include \"SurfaceTemplate.slang\""));

		// Parameters: textures and numbers each count their own slots, in the graph's order.
		CHECK(Contains(source, "MATERIAL_TEXTURE(0, gTex_pattern);"));
		CHECK(Contains(source, "MATERIAL_TEXTURE(1, gTex_mask);"));
		CHECK(Contains(source, "    float3 tint;\n    float2 scroll;\n    float glow;\n"));
		CHECK(Contains(source, "P.tint = MaterialParam(0).xyz;"));
		CHECK(Contains(source, "P.scroll = MaterialParam(1).xy;"));
		CHECK(Contains(source, "P.glow = MaterialParam(2).x;"));

		// One local per output pin that is used, sources before what reads them.
		CHECK(Contains(source, "float2 n3_out = s.uv + P.scroll * s.time;"));
		CHECK(Contains(source, "float4 n5_rgba = gTex_pattern.Sample(n3_out);"));
		CHECK(Contains(source, "float3 n5_rgb = n5_rgba.rgb;"));
		CHECK(Contains(source, "float n5_a = n5_rgba.a;"));
		CHECK(!Contains(source, "n5_r ") && !Contains(source, "n5_g "));   // not read, not written
		CHECK(Contains(source, "float3 n7_out = lerp(n5_rgb, P.tint, float3(n5_a));"));
		CHECK(Contains(source, "float n10_out = P.glow * (-0.5);"));
		CHECK(source.find("n3_out =") < source.find("n5_rgba =") && source.find("n5_rgba =") < source.find("n7_out ="));
		CHECK(!Contains(source, "n" + std::to_string(unused) + "_"));

		// The Output: links, the node's own constants, the table's defaults.
		CHECK(Contains(source, "o.albedo = n7_out;"));
		CHECK(Contains(source, "o.normalTS = float3(0.0, 0.0, 1.0);"));
		CHECK(Contains(source, "o.metallic = 0.0;"));
		CHECK(Contains(source, "o.roughness = 0.25;"));
		CHECK(Contains(source, "o.emissive = float3(n10_out);"));
		CHECK(Contains(source, "o.occlusion = 1.0;"));
		CHECK(Contains(source, "o.alpha = 1.0;"));

		// Every line knows its node.
		size_t lines = 0, lerpLine = 0, at = 0;
		for (size_t end = source.find('\n'); end != std::string::npos; at = end + 1, end = source.find('\n', at), ++lines) {
			if (Contains(std::string_view(source).substr(at, end - at), "n7_out = lerp")) { lerpLine = lines; }
		}
		CHECK(result.lineNodes.size() == lines);
		CHECK(lerpLine != 0 && result.lineNodes[lerpLine] == lerp && result.lineNodes[lerpLine - 1] == lerp);   // the "// node 7" comment too
		CHECK(result.lineNodes[0] == 0);

		// The parameter list, as the engine reads it.
		const std::string& list = result.paramsJson;
		CHECK(Contains(list, "\"version\": 1"));
		CHECK(Contains(list, "{ \"name\": \"tint\", \"type\": \"colour\", \"default\": [1.0, 0.5, 0.2] }"));
		CHECK(Contains(list, "{ \"name\": \"scroll\", \"type\": \"vec2\", \"default\": [0.1, 0.0], \"min\": -2.0, \"max\": 2.0 }"));
		CHECK(Contains(list, "{ \"name\": \"glow\", \"type\": \"float\", \"default\": 1.5, \"min\": 0.0, \"max\": 8.0 }"));
		CHECK(Contains(list, "{ \"name\": \"pattern\", \"colour\": true, \"default\": \"white\" }"));
		CHECK(Contains(list, "{ \"name\": \"mask\", \"colour\": false, \"default\": \"black\" }"));
		CHECK(list.find("\"tint\"") < list.find("\"scroll\"") && list.find("\"pattern\"") < list.find("\"mask\""));

		// A graph with no parameters has no struct, and an empty list.
		Graph plain;
		plain.AddNode("Output");
		const Generated bare = Generate(plain);
		CHECK(bare.ok && !Contains(bare.source, "GraphParams") && !Contains(bare.source, "MATERIAL_TEXTURE"));
		CHECK(Contains(bare.paramsJson, "\"params\": []") && Contains(bare.paramsJson, "\"textures\": []"));
		CHECK(Contains(bare.source, "o.albedo = float3(0.8, 0.8, 0.8);"));
	}

	// ---- The generator: what it refuses ----------------------------------------------------------------

	void TestLoops() {
		std::printf("loops\n");
		// Output <- Add(4) <- Multiply(2) <-> OneMinus(3): the loop is in the Output's way.
		Graph graph;
		const uint32_t output = graph.AddNode("Output");
		const uint32_t a = graph.AddNode("Multiply");
		const uint32_t b = graph.AddNode("OneMinus");
		const uint32_t after = graph.AddNode("Add");
		Connect(graph, a, "out", b, "in");
		Connect(graph, b, "out", a, "a");
		Connect(graph, a, "out", after, "a");
		Connect(graph, after, "out", output, "roughness");
		Generated result = Generate(graph);
		CHECK(!result.ok && result.source.empty());
		CHECK(FindProblem(result, a, "", "loop") && FindProblem(result, a, "", "loop")->stops);
		CHECK(FindProblem(result, b, "", "loop") != nullptr);
		CHECK(FindProblem(result, after, "", "loop") == nullptr);    // it only reads from the loop
		CHECK(FindProblem(result, output, "", "loop") == nullptr);
		CHECK(result.problems.size() == 2);

		// The same loop with the Output not using it: reported, but the shader is made.
		graph.links.pop_back();
		result = Generate(graph);
		CHECK(result.ok && !result.source.empty());
		CHECK(result.problems.size() == 2 && !result.problems[0].stops && !result.problems[1].stops);

		// A node linked to itself.
		Graph self;
		const uint32_t out = self.AddNode("Output");
		const uint32_t node = self.AddNode("Add");
		Connect(self, node, "out", node, "a");
		Connect(self, node, "out", out, "metallic");
		result = Generate(self);
		CHECK(!result.ok && FindProblem(result, node, "a", "own output"));

		// A long ring: every node of it is named.
		Graph ring;
		const uint32_t ringOut = ring.AddNode("Output");
		uint32_t first = 0, previous = 0;
		for (int i = 0; i < 6; ++i) {
			const uint32_t id = ring.AddNode("Saturate");
			if (previous != 0) { Connect(ring, previous, "out", id, "in"); } else { first = id; }
			previous = id;
		}
		Connect(ring, previous, "out", first, "in");
		Connect(ring, previous, "out", ringOut, "alpha");
		result = Generate(ring);
		CHECK(!result.ok && result.problems.size() == 6);
	}

	// Output <- (pin) <- one node of `type`, with `feed` (a node type) linked into its `into` pin.
	struct Trial {
		Graph    graph;
		uint32_t output = 0, node = 0, feed = 0;
	};
	Trial MakeTrial(const char* type, const char* outPin, const char* outputPin, const char* feedType, const char* feedPin, const char* into) {
		Trial trial;
		trial.output = trial.graph.AddNode("Output");
		trial.node = trial.graph.AddNode(type);
		Connect(trial.graph, trial.node, outPin, trial.output, outputPin);
		if (feedType) {
			trial.feed = trial.graph.AddNode(feedType);
			Connect(trial.graph, trial.feed, feedPin, trial.node, into);
		}
		return trial;
	}

	void TestTypes() {
		std::printf("types\n");
		// A float goes anywhere: repeated into a vector.
		Trial trial = MakeTrial("TilingOffset", "out", "emissive", "Time", "out", "tiling");
		Generated result = Generate(trial.graph);
		CHECK(!result.ok);   // ... but the float2 it gives does not go into emissive (a float3)
		CHECK(FindProblem(result, trial.output, "emissive", "Vector node"));

		Graph widen;
		const uint32_t widenOutput = widen.AddNode("Output");
		const uint32_t time = widen.AddNode("Time");
		Connect(widen, time, "out", widenOutput, "albedo");
		result = Generate(widen);
		CHECK(result.ok && Contains(result.source, "o.albedo = float3(n2_out);"));

		// A wider vector into a narrower pin: Split is the way.
		trial = MakeTrial("Panner", "out", "roughness", "WorldPosition", "out", "uv");
		result = Generate(trial.graph);
		CHECK(!result.ok && FindProblem(result, trial.node, "uv", "Split node"));
		CHECK(FindProblem(result, trial.output, "roughness", "") == nullptr);   // one mistake, one message: no echo downstream

		// Two different vectors into one "any" node.
		trial = MakeTrial("Add", "out", "albedo", "UV", "out", "a");
		const uint32_t position = trial.graph.AddNode("WorldPosition");
		Connect(trial.graph, position, "out", trial.node, "b");
		result = Generate(trial.graph);
		CHECK(!result.ok && FindProblem(result, trial.node, "a", "same type"));
		CHECK(result.problems.size() == 1);

		// The same vectors agree: the node works in float3, and its constant is repeated.
		trial = MakeTrial("Multiply", "out", "albedo", "WorldNormal", "out", "a");
		trial.graph.FindNode(trial.node)->inputs["b"] = { 0.5f, 9.0f, 9.0f, 9.0f };   // only the first number counts
		result = Generate(trial.graph);
		CHECK(result.ok && Contains(result.source, "float3 n2_out = n3_out * float3(0.5);"));

		// Textures: only into a texture pin, only from a texture parameter.
		trial = MakeTrial("SampleTexture", "rgb", "albedo", nullptr, nullptr, nullptr);
		result = Generate(trial.graph);
		CHECK(!result.ok && FindProblem(result, trial.node, "texture", "needs a texture"));

		trial = MakeTrial("SampleTexture", "rgb", "albedo", "Time", "out", "texture");
		result = Generate(trial.graph);
		CHECK(!result.ok && FindProblem(result, trial.node, "texture", "given a float"));

		trial = MakeTrial("Add", "out", "albedo", "Parameter", "out", "a");
		trial.graph.parameters.push_back({ "mask", ParameterType::Texture });
		trial.graph.FindNode(trial.feed)->settings["parameter"] = "mask";
		result = Generate(trial.graph);
		CHECK(!result.ok && FindProblem(result, trial.node, "a", "SampleTexture"));

		// Split: a pin the input does not have.
		trial = MakeTrial("Split", "z", "roughness", "UV", "out", "in");
		result = Generate(trial.graph);
		CHECK(!result.ok && FindProblem(result, trial.node, "z", "float2"));
		trial = MakeTrial("Split", "y", "roughness", "UV", "out", "in");
		result = Generate(trial.graph);
		CHECK(result.ok && Contains(result.source, "float n2_y = (n3_out).y;"));

		// A node that needs a vector.
		trial = MakeTrial("Normalize", "out", "normal", "Time", "out", "in");
		result = Generate(trial.graph);
		CHECK(!result.ok && FindProblem(result, trial.node, "", "vectors"));
		trial = MakeTrial("Length", "out", "roughness", "UV", "out", "in");
		CHECK(Generate(trial.graph).ok);
	}

	void TestStructure() {
		std::printf("structure\n");
		// No Output; two Outputs.
		Graph graph;
		graph.AddNode("Time");
		Generated result = Generate(graph);
		CHECK(!result.ok && FindProblem(result, 0, "", "no Output"));
		graph.AddNode("Output");
		CHECK(Generate(graph).ok);
		const uint32_t second = graph.AddNode("Output");
		result = Generate(graph);
		CHECK(!result.ok && FindProblem(result, second, "", "second"));

		// A node type this code does not know: fine until the Output needs it.
		Graph unknown;
		const uint32_t output = unknown.AddNode("Output");
		const uint32_t strange = unknown.AddNode("NodeFromTheFuture");
		result = Generate(unknown);
		CHECK(result.ok && FindProblem(result, strange, "", "not a kind of node") && !result.problems[0].stops);
		Connect(unknown, strange, "out", output, "albedo");
		result = Generate(unknown);
		CHECK(!result.ok && result.problems.size() == 1);

		// Links to and from what is not there.
		Trial trial = MakeTrial("Add", "out", "albedo", "Time", "out", "noSuchPin");
		result = Generate(trial.graph);
		CHECK(!result.ok && FindProblem(result, trial.node, "noSuchPin", "no such input"));

		trial = MakeTrial("Add", "out", "albedo", "Time", "noSuchPin", "a");
		result = Generate(trial.graph);
		CHECK(!result.ok && FindProblem(result, trial.node, "a", "no such output"));

		trial = MakeTrial("Add", "out", "albedo", nullptr, nullptr, nullptr);
		Connect(trial.graph, 99, "out", trial.node, "a");
		result = Generate(trial.graph);
		CHECK(!result.ok && FindProblem(result, trial.node, "a", "node 99"));

		trial = MakeTrial("Add", "out", "albedo", nullptr, nullptr, nullptr);
		Connect(trial.graph, trial.node, "out", 99, "a");   // goes nowhere: said, but harmless
		result = Generate(trial.graph);
		CHECK(result.ok && result.problems.size() == 1 && !result.problems[0].stops);

		// Two links into one input.
		trial = MakeTrial("Add", "out", "albedo", "Time", "out", "a");
		const uint32_t other = trial.graph.AddNode("Time");
		Connect(trial.graph, other, "out", trial.node, "a");
		result = Generate(trial.graph);
		CHECK(!result.ok && FindProblem(result, trial.node, "a", "more than one link"));

		// A problem on a branch the Output does not use does not stop the shader.
		Graph side;
		side.AddNode("Output");
		const uint32_t lonely = side.AddNode("SampleTexture");   // its texture pin is empty
		result = Generate(side);
		CHECK(result.ok && FindProblem(result, lonely, "texture", "needs a texture") && !result.problems[0].stops);
	}

	void TestParameters() {
		std::printf("parameters\n");
		// A Parameter node with no parameter, or one that is gone.
		Trial trial = MakeTrial("Parameter", "out", "albedo", nullptr, nullptr, nullptr);
		Generated result = Generate(trial.graph);
		CHECK(!result.ok && FindProblem(result, trial.node, "", "no parameter is chosen"));
		trial.graph.FindNode(trial.node)->settings["parameter"] = "gone";
		result = Generate(trial.graph);
		CHECK(!result.ok && FindProblem(result, trial.node, "", "\"gone\""));

		// Each type reaches the shader as the right Slang type.
		trial.graph.parameters.push_back({ "gone", ParameterType::Vec4 });
		result = Generate(trial.graph);
		CHECK(!result.ok && FindProblem(result, trial.output, "albedo", "float4 cannot go into a float3"));
		trial.graph.parameters[0].type = ParameterType::Vec3;
		result = Generate(trial.graph);
		CHECK(result.ok && Contains(result.source, "o.albedo = P.gone;") && Contains(result.source, "P.gone = MaterialParam(0).xyz;"));
		trial.graph.parameters[0].type = ParameterType::Float;
		CHECK(Contains(Generate(trial.graph).source, "o.albedo = float3(P.gone);"));

		// Names the shader cannot use.
		Graph graph;
		graph.AddNode("Output");
		graph.parameters = { Parameter{ "2fast" } };
		CHECK(!Generate(graph).ok);
		graph.parameters = { Parameter{ "has space" } };
		CHECK(!Generate(graph).ok);
		graph.parameters = { Parameter{ "" } };
		CHECK(!Generate(graph).ok);
		graph.parameters = { Parameter{ "float" } };
		result = Generate(graph);
		CHECK(!result.ok && FindProblem(result, 0, "", "word of the shader language"));
		graph.parameters = { Parameter{ "same" }, Parameter{ "same", ParameterType::Texture } };
		result = Generate(graph);
		CHECK(!result.ok && FindProblem(result, 0, "", "two parameters"));
		graph.parameters = { Parameter{ "tex", ParameterType::Texture, {}, 0.0f, 1.0f, true, "purple" } };
		result = Generate(graph);
		CHECK(!result.ok && FindProblem(result, 0, "", "purple"));
		graph.parameters = { Parameter{ "_fine_Name2" } };
		CHECK(Generate(graph).ok);

		// The engine's limits.
		graph.parameters.clear();
		for (size_t i = 0; i < kMaxTextureParameters; ++i) { graph.parameters.push_back({ "tex" + std::to_string(i), ParameterType::Texture }); }
		CHECK(Generate(graph).ok);
		graph.parameters.push_back({ "oneTooMany", ParameterType::Texture });
		result = Generate(graph);
		CHECK(!result.ok && FindProblem(result, 0, "", "texture parameters"));
	}

	// ---- Every row of the table, in one graph -----------------------------------------------------------

	// A graph that uses every node type and every output pin that exists when the "any" inputs
	// are given `components` numbers (1 = nothing linked: the nodes' own constants). Everything
	// is turned into a float and added up into the Output's roughness, so all of it is written.
	// `moved`: the total also goes into the Output's vertex offset, so every node is written in
	// both functions - except the nodes that cannot feed it, which are left out.
	Graph AllNodesGraph(uint32_t components, bool moved = false) {
		Graph graph;
		graph.parameters.push_back({ "number", ParameterType::Float, { 0.5f } });
		graph.parameters.push_back({ "pair", ParameterType::Vec2 });
		graph.parameters.push_back({ "triple", ParameterType::Vec3 });
		graph.parameters.push_back({ "four", ParameterType::Vec4 });
		graph.parameters.push_back({ "colour", ParameterType::Colour });
		graph.parameters.push_back({ "picture", ParameterType::Texture });

		const uint32_t output = graph.AddNode("Output");
		const auto parameter = [&](const char* name) {
			const uint32_t id = graph.AddNode("Parameter");
			graph.FindNode(id)->settings["parameter"] = name;
			return id;
		};
		const uint32_t texture = parameter("picture");
		const uint32_t number = parameter("number");
		constexpr const char* kVectors[5] = { "", "number", "pair", "triple", "four" };
		const uint32_t vector = parameter(kVectors[components]);

		uint32_t sum = 0;   // the node whose "out" is the total so far
		const auto add = [&](uint32_t node, const char* pin, ValueType type) {
			uint32_t value = node;
			const char* valuePin = pin;
			if (type != ValueType::Float) {   // a vector: its length is the float to add
				value = graph.AddNode("Length");
				valuePin = "out";
				Connect(graph, node, pin, value, "in");
			}
			const uint32_t total = graph.AddNode("Add");
			Connect(graph, value, valuePin, total, "a");
			if (sum != 0) { Connect(graph, sum, "out", total, "b"); }
			sum = total;
		};

		for (const NodeType& type : NodeTypes()) {
			const std::string_view name = type.name;
			if (name == kOutputNode) { continue; }
			if (type.minComponents > components) { continue; }   // would be (rightly) refused
			if (moved && IsMaterialFactorNode(name)) { continue; }   // not for the vertex offset

			const uint32_t node = graph.AddNode(type.name);
			if (name == kParameterNode) {
				graph.FindNode(node)->settings["parameter"] = "colour";
				add(node, "out", ValueType::Float3);
				continue;
			}
			bool first = true;
			for (const InputPin& pin : type.inputs) {
				if (pin.type == ValueType::Texture) {
					Connect(graph, texture, "out", node, pin.name);
				} else if (pin.type == ValueType::Any) {
					// The first "any" input gets the vector, the second a float (to be widened),
					// the rest keep their constants.
					if (first && components > 1) { Connect(graph, vector, "out", node, pin.name); }
					else if (!first && &pin == &type.inputs[1]) { Connect(graph, number, "out", node, pin.name); }
					first = false;
				}
			}
			for (const OutputPin& pin : type.outputs) {
				if (pin.needs > components) { continue; }
				add(node, pin.name, pin.type == ValueType::Any ? NumericType(components) : pin.type);
			}
		}
		Connect(graph, sum, "out", output, "roughness");
		if (moved) { Connect(graph, sum, "out", output, kVertexOffsetPin); }
		return graph;
	}

	void TestAllNodes(const char* writeTo) {
		std::printf("every node\n");
		for (uint32_t components = 1; components <= 4; ++components) {
			const Graph graph = AllNodesGraph(components);
			const Generated result = Generate(graph);
			if (!result.ok || !result.problems.empty()) { PrintProblems(graph, result); }
			CHECK(result.ok && result.problems.empty());

			// Every row is in the text (those that need a vector only when there is one).
			for (const NodeType& type : NodeTypes()) {
				if (std::string_view(type.name) == kParameterNode || type.minComponents > components) { continue; }
				CHECK(Contains(result.source, std::string(": ") + type.name + "\n"));
			}

			// Saved and loaded, it generates the same shader.
			Graph loaded;
			std::string error;
			CHECK(Load(Save(graph), loaded, error));
			CHECK(Generate(loaded).source == result.source);

			// The live-preview form (constants as hidden parameters) of the same graph.
			const Generated live = Generate(graph, Options{ true });
			CHECK(live.ok && !live.liveConstants.empty() && live.source != result.source);

			if (writeTo) {
				const std::string type = ValueTypeName(NumericType(components));
				for (const bool isLive : { false, true }) {
					const std::filesystem::path file = std::filesystem::path(writeTo) / ((isLive ? "AllNodesLive_" : "AllNodes_") + type + ".slang");
					std::ofstream out(file, std::ios::binary | std::ios::trunc);
					out << (isLive ? live.source : result.source);
					std::printf("    wrote %s\n", file.string().c_str());
				}
			}

			// The same with everything feeding the vertex offset as well: every row is written a
			// second time, in the vertex function, where a texture is read at a named mip level.
			const Graph movedGraph = AllNodesGraph(components, true);
			const Generated moved = Generate(movedGraph), movedLive = Generate(movedGraph, Options{ true });
			if (!moved.ok || !moved.problems.empty()) { PrintProblems(movedGraph, moved); }
			CHECK(moved.ok && moved.problems.empty() && moved.movesVertices && movedLive.ok && movedLive.movesVertices);
			const size_t vertexAt = moved.source.find("float3 EvaluateVertexOffset(SurfaceInput s) {");
			CHECK(vertexAt != std::string::npos);
			if (vertexAt != std::string::npos) {
				const std::string surface = moved.source.substr(0, vertexAt), vertex = moved.source.substr(vertexAt);
				for (const NodeType& type : NodeTypes()) {
					const std::string_view name = type.name;
					if (name == kParameterNode || name == kOutputNode || type.minComponents > components || IsMaterialFactorNode(name)) { continue; }
					CHECK(Contains(surface, std::string(": ") + type.name + "\n") && Contains(vertex, std::string(": ") + type.name + "\n"));
				}
				CHECK(Contains(surface, ".Sample(") && !Contains(vertex, ".Sample(") && Contains(vertex, ".SampleLevel(") && !Contains(vertex, "GraphTriplanar("));
				CHECK(!Contains(vertex, "pc."));
			}
			CHECK(moved.lineNodes.size() == static_cast<size_t>(std::count(moved.source.begin(), moved.source.end(), '\n')));
			if (writeTo) {
				const std::string type = ValueTypeName(NumericType(components));
				for (const bool isLive : { false, true }) {
					const std::filesystem::path file = std::filesystem::path(writeTo) / ((isLive ? "AllNodesMovedLive_" : "AllNodesMoved_") + type + ".slang");
					std::ofstream out(file, std::ios::binary | std::ios::trunc);
					out << (isLive ? movedLive.source : moved.source);
					std::printf("    wrote %s\n", file.string().c_str());
				}
			}
		}
	}

	// ---- The editor's edits, and undo ----------------------------------------------------------------

	void TestEdits() {
		std::printf("edits\n");
		Graph graph = NewGraph();
		const uint32_t output = OutputNodeOf(graph);
		CHECK(output != 0 && graph.nodes.size() == 1);
		CHECK(AddNode(graph, "Output", 0.0f, 0.0f) == 0);       // never a second one
		CHECK(AddNode(graph, "NoSuchNode", 0.0f, 0.0f) == 0);
		const uint32_t uv = AddNode(graph, "UV", 0.0f, 0.0f);
		const uint32_t position = AddNode(graph, "WorldPosition", 0.0f, 50.0f);
		const uint32_t add = AddNode(graph, "Add", 200.0f, 0.0f);

		// Links: refused when the types do not fit, and then nothing has changed.
		std::string why;
		CHECK(Connect(graph, uv, "out", add, "a", why));
		CHECK(!Connect(graph, position, "out", add, "b", why) && !why.empty());   // a float3 beside a float2
		CHECK(graph.links.size() == 1);
		CHECK(!Connect(graph, uv, "out", output, "emissive", why) && Contains(why, "Vector node"));

		// One link per input: a new one replaces the old.
		CHECK(Connect(graph, position, "out", add, "a", why));
		CHECK(graph.links.size() == 1 && graph.FindLinkInto(add, "a")->fromNode == position);
		CHECK(Connect(graph, add, "out", output, "albedo", why));

		// No loops, no pins that are not there.
		const uint32_t one = AddNode(graph, "OneMinus", 300.0f, 100.0f);
		CHECK(Connect(graph, add, "out", one, "in", why));
		CHECK(!Connect(graph, one, "out", add, "b", why) && Contains(why, "loop"));
		CHECK(!Connect(graph, add, "out", add, "b", why));
		CHECK(!Connect(graph, add, "nope", one, "in", why) && !Connect(graph, 99, "out", one, "in", why));
		CHECK(graph.links.size() == 3);

		// A problem the node already had is not the new link's fault.
		const uint32_t sample = AddNode(graph, "SampleTexture", 0.0f, 200.0f);   // its texture pin is empty
		CHECK(Connect(graph, uv, "out", sample, "uv", why));

		// Deleting: the Output stays, a node's links go with it.
		CHECK(DeleteNodes(graph, { add, output }) == 1);
		CHECK(OutputNodeOf(graph) == output && graph.FindNode(add) == nullptr);
		CHECK(graph.links.size() == 1);   // only uv -> sample is left
		CHECK(DeleteLinkInto(graph, sample, "uv") && !DeleteLinkInto(graph, sample, "uv"));
		CHECK(Generate(graph).ok);

		// Copy and paste: new ids, the links between the copied nodes, never the Output.
		Graph second = NewGraph();
		const uint32_t time = AddNode(second, "Time", 10.0f, 20.0f);
		const uint32_t sine = AddNode(second, "Sine", 110.0f, 40.0f);
		CHECK(Connect(second, time, "out", sine, "in", why) && Connect(second, sine, "out", OutputNodeOf(second), "roughness", why));
		second.FindNode(sine)->inputs["in"] = { 2.0f };
		const Clipboard clipboard = Copy(second, { time, sine, OutputNodeOf(second) });
		CHECK(clipboard.nodes.size() == 2 && clipboard.links.size() == 1);
		const std::vector<uint32_t> pasted = Paste(second, clipboard, 500.0f, 600.0f);
		CHECK(pasted.size() == 2 && pasted[0] != time && pasted[1] != sine && pasted[0] != pasted[1] && second.nodes.size() == 5);
		CHECK(second.FindNode(pasted[0])->x == 500.0f && second.FindNode(pasted[0])->y == 600.0f);
		CHECK(second.FindNode(pasted[1])->x == 600.0f && second.FindNode(pasted[1])->y == 620.0f);
		CHECK(second.FindLinkInto(pasted[1], "in") && second.FindLinkInto(pasted[1], "in")->fromNode == pasted[0]);
		CHECK(second.FindNode(pasted[1])->inputs.count("in") == 1);
		CHECK(second.FindLinkInto(sine, "in")->fromNode == time);   // the originals are as they were
		CHECK(Generate(second).ok);
		Graph third = NewGraph();
		CHECK(Paste(third, clipboard, 0.0f, 0.0f).size() == 2 && third.links.size() == 1 && Generate(third).ok);

		// Parameters: unique names; a rename reaches the nodes; a delete takes them along.
		Graph fourth = NewGraph();
		const size_t first = AddParameter(fourth, ParameterType::Colour);
		const size_t other = AddParameter(fourth, ParameterType::Colour);
		CHECK(fourth.parameters[first].name == "colour" && fourth.parameters[other].name == "colour2");
		const uint32_t reader = AddNode(fourth, "Parameter", 0.0f, 0.0f, "colour");
		CHECK(Connect(fourth, reader, "out", OutputNodeOf(fourth), "albedo", why));
		CHECK(RenameParameter(fourth, first, "tint", why) && fourth.FindNode(reader)->settings.at("parameter") == "tint" && Generate(fourth).ok);
		CHECK(!RenameParameter(fourth, first, "colour2", why) && !RenameParameter(fourth, first, "two words", why));
		CHECK(RenameParameter(fourth, first, "tint", why));   // to its own name: fine, nothing to do
		CHECK(MoveParameter(fourth, 0, 1) && fourth.parameters[1].name == "tint" && !MoveParameter(fourth, 1, 1) && !MoveParameter(fourth, 0, -1));
		DeleteParameter(fourth, 1);
		CHECK(fourth.parameters.size() == 1 && fourth.FindNode(reader) == nullptr && fourth.links.empty() && Generate(fourth).ok);

		// What the generator tells an editor about each node's pins.
		Trial trial = MakeTrial("Multiply", "out", "albedo", "WorldNormal", "out", "a");
		const Generated typed = Generate(trial.graph);
		CHECK(typed.nodes.size() == 3);
		for (const Generated::NodeResult& node : typed.nodes) {
			if (node.node != trial.node) { continue; }
			CHECK(node.valid && node.used && node.working == ValueType::Float3 && node.outputs.size() == 1 && node.outputs[0] == ValueType::Float3);
		}
	}

	void TestDocument() {
		std::printf("undo and redo\n");
		Document document;
		CHECK(!document.Dirty() && !document.CanUndo() && !document.CanRedo());

		const uint32_t time = AddNode(document.GetGraph(), "Time", 0.0f, 0.0f);
		document.Commit();
		CHECK(document.Dirty() && document.CanUndo());
		document.Commit();   // nothing changed: not another step
		document.GetGraph().FindNode(time)->x = 40.0f;
		document.Commit();

		CHECK(document.Undo() && document.GetGraph().FindNode(time)->x == 0.0f);
		CHECK(document.Undo() && document.GetGraph().FindNode(time) == nullptr && !document.CanUndo() && !document.Dirty());
		CHECK(!document.Undo());
		CHECK(document.Redo() && document.Redo() && !document.Redo() && document.GetGraph().FindNode(time)->x == 40.0f);

		// A new edit after an undo forgets what could have been redone; ids are still never reused.
		CHECK(document.Undo());
		const uint32_t uv = AddNode(document.GetGraph(), "UV", 0.0f, 0.0f);
		document.Commit();
		CHECK(!document.CanRedo() && uv > time);

		// Saved, then undone: changed again. Redone: as saved.
		document.MarkSaved();
		CHECK(!document.Dirty());
		CHECK(document.Undo() && document.Dirty() && document.Redo() && !document.Dirty());

		// Check() follows the graph once told it changed.
		CHECK(document.Check().ok);
		std::string why;
		const uint32_t sample = AddNode(document.GetGraph(), "SampleTexture", 0.0f, 0.0f);
		CHECK(Connect(document.GetGraph(), sample, "rgb", OutputNodeOf(document.GetGraph()), "albedo", why));
		document.Touch();
		CHECK(!document.Check().ok);
		document.Commit();
		CHECK(document.Undo() && document.Check().ok);

		// A file that does not load leaves the document alone; one that does starts clean.
		std::string error;
		CHECK(!document.Load("{ nope", error) && document.GetGraph().FindNode(uv) != nullptr && document.CanUndo());
		CHECK(document.Load(R"({ "version": 1, "nodes": [ { "id": 5, "type": "Output" } ] })", error) && !document.Dirty() && !document.CanUndo());
		CHECK(document.GetGraph().nextId == 6);
		document.New();
		CHECK(document.Dirty() && document.GetGraph().nodes.size() == 1 && !document.CanUndo());
	}

	// ---- Rows added in step 8: a few whose text is worth pinning down ---------------------------------

	void TestNewNodes() {
		std::printf("step 8 nodes\n");
		Graph graph = NewGraph();
		const uint32_t output = OutputNodeOf(graph);
		const uint32_t tint = AddNode(graph, "MaterialTint", 0.0f, 0.0f);
		const uint32_t uv = AddNode(graph, "UV", 0.0f, 0.0f);
		const uint32_t voronoi = AddNode(graph, "Voronoi", 0.0f, 0.0f);
		const uint32_t append = AddNode(graph, "AppendZ", 0.0f, 0.0f);
		const uint32_t multiply = AddNode(graph, "Multiply", 0.0f, 0.0f);
		const uint32_t roughness = AddNode(graph, "MaterialRoughness", 0.0f, 0.0f);
		std::string why;
		CHECK(Connect(graph, uv, "out", append, "xy", why) && Connect(graph, voronoi, "cell", append, "z", why));
		CHECK(Connect(graph, append, "out", multiply, "a", why) && Connect(graph, tint, "rgb", multiply, "b", why));
		CHECK(Connect(graph, multiply, "out", output, "albedo", why) && Connect(graph, tint, "a", output, "alpha", why));
		CHECK(Connect(graph, roughness, "out", output, "roughness", why));
		// The types the new rows give: a float3 from AppendZ fits albedo; a float2 would not.
		CHECK(!Connect(graph, uv, "out", output, "emissive", why));

		const Generated result = Generate(graph);
		if (!result.ok) { PrintProblems(graph, result); }
		CHECK(result.ok);
		CHECK(Contains(result.source, "float3 n2_rgb = pc.albedoTint.rgb;") && Contains(result.source, "float n2_a = pc.albedoTint.a;"));
		CHECK(Contains(result.source, "float n4_cell = GraphVoronoi(s.uv * 6.0).y;") && !Contains(result.source, "n4_distance"));
		CHECK(Contains(result.source, "float3 n5_out = float3(n3_out, n4_cell);"));
		CHECK(Contains(result.source, "o.roughness = n7_out;") && Contains(result.source, "float n7_out = pc.roughnessFactor;"));

		// A texture read from three sides, and a pin with nothing linked that is a direction of the surface.
		Trial trial = MakeTrial("TriplanarTexture", "rgb", "albedo", "Parameter", "out", "texture");
		trial.graph.parameters.push_back(Parameter{ "rock", ParameterType::Texture });
		trial.graph.FindNode(trial.feed)->settings["parameter"] = "rock";
		CHECK(Contains(Generate(trial.graph).source, "float4 n2_rgba = GraphTriplanar(gTex_rock, s.worldPos, s.normalWS, 1.0, 4.0);"));
		trial = MakeTrial("Reflect", "out", "emissive", nullptr, nullptr, nullptr);
		CHECK(Contains(Generate(trial.graph).source, "float3 n2_out = reflect(float3(0.0, -1.0, 0.0), s.normalWS);"));
	}

	// ---- Constants as hidden parameters, for a live preview	// ---- The Output's vertex offset: a second function -------------------------------------------------

	void TestVertexOffset() {
		std::printf("vertex offset\n");
		const std::string kFunction = "float3 EvaluateVertexOffset(SurfaceInput s) {";

		// Output(1). Multiply(4) = WorldNormal(2) * Sine(3) of Time(5); Fresnel(6) into emissive.
		Graph graph = NewGraph();
		const uint32_t output = OutputNodeOf(graph);
		const uint32_t normal = AddNode(graph, "WorldNormal", 0.0f, 0.0f);
		const uint32_t sine = AddNode(graph, "Sine", 0.0f, 0.0f);
		const uint32_t multiply = AddNode(graph, "Multiply", 0.0f, 0.0f);
		const uint32_t time = AddNode(graph, "Time", 0.0f, 0.0f);
		const uint32_t fresnel = AddNode(graph, "Fresnel", 0.0f, 0.0f);
		const uint32_t uv = AddNode(graph, "UV", 0.0f, 0.0f);
		std::string why;
		CHECK(Connect(graph, time, "out", sine, "in", why) && Connect(graph, normal, "out", multiply, "a", why) && Connect(graph, sine, "out", multiply, "b", why));
		CHECK(Connect(graph, fresnel, "out", output, "emissive", why));

		// The pin is in the table: a float3 with no constant to type.
		const NodeType* outputType = FindNodeType(kOutputNode);
		const int pin = outputType->FindInput(kVertexOffsetPin);
		CHECK(pin >= 0 && outputType->inputs[static_cast<size_t>(pin)].type == ValueType::Float3 && outputType->inputs[static_cast<size_t>(pin)].defaultCode != nullptr);

		// Nothing plugged into it: a shader like any other.
		const Generated still = Generate(graph);
		CHECK(still.ok && !still.movesVertices);
		CHECK(!Contains(still.source, "SURFACE_MOVES_VERTICES") && !Contains(still.source, "EvaluateVertexOffset") && !Contains(still.source, "vertexOffset"));
		CHECK(!Contains(still.source, "n4_out"));   // the Multiply feeds nothing yet
		CHECK(Generate(graph, Options{ true }).liveConstants.size() == 7);   // the Output's six and the Fresnel's power: none for the new pin

		// Plugged in: a second function, after the surface and before the engine's part.
		CHECK(Connect(graph, multiply, "out", output, kVertexOffsetPin, why));
		const Generated moved = Generate(graph);
		if (!moved.ok) { PrintProblems(graph, moved); }
		CHECK(moved.ok && moved.movesVertices && moved.problems.empty());
		const size_t define = moved.source.find("\n#define SURFACE_MOVES_VERTICES\n");
		const size_t function = moved.source.find(kFunction);
		const size_t include = moved.source.find("#include \"SurfaceTemplate.slang\"");
		CHECK(define != std::string::npos && function != std::string::npos && include != std::string::npos);
		CHECK(moved.source.find("return o;") < define && define < function && function < include);
		CHECK(moved.lineNodes.size() == static_cast<size_t>(std::count(moved.source.begin(), moved.source.end(), '\n')));
		CHECK(!Contains(moved.source, "SURFACE_CUTOUT_TEXTURE"));   // the graph has no texture
		if (function != std::string::npos && define != std::string::npos) {
			// Each function has the nodes it reads, and no others.
			const std::string surface = moved.source.substr(0, moved.source.find("// The graph moves the mesh's vertices")), vertex = moved.source.substr(function);
			CHECK(Contains(vertex, "float n5_out = s.time;") && Contains(vertex, "float3 n4_out = n2_out * float3(n3_out);") && Contains(vertex, "    return n4_out;\n}"));
			CHECK(!Contains(surface, "n4_out") && !Contains(surface, "n5_out") && Contains(surface, "o.emissive = float3(n6_out);"));
			CHECK(!Contains(vertex, "n6_out") && !Contains(vertex, "SurfaceData") && !Contains(moved.source, "o.vertexOffset"));
			// The surface function is what it was without the link.
			CHECK(still.source.substr(0, still.source.find("#include \"SurfaceTemplate.slang\"")) == surface);
		}
		for (const Generated::NodeResult& node : moved.nodes) { CHECK(node.used == (node.node != uv)); }

		// Saved and loaded, the link is still there.
		Graph loaded;
		std::string error;
		CHECK(Load(Save(graph), loaded, error) && Generate(loaded).source == moved.source);

		// A node both functions read is written in both.
		CHECK(Connect(graph, multiply, "out", output, "albedo", why));
		const Generated both = Generate(graph);
		const size_t bothAt = both.source.find(kFunction);
		CHECK(both.ok && bothAt != std::string::npos);
		if (bothAt != std::string::npos) {
			CHECK(Contains(both.source.substr(0, bothAt), "float3 n4_out = n2_out * float3(n3_out);") && Contains(both.source.substr(0, bothAt), "o.albedo = n4_out;"));
			CHECK(Contains(both.source.substr(bothAt), "float3 n4_out = n2_out * float3(n3_out);"));
		}
		// ... and a constant both read is one hidden parameter in a live preview.
		DeleteLinkInto(graph, multiply, "b");
		const Generated bothLive = Generate(graph, Options{ true });
		size_t constants = 0;
		for (const Generated::LiveConstant& constant : bothLive.liveConstants) { if (constant.node == multiply) { ++constants; } }
		CHECK(bothLive.ok && bothLive.movesVertices && constants == 1);
		CHECK(Connect(graph, sine, "out", multiply, "b", why));

		// Types: a float is repeated into the three directions; a float2 does not fit.
		CHECK(Connect(graph, sine, "out", output, kVertexOffsetPin, why));
		CHECK(Contains(Generate(graph).source, "    return float3(n3_out);\n}"));
		CHECK(!Connect(graph, uv, "out", output, kVertexOffsetPin, why));
		CHECK(Generate(graph).movesVertices);   // the refused link changed nothing

		// The material's built-in controls cannot feed it (the depth passes do not have them) -
		// though the same node can still feed the surface.
		const uint32_t tint = AddNode(graph, "MaterialTint", 0.0f, 0.0f);
		CHECK(Connect(graph, tint, "rgb", output, "albedo", why) && Generate(graph).ok);
		Graph refused = graph;
		DeleteLinkInto(refused, output, kVertexOffsetPin);
		Connect(refused, tint, "rgb", output, kVertexOffsetPin);   // the test's own Connect: no questions asked
		const Generated bad = Generate(refused);
		CHECK(!bad.ok && bad.source.empty() && !bad.movesVertices);
		bool reported = false;
		for (const Problem& problem : bad.problems) { reported = reported || (problem.node == tint && problem.stops && Contains(problem.message, "vertex offset")); }
		CHECK(reported);
		CHECK(!Connect(graph, tint, "rgb", output, kVertexOffsetPin, why) && Contains(why, "vertex offset"));   // and the editor refuses the link

		// A texture: read at a named mip level in the vertex function, the usual way in the
		// surface; and the graph's first texture is the cut-out's in the depth passes.
		Trial trial = MakeTrial("SampleTexture", "rgb", kVertexOffsetPin, "Parameter", "out", "texture");
		trial.graph.parameters.push_back(Parameter{ "wind", ParameterType::Texture });
		trial.graph.FindNode(trial.feed)->settings["parameter"] = "wind";
		Connect(trial.graph, trial.node, "rgb", trial.output, "albedo");
		const Generated textured = Generate(trial.graph);
		if (!textured.ok) { PrintProblems(trial.graph, textured); }
		const size_t texturedAt = textured.source.find(kFunction);
		CHECK(textured.ok && textured.movesVertices && texturedAt != std::string::npos);
		if (texturedAt != std::string::npos) {
			CHECK(Contains(textured.source.substr(0, texturedAt), "float4 n2_rgba = gTex_wind.Sample(s.uv);"));
			CHECK(Contains(textured.source.substr(texturedAt), "float4 n2_rgba = gTex_wind.SampleLevel(s.uv, 0.0);") && Contains(textured.source.substr(texturedAt), "return n2_rgb;"));
			CHECK(Contains(textured.source, "#define SURFACE_MOVES_VERTICES\n#define SURFACE_CUTOUT_TEXTURE gTex_wind\n"));
		}
		trial = MakeTrial("TriplanarTexture", "rgb", kVertexOffsetPin, "Parameter", "out", "texture");
		trial.graph.parameters.push_back(Parameter{ "rock", ParameterType::Texture });
		trial.graph.FindNode(trial.feed)->settings["parameter"] = "rock";
		CHECK(Contains(Generate(trial.graph).source, "float4 n2_rgba = GraphTriplanarVertex(gTex_rock, s.worldPos, s.normalWS, 1.0, 4.0);"));

		// A graph's parameters are read in the vertex function too.
		trial = MakeTrial("Multiply", "out", kVertexOffsetPin, "WorldNormal", "out", "a");
		trial.graph.parameters.push_back(Parameter{ "sway", ParameterType::Float, { 0.1f } });
		const uint32_t sway = trial.graph.AddNode("Parameter");
		trial.graph.FindNode(sway)->settings["parameter"] = "sway";
		Connect(trial.graph, sway, "out", trial.node, "b");
		const Generated swaying = Generate(trial.graph);
		const size_t swayingAt = swaying.source.find(kFunction);
		CHECK(swaying.ok && swayingAt != std::string::npos);
		if (swayingAt != std::string::npos) {
			CHECK(Contains(swaying.source.substr(swayingAt), "    GraphParams P = LoadGraphParams();\n") && Contains(swaying.source.substr(swayingAt), "float3 n2_out = n3_out * float3(P.sway);"));
		}
	}

	// ---- Constants as hidden parameters, for a live preview ----------------------------------------------

	void TestLiveConstants() {
		std::printf("live constants\n");
		// Output(1) <- Remap(2) <- Time(3); the Remap's other four pins and six of the Output's are constants.
		Graph graph = NewGraph();
		const uint32_t output = OutputNodeOf(graph);
		graph.parameters.push_back(Parameter{ "glow", ParameterType::Float, { 1.5f } });
		const uint32_t remap = AddNode(graph, "Remap", 0.0f, 0.0f);
		const uint32_t time = AddNode(graph, "Time", 0.0f, 0.0f);
		std::string why;
		CHECK(Connect(graph, time, "out", remap, "in", why) && Connect(graph, remap, "out", output, "roughness", why));
		graph.FindNode(remap)->inputs["outMax"] = { 0.25f };

		const Generated baked = Generate(graph);
		const Generated live = Generate(graph, Options{ true });
		CHECK(baked.ok && live.ok && baked.liveConstants.empty());
		CHECK(!Contains(baked.source, "MaterialParam(1)") && Contains(baked.source, "0.25"));

		// One hidden parameter per constant, after the graph's own (slot 0); a pin a row's code
		// names twice is still one parameter.
		CHECK(live.liveConstants.size() == 10);
		CHECK(Contains(live.source, "float n2_out = MaterialParam(1).x + (n3_out - MaterialParam(2).x) * (MaterialParam(3).x - MaterialParam(1).x) / (MaterialParam(4).x - MaterialParam(2).x);"));
		CHECK(Contains(live.source, "o.albedo = MaterialParam(5).xyz;") && Contains(live.source, "o.metallic = MaterialParam(7).x;"));
		CHECK(Contains(live.source, "o.roughness = n2_out;") && Contains(live.source, "P.glow = MaterialParam(0).x;"));
		CHECK(!Contains(live.source, "0.25"));
		for (const Generated::LiveConstant& constant : live.liveConstants) {
			if (constant.node == remap && constant.pin == "outMax") { CHECK(constant.slot == 3 && constant.value[0] == 0.25f && constant.name == "_n2_outMax"); }
		}

		// The list: the graph's own first, then the hidden ones with the constants as defaults.
		CHECK(live.paramsJson.find("\"glow\"") < live.paramsJson.find("\"_n2_outMin\""));
		CHECK(Contains(live.paramsJson, "{ \"name\": \"_n2_outMax\", \"type\": \"float\", \"default\": 0.25 }"));
		CHECK(Contains(live.paramsJson, "{ \"name\": \"_n1_albedo\", \"type\": \"vec3\", \"default\": [0.8, 0.8, 0.8] }"));
		CHECK(!Contains(baked.paramsJson, "_n"));

		// A constant on an "any" pin of a node working in a vector is one number, repeated.
		Trial trial = MakeTrial("Multiply", "out", "albedo", "WorldNormal", "out", "a");
		CHECK(Contains(Generate(trial.graph, Options{ true }).source, "float3 n2_out = n3_out * float3(MaterialParam(0).x);"));

		// Names: made and read back.
		uint32_t node = 0;
		std::string pin;
		CHECK(LiveConstantName(7, "b") == "_n7_b");
		CHECK(ParseLiveConstantName("_n2_outMax", node, pin) && node == 2 && pin == "outMax");
		CHECK(ParseLiveConstantName("_n4000000000_x", node, pin) && node == 4000000000u);
		CHECK(!ParseLiveConstantName("glow", node, pin) && !ParseLiveConstantName("_fine", node, pin) && !ParseLiveConstantName("_n_x", node, pin));
		CHECK(!ParseLiveConstantName("_n0_x", node, pin) && !ParseLiveConstantName("_n12", node, pin) && !ParseLiveConstantName("_n12_", node, pin));
		CHECK(!ParseLiveConstantName("_n99999999999_x", node, pin));

		// When the list is full, the constants that do not fit stay numbers in the code.
		Graph full = NewGraph();
		for (size_t i = 0; i < kMaxNumberParameters - 2; ++i) { full.parameters.push_back(Parameter{ "p" + std::to_string(i) }); }
		const Generated squeezed = Generate(full, Options{ true });
		CHECK(squeezed.ok && squeezed.liveConstants.size() == 2 && Contains(squeezed.source, "o.metallic = 0.0;"));
	}

}   // anonymous namespace

int main(int argc, char** argv) {
	const char* writeTo = argc > 1 ? argv[1] : nullptr;

	TestTable();
	TestSaveLoad();
	TestOldAndOddFiles();
	TestGeneratedText();
	TestLoops();
	TestTypes();
	TestStructure();
	TestParameters();
	TestAllNodes(writeTo);
	TestEdits();
	TestDocument();
	TestLiveConstants();
	TestNewNodes();
	TestVertexOffset();

	if (g_failed != 0) {
		std::printf("\n%d of %d checks FAILED\n", g_failed, g_checks);
		return 1;
	}
	std::printf("\nAll %d checks passed\n", g_checks);
	return 0;
}
