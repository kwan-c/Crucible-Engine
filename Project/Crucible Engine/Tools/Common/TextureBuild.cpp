/*!
* @file			TextureBuild.cpp
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		Image -> GPU texture for the offline compilers (see TextureBuild.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "TextureBuild.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstddef>      // cmp_core.h uses NULL
#include <cmp_core.h>

#include <glm/gtc/packing.hpp>   // packHalf1x16 (BC6H input)

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace Crucible::TextureBuild {

	namespace {

		void Check(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }

		// ---- Mipmaps -------------------------------------------------------------------------------

		struct SrgbTables {
			std::array<float, 256>    toLinear{};
			std::array<uint8_t, 4096> toSrgb{};
			SrgbTables() {
				for (int i = 0; i < 256; ++i) {
					const float c = static_cast<float>(i) / 255.0f;
					toLinear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
				}
				for (int i = 0; i < 4096; ++i) {
					const float l = static_cast<float>(i) / 4095.0f;
					const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
					toSrgb[i] = static_cast<uint8_t>(std::clamp(c * 255.0f + 0.5f, 0.0f, 255.0f));
				}
			}
		};

		// The next level of an RGBA8 image: each texel averages the (up to) 2x2 texels it covers.
		// Same filter as RenderResources::HalveRGBA8, which made mipmaps at runtime before .bill.
		std::vector<unsigned char> Halve(const std::vector<unsigned char>& src, uint32_t w, uint32_t h, uint32_t dw, uint32_t dh, bool srgb) {
			static const SrgbTables tables;
			std::vector<unsigned char> dst(size_t{ dw } * dh * 4);
			for (uint32_t y = 0; y < dh; ++y) {
				const uint32_t y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
				for (uint32_t x = 0; x < dw; ++x) {
					const uint32_t x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1);
					const unsigned char* p[4] = {
						&src[(size_t{ y0 } * w + x0) * 4], &src[(size_t{ y0 } * w + x1) * 4],
						&src[(size_t{ y1 } * w + x0) * 4], &src[(size_t{ y1 } * w + x1) * 4] };
					unsigned char* out = &dst[(size_t{ y } * dw + x) * 4];
					for (int c = 0; c < 4; ++c) {
						if (srgb && c < 3) {
							const float sum = tables.toLinear[p[0][c]] + tables.toLinear[p[1][c]] + tables.toLinear[p[2][c]] + tables.toLinear[p[3][c]];
							out[c] = tables.toSrgb[static_cast<size_t>(sum * 0.25f * 4095.0f + 0.5f)];
						} else {
							out[c] = static_cast<unsigned char>((p[0][c] + p[1][c] + p[2][c] + p[3][c] + 2) / 4);
						}
					}
				}
			}
			return dst;
		}

		// The next level of a tangent-space normal map: the 2x2 average of the directions (RGB as
		// -1..1), made unit length again - averaging shortens them, which would flatten the bumps.
		std::vector<unsigned char> HalveNormals(const std::vector<unsigned char>& src, uint32_t w, uint32_t h, uint32_t dw, uint32_t dh) {
			std::vector<unsigned char> dst(size_t{ dw } * dh * 4);
			for (uint32_t y = 0; y < dh; ++y) {
				const uint32_t y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
				for (uint32_t x = 0; x < dw; ++x) {
					const uint32_t x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1);
					const size_t at[4] = { (size_t{ y0 } * w + x0) * 4, (size_t{ y0 } * w + x1) * 4, (size_t{ y1 } * w + x0) * 4, (size_t{ y1 } * w + x1) * 4 };
					glm::vec3 sum(0.0f);
					unsigned alpha = 0;
					for (size_t i : at) {
						sum += glm::vec3(src[i], src[i + 1], src[i + 2]) / 127.5f - 1.0f;
						alpha += src[i + 3];
					}
					const glm::vec3 n = glm::length(sum) > 1e-6f ? glm::normalize(sum) : glm::vec3(0.0f, 0.0f, 1.0f);
					unsigned char* out = &dst[(size_t{ y } * dw + x) * 4];
					for (int c = 0; c < 3; ++c) out[c] = static_cast<unsigned char>(std::clamp(n[c] * 127.5f + 127.5f, 0.0f, 255.0f));
					out[3] = static_cast<unsigned char>((alpha + 2) / 4);
				}
			}
			return dst;
		}

		// ---- Compression ---------------------------------------------------------------------------

		// Compressonator's options for one format; each worker thread has its own.
		struct Encoder {
			CompiledTextureFormat format;
			void* options = nullptr;
			Encoder(CompiledTextureFormat f, float quality) : format(f) {
				switch (format) {
					case CompiledTextureFormat::BC1:  CreateOptionsBC1(&options); SetQualityBC1(options, quality); break;
					case CompiledTextureFormat::BC3:  CreateOptionsBC3(&options); SetQualityBC3(options, quality); break;
					case CompiledTextureFormat::BC4:  CreateOptionsBC4(&options); SetQualityBC4(options, quality); break;
					case CompiledTextureFormat::BC5:  CreateOptionsBC5(&options); SetQualityBC5(options, quality); break;
					case CompiledTextureFormat::BC6H: CreateOptionsBC6(&options); SetQualityBC6(options, quality); break;
					case CompiledTextureFormat::BC7:  CreateOptionsBC7(&options); SetQualityBC7(options, quality); break;
					default: break;
				}
			}
			~Encoder() {
				switch (format) {
					case CompiledTextureFormat::BC1:  DestroyOptionsBC1(options); break;
					case CompiledTextureFormat::BC3:  DestroyOptionsBC3(options); break;
					case CompiledTextureFormat::BC4:  DestroyOptionsBC4(options); break;
					case CompiledTextureFormat::BC5:  DestroyOptionsBC5(options); break;
					case CompiledTextureFormat::BC6H: DestroyOptionsBC6(options); break;
					case CompiledTextureFormat::BC7:  DestroyOptionsBC7(options); break;
					default: break;
				}
			}
			Encoder(const Encoder&) = delete;
			Encoder& operator=(const Encoder&) = delete;

			// Block (bx, by) of an RGBA8 image -> `out`. Texels past the edge repeat the last row/column.
			// BC1/BC3/BC7 take all four channels, BC4 the red one, BC5 red and green (two planes).
			int Encode(const unsigned char* rgba, uint32_t w, uint32_t h, uint32_t bx, uint32_t by, unsigned char* out) const {
				unsigned char block[64], red[16], green[16];
				for (uint32_t row = 0; row < 4; ++row) {
					const uint32_t y = std::min(by * 4 + row, h - 1);
					for (uint32_t col = 0; col < 4; ++col) {
						const uint32_t x = std::min(bx * 4 + col, w - 1);
						const unsigned char* texel = rgba + (size_t{ y } * w + x) * 4;
						std::copy_n(texel, 4, block + (row * 4 + col) * 4);
						red[row * 4 + col]   = texel[0];
						green[row * 4 + col] = texel[1];
					}
				}
				switch (format) {
					case CompiledTextureFormat::BC1: return CompressBlockBC1(block, 16, out, options);
					case CompiledTextureFormat::BC3: return CompressBlockBC3(block, 16, out, options);
					case CompiledTextureFormat::BC4: return CompressBlockBC4(red, 4, out, options);
					case CompiledTextureFormat::BC5: return CompressBlockBC5(red, 4, green, 4, out, options);
					case CompiledTextureFormat::BC7: return CompressBlockBC7(block, 16, out, options);
					default:                         return -1;
				}
			}

			// BC6H: block (bx, by) of an RGB half-float image (3 halves per texel) -> `out`.
			int EncodeHalf(const uint16_t* rgb, uint32_t w, uint32_t h, uint32_t bx, uint32_t by, unsigned char* out) const {
				uint16_t block[48];
				for (uint32_t row = 0; row < 4; ++row) {
					const uint32_t y = std::min(by * 4 + row, h - 1);
					for (uint32_t col = 0; col < 4; ++col) {
						const uint32_t x = std::min(bx * 4 + col, w - 1);
						std::copy_n(rgb + (size_t{ y } * w + x) * 3, 3, block + (row * 4 + col) * 3);
					}
				}
				return CompressBlockBC6(block, 12, out, options);
			}
		};

		// Compressonator fills some shared tables on first use: do that once, on one thread,
		// before any workers start.
		void WarmUp() {
			static std::once_flag once;
			std::call_once(once, [] {
				unsigned char block[64] = {}, out[16];
				const uint16_t halves[48] = {};
				for (CompiledTextureFormat f : { CompiledTextureFormat::BC1, CompiledTextureFormat::BC3, CompiledTextureFormat::BC4,
				                                 CompiledTextureFormat::BC5, CompiledTextureFormat::BC7 })
					Encoder(f, 0.1f).Encode(block, 1, 1, 0, 0, out);
				Encoder(CompiledTextureFormat::BC6H, 0.05f).EncodeHalf(halves, 1, 1, 0, 0, out);
			});
		}

		// Every 4x4 block of a w x h level, on every CPU core: encode(encoder, bx, by, out) -> 0 on success.
		template<class EncodeBlock>
		std::vector<unsigned char> CompressBlocks(uint32_t w, uint32_t h, CompiledTextureFormat format, float quality,
		                                          const EncodeBlock& encode) {
			const uint32_t blocksX = (w + 3) / 4, blocksY = (h + 3) / 4;
			const size_t   blockBytes = format == CompiledTextureFormat::BC1 || format == CompiledTextureFormat::BC4 ? 8 : 16;
			std::vector<unsigned char> out(size_t{ blocksX } * blocksY * blockBytes);

			// Rows of blocks are handed out to the workers one at a time.
			std::atomic<uint32_t> nextRow{ 0 };
			std::atomic<bool>     failed{ false };
			auto work = [&] {
				const Encoder encoder(format, quality);
				for (uint32_t by = nextRow++; by < blocksY && !failed; by = nextRow++)
					for (uint32_t bx = 0; bx < blocksX; ++bx)
						if (encode(encoder, bx, by, &out[(size_t{ by } * blocksX + bx) * blockBytes]) != 0) failed = true;
			};
			const uint32_t threads = std::clamp(std::thread::hardware_concurrency(), 1u, blocksY);
			std::vector<std::jthread> workers;
			for (uint32_t i = 1; i < threads; ++i) workers.emplace_back(work);
			work();
			workers.clear();   // joins
			Check(!failed, std::string("Compressonator failed to encode a ") + ToString(format) + " block");
			return out;
		}

		std::vector<unsigned char> CompressLevel(const std::vector<unsigned char>& rgba, uint32_t w, uint32_t h,
		                                         CompiledTextureFormat format, float quality) {
			return CompressBlocks(w, h, format, quality, [&](const Encoder& e, uint32_t bx, uint32_t by, unsigned char* out) {
				return e.Encode(rgba.data(), w, h, bx, by, out);
			});
		}

	}

	Image DecodeImage(std::span<const unsigned char> fileBytes) {
		int w = 0, h = 0, channels = 0;
		stbi_uc* pixels = stbi_load_from_memory(fileBytes.data(), static_cast<int>(fileBytes.size()), &w, &h, &channels, STBI_rgb_alpha);
		Check(pixels != nullptr, std::string("Cannot decode image: ") + (stbi_failure_reason() ? stbi_failure_reason() : "unknown error"));
		Image image;
		image.width  = static_cast<uint32_t>(w);
		image.height = static_cast<uint32_t>(h);
		image.rgba.assign(pixels, pixels + size_t(w) * h * 4);
		stbi_image_free(pixels);
		return image;
	}

	CompiledTexture Build(Image image, const Options& options) {
		const CompiledTextureFormat f = options.format;
		Check(f == CompiledTextureFormat::BC1 || f == CompiledTextureFormat::BC3 || f == CompiledTextureFormat::BC4 ||
		      f == CompiledTextureFormat::BC5 || f == CompiledTextureFormat::BC7,
		      std::string("TextureBuild cannot make ") + ToString(f) + " from an 8-bit image");
		Check(image.width > 0 && image.height > 0 && image.rgba.size() == size_t{ image.width } * image.height * 4, "Bad image size");
		WarmUp();

		// The 2x2 average for shrinking and mipmaps: of directions for a normal map, else of colours.
		const auto halve = [&](const std::vector<unsigned char>& src, uint32_t w, uint32_t h, uint32_t dw, uint32_t dh) {
			return options.normalMap ? HalveNormals(src, w, h, dw, dh) : Halve(src, w, h, dw, dh, options.srgb);
		};

		// Too big: halve (the same filter as the mipmaps) until both sides fit maxSize.
		std::vector<unsigned char>& level = image.rgba;
		uint32_t width = image.width, height = image.height;
		while (width > options.maxSize || height > options.maxSize) {
			const uint32_t nw = std::max(width / 2, 1u), nh = std::max(height / 2, 1u);
			level = halve(level, width, height, nw, nh);
			width = nw; height = nh;
		}

		CompiledTexture texture;
		texture.width  = width;
		texture.height = height;
		texture.format = options.format;
		if (options.srgb) texture.flags |= CompiledTextureFlag_Srgb;
		for (size_t i = 3; i < level.size(); i += 4)
			if (level[i] != 255) { texture.flags |= CompiledTextureFlag_HasAlpha; break; }

		uint32_t lw = width, lh = height;
		for (;;) {
			std::vector<unsigned char> blocks = CompressLevel(level, lw, lh, texture.format, options.quality);
			texture.mips.push_back({ lw, lh, texture.data.size(), blocks.size() });
			texture.data.insert(texture.data.end(), blocks.begin(), blocks.end());
			if (!options.mipmaps || (lw == 1 && lh == 1)) break;
			const uint32_t nw = std::max(lw / 2, 1u), nh = std::max(lh / 2, 1u);
			level = halve(level, lw, lh, nw, nh);
			lw = nw; lh = nh;
		}
		return texture;
	}

	Image Shrink(Image image, uint32_t maxSize, bool srgb, bool normalMap) {
		maxSize = std::max(maxSize, 1u);
		while (image.width > maxSize || image.height > maxSize) {
			const uint32_t nw = std::max(image.width / 2, 1u), nh = std::max(image.height / 2, 1u);
			image.rgba = normalMap ? HalveNormals(image.rgba, image.width, image.height, nw, nh)
			                       : Halve(image.rgba, image.width, image.height, nw, nh, srgb);
			image.width = nw; image.height = nh;
		}
		return image;
	}

	std::vector<unsigned char> DecodeLevel(const CompiledTexture& texture, size_t level) {
		Check(level < texture.mips.size() && texture.layers == 1, "DecodeLevel: no such 2D level");
		const CompiledTextureMip& mip = texture.mips[level];
		const unsigned char* data = texture.data.data() + mip.offset;
		const uint32_t w = mip.width, h = mip.height, blocksX = (w + 3) / 4, blocksY = (h + 3) / 4;
		std::vector<unsigned char> rgba(size_t{ w } * h * 4);
		if (texture.format == CompiledTextureFormat::RGBA8) { std::copy_n(data, rgba.size(), rgba.data()); return rgba; }

		const size_t blockBytes = texture.format == CompiledTextureFormat::BC1 || texture.format == CompiledTextureFormat::BC4 ? 8 : 16;
		// CMP_Core decodes BC1/BC3 as BGRA unless told otherwise (one options object serves both).
		void* rgbaOrder = nullptr;
		CreateOptionsBC1(&rgbaOrder);
		SetDecodeChannelMapping(rgbaOrder, true);
		struct Release { void* o; ~Release() { DestroyOptionsBC1(o); } } const release{ rgbaOrder };
		for (uint32_t by = 0; by < blocksY; ++by) {
			for (uint32_t bx = 0; bx < blocksX; ++bx) {
				const unsigned char* block = data + (size_t{ by } * blocksX + bx) * blockBytes;
				unsigned char texels[64] = {}, red[16] = {}, green[16] = {};
				switch (texture.format) {
					case CompiledTextureFormat::BC1: DecompressBlockBC1(block, texels, rgbaOrder); break;
					case CompiledTextureFormat::BC3: DecompressBlockBC3(block, texels, rgbaOrder); break;
					case CompiledTextureFormat::BC7: DecompressBlockBC7(block, texels); break;
					case CompiledTextureFormat::BC4:
						DecompressBlockBC4(block, red);
						for (int i = 0; i < 16; ++i) { texels[i * 4] = texels[i * 4 + 1] = texels[i * 4 + 2] = red[i]; texels[i * 4 + 3] = 255; }
						break;
					case CompiledTextureFormat::BC5:
						// Two channels: rebuild blue from them, as the shader does for normal maps.
						DecompressBlockBC5(block, red, green);
						for (int i = 0; i < 16; ++i) {
							const float x = red[i] / 127.5f - 1.0f, y = green[i] / 127.5f - 1.0f;
							const float z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
							texels[i * 4] = red[i]; texels[i * 4 + 1] = green[i];
							texels[i * 4 + 2] = static_cast<unsigned char>(std::clamp(z * 127.5f + 127.5f, 0.0f, 255.0f)); texels[i * 4 + 3] = 255;
						}
						break;
					default: Check(false, std::string("DecodeLevel cannot read ") + ToString(texture.format));
				}
				for (uint32_t row = 0; row < 4; ++row) {
					const uint32_t y = by * 4 + row;
					if (y >= h) break;
					for (uint32_t col = 0; col < 4; ++col) {
						const uint32_t x = bx * 4 + col;
						if (x >= w) break;
						std::copy_n(texels + (row * 4 + col) * 4, 4, &rgba[(size_t{ y } * w + x) * 4]);
					}
				}
			}
		}
		return rgba;
	}

	ImageHdr DecodeImageHdr(std::span<const unsigned char> fileBytes) {
		int w = 0, h = 0, channels = 0;
		float* pixels = stbi_loadf_from_memory(fileBytes.data(), static_cast<int>(fileBytes.size()), &w, &h, &channels, STBI_rgb_alpha);
		Check(pixels != nullptr, std::string("Cannot decode image: ") + (stbi_failure_reason() ? stbi_failure_reason() : "unknown error"));
		ImageHdr image;
		image.width  = static_cast<uint32_t>(w);
		image.height = static_cast<uint32_t>(h);
		image.rgba.assign(pixels, pixels + size_t(w) * h * 4);
		stbi_image_free(pixels);
		return image;
	}

	CompiledTexture BuildHdrCube(const std::vector<std::vector<glm::vec3>>& levels, const std::vector<uint32_t>& sizes, float quality) {
		Check(!levels.empty() && levels.size() == sizes.size(), "BuildHdrCube: one size per level");
		WarmUp();

		CompiledTexture texture;
		texture.width  = sizes[0];
		texture.height = sizes[0];
		texture.format = CompiledTextureFormat::BC6H;
		texture.layers = 6;
		for (size_t level = 0; level < levels.size(); ++level) {
			const uint32_t s = sizes[level];
			const size_t facePixels = size_t{ s } * s;
			Check(levels[level].size() == facePixels * 6, "BuildHdrCube: a level does not hold six faces of its size");
			const size_t levelStart = texture.data.size();
			for (uint32_t face = 0; face < 6; ++face) {
				// Unsigned half floats, as BC6H (UF16) stores them.
				std::vector<uint16_t> halves(facePixels * 3);
				for (size_t i = 0; i < facePixels; ++i) {
					const glm::vec3 c = levels[level][face * facePixels + i];
					for (int k = 0; k < 3; ++k) {
						const float v = c[k] == c[k] ? std::clamp(c[k], 0.0f, 65504.0f) : 0.0f;   // NaN -> 0
						halves[i * 3 + k] = glm::packHalf1x16(v);
					}
				}
				const std::vector<unsigned char> blocks = CompressBlocks(s, s, CompiledTextureFormat::BC6H, quality,
					[&](const Encoder& e, uint32_t bx, uint32_t by, unsigned char* out) { return e.EncodeHalf(halves.data(), s, s, bx, by, out); });
				texture.data.insert(texture.data.end(), blocks.begin(), blocks.end());
			}
			texture.mips.push_back({ s, s, levelStart, texture.data.size() - levelStart });
		}
		return texture;
	}

}
