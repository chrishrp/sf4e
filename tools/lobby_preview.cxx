// Render the production lobby view without launching or attaching to USF4.
// Usage: LobbyPreview output.png [width height] [options]
// The hidden D3D9 window is never shown; only our own render target is saved.
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <limits>
#include <stdexcept>
#include <string>
#include <windows.h>
#include <d3d9.h>
#include <wincodec.h>
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

#include "../src/sf4e/sf4e__LobbyView.hxx"
#include "../src/sf4e/sf4e__LobbyPortraits.hxx"

namespace {
void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        char message[256];
        std::snprintf(message, sizeof(message), "%s failed (HRESULT 0x%08lX)", operation,
            static_cast<unsigned long>(result));
        throw std::runtime_error(message);
    }
}

template <typename T> class ComPointer {
public:
    ComPointer() : value_(nullptr) {}
    ~ComPointer() { if (value_) value_->Release(); }
    ComPointer(const ComPointer&) = delete;
    ComPointer& operator=(const ComPointer&) = delete;
    T* Get() const { return value_; }
    T* operator->() const { return value_; }
    T** Address() { return &value_; }
private:
    T* value_;
};

class ComApartment {
public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
        if (result_ != RPC_E_CHANGED_MODE) Check(result_, "COM initialization");
    }
    ~ComApartment() { if (SUCCEEDED(result_)) CoUninitialize(); }
private:
    HRESULT result_;
};

class HiddenWindow {
public:
    HiddenWindow() : handle_(nullptr), instance_(GetModuleHandleW(nullptr)), atom_(0) {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = L"SF4LobbyVisualCheck";
        atom_ = RegisterClassExW(&windowClass);
        if (!atom_) throw std::runtime_error("Cannot register the hidden preview window");
        handle_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass.lpszClassName,
            L"SF4 Lobby Visual Check", WS_OVERLAPPED, 0, 0, 32, 32, nullptr, nullptr, instance_, nullptr);
        if (!handle_) {
            UnregisterClassW(L"SF4LobbyVisualCheck", instance_);
            throw std::runtime_error("Cannot create the hidden preview window");
        }
    }
    ~HiddenWindow() {
        if (handle_) DestroyWindow(handle_);
        if (atom_) UnregisterClassW(L"SF4LobbyVisualCheck", instance_);
    }
    HWND Get() const { return handle_; }
private:
    HWND handle_;
    HINSTANCE instance_;
    ATOM atom_;
};

class GuiSession {
public:
    GuiSession() : win32_(false), dx9_(false) { IMGUI_CHECKVERSION(); ImGui::CreateContext(); }
    ~GuiSession() {
        sf4e::LobbyPortraits::Release();
        if (dx9_) ImGui_ImplDX9_Shutdown();
        if (win32_) ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    void Initialize(HWND window, IDirect3DDevice9* device) {
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        win32_ = ImGui_ImplWin32_Init(window);
        if (!win32_) throw std::runtime_error("ImGui Win32 initialization failed");
        dx9_ = ImGui_ImplDX9_Init(device);
        if (!dx9_) throw std::runtime_error("ImGui D3D9 initialization failed");
    }
private:
    bool win32_, dx9_;
};

struct Options {
    std::wstring output;
    int width = 1600, height = 1000;
    int p1 = 0, p2 = 2, ultra1 = 0, ultra2 = 1, stage = 2;
    int edition = 14;
    int focusRow = 0, focusIndex = -1;
    bool waiting = false, spectator = false, noPortraits = false, requirePortraits = false;
    bool click = false, expectHit = false;
    ImVec2 mouse = ImVec2(-1, -1);
    int expectedRow = -1, expectedIndex = -1;
};

int Integer(const wchar_t* value, int min, int max) {
    errno = 0;
    wchar_t* end = nullptr;
    long result = std::wcstol(value, &end, 10);
    if (errno || !value[0] || !end || *end || result < min || result > max)
        throw std::runtime_error("Invalid numeric argument; use --help for accepted ranges");
    return static_cast<int>(result);
}

void Usage() {
    std::puts("LobbyPreview output.png [width height] [options]\n"
        "  Dimensions: 320..8192 (default 1600 1000)\n"
        "  --p1 ID --p2 ID             Character IDs 0..43\n"
        "  --ultra1 N --ultra2 N       Ultra 0=I, 1=II, 2=Double\n"
        "  --stage ID                  Stage 0..29\n"
        "  --edition ID                13=SF4, 1=SSF4, 2=AE2011, 4=AE2012, 14=Ultra, 16=Omega\n"
        "  --waiting --spectator       Alternate lobby states\n"
        "  --no-portraits              Exercise missing-image fallbacks\n"
        "  --require-portraits         Fail if the atlas cannot load\n"
        "  --focus-row N --focus-index N\n"
        "  --click X Y                 Logical 1600x1000 canvas coordinates, before letterboxing\n"
        "  --expect-hit ROW INDEX      Fail if the returned click target differs\n"
        "The atlas must be assets/lobby/portraits.png beside this executable.");
}

Options Parse(int argc, wchar_t** argv) {
    Options options;
    options.output = argv[1];
    int i = 2;
    if (i < argc && argv[i][0] != L'-') {
        if (i + 1 >= argc) throw std::runtime_error("Both width and height are required");
        options.width = Integer(argv[i++], 320, 8192);
        options.height = Integer(argv[i++], 320, 8192);
    }
    while (i < argc) {
        std::wstring arg = argv[i++];
        if (arg == L"--waiting") options.waiting = true;
        else if (arg == L"--spectator") options.spectator = true;
        else if (arg == L"--no-portraits") options.noPortraits = true;
        else if (arg == L"--require-portraits") options.requirePortraits = true;
        else if (arg == L"--click" || arg == L"--expect-hit") {
            if (i + 1 >= argc) throw std::runtime_error("This option requires two coordinates/indexes");
            if (arg == L"--click") {
                options.mouse.x = static_cast<float>(Integer(argv[i++], -1, 1600));
                options.mouse.y = static_cast<float>(Integer(argv[i++], -1, 1000));
                options.click = true;
            } else {
                options.expectedRow = Integer(argv[i++], -1, 100);
                options.expectedIndex = Integer(argv[i++], -1, 100);
                options.expectHit = true;
            }
        } else {
            if (i >= argc) throw std::runtime_error("Missing option value");
            if (arg == L"--p1") options.p1 = Integer(argv[i++], 0, 43);
            else if (arg == L"--p2") options.p2 = Integer(argv[i++], 0, 43);
            else if (arg == L"--ultra1") options.ultra1 = Integer(argv[i++], 0, 2);
            else if (arg == L"--ultra2") options.ultra2 = Integer(argv[i++], 0, 2);
            else if (arg == L"--stage") options.stage = Integer(argv[i++], 0, 29);
            else if (arg == L"--edition") options.edition = Integer(argv[i++], 1, 16);
            else if (arg == L"--focus-row") options.focusRow = Integer(argv[i++], 0, 10);
            else if (arg == L"--focus-index") options.focusIndex = Integer(argv[i++], 0, 43);
            else throw std::runtime_error("Unknown option; use --help");
        }
    }
    if (options.noPortraits && options.requirePortraits)
        throw std::runtime_error("--no-portraits and --require-portraits cannot be combined");
    if (options.expectHit && !options.click)
        throw std::runtime_error("--expect-hit requires --click");
    if (options.edition != 13 && options.edition != 1 && options.edition != 2 &&
        options.edition != 4 && options.edition != 14 && options.edition != 16)
        throw std::runtime_error("Unsupported edition; use --help for the valid IDs");
    return options;
}

sf4e::LobbyView::Fonts LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    ImFont* fallback = io.Fonts->AddFontDefault();
    char windows[MAX_PATH];
    UINT length = GetWindowsDirectoryA(windows, MAX_PATH);
    std::string directory = length > 0 && length < MAX_PATH ? std::string(windows) + "\\Fonts\\" : "";
    auto load = [&](const char* filename, float size) -> ImFont* {
        std::string path = directory + filename;
        if (directory.empty() || GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return fallback;
        ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), size);
        return font ? font : fallback;
    };
    sf4e::LobbyView::Fonts fonts = {
        load("impact.ttf", 96), load("impact.ttf", 44), load("segoeuib.ttf", 26), load("segoeui.ttf", 20)
    };
    return fonts;
}

sf4e::LobbyView::Model DemoModel(const Options& options) {
    sf4e::LobbyView::Model model;
    model.code = "K7PQ2M";
    model.watchers = "FramePerfect, ComboLab";
    model.publicRoom = true;
    model.scoresAvailable = true;
    model.spectator = options.spectator;
    model.character = options.p1;
    model.ultra = options.ultra1;
    model.editionId = options.edition;
    switch (options.edition) {
    case 13: model.edition = "SF4"; break;
    case 1: model.edition = "SSF4"; break;
    case 2: model.edition = "AE 2011"; break;
    case 4: model.edition = "AE 2012"; break;
    case 16: model.edition = "OMEGA"; break;
    default: model.edition = "ULTRA"; break;
    }
    model.stage = model.proposedStage = options.stage;
    model.focusRow = options.focusRow;
    model.characterCursor = options.focusIndex >= 0 ? options.focusIndex : options.p1;
    model.optionCursor = model.actionCursor = options.focusIndex >= 0 ? options.focusIndex : 0;
    model.players[0].name = "ArcadeKing";
    model.players[0].character = options.p1;
    model.players[0].ultra = options.ultra1;
    model.players[0].present = true;
    model.players[0].local = !options.spectator;
    model.players[0].wins = 7;
    model.players[0].losses = 3;
    if (!options.waiting) {
        model.players[1].name = "BluePhoenix";
        model.players[1].character = options.p2;
        model.players[1].ultra = options.ultra2;
        model.players[1].present = model.players[1].ready = true;
        model.players[1].wins = 3;
        model.players[1].losses = 7;
    }
    if (options.waiting && options.spectator) {
        // A newly joined spectator has no confirmed character to inspect.
        model.character = model.players[0].character = -1;
        model.characterCursor = -1;
    }
    return model;
}

void SavePng(IDirect3DDevice9* device, IDirect3DSurface9* renderTarget, const Options& options) {
    ComPointer<IDirect3DSurface9> pixels;
    Check(device->CreateOffscreenPlainSurface(options.width, options.height, D3DFMT_A8R8G8B8,
        D3DPOOL_SYSTEMMEM, pixels.Address(), nullptr), "Readback surface creation");
    Check(device->GetRenderTargetData(renderTarget, pixels.Get()), "Render target readback");
    struct LockedSurface {
        IDirect3DSurface9* surface;
        D3DLOCKED_RECT rect;
        explicit LockedSurface(IDirect3DSurface9* value) : surface(value), rect() {
            Check(surface->LockRect(&rect, nullptr, D3DLOCK_READONLY), "Readback surface lock");
        }
        ~LockedSurface() { surface->UnlockRect(); }
    } locked(pixels.Get());
    if (!locked.rect.pBits || locked.rect.Pitch < options.width * 4)
        throw std::runtime_error("Invalid readback row stride");
    std::uint64_t bytes = static_cast<std::uint64_t>(locked.rect.Pitch) * options.height;
    if (bytes > (std::numeric_limits<UINT>::max)()) throw std::runtime_error("Readback exceeds WIC buffer limits");

    ComPointer<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory, reinterpret_cast<void**>(factory.Address())), "WIC factory creation");
    ComPointer<IWICStream> stream;
    Check(factory->CreateStream(stream.Address()), "WIC stream creation");
    Check(stream->InitializeFromFilename(options.output.c_str(), GENERIC_WRITE), "PNG output creation");
    ComPointer<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.Address()), "PNG encoder creation");
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "PNG encoder initialization");
    ComPointer<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(frame.Address(), nullptr), "PNG frame creation");
    Check(frame->Initialize(nullptr), "PNG frame initialization");
    Check(frame->SetSize(options.width, options.height), "PNG dimensions");
    GUID format = GUID_WICPixelFormat32bppBGRA;
    Check(frame->SetPixelFormat(&format), "PNG pixel format");
    if (!IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA))
        throw std::runtime_error("PNG encoder does not support the BGRA render target format");
    Check(frame->WritePixels(options.height, static_cast<UINT>(locked.rect.Pitch),
        static_cast<UINT>(bytes), static_cast<BYTE*>(locked.rect.pBits)), "PNG pixel encoding");
    Check(frame->Commit(), "PNG frame commit");
    Check(encoder->Commit(), "PNG file commit");
}

int Render(const Options& options) {
    ComApartment apartment;
    HiddenWindow window;
    ComPointer<IDirect3D9> d3d;
    *d3d.Address() = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d.Get()) throw std::runtime_error("D3D9 is unavailable on this desktop");
    D3DPRESENT_PARAMETERS present = {};
    present.Windowed = TRUE;
    present.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present.hDeviceWindow = window.Get();
    present.BackBufferFormat = D3DFMT_UNKNOWN;
    present.BackBufferWidth = present.BackBufferHeight = 32;
    present.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    ComPointer<IDirect3DDevice9> device;
    HRESULT created = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window.Get(),
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &present, device.Address());
    if (FAILED(created)) created = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window.Get(),
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &present, device.Address());
    // Software vertex processing still requires a D3D9 graphics device. Do
    // not silently replace the production renderer with a different backend.
    Check(created, "D3D9 device creation (a desktop graphics device is required)");
    ComPointer<IDirect3DSurface9> target;
    Check(device->CreateRenderTarget(options.width, options.height, D3DFMT_A8R8G8B8,
        D3DMULTISAMPLE_NONE, 0, FALSE, target.Address(), nullptr), "Preview render target creation");
    Check(device->SetRenderTarget(0, target.Get()), "Preview render target selection");

    GuiSession gui;
    gui.Initialize(window.Get(), device.Get());
    sf4e::LobbyView::Fonts fonts = LoadFonts();
    bool portraits = !options.noPortraits && sf4e::LobbyPortraits::Load(device.Get());
    if (!portraits && options.requirePortraits) throw std::runtime_error("Portrait atlas could not load beside the preview executable");
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1600, 1000);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    sf4e::LobbyView::Model model = DemoModel(options);
    sf4e::LobbyView::Hit hit = sf4e::LobbyView::Draw(ImGui::GetBackgroundDrawList(), fonts,
        model, options.mouse, options.click);
    ImGui::Render();
    ImDrawData* data = ImGui::GetDrawData();
    // Match production: preserve the 1600x1000 canvas aspect ratio, centered
    // within the target. --click is already in canvas units, so it needs no
    // inverse transform. A screen-space pointer would subtract offset/scale.
    float scale = (std::min)(options.width / 1600.0f, options.height / 1000.0f);
    ImVec2 offset((options.width - 1600.0f * scale) * 0.5f,
        (options.height - 1000.0f * scale) * 0.5f);
    for (int i = 0; i < data->CmdListsCount; ++i) {
        ImDrawList* list = data->CmdLists[i];
        for (int j = 0; j < list->VtxBuffer.Size; ++j) {
            list->VtxBuffer[j].pos.x = list->VtxBuffer[j].pos.x * scale + offset.x;
            list->VtxBuffer[j].pos.y = list->VtxBuffer[j].pos.y * scale + offset.y;
        }
        for (int j = 0; j < list->CmdBuffer.Size; ++j) {
            ImVec4& clip = list->CmdBuffer[j].ClipRect;
            clip.x = clip.x * scale + offset.x;
            clip.y = clip.y * scale + offset.y;
            clip.z = clip.z * scale + offset.x;
            clip.w = clip.w * scale + offset.y;
        }
    }
    data->DisplaySize = ImVec2(static_cast<float>(options.width), static_cast<float>(options.height));
    Check(device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 13, 14, 18), 1.0f, 0), "Preview clear");
    Check(device->BeginScene(), "Preview scene begin");
    ImGui_ImplDX9_RenderDrawData(data);
    Check(device->EndScene(), "Preview scene end");
    SavePng(device.Get(), target.Get(), options);
    std::printf("Rendered %dx%d; portraits=%s; hit row=%d index=%d activate=%s\n",
        options.width, options.height, portraits ? "loaded" : "fallback", hit.row, hit.index, hit.activate ? "true" : "false");
    if (options.expectHit && (hit.row != options.expectedRow || hit.index != options.expectedIndex || !hit.activate))
        throw std::runtime_error("Click target did not match --expect-hit");
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || std::wstring(argv[1]) == L"--help") { Usage(); return argc < 2 ? 2 : 0; }
    try { return Render(Parse(argc, argv)); }
    catch (const std::exception& error) { std::fprintf(stderr, "LobbyPreview: %s\n", error.what()); return 1; }
}
