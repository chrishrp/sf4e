#include <map>
#include <vector>
#include <windows.h>
#include <detours/detours.h>
#include "spdlog/spdlog.h"

#include "../Dimps/Dimps__Eva.hxx"
#include "../Dimps/Dimps__Game__Battle.hxx"
#include "../Dimps/Dimps__Math.hxx"

#include "sf4e__Game__Battle.hxx"
#include "sf4e__Game__Battle__Chara.hxx"
#include "sf4e__Game__Battle__Effect.hxx"
#include "sf4e__Game__Battle__Hud.hxx"
#include "sf4e__Game__Battle__System.hxx"
#include "sf4e__Game__Battle__Vfx.hxx"
#include "sf4e__Platform.hxx"

namespace rBattle = Dimps::Game::Battle;
namespace fBattle = sf4e::Game::Battle;

using fIUnit = sf4e::Game::Battle::IUnit;
using rIUnit = Dimps::Game::Battle::IUnit;
using fJobManager = sf4e::Game::Battle::JobManager;
using rJobManager = Dimps::Game::Battle::JobManager;
using SoundHandle = Dimps::Game::Battle::Sound::SoundHandle;
using SoundReference = Dimps::Game::Battle::Sound::SoundReference;
using rSoundPlayerManager = Dimps::Game::Battle::Sound::SoundPlayerManager;
using fSoundPlayerManager = sf4e::Game::Battle::Sound::SoundPlayerManager;

bool fIUnit::bAllowHudUpdate = true;
bool fSoundPlayerManager::bUsePureSounds = true;
bool fSoundPlayerManager::bTrackRequests = false;
bool fSoundPlayerManager::bWarnOnOverflow = false;
std::map<
	rSoundPlayerManager*,
	rSoundPlayerManager*
> fSoundPlayerManager::shadowManagerMap;
std::map<
	rSoundPlayerManager::CriPlayerAdapter*,
	fSoundPlayerManager::DeferredSoundRequest
> fSoundPlayerManager::adapterToCurrentSound;
std::map<rSoundPlayerManager*, std::vector<fSoundPlayerManager::DeferredSoundRequest>> fSoundPlayerManager::queuedStops;

namespace {
	// Which real player each live stub sound owns. Decided for every sound
	// before anything is stopped or started, so two identical sounds playing
	// at once never claim the same player.
	struct SoundPairing {
		std::vector<int> realForStub;  // real player per stub, or -1
		std::vector<bool> realPaired;  // real players claimed by a stub
	};

	template <class StubLive, class RealLive, class Same>
	void PairLiveSounds(SoundPairing& pairing, int stubCount, int realCount, StubLive stubLive, RealLive realLive, Same same) {
		pairing.realForStub.assign(stubCount < 0 ? 0 : stubCount, -1);
		pairing.realPaired.assign(realCount < 0 ? 0 : realCount, false);
		for (int stub = 0; stub < stubCount; stub++) {
			if (!stubLive(stub)) continue;
			for (int real = 0; real < realCount; real++) {
				if (pairing.realPaired[real] || !realLive(real) || !same(stub, real)) continue;
				pairing.realPaired[real] = true;
				pairing.realForStub[stub] = real;
				break;
			}
		}
	}
}

void fBattle::Install() {
	Chara::Install();
	Effect::Install();
	Hud::Install();
	JobManager::Install();
	// Sound::Unit::IsStillPlaying stays unhooked: it reaches the hooked
	// CriPlayerAdapter::IsStillPlaying through the manager's vtable.
	Sound::SoundPlayerManager::Install();
	System::Install();
	Vfx::Install();
}

void fJobManager::Install() {
	BOOL (fJobManager::* _fStart)(int, int, int) = &Start;
	DetourAttach((PVOID*)&rJobManager::publicMethods.Start, *(PVOID*)&_fStart);
}

// With workers, both fighters' per-frame jobs run at once, and a thrown
// fighter's job reads the thrower's bones while the thrower's job rewrites
// them. Where the victim lands then depends on thread timing, a few ULPs
// apart on each PC, and the screen-edge clamp spreads that to both fighters.
// Zero workers runs every job list in queue order on the game thread.
BOOL fJobManager::Start(int workers, int jobs, int jobSize) {
	spdlog::info("Battle jobs: running on the game thread (engine asked for {} workers)", workers);
	return (this->*rJobManager::publicMethods.Start)(0, jobs, jobSize);
}

void fIUnit::SharedHudUpdate(Task** task) {
	if (bAllowHudUpdate) {
		(this->*rIUnit::publicMethods.SharedHudUpdate)(task);
	}
}

void fSoundPlayerManager::Install() {
	void (fSoundPlayerManager:: * _fDestructor)(BOOL) = &destructor;
	SoundHandle(fSoundPlayerManager:: * _fPlaySound)(SoundHandle,
		uint32_t,
		SoundType,
		SoundFlags,
		Dimps::Math::Vec4F*) = &PlaySound;
	void (fSoundPlayerManager:: * _fStopSound)(SoundHandle, BOOL) = &StopSound;
	void (fSoundPlayerManager:: * _fStopAll)(BOOL) = &StopAll;

	DetourAttach((PVOID*)&rSoundPlayerManager::publicMethods.destructor, *(PVOID*)&_fDestructor);
	DetourAttach((PVOID*)&rSoundPlayerManager::publicMethods.PlaySound, *(PVOID*)&_fPlaySound);
	DetourAttach((PVOID*)&rSoundPlayerManager::publicMethods.StopSound, *(PVOID*)&_fStopSound);
	DetourAttach((PVOID*)&rSoundPlayerManager::publicMethods.StopAll, *(PVOID*)&_fStopAll);

	DetourAttach((PVOID*)&rSoundPlayerManager::staticMethods.Factory, Factory); 

	CriPlayerAdapter::Install();
}

// A new manager's adapters can land where a previous battle's were. Start
// their metadata clean so no stale sound attaches to them.
static void ResetAdapterMetadata(rSoundPlayerManager* m) {
	rSoundPlayerManager::CriPlayerAdapter* adapters = *rSoundPlayerManager::GetAdapters(m);
	for (int i = 0; i < *rSoundPlayerManager::GetNumAdapters(m); i++) {
		fSoundPlayerManager::adapterToCurrentSound[&adapters[i]] = fSoundPlayerManager::DeferredSoundRequest{};
	}
}

rSoundPlayerManager* fSoundPlayerManager::Factory(DWORD param_1, DWORD param_2, int* numAdapters) {
	if (!bUsePureSounds) {
		rSoundPlayerManager* out = rSoundPlayerManager::staticMethods.Factory(param_1, param_2, numAdapters);
		shadowManagerMap[out] = NULL;
		ResetAdapterMetadata(out);
		return out;
	}

	bool _bAllowNewPlayers = Platform::Sound::bAllowNewPlayers;
	Platform::Sound::bAllowNewPlayers = false;
	rSoundPlayerManager* stub = rSoundPlayerManager::staticMethods.Factory(param_1, param_2, numAdapters);
	Platform::Sound::bAllowNewPlayers = true;
	rSoundPlayerManager* real = rSoundPlayerManager::staticMethods.Factory(param_1, param_2, numAdapters);
	Platform::Sound::bAllowNewPlayers = _bAllowNewPlayers;
	shadowManagerMap[stub] = real;
	queuedStops[stub] = std::vector<DeferredSoundRequest>();
	ResetAdapterMetadata(stub);
	ResetAdapterMetadata(real);
	return stub;
}

void fSoundPlayerManager::destructor(BOOL param_1) {
	// Drop this manager's adapters, and its shadow's, from the metadata map
	// before the arrays are freed; the heap reuses those addresses.
	if (bUsePureSounds) {
		rSoundPlayerManager* real = shadowManagerMap[this];
		if (real) {
			rSoundPlayerManager::CriPlayerAdapter* realAdapters = *rSoundPlayerManager::GetAdapters(real);
			for (int i = 0; i < *rSoundPlayerManager::GetNumAdapters(real); i++) {
				adapterToCurrentSound.erase(&realAdapters[i]);
			}
			(real->*rSoundPlayerManager::publicMethods.destructor)(param_1);
		}
	}
	rSoundPlayerManager::CriPlayerAdapter* adapters = *rSoundPlayerManager::GetAdapters(this);
	for (int i = 0; i < *rSoundPlayerManager::GetNumAdapters(this); i++) {
		adapterToCurrentSound.erase(&adapters[i]);
	}
	queuedStops.erase(this);
	shadowManagerMap.erase(this);
	(this->*rSoundPlayerManager::publicMethods.destructor)(param_1);
}

SoundHandle fSoundPlayerManager::PlaySound(
	SoundHandle cueSheetHandle,
	uint32_t cueIdx,
	SoundType type,
	SoundFlags flags,
	Dimps::Math::Vec4F* position
) {
	SoundHandle out = (this->*rSoundPlayerManager::publicMethods.PlaySound)(
		cueSheetHandle,
		cueIdx,
		type,
		flags,
		position
	);

	if (bTrackRequests) {
		SoundReference cueSheetRef = SoundReference::FromHandle(cueSheetHandle);
		spdlog::warn(
			"SoundPlayerManager sound requested: {} {} {} {} {} {} {} {} {} {} and got {}",
			cueSheetRef.index,
			cueSheetRef.useCount,
			cueIdx,
			type,
			flags,
			(void*)position,
			position ? position->x : 0,
			position ? position->y : 0,
			position ? position->z : 0,
			position ? position->w : 0,
			out
		);
		if (bWarnOnOverflow && out == 0xffffffff) {
			MessageBox(NULL, "Sound player overflow", NULL, MB_OK);
		}
	}

	if (bUsePureSounds) {
		if (out == 0xffffffff) {
			// The sound could not be played because there were no players
			// available to serve the request. Stop the oldest sound in this
			// manager, then play this one again.
			//
			// XXX (adanducci): I'm actually quite sure this case can never occur.
			// The vast majority of sound effects use the interruptable flag,
			// and the VO infrastructure already explicitly stops the existing
			// VO sound if one exists.
			DeferredSoundRequest* oldestReq = NULL;
			rSoundPlayerManager::CriPlayerAdapter* adapters = *rSoundPlayerManager::GetAdapters(this);
			for (int i = 0; i < *rSoundPlayerManager::GetNumAdapters(this); i++) {
				DeferredSoundRequest* req = &adapterToCurrentSound[&adapters[i]];
				if (!req->bLive) {
					continue;
				}
				if (oldestReq == NULL || req->nFrame < oldestReq->nFrame) {
					oldestReq = req;
				}
			}

			if (oldestReq != NULL) {
				this->StopSound(oldestReq->currentAdapterHandle, 1);
				out = (this->*rSoundPlayerManager::publicMethods.PlaySound)(
					cueSheetHandle,
					cueIdx,
					type,
					flags,
					position
				);
			}
		}

		if (out == 0xffffffff) {
			// Nothing is playing, so record nothing: decoding the failure value
			// would index far outside the adapter array.
			return out;
		}

		SoundReference adapterReference = SoundReference::FromHandle(out);
		assert(adapterReference.index < *rSoundPlayerManager::GetNumAdapters(this));
		rSoundPlayerManager::CriPlayerAdapter* adapter = &(*rSoundPlayerManager::GetAdapters(this))[adapterReference.index];
		DeferredSoundRequest& meta = adapterToCurrentSound[adapter];
		if (meta.bLive) {
			// If this adapter was already live, the successful `PlaySound`
			// call must have interrupted an existing sound. The syncing
			// step can't really differentiate between interrupts and
			// stops, so just treat it as a stop. Only stub managers queue
			// stops- for the real manager (reached via SyncState), the
			// interrupt already took effect and nothing ever drains a
			// real manager's queue.
			if (shadowManagerMap.count(this)) {
				queuedStops[this].push_back(meta);
			}
		}
		meta.bLive = true;
		meta.nFrame = System::GetNumFramesSimulated_FixedPoint(System::staticMethods.GetSingleton())->integral;
		meta.currentAdapterHandle = out;
		meta.cueSheetHandle = cueSheetHandle;
		meta.cueIdx = cueIdx;
		meta.type = type;
		meta.flags = flags;
		meta.position = position;
	}

	return out;
};

void fSoundPlayerManager::StopSound(SoundHandle adapterHandle, BOOL criParam) {
	if (bUsePureSounds) {
		SoundReference adapterReference = SoundReference::FromHandle(adapterHandle);
		rSoundPlayerManager::CriPlayerAdapter* adapter = &(*rSoundPlayerManager::GetAdapters(this))[adapterReference.index];
		DeferredSoundRequest& meta = adapterToCurrentSound[adapter];
		meta.bLive = false;
		if (shadowManagerMap.count(this)) {
			queuedStops[this].push_back(meta);
		}
	}

	rSoundPlayerManager* _this = this;
	(this->*rSoundPlayerManager::publicMethods.StopSound)(adapterHandle, criParam);
}

void fSoundPlayerManager::StopAll(BOOL criParam) {
	if (bUsePureSounds) {
		rSoundPlayerManager::CriPlayerAdapter* adapters = *rSoundPlayerManager::GetAdapters(this);
		bool bIsStub = shadowManagerMap.count(this) > 0;
		for (int i = 0; i < *rSoundPlayerManager::GetNumAdapters(this); i++) {
			DeferredSoundRequest& meta = adapterToCurrentSound[&adapters[i]];
			meta.bLive = false;
			if (bIsStub) {
				queuedStops[this].push_back(meta);
			}
		}
		return;
	}

	rSoundPlayerManager* _this = this;
	(this->*rSoundPlayerManager::publicMethods.StopAll)(criParam);
}

bool fSoundPlayerManager::DeferredSoundRequest::IsEqual(DeferredSoundRequest* lhs, DeferredSoundRequest* rhs) {
	return (
		lhs->cueIdx == rhs->cueIdx &&
		lhs->cueSheetHandle == rhs->cueSheetHandle &&
		lhs->flags == rhs->flags &&
		lhs->type == rhs->type &&
		lhs->position == rhs->position
	);
}

void fSoundPlayerManager::SyncState() {
	for (auto managerIter = shadowManagerMap.begin(); managerIter != shadowManagerMap.end(); managerIter++) {
		rSoundPlayerManager* stubManager = managerIter->first;
		rSoundPlayerManager* realManager = managerIter->second;
		rSoundPlayerManager::CriPlayerAdapter* stubPlayers = *rSoundPlayerManager::GetAdapters(stubManager);
		rSoundPlayerManager::CriPlayerAdapter* realPlayers = *rSoundPlayerManager::GetAdapters(realManager);

		// If a sound in the stub manager was imperatively stopped, stop
		// up to one corresponding sound that is actively playing. A stop
		// targets one sound instance, so it must match the request and the
		// frame the sound started on. With no match the instance is already
		// stopped; matching on the request alone would kill a retriggered copy
		// of the same cue, which would then audibly restart.
		for (auto iter = queuedStops[stubManager].begin(); iter != queuedStops[stubManager].end(); iter++) {
			DeferredSoundRequest stoppedSound = *iter;
			for (int i = 0; i < *rSoundPlayerManager::GetNumAdapters(realManager); i++) {
				rSoundPlayerManager::CriPlayerAdapter* realPlayer = &realPlayers[i];
				DeferredSoundRequest* realSound = &adapterToCurrentSound[realPlayer];
				if (!realSound->bLive) {
					continue;
				}

				if (
					DeferredSoundRequest::IsEqual(&stoppedSound, realSound) &&
					realSound->nFrame == stoppedSound.nFrame
				) {
					((fSoundPlayerManager*)realManager)->StopSound(realSound->currentAdapterHandle, 1);
					break;
				}
			}
		}

		// Pair every live stub sound with its own live real adapter before
		// anything is stopped or started. Identical
		// sounds playing in parallel then update two different adapters,
		// instead of the same one.
		const int stubCount = *rSoundPlayerManager::GetNumAdapters(stubManager);
		const int realCount = *rSoundPlayerManager::GetNumAdapters(realManager);
		static SoundPairing pairing; // Game thread only; reused storage.
		PairLiveSounds(pairing, stubCount, realCount,
			[&](int stub) { return adapterToCurrentSound[&stubPlayers[stub]].bLive; },
			[&](int real) { return adapterToCurrentSound[&realPlayers[real]].bLive; },
			[&](int stub, int real) {
				return DeferredSoundRequest::IsEqual(&adapterToCurrentSound[&stubPlayers[stub]],
					&adapterToCurrentSound[&realPlayers[real]]);
			});

		// Stop every live real sound no stub claimed. This has to happen
		// first in order to free up players for playing sounds later.
		for (int i = 0; i < realCount; i++) {
			DeferredSoundRequest* realSound = &adapterToCurrentSound[&realPlayers[i]];
			if (realSound->bLive && !pairing.realPaired[i]) {
				((fSoundPlayerManager*)realManager)->StopSound(realSound->currentAdapterHandle, 1);
			}
		}

		// Finally, reconcile playing sounds. A paired stub updates the
		// parameters of its real player, so things like fades carry over. An
		// unpaired live stub starts a new real sound.
		for (int i = 0; i < stubCount; i++) {
			rSoundPlayerManager::CriPlayerAdapter* stubPlayer = &stubPlayers[i];
			DeferredSoundRequest* stubSound = &adapterToCurrentSound[stubPlayer];
			if (!stubSound->bLive) {
				continue;
			}

			int targetAdapter = pairing.realForStub[i];
			if (targetAdapter == -1) {
				SoundHandle playerHandle = ((fSoundPlayerManager*)realManager)->PlaySound(
					stubSound->cueSheetHandle,
					stubSound->cueIdx,
					stubSound->type,
					stubSound->flags,
					stubSound->position
				);
				if (playerHandle == 0xffffffff) {
					// The real manager could not serve the request, most likely a
					// stale cue sheet handle. Skip it rather than decode the
					// failure value into an adapter index.
					spdlog::warn(
						"SyncState: real PlaySound failed for cue {} (sheet {:#x}); skipping reconcile",
						stubSound->cueIdx,
						stubSound->cueSheetHandle
					);
					continue;
				}
				SoundReference playerRef = SoundReference::FromHandle(playerHandle);
				targetAdapter = playerRef.index;
				if (targetAdapter < 0 || targetAdapter >= realCount) {
					spdlog::warn("SyncState: real PlaySound returned adapter {} of {}; skipping reconcile",
						targetAdapter, realCount);
					continue;
				}
			}
			rSoundPlayerManager::CriPlayerAdapter* realPlayer = &realPlayers[targetAdapter];
			// The real side takes the stub's start frame, so a queued stop can
			// find this exact instance even after a rollback moves its start.
			adapterToCurrentSound[realPlayer].nFrame = stubSound->nFrame;
			// XXX (adanducci): This is extremely likely to be broken.
			realPlayer->flags = stubPlayer->flags;
			realPlayer->position = stubPlayer->position;
			realPlayer->volume = stubPlayer->volume;
			realPlayer->fadeScale = stubPlayer->fadeScale;
			realPlayer->playState = stubPlayer->playState;
			realPlayer->field6_0x18 = stubPlayer->field6_0x18;
		}

		queuedStops[stubManager].clear();
		(realManager->*rSoundPlayerManager::publicMethods.Update)();
	}
}

void fSoundPlayerManager::CriPlayerAdapter::Install() {
	BOOL(CriPlayerAdapter:: * _fIsStillPlaying)() = &IsStillPlaying;
	DetourAttach((PVOID*)&rSoundPlayerManager::CriPlayerAdapter::publicMethods.IsStillPlaying, *(PVOID*)&_fIsStillPlaying);
}

BOOL fSoundPlayerManager::CriPlayerAdapter::IsStillPlaying() {
	if (bUsePureSounds) {
		DeferredSoundRequest* stubSound = &adapterToCurrentSound[this];
		return stubSound->bLive;
	}

	return (this->*rSoundPlayerManager::CriPlayerAdapter::publicMethods.IsStillPlaying)();
}
