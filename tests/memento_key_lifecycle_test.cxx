#include "../src/sf4e/sf4e__MementoKeyLifecycle.hxx"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>

namespace {
using Scope = sf4e::Game::MementoKeyReinitializationScope;

void Require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

// This models the verified Initialize -> ClearKey sequence and the ownership
// transfer used by SaveState::Save. It does not execute USFIV or prove its
// complete restore behavior. Production hooks use the same scope helper.
struct Key {
    const void* owner = nullptr;
    int* payload = nullptr;
};

struct Engine {
    Key key, other;
    Key* retainedKey = nullptr;
    const void* retainedOwner = nullptr;
    std::unique_ptr<int> retainedPayload;
    std::set<Key*> tracked;
    bool retainedValid = false;
    int notifications = 0;
    int nextPayload = 0;

    ~Engine() { delete key.payload; delete other.payload; }

    void Invalidate() { retainedValid = false; retainedPayload.reset(); }

    void ClearKey(Key& target) {
        if (!Scope::ConsumeExpectedClear(&target)) {
            ++notifications;
            if (retainedValid && retainedKey == &target) Invalidate();
        }
        delete target.payload;
        target = Key();
        tracked.erase(&target);
    }

    void Initialize(Key& target, const void* owner, int count) {
        if (retainedValid && retainedKey == &target && retainedOwner != owner) Invalidate();
        Scope reinitialization(&target, owner != nullptr && count > 0);
        ClearKey(target); // The original initializer always calls this first.
        if (owner && count > 0) {
            target.owner = owner;
            target.payload = new int(++nextPayload);
        }
        tracked.insert(&target);
    }

    void RetainAndZero(Key& target) {
        retainedKey = &target;
        retainedOwner = target.owner;
        retainedPayload.reset(target.payload);
        retainedValid = true;
        target = Key(); // Save transfers allocation ownership out of the key.
    }
};
}

int main() {
    int owner = 0, replacementOwner = 0;
    {
        Engine engine;
        engine.Initialize(engine.key, &owner, 1);
        engine.RetainAndZero(engine.key);
        const int baseline = *engine.retainedPayload;
        std::unique_ptr<int> rolling[3];
        for (int frame = 0; frame < 64; ++frame) {
            engine.Initialize(engine.key, &owner, 1);
            Require(engine.retainedValid, "same-owner recording keeps detached baseline valid");
            Require(engine.tracked.count(&engine.key) == 1, "clear/reinitialize preserves tracking");
            // Ring retirement frees its own detached buffer, not the baseline.
            rolling[frame % 3].reset(engine.key.payload);
            engine.key = Key();
        }
        Require(*engine.retainedPayload == baseline, "ring reuse leaves baseline payload unchanged");
        Require(engine.notifications == 0, "initializer cleanup is not owner destruction");
        engine.ClearKey(engine.key);
        Require(!engine.retainedValid, "real clear outside Initialize invalidates even a zeroed key");
        Require(engine.notifications == 1, "real zero-key clear reaches lifetime tracking");
    }
    {
        Engine engine;
        engine.Initialize(engine.key, &owner, 1);
        engine.RetainAndZero(engine.key);
        engine.Initialize(engine.key, &replacementOwner, 1);
        Require(!engine.retainedValid, "new owner invalidates before expected buffer replacement");
        Require(engine.notifications == 0, "owner mismatch does not depend on clear notification");
    }
    {
        Engine engine;
        engine.Initialize(engine.key, &owner, 1);
        engine.RetainAndZero(engine.key);
        Scope unrelated(&engine.other, true);
        engine.ClearKey(engine.key);
        Require(!engine.retainedValid, "another key's initialization cannot hide a real release");
        Require(Scope::ConsumeExpectedClear(&engine.other), "unrelated clear leaves expected allowance intact");
    }
    {
        Engine engine;
        engine.Initialize(engine.key, &owner, 1);
        engine.RetainAndZero(engine.key);
        Scope reinitialization(&engine.key, true);
        engine.ClearKey(engine.key);
        Require(engine.retainedValid, "first matching clear replaces the buffer");
        engine.ClearKey(engine.key);
        Require(!engine.retainedValid, "second matching clear remains a lifetime boundary");
    }
    for (int invalid = 0; invalid < 3; ++invalid) {
        Engine engine;
        engine.Initialize(engine.key, &owner, 1);
        engine.RetainAndZero(engine.key);
        engine.Initialize(engine.key, invalid == 0 ? nullptr : &owner,
            invalid == 0 ? 1 : invalid == 1 ? 0 : -1);
        Require(!engine.retainedValid, "invalid initialization cannot preserve a retained owner");
        Require(engine.notifications == 1, "invalid initialization does not suppress clear");
    }
    {
        Key a, b;
        Scope outer(&a, true);
        try {
            Scope inner(&b, true);
            Require(Scope::ConsumeExpectedClear(&b), "nested key receives its own expected clear");
            throw std::runtime_error("unwind");
        } catch (const std::runtime_error&) {}
        Require(!Scope::ConsumeExpectedClear(&b), "exception unwinds nested key context");
        Require(Scope::ConsumeExpectedClear(&a), "exception preserves outer key context");
        Require(!Scope::ConsumeExpectedClear(&a), "outer allowance is also single-use");
    }
    {
        Key key;
        Scope outer(&key, true);
        {
            Scope inner(&key, true);
            Require(Scope::ConsumeExpectedClear(&key), "recursive same-key scope has its own allowance");
            Require(!Scope::ConsumeExpectedClear(&key), "second inner clear cannot consume outer allowance");
        }
        Require(Scope::ConsumeExpectedClear(&key), "unwinding restores nearest same-key scope");
    }
    {
        Key key;
        Scope outer(&key, true);
        {
            Scope invalidInner(&key, false);
            Require(!Scope::ConsumeExpectedClear(&key), "invalid inner scope masks outer allowance");
        }
        Require(Scope::ConsumeExpectedClear(&key), "invalid inner scope leaves outer allowance intact");
    }
    {
        Key key;
        Scope recording(&key, true);
        bool consumedFromAnotherThread = true;
        std::thread otherThread([&] {
            consumedFromAnotherThread = Scope::ConsumeExpectedClear(&key);
        });
        otherThread.join();
        Require(!consumedFromAnotherThread, "another thread cannot suppress a real key release");
        Require(Scope::ConsumeExpectedClear(&key), "another thread cannot consume the recording allowance");
    }
    Require(!Scope::ConsumeExpectedClear(&owner), "no scopes remain after all lifetimes end");
    Scope nullKey(nullptr, true);
    Require(!Scope::ConsumeExpectedClear(nullptr), "null key is never a valid replacement");
}
