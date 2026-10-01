#include "sf4e__LobbyPortraits.hxx"
#include "sf4e__LobbyCatalog.hxx"
#include "sf4e__PortraitArchive.hxx"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <d3d9.h>
#include <spdlog/spdlog.h>

namespace {
    struct Portrait {
        IDirect3DTexture9* texture = nullptr;
        UINT width = 0, height = 0;
    } g_portraits[sf4e::LobbyCatalog::CharacterCount], g_icons[sf4e::LobbyCatalog::CharacterCount];
    IDirect3DDevice9* g_device = nullptr; // Loaded textures retain the device.
    std::wstring g_root;
    int g_loaded = 0, g_iconCount = 0;
    const wchar_t* kLayers[] = {L"patch_ae2_tu3", L"patch_ae2_tu2", L"patch_ae2_tu1b",
        L"patch_ae2_tu1", L"patch_ae2", L"dlc\\04_ae2", L"dlc\\03_character_free",
        L"dlc\\03_character", L"patch", L"resource"};

    bool GameRoot(const wchar_t* directory, std::wstring& root) {
        std::vector<wchar_t> path(32768);
        if (directory && *directory) {
            DWORD length = GetFullPathNameW(directory, static_cast<DWORD>(path.size()), path.data(), nullptr);
            if (!length || length >= path.size()) return false;
            root.assign(path.data(), length);
        } else {
            // The host is SSFIV.exe when injected, regardless of the DLL's
            // launcher folder or the process's current working directory.
            DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (!length || length >= path.size()) return false;
            root.assign(path.data(), length);
            auto slash = root.find_last_of(L"\\/");
            if (slash == std::wstring::npos) return false;
            root.resize(slash);
        }
        if (root.empty()) return false;
        if (root.back() != L'\\' && root.back() != L'/') root += L'\\';
        return true;
    }

    bool ReadFileBytes(const std::wstring& path, std::vector<std::uint8_t>& bytes) {
        struct File {
            HANDLE handle;
            ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
        } file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (file.handle == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER length;
        if (!GetFileSizeEx(file.handle, &length) || length.QuadPart <= 0 ||
            length.QuadPart > static_cast<LONGLONG>(sf4e::PortraitArchive::MaxArchiveBytes)) return false;
        bytes.resize(static_cast<size_t>(length.QuadPart));
        DWORD read = 0;
        return ReadFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)
            && read == bytes.size();
    }

    bool ReadGameAsset(const std::wstring& root, const std::wstring& relative,
        std::vector<std::uint8_t>& bytes) {
        // Prefer installed title updates, then official DLC/base resources.
        for (const auto* layer : kLayers) {
            std::wstring path = root + layer + L"\\" + relative;
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            // A present override owns this asset; don't ignore a broken mod
            // by silently substituting a different edition's texture.
            return ReadFileBytes(path, bytes);
        }
        return false;
    }

    bool ReadPortrait(const std::wstring& root, const char* code, sf4e::PortraitArchive::Texture& data) {
        std::wstring name;
        for (const char* p = code; *p; ++p) name += static_cast<wchar_t>(*p);
        std::vector<std::uint8_t> bytes;
        if (!ReadGameAsset(root, L"ui\\chara_select\\chara\\sel_" + name + L".tex.emz", bytes)) return false;
        std::string error;
        if (sf4e::PortraitArchive::Decode(bytes, 0, data, error)) return true;
        spdlog::warn("Lobby: cannot decode official portrait {}: {}", code, error);
        return false;
    }

    bool Upload(IDirect3DDevice9* device, const sf4e::PortraitArchive::Texture& data, Portrait& result) {
        D3DCAPS9 caps = {};
        if (FAILED(device->GetDeviceCaps(&caps)) ||
            data.width > caps.MaxTextureWidth || data.height > caps.MaxTextureHeight) return false;
        IDirect3DTexture9* texture = nullptr;
        if (FAILED(device->CreateTexture(data.width, data.height, 1, 0,
            static_cast<D3DFORMAT>(data.fourCC), D3DPOOL_MANAGED, &texture, nullptr))) return false;
        D3DLOCKED_RECT locked = {};
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) { texture->Release(); return false; }
        bool copied = locked.pBits && locked.Pitch > 0 && static_cast<UINT>(locked.Pitch) >= data.rowBytes;
        if (copied) {
            auto* destination = static_cast<std::uint8_t*>(locked.pBits);
            for (UINT row = 0; row < data.rowCount; ++row)
                std::memcpy(destination + static_cast<size_t>(row) * locked.Pitch,
                    data.pixels.data() + static_cast<size_t>(row) * data.rowBytes, data.rowBytes);
        }
        HRESULT unlocked = texture->UnlockRect(0);
        if (!copied || FAILED(unlocked)) { texture->Release(); return false; }
        result.texture = texture;
        result.width = data.width;
        result.height = data.height;
        return true;
    }

    void LoadIcons(IDirect3DDevice9* device, const std::wstring& root) {
        std::vector<std::uint8_t> bytes, archive;
        std::string error;
        if (!ReadGameAsset(root, L"ui\\chara_select\\chara_select_dlc4.emz", bytes) ||
            !sf4e::PortraitArchive::Unpack(bytes, archive, error)) return;
        // Inflate this shared archive once. Entry names avoid depending on
        // incidental archive ordering or the game's localized display names.
        for (int id = 0; id < sf4e::LobbyCatalog::CharacterCount; ++id) {
            const char* code = sf4e::LobbyCatalog::Find(id)->code;
            std::string entry = std::string(std::strcmp(code, "JHA") == 0 ? "JHN" : code) + ".dds";
            sf4e::PortraitArchive::Texture data;
            if (sf4e::PortraitArchive::DecodeNamed(archive, entry, data, error) && Upload(device, data, g_icons[id]))
                ++g_iconCount;
            else spdlog::warn("Lobby: official roster icon unavailable for {}: {}", code, error);
        }
    }
}

bool sf4e::LobbyPortraits::Load(IDirect3DDevice9* device, const wchar_t* gameDirectory) {
    if (!device) return false;
    try {
        std::wstring root;
        if (!GameRoot(gameDirectory, root)) return false;
        if (g_device == device && g_root == root)
            return g_loaded == LobbyCatalog::CharacterCount && g_iconCount == LobbyCatalog::CharacterCount;
        Release();
        g_root = root;
        for (int id = 0; id < LobbyCatalog::CharacterCount; ++id) {
            PortraitArchive::Texture data;
            if (ReadPortrait(root, LobbyCatalog::Find(id)->code, data) && Upload(device, data, g_portraits[id]))
                ++g_loaded;
            else spdlog::warn("Lobby: official portrait unavailable for {}", LobbyCatalog::Find(id)->code);
        }
        LoadIcons(device, root);
        if (g_loaded || g_iconCount) g_device = device;
        spdlog::info("Lobby: loaded {}/44 official portraits and {}/44 roster icons from the installed game", g_loaded, g_iconCount);
        return g_loaded == LobbyCatalog::CharacterCount && g_iconCount == LobbyCatalog::CharacterCount;
    } catch (...) {
        // Presentation assets cannot prevent opening the lobby.
        Release();
        return false;
    }
}

int sf4e::LobbyPortraits::LoadedCount() { return g_loaded; }
int sf4e::LobbyPortraits::IconCount() { return g_iconCount; }

void sf4e::LobbyPortraits::Release() {
    for (auto* group : {g_portraits, g_icons}) for (int i = 0; i < LobbyCatalog::CharacterCount; ++i) {
        if (group[i].texture) group[i].texture->Release();
        group[i] = Portrait();
    }
    g_device = nullptr;
    g_root.clear();
    g_loaded = g_iconCount = 0;
}

bool sf4e::LobbyPortraits::Draw(ImDrawList* drawList, int id, ImVec2 min, ImVec2 max, bool rosterIcon, ImU32 tint) {
    if (!drawList || id < 0 || id >= LobbyCatalog::CharacterCount ||
        max.x <= min.x || max.y <= min.y) return false;
    const auto& preferred = rosterIcon ? g_icons[id] : g_portraits[id];
    const auto& alternate = rosterIcon ? g_portraits[id] : g_icons[id];
    const auto& portrait = preferred.texture ? preferred : alternate;
    if (!portrait.texture) return false;
    ImVec2 uvMin(0.5f / portrait.width, 0.5f / portrait.height);
    ImVec2 uvMax(1.f - uvMin.x, 1.f - uvMin.y);
    drawList->AddImage((ImTextureID)(std::intptr_t)portrait.texture, min, max, uvMin, uvMax, tint);
    return true;
}
