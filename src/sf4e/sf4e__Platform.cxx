#include <chrono>
#include <memory>
#include <vector>

#include <windows.h>
#include <KnownFolders.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <strsafe.h>

#include <detours/detours.h>
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/wincolor_sink.h"

#include "../Dimps/Dimps__Eva.hxx"
#include "../Dimps/Dimps__Platform.hxx"
#include "sf4e.hxx"
#include "sf4e__Platform.hxx"
#include "sf4e__UserApp.hxx"
#include "sf4e__Overlay.hxx"
#include "sf4e__Lobby.hxx"
#include "sf4e__Game__Battle__System.hxx"

namespace rPlatform = Dimps::Platform;
using rD3D = rPlatform::D3D;
using rGFxApp = rPlatform::GFxApp;
using rMain = rPlatform::Main;
using rSound = rPlatform::Sound;

namespace fPlatform = sf4e::Platform;
using fD3D = fPlatform::D3D;
using fGFxApp = fPlatform::GFxApp;
using fMain = fPlatform::Main;
using fUserApp = sf4e::UserApp;
using fSound = fPlatform::Sound;

template <int N>
using fSoundObjectPoolEntry = fPlatform::SoundObjectPoolEntry<N>;

template <int N>
using fSoundObjectPool = fPlatform::SoundObjectPool<N>;

bool fSound::bAllowNewPlayers = true;

void fPlatform::Install() {
    D3D::Install();
    Main::Install();
    Sound::Install();
}

void fD3D::Install() {
    void (fD3D:: * _fDestroy)() = &Destroy;
    DWORD(fD3D:: * _fReset)() = &Reset;
    void(fD3D:: * _fRunScene_Render)(void*) = &RunScene_Render;
    DetourAttach((PVOID*)&rD3D::privateMethods.Destroy, *(PVOID*)&_fDestroy);
    DetourAttach((PVOID*)&rD3D::privateMethods.Reset, *(PVOID*)&_fReset);
    DetourAttach((PVOID*)&rD3D::privateMethods.RunScene_Render, *(PVOID*)&_fRunScene_Render);
}

void fD3D::RunScene_Render(void* sceneCommandList) {
    (this->*rD3D::privateMethods.RunScene_Render)(sceneCommandList);
    Overlay::DrawOverlay();
}

void fD3D::Destroy() {
    Overlay::FreeOverlay();
    (this->*rD3D::privateMethods.Destroy)();
}

DWORD fD3D::Reset() {
    Overlay::FreeOverlay();
    DWORD out = (this->*rD3D::privateMethods.Reset)();
    // Only rebuild the overlay if the reset actually produced a device. A
    // lost device (alt-tab out of fullscreen) can make this fail, and the
    // game retries later; DrawOverlay initializes lazily once one exists.
    IDirect3DDevice9* device = Dimps::Platform::D3D::staticMethods.GetSingleton()->lpD3DDevice;
    if (out == 0 && device != nullptr) {
        Overlay::InitializeOverlay(
            (*rMain::GetWindowData(rMain::staticMethods.GetSingleton()))->hWnd,
            device
        );
    }
    else {
        spdlog::warn("D3D reset returned {} with device {}; overlay will re-initialize when a device is back", out, (void*)device);
    }
    return out;
}

void fGFxApp::RecordToAdditionalMemento(rGFxApp* a, AdditionalMemento& m) {
    int i;

    rGFxApp::ObjectPool<Dimps::Eva::IEmSpriteAction>* actionPool = rGFxApp::GetActionPool(a);
    for (i = 0; i < NUM_GFX_ACTIONS; i++) {
        m.actions[i].first = actionPool->useIndex[i];
        if (actionPool->useIndex[i]) {
            sf4e::Eva::IEmSpriteAction::RecordToAdditionalMemento(&actionPool->raw[i], m.actions[i].second);
        }
    }
}

void fGFxApp::RestoreFromAdditionalMemento(rGFxApp* a, const AdditionalMemento& m) {
    int i;

    rGFxApp::ObjectPool<Dimps::Eva::IEmSpriteAction>* actionPool = rGFxApp::GetActionPool(a);
    for (i = 0; i < NUM_GFX_ACTIONS; i++) {
        actionPool->useIndex[i] = m.actions[i].first;
        if (actionPool->useIndex[i]) {
            sf4e::Eva::IEmSpriteAction::RestoreFromAdditionalMemento(&actionPool->raw[i], m.actions[i].second);
        }
    }
}

void fMain::Install() {
    int (fMain:: * _fInitialize)(void*, void*, void*) = &Initialize;
    void (fMain:: * _fDestroy)() = &Destroy;
    DetourAttach((PVOID*)&rMain::publicMethods.Initialize, *(PVOID*)&_fInitialize);
    DetourAttach((PVOID*)&rMain::publicMethods.Destroy, *(PVOID*)&_fDestroy);
    DetourAttach((PVOID*)&rMain::staticMethods.RunWindowFunc, &RunWindowFunc);
}

int fMain::Initialize(void* a, void* b, void* c) {
    if (sf4e::hSyncEvent != NULL) {
        SetEvent(sf4e::hSyncEvent);
        CloseHandle(sf4e::hSyncEvent);
        sf4e::hSyncEvent = NULL;
    }

    int rval = (this->*(rMain::publicMethods.Initialize))(a, b, c);

    BOOL hasConsole = false;
    if (sf4e::args.bShowConsole) {
        hasConsole = AllocConsole();
        if (!hasConsole) {
            MessageBox(NULL, TEXT("Could not allocate console!"), NULL, MB_OK);
        }
    }

    // Set up spdlog
    PWSTR path;
    HRESULT queryResult = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &path);
    if (queryResult == S_OK) {
        try
        {
            wchar_t logpath[MAX_PATH];
            PathCombineW(logpath, path, L"sf4e/logs/sf4e.log");
            int max_size = 1048576 * 5; // 5MB
            int max_files = 10;

            std::vector<spdlog::sink_ptr> sinks;
            sinks.push_back(std::shared_ptr<spdlog::sinks::rotating_file_sink_mt>(
                new spdlog::sinks::rotating_file_sink_mt(logpath, max_size, max_files, true)
            ));
            if (hasConsole) {
                sinks.push_back(std::shared_ptr<spdlog::sinks::wincolor_stdout_sink_mt>(
                    new spdlog::sinks::wincolor_stdout_sink_mt()
                ));
            }
            std::shared_ptr<spdlog::logger> logger(new spdlog::logger("sf4e", sinks.begin(), sinks.end()));

            // Write through on every message. Buffered output is lost whenever
            // the game is killed rather than quit, which is most of the time
            // while debugging, and it silently truncated the tail of every log
            // written so far. Diagnostics are worthless if they don't survive.
            logger->flush_on(spdlog::level::trace);
            spdlog::set_default_logger(logger);
            spdlog::flush_every(std::chrono::seconds(1));
            // The build, first line of every log. Two player logs arrived with a
            // crash in them and there was no way to tell which build either
            // was running, or whether the fixes they needed were even in it.
            // The build, from the DLL's own PE header.
            //
            // This used to be __DATE__/__TIME__, which is baked in when THIS
            // file is compiled -- so an incremental build that changed other
            // sources left the stamp frozen at an older time and the log
            // confidently reported the wrong build. The PE TimeDateStamp is
            // written by the linker, so it moves whenever the DLL is actually
            // relinked, which is the question being asked.
            char built[64] = "unknown";
            HMODULE self = nullptr;
            if (GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    (LPCSTR)&sf4e::bSoakTest, &self) && self != nullptr) {
                const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)self;
                if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
                    const IMAGE_NT_HEADERS* nt =
                        (const IMAGE_NT_HEADERS*)((const BYTE*)self + dos->e_lfanew);
                    if (nt->Signature == IMAGE_NT_SIGNATURE) {
                        time_t t = (time_t)nt->FileHeader.TimeDateStamp;
                        struct tm g;
                        if (gmtime_s(&g, &t) == 0) {
                            strftime(built, sizeof(built), "%Y-%m-%d %H:%M:%S UTC", &g);
                        }
                    }
                }
            }
            spdlog::info("Welcome to sf4e {} (built {})", SF4E_VERSION, built);

            // Unattended two-machine soak. Independent of the sync test: this
            // one needs a real opponent and a real network, which is exactly
            // what the sync test does not have.
            //
            //   set SF4E_NETSOAK=1   (on BOTH machines)
            //
            // Both sides auto-ready, pick a fresh character and stage every
            // match, and throw specials, supers and ultras. Join a lobby once
            // and leave them running.
            char delayEnv[8] = { 0 };
            if (GetEnvironmentVariableA("SF4E_DELAY", delayEnv, sizeof(delayEnv)) > 0) {
                sf4e::Lobby::SetInputDelayOverride(atoi(delayEnv));
            }

            char idleEnv[8] = { 0 };
            if (GetEnvironmentVariableA("SF4E_GGPO_IDLE", idleEnv, sizeof(idleEnv)) > 0 && idleEnv[0] == '0') {
                sf4e::Game::Battle::System::bGgpoDuringIdle = false;
                spdlog::warn("SF4E_GGPO_IDLE=0: round transitions will run OUTSIDE the rollback "
                    "timeline (the old behaviour). Both players must match or they will desync.");
            }
            spdlog::info("Round transitions are {} the rollback timeline",
                sf4e::Game::Battle::System::bGgpoDuringIdle ? "INSIDE" : "outside");

            char roundCpEnv[8] = { 0 };
            if (GetEnvironmentVariableA("SF4E_ROUND_CHECKPOINT", roundCpEnv, sizeof(roundCpEnv)) > 0 && roundCpEnv[0] != '0') {
                sf4e::Game::Battle::System::bRoundCheckpoint = true;
                spdlog::warn("SF4E_ROUND_CHECKPOINT is set: round transitions will stall waiting for "
                    "input confirmation. Known not to clear at real ping - diagnostic only.");
            }

            char netSoakEnv[8] = { 0 };
            if (GetEnvironmentVariableA("SF4E_NETSOAK", netSoakEnv, sizeof(netSoakEnv)) > 0 && netSoakEnv[0] != '0') {
                sf4e::bSoakTest = true;
                sf4e::Game::Battle::System::syncTest.bSoak = true;
                spdlog::warn("SF4E_NETSOAK is set: this PC will auto-ready and play itself. "
                    "Run it on BOTH machines, join a lobby once, and leave them.");
            }

            // Arm the rollback sync test straight from the environment, so a
            // tester only has to set a variable and play a VS match instead of
            // finding it in the debug overlay:
            //
            //   set SF4E_SYNCTEST=6   (frames of rollback to verify, 1-8)
            //
            // It saves the state, rolls back that many frames, re-simulates and
            // compares -- on ONE machine, against itself.
            char syncTestEnv[16] = { 0 };
            if (GetEnvironmentVariableA("SF4E_SYNCTEST", syncTestEnv, sizeof(syncTestEnv)) > 0) {
                int distance = atoi(syncTestEnv);
                if (distance > 0) {
                    sf4e::Game::Battle::System::ArmSyncTest(distance);
                    char skipEnv[8] = { 0 };
                    if (GetEnvironmentVariableA("SF4E_SKIP_RESET", skipEnv, sizeof(skipEnv)) > 0 && skipEnv[0] != '0') {
                        // Controlled comparison: RestoreAllFromInternalMementos
                        // normally calls Chara::Actor::ResetAfterMemento, which
                        // RECOMPUTES derived state. Skipping it separates "the
                        // saved bytes are incomplete" from "the recomputation
                        // does not reproduce what was saved".
                        sf4e::Game::Battle::System::bSkipResetAfterMemento = true;
                        spdlog::warn("SF4E_SKIP_RESET is set: ResetAfterMemento will NOT run on restore. "
                            "Diagnostic only - do not play online like this.");
                    }
                    char soakEnv[8] = { 0 };
                    if (GetEnvironmentVariableA("SF4E_SYNCTEST_SOAK", soakEnv, sizeof(soakEnv)) > 0 && soakEnv[0] != '0') {
                        sf4e::Game::Battle::System::syncTest.bSoak = true;
                        spdlog::warn("SOAK MODE: both sides play themselves with random inputs, a random "
                            "pairing every match, and the test re-arms itself. Start one VS match and leave it.");
                    }
                    spdlog::warn("SF4E_SYNCTEST is set: start VERSUS > player vs player and play a minute. "
                        "Results are written to this log when the match ends.");
                }
            }
            if (sf4e::bSoakTest) {
                spdlog::warn("=== SOAK TEST BUILD: automated endless matches. The pad is overridden; not for normal play. ===");
            }
            else if (sf4e::bDiagLogging) {
                spdlog::warn("=== DIAGNOSTIC BUILD: plays normally, logs battle-flow + state divergence. Both players must run this same build. ===");
            }
        }
        catch (const spdlog::spdlog_ex& ex)
        {
            MessageBoxA(NULL, ex.what(), NULL, MB_OK);
            DebugBreak();
        }
    }
    else {
        MessageBox(NULL, TEXT("Could not get appdata path for logs!"), NULL, MB_OK);
    }
    CoTaskMemFree(path);

    Overlay::InitializeOverlay(
        (*rMain::GetWindowData(rMain::staticMethods.GetSingleton()))->hWnd,
        Dimps::Platform::D3D::staticMethods.GetSingleton()->lpD3DDevice
    );

    SteamDatagramErrMsg errMsg;
    if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
        spdlog::error("GameNetworkingSockets_Init failed.  {}", errMsg);
    }

    return rval;
}

void fMain::Destroy() {
    if (fUserApp::netplay) {
        delete fUserApp::netplay.release();
    }
    if (fUserApp::server) {
        delete fUserApp::server.release();
    }
    GameNetworkingSockets_Kill();
    spdlog::shutdown();
    (this->*rMain::publicMethods.Destroy)();
}

void WINAPI fMain::RunWindowFunc(rMain* lpMain, HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (Overlay::OverlayWindowFunc(hwnd, uMsg, wParam, lParam)) {
        return;
    }
    rMain::staticMethods.RunWindowFunc(lpMain, hwnd, uMsg, wParam, lParam);
}


void fSound::Install() {
    DetourAttach((PVOID*)&rSound::staticMethods.GetNewPlayerHandle, &GetNewPlayerHandle);
}

uint32_t fSound::GetNewPlayerHandle() {
    if (!bAllowNewPlayers) {
        return 0xffffffff;
    }

    return rSound::staticMethods.GetNewPlayerHandle();
}
