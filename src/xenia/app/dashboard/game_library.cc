/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/dashboard/game_library.h"

#include <algorithm>
#include <fstream>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wabsolute-value"
#endif
#include "third_party/tomlplusplus/toml.hpp"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/app/dashboard/xex_metadata.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/base/utf8.h"
#include "xenia/kernel/xam/xcontent/xcontent.h"
#include "xenia/vfs/devices/disc_image_device.h"
#include "xenia/vfs/devices/disc_zarchive_device.h"
#include "xenia/vfs/entry.h"
#include "xenia/vfs/file.h"

namespace xe {
namespace app {
namespace dashboard {

namespace {

constexpr size_t kMaxXexSize = 256 * 1024 * 1024;
constexpr size_t kMaxCoverSize = 16 * 1024 * 1024;
constexpr int kMaxScanDepth = 10;
constexpr std::string_view kCacheFileName = "library_cache.toml";

std::string Lower(const std::filesystem::path& path) {
  return xe::utf8::lower_ascii(xe::path_to_utf8(path));
}

bool ReadHostFile(const std::filesystem::path& path, size_t max_size,
                  std::vector<uint8_t>* out) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size == 0 || size > max_size) {
    return false;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  out->resize(size_t(size));
  file.read(reinterpret_cast<char*>(out->data()), std::streamsize(size));
  return bool(file);
}

int64_t WriteTime(const std::filesystem::path& path) {
  std::error_code ec;
  const auto time = std::filesystem::last_write_time(path, ec);
  return ec ? 0 : int64_t(time.time_since_epoch().count());
}

void ApplyXexMetadata(const XexMetadata& meta, GameEntry* entry) {
  entry->title_id = meta.title_id;
  entry->media_id = meta.media_id;
  entry->version = meta.version;
  entry->disc_number = meta.disc_number;
  entry->disc_count = meta.disc_count;
  entry->achievement_count = meta.achievement_count;
  entry->total_gamerscore = meta.total_gamerscore;
  if (!meta.title_name.empty()) {
    entry->title_name = meta.title_name;
  }
  entry->icon_image = meta.icon_png;
}

}  // namespace

const char* GameSourceName(GameSource source) {
  switch (source) {
    case GameSource::kDiscImage:
      return "Disc Image";
    case GameSource::kExtractedXex:
      return "Extracted Disc";
    case GameSource::kXex:
      return "Executable";
    case GameSource::kGamesOnDemand:
      return "Games on Demand";
    case GameSource::kArcade:
      return "Xbox Live Arcade";
    case GameSource::kZarchive:
      return "Zarchive";
  }
  return "Unknown";
}

std::string GameEntry::DisplayName() const {
  if (disc_count > 1 && disc_number > 0) {
    return fmt::format("{} (Disc {})", title_name, disc_number);
  }
  return title_name;
}

std::string GameEntry::TitleIdString() const {
  return fmt::format("{:08X}", title_id);
}

struct GameLibrary::CacheRecord {
  std::string key_path;
  uint64_t size = 0;
  int64_t write_time = 0;
  std::shared_ptr<GameEntry> entry;
};

GameLibrary::GameLibrary(std::filesystem::path cache_dir)
    : cache_dir_(std::move(cache_dir)) {}

GameLibrary::~GameLibrary() { CancelScan(); }

void GameLibrary::StartScan(const std::filesystem::path& root) {
  CancelScan();
  root_ = root;
  if (root.empty()) {
    Publish({});
    return;
  }
  cancel_ = false;
  scanning_ = true;
  thread_ = std::thread([this, root]() { ScanThread(root); });
}

void GameLibrary::CancelScan() {
  cancel_ = true;
  if (thread_.joinable()) {
    thread_.join();
  }
  scanning_ = false;
}

void GameLibrary::WaitForScan() {
  if (thread_.joinable()) {
    thread_.join();
  }
  scanning_ = false;
}

namespace {
std::string JsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  for (const char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (uint8_t(c) < 0x20) {
          out += fmt::format("\\u{:04x}", uint8_t(c));
        } else {
          out += c;
        }
    }
  }
  return out;
}
}  // namespace

bool GameLibrary::ExportJson(const std::filesystem::path& path) const {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::trunc);
  if (!file) {
    return false;
  }
  const GameList games = GetGames();
  file << "{\n  \"version\": 1,\n  \"games\": [";
  bool first = true;
  for (const auto& game : games) {
    const std::string key = xe::path_to_utf8(game->launch_path);
    std::string icon_path;
    if (!game->icon_image.empty()) {
      icon_path = xe::path_to_utf8(
          cache_dir_ / "icons" /
          fmt::format("{:016X}.png", std::hash<std::string>()(key)));
    }
    file << (first ? "\n" : ",\n");
    first = false;
    file << "    {"
         << "\"title\": \"" << JsonEscape(game->DisplayName()) << "\", "
         << "\"path\": \"" << JsonEscape(key) << "\", "
         << "\"titleId\": \"" << game->TitleIdString() << "\", "
         << "\"mediaId\": \"" << fmt::format("{:08X}", game->media_id)
         << "\", "
         << "\"source\": \"" << GameSourceName(game->source) << "\", "
         << "\"achievements\": " << game->achievement_count << ", "
         << "\"gamerscore\": " << game->total_gamerscore << ", "
         << "\"iconPath\": \"" << JsonEscape(icon_path) << "\", "
         << "\"coverPath\": \""
         << JsonEscape(xe::path_to_utf8(game->cover_path)) << "\"}";
  }
  file << "\n  ]\n}\n";
  return bool(file);
}

GameList GameLibrary::GetGames() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return games_;
}

void GameLibrary::Publish(GameList games) {
  std::ranges::sort(games, [](const auto& a, const auto& b) {
    return xe::utf8::lower_ascii(a->DisplayName()) <
           xe::utf8::lower_ascii(b->DisplayName());
  });
  {
    std::lock_guard<std::mutex> lock(mutex_);
    games_ = std::move(games);
  }
  generation_++;
}

void GameLibrary::ScanThread(std::filesystem::path root) {
  files_examined_ = 0;
  LoadCache();

  GameList found;
  size_t published_count = 0;

  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec)) {
    XELOGW("Dashboard: game library path {} is not a directory",
           xe::path_to_utf8(root));
    Publish({});
    scanning_ = false;
    return;
  }

  // A root that is itself an extracted disc.
  if (std::filesystem::exists(root / "default.xex", ec)) {
    if (auto entry = ProbeFile(root / "default.xex", true)) {
      found.push_back(entry);
    }
  }

  auto it = std::filesystem::recursive_directory_iterator(
      root, std::filesystem::directory_options::skip_permission_denied, ec);
  const auto end = std::filesystem::recursive_directory_iterator();
  while (!ec && it != end && !cancel_) {
    const std::filesystem::directory_entry& dir_entry = *it;
    const auto& path = dir_entry.path();
    const std::string name = Lower(path.filename());

    if (dir_entry.is_directory(ec)) {
      const bool skip = it.depth() >= kMaxScanDepth || name.ends_with(".data") ||
                        name.starts_with("$") || name == "covers";
      if (skip) {
        it.disable_recursion_pending();
      } else if (std::filesystem::exists(path / "default.xex", ec)) {
        // Extracted disc: the default.xex is the game, nested executables
        // belong to it.
        if (auto entry = ProbeFile(path / "default.xex", true)) {
          found.push_back(entry);
        }
        it.disable_recursion_pending();
      }
    } else if (dir_entry.is_regular_file(ec)) {
      files_examined_++;
      if (it.depth() > 0 || name != "default.xex") {
        if (auto entry = ProbeFile(path, false)) {
          found.push_back(entry);
        }
      }
    }

    // Publish progressively so the library fills in while scanning.
    if (found.size() >= published_count + 8) {
      published_count = found.size();
      Publish(found);
    }

    it.increment(ec);
    if (ec) {
      // Skip unreadable entries rather than aborting the whole scan.
      ec.clear();
      if (it == end) {
        break;
      }
    }
  }

  if (!cancel_) {
    SaveCache(found);
  }
  Publish(std::move(found));
  scanning_ = false;
  XELOGI("Dashboard: library scan of {} finished, {} titles",
         xe::path_to_utf8(root), GetGames().size());
}

std::shared_ptr<GameEntry> GameLibrary::ProbeFile(
    const std::filesystem::path& path, bool is_default_xex) {
  std::error_code ec;
  const uint64_t size = std::filesystem::file_size(path, ec);
  if (ec) {
    return nullptr;
  }
  const std::string ext = Lower(path.extension());
  const bool known_extension =
      ext == ".iso" || ext == ".xex" || ext == ".zar" || ext.empty();
  if (!known_extension) {
    return nullptr;
  }

  // Cache lookup.
  const std::string key = xe::path_to_utf8(path);
  const int64_t write_time = WriteTime(path);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& record : cache_) {
      if (record->key_path == key && record->size == size &&
          record->write_time == write_time) {
        return record->entry;
      }
    }
  }

  std::shared_ptr<GameEntry> entry;
  if (ext == ".iso") {
    entry = ProbeDiscImage(path, false);
  } else if (ext == ".zar") {
    entry = ProbeDiscImage(path, true);
  } else if (ext == ".xex") {
    entry = ProbeXex(path, is_default_xex || Lower(path.filename()) ==
                                                 "default.xex");
  } else if (size >= sizeof(kernel::xam::XContentHeader)) {
    entry = ProbePackage(path);
  }
  if (!entry) {
    return nullptr;
  }
  entry->file_size = size;
  AttachCover(root_, entry.get());

  auto record = std::make_unique<CacheRecord>();
  record->key_path = key;
  record->size = size;
  record->write_time = write_time;
  record->entry = entry;
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.push_back(std::move(record));
  return entry;
}

std::shared_ptr<GameEntry> GameLibrary::ProbeDiscImage(
    const std::filesystem::path& path, bool zarchive) {
  std::unique_ptr<vfs::Device> device;
  if (zarchive) {
    device = std::make_unique<vfs::DiscZarchiveDevice>(
        "\\Device\\DashboardProbe", path);
  } else {
    device = std::make_unique<vfs::DiscImageDevice>("\\Device\\DashboardProbe",
                                                    path);
  }
  if (!device->Initialize()) {
    return nullptr;
  }
  vfs::Entry* xex_entry = device->ResolvePath("default.xex");
  if (!xex_entry || !xex_entry->size() || xex_entry->size() > kMaxXexSize) {
    return nullptr;
  }
  vfs::File* file = nullptr;
  if (xex_entry->Open(xe::filesystem::FileAccess::kFileReadData, &file) !=
          X_STATUS_SUCCESS ||
      !file) {
    return nullptr;
  }
  std::vector<uint8_t> data(xex_entry->size());
  size_t bytes_read = 0;
  const X_STATUS read_status = file->ReadSync(data, 0, &bytes_read);
  file->Destroy();
  if (read_status != X_STATUS_SUCCESS || bytes_read != data.size()) {
    return nullptr;
  }

  XexMetadata meta;
  if (!ReadXexMetadata(data, &meta)) {
    return nullptr;
  }
  auto entry = std::make_shared<GameEntry>();
  entry->launch_path = path;
  entry->source = zarchive ? GameSource::kZarchive : GameSource::kDiscImage;
  entry->title_name = xe::path_to_utf8(path.stem());
  ApplyXexMetadata(meta, entry.get());
  return entry;
}

std::shared_ptr<GameEntry> GameLibrary::ProbeXex(
    const std::filesystem::path& path, bool is_default_xex) {
  std::vector<uint8_t> data;
  if (!ReadHostFile(path, kMaxXexSize, &data)) {
    return nullptr;
  }
  XexMetadata meta;
  if (!ReadXexMetadata(data, &meta)) {
    return nullptr;
  }
  auto entry = std::make_shared<GameEntry>();
  entry->launch_path = path;
  entry->source =
      is_default_xex ? GameSource::kExtractedXex : GameSource::kXex;
  entry->title_name =
      is_default_xex ? xe::path_to_utf8(path.parent_path().filename())
                     : xe::path_to_utf8(path.stem());
  ApplyXexMetadata(meta, entry.get());
  return entry;
}

std::shared_ptr<GameEntry> GameLibrary::ProbePackage(
    const std::filesystem::path& path) {
  using kernel::xam::XContentContainerHeader;
  constexpr size_t kHeaderReadSize =
      sizeof(kernel::xam::XContentHeader) +
      sizeof(kernel::xam::XContentMetadata);

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return nullptr;
  }
  auto header = std::make_unique<XContentContainerHeader>();
  file.read(reinterpret_cast<char*>(header.get()), kHeaderReadSize);
  if (!file || !header->content_header.is_magic_valid()) {
    return nullptr;
  }

  const auto& metadata = header->content_metadata;
  const XContentType content_type = metadata.content_type;
  GameSource source;
  switch (content_type) {
    case XContentType::kXbox360Title:
    case XContentType::kInstalledGame:
      source = GameSource::kGamesOnDemand;
      break;
    case XContentType::kArcadeTitle:
      source = GameSource::kArcade;
      break;
    default:
      // DLC, title updates, saves etc. are not launchable titles.
      return nullptr;
  }

  auto entry = std::make_shared<GameEntry>();
  entry->launch_path = path;
  entry->source = source;
  entry->title_id = metadata.execution_info.title_id;
  entry->media_id = metadata.execution_info.media_id;
  entry->version = metadata.execution_info.version_value;
  entry->disc_number = metadata.execution_info.disc_number;
  entry->disc_count = metadata.execution_info.disc_count;

  std::string name = xe::to_utf8(metadata.display_name(XLanguage::kEnglish));
  if (name.empty()) {
    name = xe::to_utf8(metadata.title_name());
  }
  entry->title_name = name.empty() ? xe::path_to_utf8(path.filename()) : name;

  const uint32_t title_thumb_size =
      std::min<uint32_t>(metadata.title_thumbnail_size,
                         uint32_t(sizeof(metadata.title_thumbnail)));
  const uint32_t thumb_size = std::min<uint32_t>(
      metadata.thumbnail_size, uint32_t(sizeof(metadata.thumbnail)));
  if (title_thumb_size) {
    entry->icon_image.assign(metadata.title_thumbnail,
                             metadata.title_thumbnail + title_thumb_size);
  } else if (thumb_size) {
    entry->icon_image.assign(metadata.thumbnail,
                             metadata.thumbnail + thumb_size);
  }
  return entry;
}

void GameLibrary::AttachCover(const std::filesystem::path& root,
                              GameEntry* entry) {
  if (!entry->cover_image.empty()) {
    return;
  }
  std::vector<std::filesystem::path> candidates;
  const std::string id = entry->TitleIdString();
  for (const char* ext : {".png", ".jpg", ".jpeg"}) {
    candidates.push_back(root / "covers" / (id + ext));
    if (entry->source == GameSource::kExtractedXex) {
      candidates.push_back(entry->launch_path.parent_path() /
                           (std::string("cover") + ext));
    } else {
      auto beside = entry->launch_path;
      candidates.push_back(beside.replace_extension(ext));
    }
  }
  std::error_code ec;
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate, ec) &&
        ReadHostFile(candidate, kMaxCoverSize, &entry->cover_image)) {
      entry->cover_path = candidate;
      return;
    }
  }
}

void GameLibrary::LoadCache() {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.clear();

  const auto cache_path = cache_dir_ / kCacheFileName;
  std::ifstream file(cache_path);
  if (!file) {
    return;
  }
  toml::parse_result parsed;
  try {
    parsed = toml::parse(file);
  } catch (const toml::parse_error& e) {
    XELOGW("Dashboard: ignoring corrupt library cache: {}", e.what());
    return;
  }
  const toml::array* games = parsed["game"].as_array();
  if (!games) {
    return;
  }
  for (const toml::node& node : *games) {
    const toml::table* t = node.as_table();
    if (!t) {
      continue;
    }
    auto record = std::make_unique<CacheRecord>();
    record->key_path = (*t)["path"].value_or(std::string());
    record->size = uint64_t((*t)["size"].value_or(int64_t(0)));
    record->write_time = (*t)["write_time"].value_or(int64_t(0));
    if (record->key_path.empty()) {
      continue;
    }
    auto entry = std::make_shared<GameEntry>();
    entry->launch_path = xe::to_path(record->key_path);
    entry->source =
        GameSource(uint8_t((*t)["source"].value_or(int64_t(0))));
    entry->title_id = uint32_t((*t)["title_id"].value_or(int64_t(0)));
    entry->media_id = uint32_t((*t)["media_id"].value_or(int64_t(0)));
    entry->version = uint32_t((*t)["version"].value_or(int64_t(0)));
    entry->disc_number = uint8_t((*t)["disc_number"].value_or(int64_t(0)));
    entry->disc_count = uint8_t((*t)["disc_count"].value_or(int64_t(0)));
    entry->title_name = (*t)["title_name"].value_or(std::string());
    entry->achievement_count =
        uint32_t((*t)["achievements"].value_or(int64_t(0)));
    entry->total_gamerscore =
        uint32_t((*t)["gamerscore"].value_or(int64_t(0)));
    entry->file_size = record->size;
    const std::string icon = (*t)["icon"].value_or(std::string());
    if (!icon.empty()) {
      ReadHostFile(cache_dir_ / "icons" / icon, kMaxCoverSize,
                   &entry->icon_image);
    }
    // Entries are recreated on every scan, so covers added since the last
    // scan are picked up without invalidating the cache.
    AttachCover(root_, entry.get());
    record->entry = std::move(entry);
    cache_.push_back(std::move(record));
  }
}

void GameLibrary::SaveCache(const GameList& games) {
  std::error_code ec;
  std::filesystem::create_directories(cache_dir_ / "icons", ec);

  toml::array array;
  for (const auto& game : games) {
    const std::string key = xe::path_to_utf8(game->launch_path);
    std::string icon_name;
    if (!game->icon_image.empty()) {
      icon_name = fmt::format("{:016X}.png", std::hash<std::string>()(key));
      std::ofstream icon_file(cache_dir_ / "icons" / icon_name,
                              std::ios::binary | std::ios::trunc);
      icon_file.write(reinterpret_cast<const char*>(game->icon_image.data()),
                      std::streamsize(game->icon_image.size()));
    }
    toml::table t;
    t.insert("path", key);
    t.insert("size", int64_t(game->file_size));
    t.insert("write_time", WriteTime(game->launch_path));
    t.insert("source", int64_t(game->source));
    t.insert("title_id", int64_t(game->title_id));
    t.insert("media_id", int64_t(game->media_id));
    t.insert("version", int64_t(game->version));
    t.insert("disc_number", int64_t(game->disc_number));
    t.insert("disc_count", int64_t(game->disc_count));
    t.insert("title_name", game->title_name);
    t.insert("achievements", int64_t(game->achievement_count));
    t.insert("gamerscore", int64_t(game->total_gamerscore));
    t.insert("icon", icon_name);
    array.push_back(std::move(t));
  }
  toml::table root;
  root.insert("game", std::move(array));
  std::ofstream file(cache_dir_ / kCacheFileName, std::ios::trunc);
  file << root;
}

}  // namespace dashboard
}  // namespace app
}  // namespace xe
