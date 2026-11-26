/*!
* @file			ImGuiPanel_AssetBrowser_Thumbnails.cpp
*
* @brief		The Asset Browser's texture thumbnails: an image's tile shows the image.
*
*				  - Only tiles on screen ask for one (ThumbnailFor), so a big folder costs nothing
*				    until it is scrolled to.
*				  - The file is read, decoded and shrunk to kThumbnailSize on worker threads, a few at
*				    a time (TextureBuild, the same decoder the texture compiler uses).
*				  - The pixels are uploaded on the main thread; ImGui gets the texture two frames later,
*				    once the upload is on the GPU (the Texture Importer's previews do the same).
*				  - A changed file (its content hash) gets a new thumbnail. Past kMaxThumbnails, the
*				    ones drawn longest ago are freed, a few frames later, once no frame in flight
*				    still draws them.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/Panels/ImGuiPanel_AssetBrowser.h"
#include "Diagnostics/Logger.h"
#include "Graphics/RenderSystem.h"
#include "TextureBuild.h"   // Tools/Common: DecodeImage, Shrink
#include <Core.h>

#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>

namespace fs = std::filesystem;

namespace {

	constexpr std::uint32_t kThumbnailSize   = 128;   // longest side; enough for the largest tile
	constexpr std::size_t   kMaxJobs         = 3;     // images decoded at once
	constexpr std::size_t   kMaxThumbnails   = 256;   // kept on the GPU (ImGui's descriptor pool holds 512)
	constexpr int           kUploadDelay     = 2;     // frames: a new texture is on the GPU from the next frame
	constexpr int           kRetireFrames    = 4;     // frames before a freed thumbnail is destroyed (> frames in flight)
	constexpr std::uintmax_t kMaxFileBytes   = 64ull << 20;   // a larger image keeps its badge

	// Images stb_image reads (TextureBuild::DecodeImage). Others keep the badge (.dds, .psd...).
	constexpr std::array<std::string_view, 7> kDecodable = { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".gif" };

	bool IsDecodable(std::string_view extension) {
		return std::find(kDecodable.begin(), kDecodable.end(), extension) != kDecodable.end();
	}

	Crucible::Renderer* GetRenderer() {
		Crucible::Core* core = Crucible::EditorManager::GetInstance().GetCore();
		return core ? &core->GetRenderer() : nullptr;
	}
}

void* Crucible::AssetBrowser::ThumbnailFor(Item const& item, ImVec2& imageSize) {
	if (item.isFolder || item.type != AssetType::Texture) return nullptr;
	AssetEntry const* entry = m_assets.Find(item.virtualPath);
	if (entry == nullptr || entry->compiled || !IsDecodable(entry->extension)) return nullptr;

	int const frame = ImGui::GetFrameCount();
	auto found = m_thumbnails.find(item.virtualPath);
	if (found != m_thumbnails.end() && found->second.contentHash != entry->contentHash) {
		RetireThumbnail(found->second);   // the image changed on disk: make it again
		m_thumbnails.erase(found);
		found = m_thumbnails.end();
	}
	if (found == m_thumbnails.end()) {
		if (m_thumbnailInFlight.count(item.virtualPath) == 0) m_thumbnailWanted.push_back(item.virtualPath);
		return nullptr;
	}

	Thumbnail& thumbnail = found->second;
	thumbnail.lastUsedFrame = frame;
	if (thumbnail.failed) return nullptr;
	if (thumbnail.set == nullptr) {
		if (!thumbnail.texture.IsValid() || frame < thumbnail.readyFrame) return nullptr;
		Renderer* renderer = GetRenderer();
		if (renderer == nullptr) return nullptr;
		try {
			thumbnail.set = ImGui_ImplVulkan_AddTexture(renderer->GetTextureView(thumbnail.texture), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
		catch (std::exception const& e) {
			CRU_ERROR("[AssetBrowser] Thumbnail for {} failed: {}", item.virtualPath, e.what());
		}
		if (thumbnail.set == nullptr) { thumbnail.failed = true; return nullptr; }
	}
	imageSize = ImVec2(static_cast<float>(thumbnail.width), static_cast<float>(thumbnail.height));
	return thumbnail.set;
}

void Crucible::AssetBrowser::UpdateThumbnails() {
	FreeRetiredThumbnails(false);
	Renderer* renderer = GetRenderer();
	int const frame = ImGui::GetFrameCount();

	// Finished decodes: upload them.
	for (auto job = m_thumbnailJobs.begin(); job != m_thumbnailJobs.end();) {
		if (job->wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++job; continue; }
		ThumbnailResult result = job->get();
		job = m_thumbnailJobs.erase(job);
		m_thumbnailInFlight.erase(result.virtualPath);

		Thumbnail thumbnail;
		thumbnail.contentHash = result.contentHash;
		thumbnail.lastUsedFrame = frame;
		thumbnail.failed = result.failed || renderer == nullptr;
		if (!thumbnail.failed) {
			try {
				thumbnail.texture = renderer->CreateTextureFromPixels(result.rgba.data(), result.width, result.height, TexturePixelFormat::RGBA8, false);
				thumbnail.width = result.width;
				thumbnail.height = result.height;
				thumbnail.readyFrame = frame + kUploadDelay;
			}
			catch (std::exception const& e) {
				CRU_ERROR("[AssetBrowser] Thumbnail for {} failed: {}", result.virtualPath, e.what());
				thumbnail.failed = true;
			}
		}
		if (auto old = m_thumbnails.find(result.virtualPath); old != m_thumbnails.end()) RetireThumbnail(old->second);
		m_thumbnails[result.virtualPath] = std::move(thumbnail);
	}

	// Past the cap: free the ones drawn longest ago (never one drawn in the last two frames).
	if (m_thumbnails.size() > kMaxThumbnails) {
		std::vector<std::pair<int, std::string>> byAge;
		for (auto const& [path, thumbnail] : m_thumbnails) byAge.emplace_back(thumbnail.lastUsedFrame, path);
		std::sort(byAge.begin(), byAge.end());
		std::size_t excess = m_thumbnails.size() - kMaxThumbnails;
		for (auto const& [lastUsed, path] : byAge) {
			if (excess == 0 || lastUsed >= frame - 2) break;
			RetireThumbnail(m_thumbnails[path]);
			m_thumbnails.erase(path);
			--excess;
		}
	}

	// Start decoding what last frame's tiles asked for, in the order they were drawn.
	std::vector<std::string> const wanted = std::move(m_thumbnailWanted);
	m_thumbnailWanted.clear();
	for (std::string const& path : wanted) {
		if (m_thumbnailJobs.size() >= kMaxJobs) break;
		if (m_thumbnailInFlight.count(path) != 0 || m_thumbnails.count(path) != 0) continue;
		if (m_thumbnails.size() + m_thumbnailJobs.size() >= kMaxThumbnails) break;   // all on screen already: wait for room
		AssetEntry const* entry = m_assets.Find(path);
		if (entry == nullptr) continue;

		m_thumbnailInFlight.insert(path);
		fs::path const file = entry->absolutePath;
		std::uint64_t const hash = entry->contentHash;
		m_thumbnailJobs.push_back(std::async(std::launch::async, [file, path, hash]() {
			ThumbnailResult result;
			result.virtualPath = path;
			result.contentHash = hash;
			try {
				std::error_code ec;
				if (fs::file_size(file, ec) > kMaxFileBytes || ec) throw std::runtime_error("too large or unreadable");
				std::ifstream in(file, std::ios::binary);
				std::vector<unsigned char> const bytes{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
				TextureBuild::Image image = TextureBuild::Shrink(TextureBuild::DecodeImage(bytes), kThumbnailSize, true, false);
				result.width = image.width;
				result.height = image.height;
				result.rgba = std::move(image.rgba);
				result.failed = result.width == 0 || result.height == 0;
			}
			catch (std::exception const&) {
				result.failed = true;   // the badge stays; the Texture Importer says why if it is opened
			}
			return result;
		}));
	}
}

void Crucible::AssetBrowser::RetireThumbnail(Thumbnail& thumbnail) {
	if (thumbnail.set != nullptr || thumbnail.texture.IsValid()) {
		int const frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : 0;
		m_retiredThumbnails.push_back({ thumbnail.set, thumbnail.texture, frame + kRetireFrames });
	}
	thumbnail = {};
}

void Crucible::AssetBrowser::FreeRetiredThumbnails(bool all) {
	Renderer* renderer = GetRenderer();
	int const frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : 0;
	std::erase_if(m_retiredThumbnails, [&](RetiredThumbnail const& retired) {
		if (!all && frame < retired.freeAtFrame) return false;
		if (retired.set != nullptr) ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(retired.set));
		if (retired.texture.IsValid() && renderer != nullptr) renderer->DestroyTexture(retired.texture);
		return true;
	});
}

void Crucible::AssetBrowser::ReleaseThumbnails() {
	for (std::future<ThumbnailResult>& job : m_thumbnailJobs)
		if (job.valid()) job.wait();
	m_thumbnailJobs.clear();
	m_thumbnailInFlight.clear();
	for (auto& [path, thumbnail] : m_thumbnails) RetireThumbnail(thumbnail);
	m_thumbnails.clear();
	FreeRetiredThumbnails(true);
}
