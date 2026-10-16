/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/dashboard/xex_metadata.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/lzx.h"
#include "xenia/kernel/util/xex2_info.h"
#include "xenia/kernel/xam/xdbf/spa_info.h"

// Defined in xenia/cpu/xex_module.cc (CBC decrypt with a zero IV).
void aes_decrypt_buffer(const uint8_t* session_key, const uint8_t* input_buffer,
                        const size_t input_size, uint8_t* output_buffer,
                        const size_t output_size);

namespace xe {
namespace app {
namespace dashboard {

namespace {

constexpr uint32_t kXex2Magic = 0x58455832;  // 'XEX2'
constexpr uint32_t kXdbfMagic = 0x58444246;  // 'XDBF'

constexpr uint8_t kXex2RetailKey[16] = {0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28,
                                        0xFD, 0xC3, 0x40, 0x58, 0x3F, 0xBB,
                                        0x08, 0x96, 0xBF, 0x91};
constexpr uint8_t kXex2DevkitKey[16] = {};

// Returns a pointer to the optional header data for `key`, or nullptr. For
// keys whose low byte is 0x00 or 0x01 the value is stored inline.
const uint8_t* FindOptHeader(std::span<const uint8_t> data,
                             const xex2_header* header, uint32_t key,
                             uint32_t* inline_value = nullptr) {
  const size_t headers_end =
      offsetof(xex2_header, headers) +
      size_t(header->header_count) * sizeof(xex2_opt_header);
  if (headers_end > data.size()) {
    return nullptr;
  }
  for (uint32_t i = 0; i < header->header_count; i++) {
    const xex2_opt_header& opt = header->headers[i];
    if (opt.key != key) {
      continue;
    }
    switch (key & 0xFF) {
      case 0x00:
      case 0x01:
        if (inline_value) {
          *inline_value = opt.value;
        }
        return reinterpret_cast<const uint8_t*>(&opt.value);
      default:
        if (opt.offset >= data.size()) {
          return nullptr;
        }
        return data.data() + opt.offset;
    }
  }
  return nullptr;
}

std::vector<uint8_t> Decrypt(const uint8_t* session_key, const uint8_t* src,
                             size_t length) {
  // aes_decrypt_buffer works on whole 16 byte blocks and must not alias.
  const size_t padded = xe::round_up(length, size_t(16));
  std::vector<uint8_t> input(padded, 0);
  std::memcpy(input.data(), src, length);
  std::vector<uint8_t> output(padded, 0);
  aes_decrypt_buffer(session_key, input.data(), padded, output.data(), padded);
  output.resize(length);
  return output;
}

bool DecodeImage(std::span<const uint8_t> data, const xex2_header* header,
                 const xex2_security_info* security,
                 const xex2_opt_file_format_info* format,
                 const uint8_t* xex_key, std::vector<uint8_t>* out_image) {
  const uint32_t header_size = header->header_size;
  if (header_size >= data.size()) {
    return false;
  }
  const uint8_t* payload = data.data() + header_size;
  const size_t payload_size = data.size() - header_size;
  const uint32_t image_size = security->image_size;
  if (!image_size || image_size > 512 * 1024 * 1024) {
    return false;
  }

  uint8_t session_key[16];
  aes_decrypt_buffer(xex_key,
                     reinterpret_cast<const uint8_t*>(security->aes_key), 16,
                     session_key, 16);

  std::vector<uint8_t> plain;
  const uint8_t* source = payload;
  if (format->encryption_type == XEX_ENCRYPTION_NORMAL) {
    plain = Decrypt(session_key, payload, payload_size);
    source = plain.data();
  } else if (format->encryption_type != XEX_ENCRYPTION_NONE) {
    return false;
  }

  std::vector<uint8_t>& image = *out_image;
  image.assign(image_size, 0);

  switch (format->compression_type) {
    case XEX_COMPRESSION_NONE: {
      std::memcpy(image.data(), source, std::min<size_t>(payload_size, image_size));
    } break;
    case XEX_COMPRESSION_BASIC: {
      const uint32_t block_count = (format->info_size - 8) / 8;
      const auto* blocks = format->compression_info.basic.blocks;
      size_t src_offset = 0;
      size_t dst_offset = 0;
      for (uint32_t i = 0; i < block_count; i++) {
        const uint32_t data_size = blocks[i].data_size;
        const uint32_t zero_size = blocks[i].zero_size;
        if (src_offset + data_size > payload_size) {
          return false;
        }
        if (dst_offset + data_size > image.size()) {
          image.resize(dst_offset + data_size + zero_size, 0);
        }
        std::memcpy(image.data() + dst_offset, source + src_offset, data_size);
        src_offset += data_size;
        dst_offset += size_t(data_size) + zero_size;
      }
    } break;
    case XEX_COMPRESSION_NORMAL: {
      // De-block: every block starts with the size and SHA-1 of the next one,
      // followed by length-prefixed LZX chunks.
      std::vector<uint8_t> compressed;
      compressed.reserve(payload_size);
      size_t offset = 0;
      uint32_t block_size =
          format->compression_info.normal.first_block.block_size;
      while (block_size) {
        if (offset + block_size > payload_size || block_size < 24) {
          return false;
        }
        const uint8_t* block = source + offset;
        const uint32_t next_block_size = xe::load_and_swap<uint32_t>(block);
        size_t p = 24;
        while (p + 2 <= block_size) {
          const size_t chunk_size = (size_t(block[p]) << 8) | block[p + 1];
          p += 2;
          if (!chunk_size) {
            break;
          }
          if (p + chunk_size > block_size) {
            return false;
          }
          compressed.insert(compressed.end(), block + p, block + p + chunk_size);
          p += chunk_size;
        }
        offset += block_size;
        block_size = next_block_size;
      }
      if (lzx_decompress(compressed.data(), compressed.size(), image.data(),
                         image.size(),
                         format->compression_info.normal.window_size, nullptr,
                         0)) {
        return false;
      }
    } break;
    default:
      return false;
  }

  // A correctly decrypted image is a PE file.
  return image.size() > 2 && image[0] == 'M' && image[1] == 'Z';
}

void ReadSpaResource(std::span<const uint8_t> data, const xex2_header* header,
                     const xex2_security_info* security,
                     std::span<const uint8_t> image, XexMetadata* out) {
  const auto* resource_info = reinterpret_cast<const xex2_opt_resource_info*>(
      FindOptHeader(data, header, XEX_HEADER_RESOURCE_INFO));
  if (!resource_info) {
    return;
  }
  const uint32_t count = (resource_info->size - 4) / sizeof(xex2_resource);
  const std::string wanted = fmt::format("{:08X}", out->title_id);
  const uint32_t load_address = security->load_address;

  const xex2_resource* best = nullptr;
  for (uint32_t i = 0; i < count; i++) {
    const xex2_resource& res = resource_info->resources[i];
    if (res.address < load_address) {
      continue;
    }
    const size_t offset = res.address - load_address;
    if (offset + 4 > image.size() ||
        xe::load_and_swap<uint32_t>(image.data() + offset) != kXdbfMagic) {
      continue;
    }
    const std::string name(res.name, strnlen(res.name, sizeof(res.name)));
    if (!best || name == wanted) {
      best = &res;
    }
  }
  if (!best) {
    return;
  }

  const size_t offset = best->address - load_address;
  const size_t size = std::min<size_t>(best->size, image.size() - offset);
  std::vector<uint8_t> spa_data(image.begin() + offset,
                                image.begin() + offset + size);
  kernel::xam::SpaInfo spa(std::span<uint8_t>(spa_data.data(), spa_data.size()));
  spa.Load();

  out->title_name = spa.title_name();
  const auto icon = spa.title_icon();
  out->icon_png.assign(icon.begin(), icon.end());
  out->achievement_count = spa.achievement_count();
  out->total_gamerscore = spa.total_gamerscore();
}

}  // namespace

bool ReadXexMetadata(std::span<const uint8_t> data, XexMetadata* out) {
  *out = {};
  if (data.size() < sizeof(xex2_header)) {
    return false;
  }
  const auto* header = reinterpret_cast<const xex2_header*>(data.data());
  if (header->magic != kXex2Magic) {
    return false;
  }
  if (header->security_offset + sizeof(xex2_security_info) > data.size()) {
    return false;
  }

  const auto* exec_info = reinterpret_cast<const xex2_opt_execution_info*>(
      FindOptHeader(data, header, XEX_HEADER_EXECUTION_INFO));
  if (exec_info) {
    out->title_id = exec_info->title_id;
    out->media_id = exec_info->media_id;
    out->version = exec_info->version_value;
    out->base_version = exec_info->base_version_value;
    out->disc_number = exec_info->disc_number;
    out->disc_count = exec_info->disc_count;
  }
  out->valid = true;

  const auto* security = reinterpret_cast<const xex2_security_info*>(
      data.data() + header->security_offset);
  const auto* format = reinterpret_cast<const xex2_opt_file_format_info*>(
      FindOptHeader(data, header, XEX_HEADER_FILE_FORMAT_INFO));
  if (!format) {
    return true;
  }

  std::vector<uint8_t> image;
  if (!DecodeImage(data, header, security, format, kXex2RetailKey, &image) &&
      !DecodeImage(data, header, security, format, kXex2DevkitKey, &image)) {
    XELOGW("Dashboard: unable to decode XEX image for title {:08X}",
           out->title_id);
    return true;
  }

  ReadSpaResource(data, header, security, image, out);
  return true;
}

}  // namespace dashboard
}  // namespace app
}  // namespace xe
