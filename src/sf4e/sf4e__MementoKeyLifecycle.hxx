#pragma once

namespace sf4e {
namespace Game {

// The game's Initialize starts by clearing this key's previous memento buffer.
// That one clear does not destroy its owner. Other clears remain lifetime
// boundaries, including a second clear of the same key during initialization.
// Check owner replacement before entering this scope.
class MementoKeyReinitializationScope {
public:
    explicit MementoKeyReinitializationScope(const void* key, bool allowBufferReplacement) noexcept
        : key_(key), expectedClear_(key != nullptr && allowBufferReplacement), previous_(Head()) {
        Head() = this;
    }

    ~MementoKeyReinitializationScope() noexcept { Head() = previous_; }

    MementoKeyReinitializationScope(const MementoKeyReinitializationScope&) = delete;
    MementoKeyReinitializationScope& operator=(const MementoKeyReinitializationScope&) = delete;

    static bool ConsumeExpectedClear(const void* key) noexcept {
        if (!key) return false;
        for (auto* scope = Head(); scope; scope = scope->previous_) {
            if (scope->key_ != key) continue;
            const bool expected = scope->expectedClear_;
            scope->expectedClear_ = false;
            // Never consume an outer same-key scope when the inner scope has
            // already used its allowance, or rejected invalid arguments.
            return expected;
        }
        return false;
    }

private:
    static MementoKeyReinitializationScope*& Head() noexcept {
        static thread_local MementoKeyReinitializationScope* head = nullptr;
        return head;
    }

    const void* key_;
    bool expectedClear_;
    MementoKeyReinitializationScope* previous_;
};

} // namespace Game
} // namespace sf4e
