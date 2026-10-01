#include "sf4e__PortraitArchive.hxx"

#include <cstring>
#include <new>
#include <utility>
#include <zlib.h>

namespace sf4e { namespace PortraitArchive {
namespace {
struct Bytes {
    const uint8_t* data;
    std::size_t size;

    bool Has(std::size_t offset, std::size_t count) const {
        return offset <= size && count <= size - offset;
    }
    bool Magic(const char* expected) const {
        return Has(0, 4) && std::memcmp(data, expected, 4) == 0;
    }
    uint16_t U16(std::size_t offset) const {
        return uint16_t(data[offset]) | (uint16_t(data[offset + 1]) << 8);
    }
    uint32_t U32(std::size_t offset) const {
        return uint32_t(data[offset]) | (uint32_t(data[offset + 1]) << 8)
            | (uint32_t(data[offset + 2]) << 16) | (uint32_t(data[offset + 3]) << 24);
    }
};

bool Fail(std::string& error, const char* reason) {
    error = reason;
    return false;
}

bool InflateEmz(Bytes source, std::vector<uint8_t>& inflated, std::string& error) {
    if (!source.Has(0, 16)) return Fail(error, "Invalid EMZ header");
    const uint32_t expected = source.U32(8);
    const uint32_t offset = source.U32(12);
    if (expected == 0 || expected > MaxInflatedBytes)
        return Fail(error, "EMZ expanded size exceeds the portrait limit");
    if (offset < 16 || !source.Has(offset, 1))
        return Fail(error, "EMZ compressed data offset is outside the file");

    inflated.resize(expected);
    z_stream stream = {};
    stream.next_in = const_cast<Bytef*>(source.data + offset);
    stream.avail_in = static_cast<uInt>(source.size - offset);
    stream.next_out = inflated.data();
    stream.avail_out = expected;
    // USFIV EMZ stores raw DEFLATE. Its own header carries the expanded CRC32;
    // there is no zlib wrapper around the stream.
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return Fail(error, "Could not initialize EMZ decompression");
    const int status = inflate(&stream, Z_FINISH);
    const bool complete = status == Z_STREAM_END && stream.total_out == expected
        && stream.total_in == source.size - offset;
    inflateEnd(&stream);
    if (!complete) return Fail(error, "EMZ stream is truncated, oversized, or has trailing data");
    const uLong checksum = crc32(crc32(0L, Z_NULL, 0), inflated.data(), expected);
    if (checksum != source.U32(4)) return Fail(error, "EMZ expanded data checksum does not match");
    return true;
}

bool ReadDds(Bytes source, Texture& out, std::string& error) {
    if (!source.Magic("DDS ") || !source.Has(0, 128)
        || source.U32(4) != 124 || source.U32(76) != 32)
        return Fail(error, "Invalid DDS header");
    const uint32_t requiredFlags = 0x1007; // caps, width, height, pixel format.
    if ((source.U32(8) & requiredFlags) != requiredFlags || (source.U32(80) & 4) == 0)
        return Fail(error, "DDS does not describe a block-compressed texture");
    const uint32_t width = source.U32(16), height = source.U32(12);
    if (width == 0 || height == 0 || width > MaxTextureDimension || height > MaxTextureDimension)
        return Fail(error, "DDS dimensions exceed the portrait limit");
    if (source.U32(24) > 1 || (source.U32(112) & 0x20fe00) != 0)
        return Fail(error, "DDS cube maps and volume textures are not portrait images");
    const uint32_t fourCC = source.U32(84);
    uint32_t blockBytes = 0;
    if (fourCC == 0x31545844) blockBytes = 8; // DXT1 / BC1.
    else if (fourCC == 0x33545844 || fourCC == 0x35545844) blockBytes = 16; // BC2/BC3.
    else return Fail(error, "DDS portrait must use DXT1, DXT3, or DXT5");
    const uint32_t rowBytes = ((width + 3) / 4) * blockBytes;
    const uint32_t rowCount = (height + 3) / 4;
    const std::size_t byteCount = std::size_t(rowBytes) * rowCount;
    if (!source.Has(128, byteCount)) return Fail(error, "DDS top mip is truncated");

    Texture result;
    result.width = width;
    result.height = height;
    result.fourCC = fourCC;
    result.rowBytes = rowBytes;
    result.rowCount = rowCount;
    result.pixels.assign(source.data + 128, source.data + 128 + byteCount);
    out = std::move(result);
    return true;
}

bool EmbTable(Bytes source, uint32_t& count, uint32_t& table, std::string& error) {
    if (!source.Magic("#EMB") || !source.Has(0, 32) || source.U16(4) != 0xfffe)
        return Fail(error, "Invalid EMB header or byte order");
    const uint16_t headerSize = source.U16(6);
    count = source.U32(12);
    table = source.U32(24);
    if (headerSize < 32 || !source.Has(0, headerSize) || count == 0 || count > 256
        || table < headerSize || !source.Has(table, std::size_t(count) * 8))
        return Fail(error, "EMB entry table is outside the archive");
    return true;
}

bool ReadArchive(Bytes source, std::size_t entryIndex, Texture& out, std::string& error) {
    if (source.Magic("DDS ")) {
        if (entryIndex != 0) return Fail(error, "A standalone DDS has only one entry");
        return ReadDds(source, out, error);
    }
    uint32_t count = 0, table = 0;
    if (!EmbTable(source, count, table, error)) return false;
    if (entryIndex >= count) return Fail(error, "EMB portrait entry does not exist");
    const std::size_t entry = table + entryIndex * 8;
    const uint32_t relative = source.U32(entry), length = source.U32(entry + 4);
    if (!source.Has(entry, relative)) return Fail(error, "EMB relative offset is outside the archive");
    const std::size_t offset = entry + relative;
    if (offset < table + std::size_t(count) * 8 || !source.Has(offset, length))
        return Fail(error, "EMB portrait data overlaps the table or exceeds the archive");
    return ReadDds(Bytes{ source.data + offset, length }, out, error);
}
}

bool Decode(const std::vector<uint8_t>& bytes, std::size_t entryIndex, Texture& out, std::string& error) {
    out = Texture();
    error.clear();
    if (bytes.empty() || bytes.size() > MaxArchiveBytes)
        return Fail(error, "Portrait archive is empty or exceeds the file size limit");
    try {
        Bytes source = { bytes.data(), bytes.size() };
        std::vector<uint8_t> inflated;
        if (source.Magic("#EMZ")) {
            if (!InflateEmz(source, inflated, error)) return false;
            source = Bytes{ inflated.data(), inflated.size() };
        }
        return ReadArchive(source, entryIndex, out, error);
    } catch (const std::bad_alloc&) {
        return Fail(error, "Not enough memory to load the portrait archive");
    }
}

bool Unpack(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& archive, std::string& error) {
    error.clear();
    if (bytes.empty() || bytes.size() > MaxArchiveBytes) {
        archive.clear();
        return Fail(error, "Portrait archive is empty or exceeds the file size limit");
    }
    try {
        const Bytes source = { bytes.data(), bytes.size() };
        std::vector<uint8_t> result;
        if (source.Magic("#EMZ")) {
            if (!InflateEmz(source, result, error)) { archive.clear(); return false; }
        } else {
            result = bytes;
        }
        uint32_t count = 0, table = 0;
        if (!EmbTable(Bytes{ result.data(), result.size() }, count, table, error)) {
            archive.clear();
            return false;
        }
        archive.swap(result);
        return true;
    } catch (const std::bad_alloc&) {
        archive.clear();
        return Fail(error, "Not enough memory to unpack the portrait archive");
    }
}

bool DecodeNamed(const std::vector<uint8_t>& archive, const std::string& name,
    Texture& out, std::string& error) {
    out = Texture();
    error.clear();
    if (archive.empty() || archive.size() > MaxInflatedBytes || name.empty() || name.size() > 255)
        return Fail(error, "Invalid named portrait request");
    try {
        const Bytes source = { archive.data(), archive.size() };
        uint32_t count = 0, table = 0;
        if (!EmbTable(source, count, table, error)) return false;
        const uint32_t names = source.U32(28);
        if (names < source.U16(6) || !source.Has(names, std::size_t(count) * 4))
            return Fail(error, "EMB filename table is outside the archive");
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t offset = source.U32(names + std::size_t(i) * 4);
            if (offset < source.U16(6) || !source.Has(offset, 1))
                return Fail(error, "EMB filename offset is outside the archive");
            std::size_t length = 0;
            while (length < 256 && source.Has(offset, length + 1) && source.data[offset + length] != 0) ++length;
            if (length == 256 || !source.Has(offset, length + 1))
                return Fail(error, "EMB filename is unterminated or too long");
            if (length == name.size() && std::memcmp(source.data + offset, name.data(), length) == 0)
                return ReadArchive(source, i, out, error);
        }
        return Fail(error, "Named portrait does not exist in the EMB archive");
    } catch (const std::bad_alloc&) {
        return Fail(error, "Not enough memory to decode the named portrait");
    }
}
} }
