#include "../src/sf4e/sf4e__PortraitArchive.hxx"
#include "../src/sf4e/sf4e__LobbyCatalog.hxx"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <zlib.h>

namespace {
namespace P = sf4e::PortraitArchive;
using Data = std::vector<uint8_t>;

void Require(bool ok, const char* why) {
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}

void U32(Data& bytes, std::size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[offset + i] = uint8_t(value >> (8 * i));
}

Data Dds(uint32_t width = 8, uint32_t height = 4, uint32_t fourCC = 0x35545844) {
    const uint32_t count = ((width + 3) / 4) * ((height + 3) / 4) * (fourCC == 0x31545844 ? 8 : 16);
    Data bytes(128 + count, 0);
    std::copy_n("DDS ", 4, bytes.begin());
    U32(bytes, 4, 124); U32(bytes, 8, 0x81007); U32(bytes, 12, height); U32(bytes, 16, width);
    U32(bytes, 20, count); U32(bytes, 76, 32); U32(bytes, 80, 4); U32(bytes, 84, fourCC); U32(bytes, 108, 0x1000);
    for (uint32_t i = 0; i < count; ++i) bytes[128 + i] = uint8_t(i + 1);
    return bytes;
}

Data Emb() {
    Data first = Dds(), second = Dds(4, 4, 0x31545844);
    const std::size_t secondOffset = 64 + first.size(), namesOffset = secondOffset + second.size();
    Data bytes(namesOffset + 16, 0);
    std::copy_n("#EMB", 4, bytes.begin()); bytes[4] = 0xfe; bytes[5] = 0xff; bytes[6] = 32;
    U32(bytes, 12, 2); U32(bytes, 24, 32); U32(bytes, 28, 48);
    U32(bytes, 32, 64 - 32); U32(bytes, 36, uint32_t(first.size()));
    U32(bytes, 40, uint32_t(secondOffset - 40)); U32(bytes, 44, uint32_t(second.size()));
    U32(bytes, 48, uint32_t(namesOffset)); U32(bytes, 52, uint32_t(namesOffset + 8));
    std::copy(first.begin(), first.end(), bytes.begin() + 64);
    std::copy(second.begin(), second.end(), bytes.begin() + secondOffset);
    std::copy_n("RYU.dds", 8, bytes.begin() + namesOffset);
    std::copy_n("JHN.dds", 8, bytes.begin() + namesOffset + 8);
    return bytes;
}

Data Emz(const Data& source) {
    Data bytes(16 + compressBound(static_cast<uLong>(source.size())), 0);
    std::copy_n("#EMZ", 4, bytes.begin());
    U32(bytes, 4, uint32_t(crc32(crc32(0L, Z_NULL, 0), source.data(), static_cast<uInt>(source.size()))));
    U32(bytes, 8, uint32_t(source.size())); U32(bytes, 12, 16);
    z_stream stream = {};
    stream.next_in = const_cast<Bytef*>(source.data()); stream.avail_in = static_cast<uInt>(source.size());
    stream.next_out = bytes.data() + 16; stream.avail_out = static_cast<uInt>(bytes.size() - 16);
    Require(deflateInit2(&stream, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK, "fixture deflate starts");
    Require(deflate(&stream, Z_FINISH) == Z_STREAM_END, "fixture raw DEFLATE completes");
    bytes.resize(16 + stream.total_out);
    deflateEnd(&stream);
    return bytes;
}

void Reject(const Data& bytes, const char* why, std::size_t index = 0) {
    P::Texture texture; texture.width = 100; texture.pixels.push_back(1);
    std::string error;
    Require(!P::Decode(bytes, index, texture, error), why);
    Require(texture.width == 0 && texture.pixels.empty() && !error.empty(), "failed decode clears old image and explains failure");
}

Data Read(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary | std::ios::ate);
    Require(bool(input), path.c_str());
    const std::streamoff length = input.tellg();
    Require(length > 0 && uint64_t(length) <= P::MaxArchiveBytes, "installed archive fits file limit");
    Data bytes(static_cast<std::size_t>(length)); input.seekg(0);
    Require(bool(input.read(reinterpret_cast<char*>(bytes.data()), length)), "installed archive read completes");
    return bytes;
}

void Installed(const std::string& game) {
    std::string error;
    Data icons;
    if (!P::Unpack(Read(game + "/dlc/04_ae2/ui/chara_select/chara_select_dlc4.emz"), icons, error)) Require(false, error.c_str());
    for (int id = 0; id < sf4e::LobbyCatalog::CharacterCount; ++id) {
        const std::string code = sf4e::LobbyCatalog::Find(id)->code;
        const char* layer = id >= 39 ? "dlc/04_ae2" : id >= 35 ? "dlc/03_character_free" : "resource";
        const std::string path = game + "/" + layer + "/ui/chara_select/chara/sel_" + code + ".tex.emz";
        P::Texture bust, icon;
        if (!P::Decode(Read(path), 0, bust, error)) Require(false, error.c_str());
        Require(bust.width == 1024 && bust.height == 1024 && bust.fourCC == 0x35545844
            && bust.pixels.size() == 1048576, "installed bust has expected BC3 dimensions");
        if (!P::DecodeNamed(icons, (code == "JHA" ? "JHN" : code) + ".dds", icon, error)) Require(false, error.c_str());
        Require(icon.width == 64 && icon.height == 64 && icon.fourCC == 0x35545844
            && icon.pixels.size() == 4096, "installed icon has expected BC3 dimensions");
    }
    std::cout << "Installed assets passed: all 44 original busts and 44 named roster icons.\n";
}
}

int main(int argc, char** argv) {
    P::Texture image; std::string error;
    Require(P::Decode(Dds(), 0, image, error), "raw DDS decodes");
    Require(image.width == 8 && image.height == 4 && image.rowBytes == 32 && image.rowCount == 1
        && image.pixels.front() == 1 && image.pixels.back() == 32, "DDS block geometry and bytes preserved");
    Require(P::Decode(Dds(7, 5, 0x33545844), 0, image, error)
        && image.rowBytes == 32 && image.rowCount == 2, "partial DXT blocks round up correctly");
    Require(P::Decode(Emb(), 1, image, error) && image.width == 4 && image.fourCC == 0x31545844,
        "EMB offset is relative to the selected entry, not the table");
    const Data packed = Emz(Emb());
    Require(P::Decode(packed, 0, image, error) && image.pixels.size() == 32, "raw-DEFLATE EMZ unwraps to EMB");
    Data unpacked;
    Require(P::Unpack(packed, unpacked, error) && unpacked == Emb(), "shared EMB decompresses exactly once");
    Require(P::DecodeNamed(unpacked, "JHN.dds", image, error) && image.fourCC == 0x31545844,
        "absolute name pointers select the correct named entry");
    unpacked[64] = '!';
    Require(P::DecodeNamed(unpacked, "JHN.dds", image, error), "unrelated non-DDS entries can be skipped");
    Require(!P::DecodeNamed(unpacked, "MISSING.dds", image, error) && image.pixels.empty(), "missing name has no image");
    unpacked = Emb(); U32(unpacked, 48, 0xffffffff);
    Require(!P::DecodeNamed(unpacked, "JHN.dds", image, error), "invalid filename pointer rejected");
    unpacked = Emb(); U32(unpacked, 48, uint32_t(unpacked.size() - 1)); unpacked.back() = '!';
    Require(!P::DecodeNamed(unpacked, "RYU.dds", image, error), "unterminated filename rejected");
    unpacked = Emb(); U32(unpacked, 28, 0xfffffff0);
    Require(!P::DecodeNamed(unpacked, "RYU.dds", image, error), "out-of-range filename table rejected");

    Reject(Data(), "empty archive rejected");
    Reject(Data(15, 0), "short archive rejected");
    Reject(Dds(), "standalone DDS has no second entry", 1);
    Reject(Emb(), "EMB index is bounded", 2);
    Data damaged = Dds(); damaged.pop_back(); Reject(damaged, "truncated DDS pixels rejected");
    damaged = Dds(); U32(damaged, 4, 123); Reject(damaged, "wrong DDS header size rejected");
    damaged = Dds(); U32(damaged, 76, 31); Reject(damaged, "wrong DDS format size rejected");
    damaged = Dds(); U32(damaged, 16, 0); Reject(damaged, "zero DDS width rejected");
    damaged = Dds(); U32(damaged, 16, 8192); Reject(damaged, "oversized DDS width rejected");
    damaged = Dds(); U32(damaged, 84, 0x30315844); Reject(damaged, "unsupported DX10 DDS rejected");
    damaged = Dds(); U32(damaged, 112, 0x200); Reject(damaged, "cube map rejected");
    damaged = Emb(); U32(damaged, 12, 257); Reject(damaged, "unbounded entry count rejected");
    damaged = Emb(); U32(damaged, 24, 0xfffffff0); Reject(damaged, "out-of-range entry table rejected");
    damaged = Emb(); U32(damaged, 32, 0xffffffff); Reject(damaged, "overflowing relative offset rejected");
    damaged = Emb(); U32(damaged, 32, 0); Reject(damaged, "payload/table overlap rejected");
    damaged = Emb(); U32(damaged, 36, 0xffffffff); Reject(damaged, "unbounded entry size rejected");
    damaged = packed; U32(damaged, 8, uint32_t(P::MaxInflatedBytes + 1)); Reject(damaged, "expansion limit enforced before allocation");
    damaged = packed; U32(damaged, 8, uint32_t(Emb().size() - 1)); Reject(damaged, "understated expansion rejected");
    damaged = packed; U32(damaged, 8, uint32_t(Emb().size() + 1)); Reject(damaged, "overstated expansion rejected");
    damaged = packed; U32(damaged, 12, 15); Reject(damaged, "compressed data cannot overlap header");
    damaged = packed; damaged[4] ^= 1; Reject(damaged, "incorrect expanded checksum rejected");
    damaged = packed; damaged.pop_back(); Reject(damaged, "truncated raw DEFLATE rejected");
    damaged = packed; damaged.push_back(0); Reject(damaged, "trailing compressed bytes rejected");
    Require(!P::Unpack(Dds(), unpacked, error) && unpacked.empty(), "shared icon unpack requires an EMB");
    std::cout << "Portrait archive tests passed: EMZ, EMB, DDS, named selection, bounds and malformed data.\n";
    if (argc == 2) Installed(argv[1]);
    else Require(argc == 1, "usage: PortraitArchiveTest [GAME_DIRECTORY]");
    return 0;
}
