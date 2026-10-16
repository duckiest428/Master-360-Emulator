/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_DASHBOARD_GAME_LIBRARY_H_
#define XENIA_APP_DASHBOARD_GAME_LIBRARY_H_

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace xe {
namespace app {
namespace dashboard {

enum class GameSource : uint8_t {
  kDiscImage,     // .iso (XGD)
  kExtractedXex,  // default.xex inside an extracted disc folder
  kXex,           // any other loose .xex
  kGamesOnDemand, // STFS/SVOD package, content type 0x7000 or 0x4000
  kArcade,        // STFS package, content type 0xD0000
  kZarchive,      // .zar disc archive
};

const char* GameSourceName(GameSource source);

struct GameEntry {
  std::filesystem::path launch_path;
  GameSource source = GameSource::kXex;
  uint32_t title_id = 0;
  uint32_t media_id = 0;
  uint32_t version = 0;
  uint8_t disc_number = 0;
  uint8_t disc_count = 0;
  std::string title_name;
  uint32_t achievement_count = 0;
  uint32_t total_gamerscore = 0;
  uint64_t file_size = 0;
  // PNG from the title's SPA resource (or the package title thumbnail).
  std::vector<uint8_t> icon_image;
  // Optional box art supplied by the user (covers/<TITLEID>.png|jpg or a
  // cover image next to the game). Real game files contain no box art.
  std::vector<uint8_t> cover_image;

  std::string DisplayName() const;
  std::string TitleIdString() const;
};

using GameList = std::vector<std::shared_ptr<const GameEntry>>;

// Recursively scans one directory for launchable titles on a background
// thread. Results are cached by path, size and modification time so a rescan
// only decodes new or changed executables.
class GameLibrary {
 public:
  explicit GameLibrary(std::filesystem::path cache_dir);
  ~GameLibrary();

  void StartScan(const std::filesystem::path& root);
  void CancelScan();

  bool IsScanning() const { return scanning_.load(); }
  // Incremented every time the published game list changes.
  uint32_t generation() const { return generation_.load(); }
  size_t files_examined() const { return files_examined_.load(); }

  GameList GetGames() const;
  const std::filesystem::path& root() const { return root_; }

 private:
  struct CacheRecord;

  void ScanThread(std::filesystem::path root);
  void Publish(GameList games);

  std::shared_ptr<GameEntry> ProbeFile(const std::filesystem::path& path,
                                       bool is_default_xex);
  std::shared_ptr<GameEntry> ProbeDiscImage(const std::filesystem::path& path,
                                            bool zarchive);
  std::shared_ptr<GameEntry> ProbeXex(const std::filesystem::path& path,
                                      bool is_default_xex);
  std::shared_ptr<GameEntry> ProbePackage(const std::filesystem::path& path);
  void AttachCover(const std::filesystem::path& root, GameEntry* entry);

  void LoadCache();
  void SaveCache(const GameList& games);

  std::filesystem::path cache_dir_;
  std::filesystem::path root_;

  mutable std::mutex mutex_;
  GameList games_;
  std::vector<std::unique_ptr<CacheRecord>> cache_;

  std::thread thread_;
  std::atomic<bool> scanning_ = false;
  std::atomic<bool> cancel_ = false;
  std::atomic<uint32_t> generation_ = 0;
  std::atomic<size_t> files_examined_ = 0;
};

}  // namespace dashboard
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_DASHBOARD_GAME_LIBRARY_H_
