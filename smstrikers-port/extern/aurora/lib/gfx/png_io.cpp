#include "png_io.hpp"

#include "../io.hpp"
#include "png.h"

#include <cstring>
#include <vector>

static aurora::Module Log("aurora::gfx::png");

namespace aurora::gfx::png {

struct PngStructs {
  png_structp pStruct = nullptr;
  png_infop pInfo = nullptr;

  ~PngStructs() {
    png_destroy_read_struct(&pStruct, &pInfo, nullptr);
  }
};

struct MemoryCursor {
  ArrayRef<uint8_t> bytes;
  size_t pos = 0;
};

static void readPngData(png_structp png, png_bytep data, const size_t length) {
  auto* cursor = static_cast<MemoryCursor*>(png_get_io_ptr(png));
  if (length > cursor->bytes.size() - cursor->pos) {
    png_error(png, "unexpected end of data");
  }
  std::memcpy(data, cursor->bytes.data() + cursor->pos, length);
  cursor->pos += length;
}

std::optional<ConvertedTexture> parse_png_bytes(ArrayRef<uint8_t> bytes) noexcept {
  PngStructs structs{};
  MemoryCursor cursor{bytes};

  structs.pStruct = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!structs.pStruct) {
    Log.error("png_create_read_struct failed");
    return std::nullopt;
  }

  structs.pInfo = png_create_info_struct(structs.pStruct);
  if (!structs.pInfo) {
    Log.error("png_create_info_struct failed");
    return std::nullopt;
  }

  // I'm scared of putting any locals after that setjmp.
  std::vector<png_bytep> rowPointers;
  ByteBuffer imageData{};
  png_uint_32 width, height;
  int bit_depth, color_type, interlace_type, compression_type, filter_type;
  size_t rowBytes;
  int i;

  if (setjmp(png_jmpbuf(structs.pStruct))) {
    Log.error("libpng encountered an error");
    return std::nullopt;
  }

  png_set_read_fn(structs.pStruct, &cursor, readPngData);
  png_read_info(structs.pStruct, structs.pInfo);

  if (!png_get_IHDR(structs.pStruct, structs.pInfo, &width, &height, &bit_depth, &color_type, &interlace_type, &compression_type, &filter_type)) {
    Log.error("libpng unable to read IHDR");
    return std::nullopt;
  }

  // Always read as RGBA8.
  png_set_gray_to_rgb(structs.pStruct);
  png_set_filler(structs.pStruct, 0xFF, PNG_FILLER_AFTER);
  png_set_expand(structs.pStruct);
  png_set_strip_16(structs.pStruct);

  png_read_update_info(structs.pStruct, structs.pInfo);
  rowBytes = png_get_rowbytes(structs.pStruct, structs.pInfo);
  rowPointers.resize(height);

  imageData.append_zeroes(rowBytes * height);
  // smstrikers-port: the allocation can fail on the Switch, where a large texture pack fills the heap.
  if (imageData.data() == nullptr) {
    Log.error("out of memory for a {}x{} PNG", width, height);
    return std::nullopt;
  }

  for (i = 0; i < height; i++) {
    rowPointers[i] = imageData.data() + i * rowBytes;
  }

  png_read_image(structs.pStruct, rowPointers.data());
  png_read_end(structs.pStruct, nullptr);

  return ConvertedTexture{
    .format = wgpu::TextureFormat::RGBA8Unorm,
    .width = width,
    .height = height,
    .mips = 1,
    .data = std::move(imageData)
  };
}

static void writePngData(png_structp png, png_bytep data, const size_t length) {
  auto* out = static_cast<std::vector<uint8_t>*>(png_get_io_ptr(png));
  out->insert(out->end(), data, data + length);
}

static void flushPngData(png_structp) {}

bool write_rgba8_png(const std::filesystem::path& path, uint32_t width, uint32_t height,
                     ArrayRef<uint8_t> pixels) noexcept {
  if (width == 0 || height == 0 || pixels.size() != static_cast<size_t>(width) * height * 4) {
    return false;
  }
  png_structp pStruct = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop pInfo = pStruct != nullptr ? png_create_info_struct(pStruct) : nullptr;
  if (pInfo == nullptr) {
    png_destroy_write_struct(&pStruct, nullptr);
    return false;
  }

  std::vector<uint8_t> encoded;
  std::vector<png_bytep> rows(height);
  for (uint32_t y = 0; y < height; ++y) {
    rows[y] = const_cast<png_bytep>(pixels.data()) + static_cast<size_t>(y) * width * 4;
  }
  if (setjmp(png_jmpbuf(pStruct))) {
    png_destroy_write_struct(&pStruct, &pInfo);
    return false;
  }
  png_set_write_fn(pStruct, &encoded, writePngData, flushPngData);
  png_set_IHDR(pStruct, pInfo, width, height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_write_info(pStruct, pInfo);
  png_write_image(pStruct, rows.data());
  png_write_end(pStruct, nullptr);
  png_destroy_write_struct(&pStruct, &pInfo);
  return io::write_file(path, {encoded.data(), encoded.size()});
}

std::optional<ConvertedTexture>
load_png_file(const std::filesystem::path& path) noexcept {
  const auto bytes = io::read_file(path);
  if (!bytes.has_value()) {
    Log.error("failed to open file: {}", io::fs_path_to_string(path));
    return std::nullopt;
  }
  return parse_png_bytes(*bytes);
}
}
