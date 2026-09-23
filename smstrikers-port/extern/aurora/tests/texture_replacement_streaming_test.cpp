#include "gfx/dds_io.hpp"
#include "internal.hpp"
#include "gfx/png_io.hpp"
#include "gfx/texture_replacement.hpp"

#include <aurora/texture.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace aurora::gfx::texture_replacement {
namespace {
using namespace std::chrono_literals;

constexpr const char* VirtualPathA = "tex1_4x4_0000000000000001_6.dds";
constexpr const char* VirtualPathB = "tex1_4x4_0000000000000002_6.dds";
constexpr const char* VirtualPngPath = "tex1_4x4_0000000000000001_6.png";

struct BlockingSource {
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false;
  bool release = false;
  std::atomic<uint32_t> reads = 0;

  static bool read(void* userData, const char*, std::vector<uint8_t>&) {
    auto& source = *static_cast<BlockingSource*>(userData);
    ++source.reads;
    std::unique_lock lock{source.mutex};
    source.entered = true;
    source.cv.notify_all();
    source.cv.wait(lock, [&] { return source.release; });
    return false;
  }
};

struct RecordingSource {
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::string> paths;

  static bool read(void* userData, const char* path, std::vector<uint8_t>&) {
    auto& source = *static_cast<RecordingSource*>(userData);
    {
      std::lock_guard lock{source.mutex};
      source.paths.emplace_back(path);
    }
    source.cv.notify_all();
    return false;
  }
};

class ReplacementStreamingTest : public ::testing::Test {
protected:
  void SetUp() override {
    shutdown();
    testing::set_worker_count(1);
  }

  void TearDown() override { shutdown(); }
};

TEST_F(ReplacementStreamingTest, UnregisterWaitsForInFlightVirtualRead) {
  BlockingSource source;
  const auto registration =
      texture::register_virtual_replacement(VirtualPathA, {.read = &BlockingSource::read, .userData = &source});
  ASSERT_NE(registration.id, 0u);
  {
    std::unique_lock lock{source.mutex};
    ASSERT_TRUE(source.cv.wait_for(lock, 2s, [&] { return source.entered; }));
  }

  auto unregister = std::async(std::launch::async, [&] { texture::unregister_replacement(registration); });
  EXPECT_EQ(unregister.wait_for(50ms), std::future_status::timeout);
  {
    std::lock_guard lock{source.mutex};
    source.release = true;
  }
  source.cv.notify_all();

  EXPECT_EQ(unregister.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(source.reads.load(), 1u);
}

TEST_F(ReplacementStreamingTest, CancelledQueuedVirtualJobNeverReads) {
  RecordingSource source;
  testing::set_workers_paused(true);
  const auto registration =
      texture::register_virtual_replacement(VirtualPathA, {.read = &RecordingSource::read, .userData = &source});
  ASSERT_NE(registration.id, 0u);

  texture::unregister_replacement(registration);
  testing::set_workers_paused(false);
  shutdown();

  EXPECT_TRUE(source.paths.empty());
}

TEST_F(ReplacementStreamingTest, HighPriorityFullLoadRunsBeforeThumbnailQueue) {
  RecordingSource source;
  testing::set_workers_paused(true);
  const auto low =
      texture::register_virtual_replacement(VirtualPathA, {.read = &RecordingSource::read, .userData = &source});
  const auto high =
      texture::register_virtual_replacement(VirtualPathB, {.read = &RecordingSource::read, .userData = &source});
  ASSERT_NE(low.id, 0u);
  ASSERT_NE(high.id, 0u);

  GXTexObj_ obj{};
  const auto key = std::get<texture::TextureSourceKey>(high.key);
  ASSERT_TRUE(find_source_replacement(obj, key).has_value());
  testing::set_workers_paused(false);
  {
    std::unique_lock lock{source.mutex};
    ASSERT_TRUE(source.cv.wait_for(lock, 2s, [&] { return !source.paths.empty(); }));
    EXPECT_EQ(source.paths.front(), VirtualPathB);
  }
  const std::array registrations{low, high};
  texture::unregister_replacements(registrations);
}

TEST_F(ReplacementStreamingTest, StaleFailureDoesNotPoisonNewRegistration) {
  RecordingSource source;
  testing::set_workers_paused(true);
  const auto oldRegistration =
      texture::register_virtual_replacement(VirtualPngPath, {.read = &RecordingSource::read, .userData = &source});
  ASSERT_NE(oldRegistration.id, 0u);
  GXTexObj_ obj{};
  const auto key = std::get<texture::TextureSourceKey>(oldRegistration.key);
  ASSERT_TRUE(find_source_replacement(obj, key).has_value());
  testing::set_workers_paused(false);
  ASSERT_TRUE(testing::wait_for_completions(oldRegistration.id, 1, 2000));
  texture::unregister_replacement(oldRegistration);

  testing::set_workers_paused(true);
  const auto newRegistration =
      texture::register_virtual_replacement(VirtualPngPath, {.read = &RecordingSource::read, .userData = &source});
  process_streaming();

  ASSERT_TRUE(find_source_replacement(obj, key).has_value());
  testing::set_workers_paused(false);
  EXPECT_TRUE(testing::wait_for_completions(newRegistration.id, 1, 2000));
  texture::unregister_replacement(newRegistration);
}

std::vector<uint8_t> encode_png(uint32_t width, uint32_t height) {
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0x80);
  const auto path =
      std::filesystem::temp_directory_path() /
      ("aurora-png-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
  EXPECT_TRUE(png::write_rgba8_png(path, width, height, {pixels.data(), pixels.size()}));
  std::vector<uint8_t> bytes;
  {
    std::ifstream file(path, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  }
  std::error_code removeError;
  std::filesystem::remove(path, removeError);
  return bytes;
}

struct PngSource {
  std::vector<uint8_t> bytes;
  std::atomic<uint32_t> reads = 0;

  // Only the base level exists; the loader also asks for `_mip1` sidecars.
  static bool read(void* userData, const char* path, std::vector<uint8_t>& outBytes) {
    auto& source = *static_cast<PngSource*>(userData);
    if (std::string_view{path} != VirtualPngPath) {
      return false;
    }
    ++source.reads;
    outBytes = source.bytes;
    return true;
  }
};

class ReplacementWaitTest : public ReplacementStreamingTest {
protected:
  void TearDown() override {
    texture::set_replacement_wait(false);
    texture::set_replacement_strict_budget(false);
    texture::set_replacement_cache_budget(4294967296);
    ReplacementStreamingTest::TearDown();
  }
};

TEST_F(ReplacementWaitTest, StrictBudgetLeavesOutPackBiggerThanBudget) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("aurora-packs-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto write = [&](const char* pack, const char* name, uint32_t size) {
    std::filesystem::create_directories(root / pack);
    const auto bytes = encode_png(size, size);
    std::ofstream file(root / pack / name, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  };
  write("Small", "tex1_4x4_0000000000000001_6.png", 4);
  write("Big", "tex1_4x4_0000000000000002_6.png", 16);

  // The loader keeps dumps out of the scan, so it needs a cache path.
  const auto cachePath = root.string();
  const char* savedCachePath = g_config.cachePath;
  g_config.cachePath = cachePath.c_str();
  texture::set_replacement_strict_budget(true);
  texture::set_replacement_cache_budget(512);
  const auto group = texture::load_replacement_directory(root);
  const auto whole = texture::load_replacement_directory(root, {.onePack = true});
  g_config.cachePath = savedCachePath;
  std::error_code removeError;
  std::filesystem::remove_all(root, removeError);

  EXPECT_TRUE(whole.registrations.empty());
  texture::unregister_replacements(whole);
  ASSERT_EQ(group.registrations.size(), 1u);
  EXPECT_EQ(std::get<texture::TextureSourceKey>(group.registrations.front().key).textureHash, 1u);
  texture::unregister_replacements(group);
}

TEST_F(ReplacementWaitTest, FirstDrawTakesOverQueuedLoad) {
  RecordingSource source;
  testing::set_workers_paused(true);
  const auto registration =
      texture::register_virtual_replacement(VirtualPngPath, {.read = &RecordingSource::read, .userData = &source});
  ASSERT_NE(registration.id, 0u);
  GXTexObj_ obj{};
  const auto key = std::get<texture::TextureSourceKey>(registration.key);
  ASSERT_TRUE(find_source_replacement(obj, key).has_value());

  texture::set_replacement_wait(true);
  const auto result = find_source_replacement(obj, key);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->id, registration.id);
  {
    std::lock_guard lock{source.mutex};
    EXPECT_EQ(source.paths.size(), 1u);
  }

  testing::set_workers_paused(false);
  EXPECT_FALSE(testing::wait_for_completions(registration.id, 1, 100));
  std::lock_guard lock{source.mutex};
  EXPECT_EQ(source.paths.size(), 1u);
}

TEST_F(ReplacementWaitTest, FirstDrawWaitsForRunningLoad) {
  BlockingSource source;
  const auto registration =
      texture::register_virtual_replacement(VirtualPngPath, {.read = &BlockingSource::read, .userData = &source});
  ASSERT_NE(registration.id, 0u);
  GXTexObj_ obj{};
  const auto key = std::get<texture::TextureSourceKey>(registration.key);
  ASSERT_TRUE(find_source_replacement(obj, key).has_value());
  {
    std::unique_lock lock{source.mutex};
    ASSERT_TRUE(source.cv.wait_for(lock, 2s, [&] { return source.entered; }));
  }

  texture::set_replacement_wait(true);
  auto draw = std::async(std::launch::async, [&] { return find_source_replacement(obj, key); });
  EXPECT_EQ(draw.wait_for(50ms), std::future_status::timeout);
  {
    std::lock_guard lock{source.mutex};
    source.release = true;
  }
  source.cv.notify_all();

  ASSERT_EQ(draw.wait_for(2s), std::future_status::ready);
  EXPECT_TRUE(draw.get().has_value());
  EXPECT_EQ(source.reads.load(), 1u);
}

TEST_F(ReplacementWaitTest, ReplacementThatCannotFitIsNotLoadedAgain) {
  PngSource source{.bytes = encode_png(4, 4)};
  texture::set_replacement_strict_budget(true);
  texture::set_replacement_cache_budget(1);
  const auto registration =
      texture::register_virtual_replacement(VirtualPngPath, {.read = &PngSource::read, .userData = &source});
  ASSERT_NE(registration.id, 0u);
  GXTexObj_ obj{};
  const auto key = std::get<texture::TextureSourceKey>(registration.key);
  ASSERT_TRUE(find_source_replacement(obj, key).has_value());
  ASSERT_TRUE(testing::wait_for_completions(registration.id, 1, 2000));
  process_streaming();

  const auto again = find_source_replacement(obj, key);
  ASSERT_TRUE(again.has_value());
  EXPECT_FALSE(again->handle);
  EXPECT_FALSE(testing::wait_for_completions(registration.id, 1, 100));
  EXPECT_EQ(source.reads.load(), 1u);
}

template <typename T>
void write_u32(std::vector<uint8_t>& bytes, size_t offset, T value) {
  const uint32_t word = static_cast<uint32_t>(value);
  std::memcpy(bytes.data() + offset, &word, sizeof(word));
}

TEST(DdsMipTailTest, SlicesTheSameTailAsFullDecode) {
  std::vector<uint8_t> base(8 * 8 * 4);
  auto encoded = dds::encode_rgba8_dds(8, 8, base);
  std::vector<uint8_t> bytes(encoded.data(), encoded.data() + encoded.size());
  bytes.resize(128 + (8 * 8 + 4 * 4 + 2 * 2 + 1) * 4);
  for (size_t i = 128; i < bytes.size(); ++i) {
    bytes[i] = static_cast<uint8_t>(i);
  }
  write_u32(bytes, 8, 0x1u | 0x2u | 0x4u | 0x8u | 0x1000u | 0x20000u);
  write_u32(bytes, 28, 4u);
  write_u32(bytes, 108, 0x1000u | 0x8u | 0x400000u);

  const auto full = dds::parse_dds_bytes({bytes.data(), bytes.size()});
  const auto tail = dds::parse_dds_mip_tail({bytes.data(), bytes.size()}, 2);
  const auto path =
      std::filesystem::temp_directory_path() /
      ("aurora-dds-tail-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".dds");
  {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const auto fileTail = dds::load_dds_mip_tail(path, 2);
  std::error_code removeError;
  std::filesystem::remove(path, removeError);
  ASSERT_TRUE(full.has_value());
  ASSERT_TRUE(tail.has_value());
  ASSERT_TRUE(fileTail.has_value());
  EXPECT_FALSE(tail->includesBase);
  EXPECT_EQ(tail->texture.width, 2u);
  EXPECT_EQ(tail->texture.height, 2u);
  EXPECT_EQ(tail->texture.mips, 2u);
  ASSERT_EQ(tail->texture.data.size(), 20u);
  EXPECT_TRUE(std::equal(tail->texture.data.data(), tail->texture.data.data() + tail->texture.data.size(),
                         full->data.data() + full->data.size() - 20));
  ASSERT_EQ(fileTail->texture.data.size(), tail->texture.data.size());
  EXPECT_TRUE(std::equal(fileTail->texture.data.data(), fileTail->texture.data.data() + fileTail->texture.data.size(),
                         tail->texture.data.data()));
}

TEST(DdsMipTailTest, RejectsMiplessLargeTextureButKeepsSmallWholeTexture) {
  std::vector<uint8_t> largePixels(128 * 128 * 4);
  const auto large = dds::encode_rgba8_dds(128, 128, largePixels);
  EXPECT_FALSE(dds::parse_dds_mip_tail(large, 64).has_value());

  std::vector<uint8_t> smallPixels(32 * 16 * 4);
  const auto small = dds::encode_rgba8_dds(32, 16, smallPixels);
  const auto tail = dds::parse_dds_mip_tail(small, 64);
  ASSERT_TRUE(tail.has_value());
  EXPECT_TRUE(tail->includesBase);
  ASSERT_EQ(tail->texture.data.size(), smallPixels.size());
  EXPECT_TRUE(
      std::equal(tail->texture.data.data(), tail->texture.data.data() + tail->texture.data.size(), smallPixels.data()));
}
} // namespace
} // namespace aurora::gfx::texture_replacement
