#include "sf4e__LobbyPortraits.hxx"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <d3d9.h>
#include <wincodec.h>

namespace {
	const UINT kColumns = 8;
	const UINT kRows = 6;
	const int kPortraitCount = 44;
	const UINT kMaxDimension = 8192;

	IDirect3DTexture9* g_texture = nullptr;
	IDirect3DDevice9* g_device = nullptr; // Kept alive by g_texture.
	UINT g_width = 0;
	UINT g_height = 0;

	template <typename T>
	class ComPointer {
	public:
		ComPointer() : value_(nullptr) {}
		~ComPointer() { if (value_) value_->Release(); }
		ComPointer(const ComPointer&) = delete;
		ComPointer& operator=(const ComPointer&) = delete;
		T* operator->() const { return value_; }
		T* Get() const { return value_; }
		T** Address() { return &value_; }
		T* Detach() { T* value = value_; value_ = nullptr; return value; }
	private:
		T* value_;
	};

	class ComApartment {
	public:
		ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
		~ComApartment() { if (SUCCEEDED(result_)) CoUninitialize(); }
		ComApartment(const ComApartment&) = delete;
		ComApartment& operator=(const ComApartment&) = delete;
		bool Ready() const {
			// The game's render thread may already have a different apartment.
			// WIC can use that apartment; we do not own its initialization.
			return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
		}
	private:
		HRESULT result_;
	};

	bool AtlasPath(std::wstring& path) {
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&sf4e::LobbyPortraits::Load), &module)) {
			return false;
		}
		std::vector<wchar_t> filename(32768);
		DWORD length = GetModuleFileNameW(module, filename.data(), static_cast<DWORD>(filename.size()));
		if (length == 0 || length >= filename.size()) return false;
		path.assign(filename.data(), length);
		std::wstring::size_type slash = path.find_last_of(L"\\/");
		if (slash == std::wstring::npos) return false;
		path.resize(slash + 1);
		path += L"assets\\lobby\\portraits.png";
		return true;
	}

	bool LoadAtlas(IDirect3DDevice9* device) {
		std::wstring path;
		if (!AtlasPath(path)) return false;

		ComApartment apartment;
		if (!apartment.Ready()) return false;
		ComPointer<IWICImagingFactory> factory;
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
			IID_IWICImagingFactory, reinterpret_cast<void**>(factory.Address())))) return false;

		ComPointer<IWICBitmapDecoder> decoder;
		if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
			WICDecodeMetadataCacheOnDemand, decoder.Address()))) return false;
		GUID container = {};
		if (FAILED(decoder->GetContainerFormat(&container)) ||
			!IsEqualGUID(container, GUID_ContainerFormatPng)) return false;

		ComPointer<IWICBitmapFrameDecode> frame;
		if (FAILED(decoder->GetFrame(0, frame.Address()))) return false;
		UINT width = 0, height = 0;
		if (FAILED(frame->GetSize(&width, &height)) ||
			width < kColumns * 2 || height < kRows * 2 ||
			width > kMaxDimension || height > kMaxDimension ||
			width % kColumns != 0 || height % kRows != 0) return false;

		D3DCAPS9 caps = {};
		if (FAILED(device->GetDeviceCaps(&caps)) ||
			width > caps.MaxTextureWidth || height > caps.MaxTextureHeight) return false;

		// D3DFMT_A8R8G8B8 is stored as BGRA bytes on Windows. WIC converts any
		// supported PNG pixel format without relying on a third-party decoder.
		ComPointer<IWICFormatConverter> converter;
		if (FAILED(factory->CreateFormatConverter(converter.Address())) ||
			FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
				WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;

		ComPointer<IDirect3DTexture9> texture;
		if (FAILED(device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8,
			D3DPOOL_MANAGED, texture.Address(), nullptr))) return false;

		D3DLOCKED_RECT locked = {};
		if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) return false;
		bool copied = false;
		if (locked.pBits && locked.Pitch > 0 && static_cast<UINT>(locked.Pitch) >= width * 4) {
			UINT stride = static_cast<UINT>(locked.Pitch);
			std::uint64_t bytes = static_cast<std::uint64_t>(stride) * height;
			if (bytes <= (std::numeric_limits<UINT>::max)()) {
				copied = SUCCEEDED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(bytes),
					static_cast<BYTE*>(locked.pBits)));
			}
		}
		HRESULT unlocked = texture->UnlockRect(0);
		if (!copied || FAILED(unlocked)) return false;

		g_texture = texture.Detach();
		g_device = device;
		g_width = width;
		g_height = height;
		return true;
	}
}

bool sf4e::LobbyPortraits::Load(IDirect3DDevice9* device) {
	if (!device) return false;
	if (g_texture && g_device == device) return true;
	Release();
	try {
		return LoadAtlas(device);
	} catch (...) {
		// Optional presentation assets must never stop the game or lobby.
		return false;
	}
}

void sf4e::LobbyPortraits::Release() {
	if (g_texture) g_texture->Release();
	g_texture = nullptr;
	g_device = nullptr;
	g_width = 0;
	g_height = 0;
}

bool sf4e::LobbyPortraits::Draw(ImDrawList* drawList, int id, ImVec2 min, ImVec2 max, ImU32 tint) {
	if (!drawList || !g_texture || id < 0 || id >= kPortraitCount ||
		max.x <= min.x || max.y <= min.y) return false;
	// The generated sheet places Abel before Viper/Rufus/El Fuerte. Engine
	// IDs 12..15 instead put Abel last; preserve the original image pixels.
	const int slot = id >= 12 && id <= 14 ? id + 1 : id == 15 ? 12 : id;
	UINT col = static_cast<UINT>(slot) % kColumns;
	UINT row = static_cast<UINT>(slot) / kColumns;
	float cellWidth = static_cast<float>(g_width / kColumns);
	float cellHeight = static_cast<float>(g_height / kRows);
	// Sample inside the cell's first/last texel centers to avoid bleeding a
	// neighboring portrait when ImGui's bilinear sampler scales the atlas.
	ImVec2 uvMin((col * cellWidth + 0.5f) / g_width, (row * cellHeight + 0.5f) / g_height);
	ImVec2 uvMax(((col + 1) * cellWidth - 0.5f) / g_width, ((row + 1) * cellHeight - 0.5f) / g_height);
	drawList->AddImage((ImTextureID)(std::intptr_t)g_texture, min, max, uvMin, uvMax, tint);
	return true;
}
