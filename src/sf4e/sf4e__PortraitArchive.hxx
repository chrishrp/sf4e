#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sf4e { namespace PortraitArchive {

// Presentation assets are optional. Bound both disk reads and decompression
// before allocating; stock character-select archives contain one or two 1 MiB
// textures. The generous limits allow ordinary replacements without trusting
// arbitrary sizes from a modified file.
static const std::size_t MaxArchiveBytes = 16 * 1024 * 1024;
static const std::size_t MaxInflatedBytes = 32 * 1024 * 1024;
static const uint32_t MaxTextureDimension = 4096;

struct Texture {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fourCC = 0;   // DXT1, DXT3 or DXT5; directly usable as D3DFORMAT.
    uint32_t rowBytes = 0;
    uint32_t rowCount = 0; // Block rows, not pixel rows.
    std::vector<uint8_t> pixels; // Only the top mip's compressed blocks.
};

// Decode an installed sel_<CODE>.tex.emz in memory. Also accepts the raw EMB
// container and a standalone DDS (entryIndex must then be zero). EMB offsets
// are relative to each entry, not to the table or archive. Names are not needed
// for the stock selection: entry 0 is sel_chara.dds; optional entry 1 is reversed.
// No Windows, game, graphics, or filesystem dependency. On failure out is empty
// and error describes the reason; malformed assets must not affect gameplay.
bool Decode(const std::vector<uint8_t>& bytes, std::size_t entryIndex,
    Texture& out, std::string& error);

// Inflate a shared icon container once, then select many named images without
// repeatedly decompressing it. Unpack accepts raw EMB or EMZ-wrapped EMB;
// DecodeNamed accepts only an unpacked EMB, and compares exact entry names.
bool Unpack(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& archive, std::string& error);
bool DecodeNamed(const std::vector<uint8_t>& archive, const std::string& name,
    Texture& out, std::string& error);

} }
