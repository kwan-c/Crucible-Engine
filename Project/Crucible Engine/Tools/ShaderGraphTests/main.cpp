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
	Graph AllNodesGraph(uint32_t components) {
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

			if (writeTo) {
				const std::filesystem::path file = std::filesystem::path(writeTo) / ("AllNodes_" + std::string(ValueTypeName(NumericType(components))) + ".slang");
				std::ofstream out(file, std::ios::binary | std::ios::trunc);
				out << result.source;
				std::printf("    wrote %s\n", file.string().c_str());
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

	if (g_failed != 0) {
		std::printf("\n%d of %d checks FAILED\n", g_failed, g_checks);
		return 1;
	}
	std::printf("\nAll %d checks passed\n", g_checks);
	return 0;
}
