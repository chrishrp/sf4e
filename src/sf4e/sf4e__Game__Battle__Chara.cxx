// Rollback for the fighters' shadows: Yun's Genei Jin, Yang's Seiei Enbu,
// Rose's Soul Illusion and every other shadow move.
//
// Each shadow is a Chara::Afterimage, an Action::Actor whose memento override
// saves only the afterimage's own fields and never the actor underneath:
// its animation frame, collision boxes and action engine. After a load those
// stayed on the discarded timeline, so a shadow's hits could land on
// different frames on each PC, and the shadows froze or trailed on screen.
//
// These detours append the actor's memento after the afterimage's own, in the
// same engine slot, with a tail that describes the layout:
//
//   [afterimage memento][pad to 16][actor memento][tail]
//
// The tail sits at the end of the slot so a restore does not have to trust
// the afterimage's current size. The engine sizes the slot by calling
// GetMementoSize right before each record, which is where the extra room
// comes from. When the slot cannot be verified the actor part is skipped and
// the shadow behaves as the engine had it, logged once.
//
// The restore also leaves the pose ring alone. It holds skeleton snapshots
// captured while drawing and is read only by the draw; frames a rollback
// re-simulates are never drawn, so restoring it put the shadows behind the
// fighter.
#include <cstring>
#include <set>
#include <string>
#include <windows.h>
#include <detours/detours.h>

#include "spdlog/spdlog.h"

#include "../Dimps/Dimps__Game.hxx"
#include "../Dimps/Dimps__Game__Battle__Action.hxx"
#include "../Dimps/Dimps__Game__Battle__Chara.hxx"
#include "sf4e__Game__Battle__Chara.hxx"

namespace fChara = sf4e::Game::Battle::Chara;
using fAfterimage = fChara::Afterimage;
using rAfterimage = Dimps::Game::Battle::Chara::Afterimage;
using rActor = Dimps::Game::Battle::Action::Actor;
using rKey = Dimps::Game::GameMementoKey;

namespace {

// The afterimage object around the IMementoable subobject.
constexpr size_t kMementoableOffset = 0x60;
// Its own restore copies this much from memento +16 to object +676, then
// restores the pose ring that follows (144-byte header, then poses).
constexpr size_t kStateMementoOffset = 16;
constexpr size_t kStateObjectOffset = 676;
constexpr size_t kStateBytes = 0x6870;
constexpr size_t kPoseRingHeaderBytes = 144;
constexpr size_t kFixedBytes = kStateMementoOffset + kStateBytes + kPoseRingHeaderBytes;
// Action::Actor's memento: 576 bytes plus 272 per collision box, with the
// five list counts at +4..+20.
constexpr size_t kActorFixedBytes = 576;
constexpr size_t kActorBoxBytes = 272;
constexpr int kActorLists = 5;
constexpr size_t kActorCountsOffset = 4;
constexpr int kMaxMementos = 64;

constexpr uint32_t kTailMagic = 0x57444853; // "SHDW"
enum TailKind : uint32_t { TK_AFTERIMAGE_ONLY = 1, TK_WITH_ACTOR = 2 };

struct Tail {
	uint32_t magic;
	uint32_t kind;
	uint32_t afterimageSize;
	uint32_t actorSize;
};
constexpr size_t kTailBytes = sizeof(Tail);

constexpr size_t AlignUp16(size_t n) { return (n + 15) & ~size_t(15); }

struct Slot {
	uint8_t* m = nullptr;
	size_t size = 0;
	const char* reject = "slot not found";
};

// The engine slot this memento lives in, verified against the key: the key's
// buffer holds numMementos slots of equal size, then the metadata entries.
Slot FindSlot(fAfterimage* self, void* memento) {
	Slot slot;
	const rKey* key = rAfterimage::GetKey(self);
	if (!memento || !key->mementos || !key->metadata) { slot.reject = "key has no buffer"; return slot; }
	const int n = key->numMementos;
	if (n <= 0 || n > kMaxMementos) { slot.reject = "memento count out of range"; return slot; }
	const uint8_t* base = (const uint8_t*)key->mementos;
	const size_t total = (const uint8_t*)key->metadata - base;
	if (total == 0 || total % n != 0) { slot.reject = "buffer is not whole slots"; return slot; }
	const size_t size = total / n;
	if (size < kFixedBytes + kTailBytes) { slot.reject = "slot smaller than the fixed memento"; return slot; }
	for (int i = 0; i < n; i++) {
		if (key->metadata[i].memento != memento) continue;
		if (memento != base + i * size) { slot.reject = "memento is not at its slot"; return slot; }
		slot.m = (uint8_t*)memento;
		slot.size = size;
		slot.reject = nullptr;
		return slot;
	}
	slot.reject = "memento is not in its key";
	return slot;
}

void WarnOnce(const char* op, const char* why) {
	static std::set<std::string> seen;
	if (!seen.insert(std::string(op) + why).second) return;
	spdlog::warn("Shadow {}: actor state left as the engine had it ({})", op, why);
}

}

void fChara::Install() {
	Afterimage::Install();
}

void fAfterimage::Install() {
	size_t (fAfterimage::* _fGetMementoSize)() = &GetMementoSize;
	int (fAfterimage::* _fRecordToMemento)(void*, rKey::MementoID*) = &RecordToMemento;
	int (fAfterimage::* _fRestoreFromMemento)(void*, rKey::MementoID*) = &RestoreFromMemento;
	DetourAttach((PVOID*)&rAfterimage::mementoableMethods.GetMementoSize, *(PVOID*)&_fGetMementoSize);
	DetourAttach((PVOID*)&rAfterimage::mementoableMethods.RecordToMemento, *(PVOID*)&_fRecordToMemento);
	DetourAttach((PVOID*)&rAfterimage::mementoableMethods.RestoreFromMemento, *(PVOID*)&_fRestoreFromMemento);
}

size_t fAfterimage::GetMementoSize() {
	const size_t afterimageSize = (this->*rAfterimage::mementoableMethods.GetMementoSize)();
	const size_t actorSize = (((rActor*)this)->*rActor::publicMethods.GetMementoSize)();
	return AlignUp16(afterimageSize) + actorSize + kTailBytes;
}

int fAfterimage::RecordToMemento(void* memento, rKey::MementoID* id) {
	const int result = (this->*rAfterimage::mementoableMethods.RecordToMemento)(memento, id);
	const size_t afterimageSize = (this->*rAfterimage::mementoableMethods.GetMementoSize)();
	const size_t actorSize = (((rActor*)this)->*rActor::publicMethods.GetMementoSize)();
	const Slot slot = FindSlot(this, memento);
	const char* why = slot.reject;
	const size_t actorOffset = AlignUp16(afterimageSize);
	if (!why && (afterimageSize < kFixedBytes || actorSize < kActorFixedBytes ||
		actorOffset + actorSize + kTailBytes != slot.size)) {
		why = "slot was not sized for this actor memento";
	}
	if (!why && !(((rActor*)this)->*rActor::publicMethods.RecordToMemento)(slot.m + actorOffset, id)) {
		why = "the actor record returned 0";
	}
	if (!slot.reject) {
		Tail tail = { kTailMagic, why ? TK_AFTERIMAGE_ONLY : TK_WITH_ACTOR, (uint32_t)afterimageSize, (uint32_t)actorSize };
		std::memcpy(slot.m + slot.size - kTailBytes, &tail, sizeof(tail));
	}
	if (why) WarnOnce("record", why);
	return result;
}

int fAfterimage::RestoreFromMemento(void* memento, rKey::MementoID* id) {
	if (!memento) return 0;
	const Slot slot = FindSlot(this, memento);
	const char* why = slot.reject;
	bool reported = false;
	if (!why) {
		Tail tail;
		std::memcpy(&tail, slot.m + slot.size - kTailBytes, sizeof(tail));
		const size_t actorOffset = AlignUp16(tail.afterimageSize);
		if (tail.magic != kTailMagic) why = "no tail";
		else if (tail.kind == TK_AFTERIMAGE_ONLY) { why = "recorded without the actor part"; reported = true; }
		else if (tail.kind != TK_WITH_ACTOR) why = "unknown tail kind";
		else if (tail.afterimageSize < kFixedBytes) why = "tail afterimage size below its fixed part";
		else if (actorOffset + tail.actorSize + kTailBytes != slot.size) why = "tail does not describe this slot";
		else if (tail.actorSize < kActorFixedBytes) why = "actor payload smaller than its fixed part";
		else {
			size_t boxes = 0;
			for (int i = 0; i < kActorLists && !why; i++) {
				int32_t count;
				std::memcpy(&count, slot.m + actorOffset + kActorCountsOffset + 4 * i, sizeof(count));
				if (count < 0) why = "negative actor box count";
				boxes += (size_t)count;
			}
			if (!why && kActorFixedBytes + kActorBoxBytes * boxes != tail.actorSize) why = "actor payload does not match its box counts";
			if (!why && !(((rActor*)this)->*rActor::publicMethods.RestoreFromMemento)(slot.m + actorOffset, id)) why = "the actor restore returned 0";
		}
	}
	if (why && !reported) WarnOnce("restore", why);
	// The afterimage's own state, without the pose ring.
	uint8_t* object = (uint8_t*)this - kMementoableOffset;
	std::memcpy(object + kStateObjectOffset, (const uint8_t*)memento + kStateMementoOffset, kStateBytes);
	return 1;
}
