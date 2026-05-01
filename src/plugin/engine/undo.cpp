// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "undo.hpp"

#include "tests/framework.hpp"

UndoStepRing::UndoStepRing(Allocator& a, u32 cap) : allocator(a), capacity(cap) {
    auto const span = allocator.AllocateExactSizeUninitialised<UndoableStep>(capacity);
    items = span.data;
}

UndoStepRing::~UndoStepRing() {
    if (items) allocator.Free(Span<u8> {(u8*)items, capacity * sizeof(UndoableStep)});
}

void UndoStepRing::PushEvictOldest(UndoableStep const& entry) {
    if (size == capacity) {
        head = (head + 1) % capacity;
        --size;
    }
    items[(head + size) % capacity] = entry;
    ++size;
}

UndoableStep UndoStepRing::PopTop() {
    ASSERT(size);
    auto const result = items[(head + size - 1) % capacity];
    --size;
    return result;
}

UndoableStep const& UndoStepRing::Top() const {
    ASSERT(size);
    return items[(head + size - 1) % capacity];
}

UndoableStep& UndoStepRing::At(u32 i) {
    ASSERT(i < size);
    return items[(head + i) % capacity];
}

UndoableStep const& UndoStepRing::At(u32 i) const {
    ASSERT(i < size);
    return items[(head + i) % capacity];
}

void UndoStepRing::Clear() {
    head = 0;
    size = 0;
}

bool UndoHistory::CanUndo() const { return undo.Size() > 1; }
bool UndoHistory::CanRedo() const { return redo.Size(); }

Optional<String> UndoHistory::NextUndoName() const {
    if (!CanUndo()) return k_nullopt;
    return String {undo.Top().name};
}

Optional<String> UndoHistory::NextRedoName() const {
    if (!CanRedo()) return k_nullopt;
    return String {redo.Top().name};
}

void UndoHistory::Clear() {
    undo.Clear();
    redo.Clear();
}

void UndoHistory::Record(UndoableStep const& step) {
    if (undo.Size() && undo.Top().snapshot == step.snapshot) return;
    undo.PushEvictOldest(step);
    redo.Clear();
}

UndoableStep UndoHistory::Undo() {
    ASSERT(CanUndo());
    auto const popped = undo.PopTop();
    redo.PushEvictOldest(popped);
    return undo.Top();
}

UndoableStep UndoHistory::Redo() {
    ASSERT(CanRedo());
    auto const popped = redo.PopTop();
    undo.PushEvictOldest(popped);
    return popped;
}

TEST_CASE(TestUndoStepRing) {
    UndoStepRing ring {tester.scratch_arena, 4};

    REQUIRE(!ring.Size());
    REQUIRE_EQ(ring.Size(), 0u);

    auto make = [](char c) {
        UndoableStep e {};
        dyn::AssignFitInCapacity(e.name, String {&c, 1});
        return e;
    };

    SUBCASE("push/pop LIFO") {
        ring.PushEvictOldest(make('a'));
        ring.PushEvictOldest(make('b'));
        ring.PushEvictOldest(make('c'));
        REQUIRE_EQ(ring.Size(), 3u);
        REQUIRE_EQ(String {ring.Top().name}, "c"_s);

        auto top = ring.PopTop();
        REQUIRE_EQ(String {top.name}, "c"_s);
        REQUIRE_EQ(String {ring.Top().name}, "b"_s);

        REQUIRE_EQ(String {ring.PopTop().name}, "b"_s);
        REQUIRE_EQ(String {ring.PopTop().name}, "a"_s);
        REQUIRE(!ring.Size());
    }

    SUBCASE("eviction at cap drops oldest") {
        ring.PushEvictOldest(make('a'));
        ring.PushEvictOldest(make('b'));
        ring.PushEvictOldest(make('c'));
        ring.PushEvictOldest(make('d'));
        REQUIRE_EQ(ring.Size(), 4u);
        REQUIRE_EQ(String {ring.At(0).name}, "a"_s);

        ring.PushEvictOldest(make('e'));
        REQUIRE_EQ(ring.Size(), 4u);
        REQUIRE_EQ(String {ring.At(0).name}, "b"_s);
        REQUIRE_EQ(String {ring.At(3).name}, "e"_s);
        REQUIRE_EQ(String {ring.Top().name}, "e"_s);

        ring.PushEvictOldest(make('f'));
        REQUIRE_EQ(String {ring.At(0).name}, "c"_s);
        REQUIRE_EQ(String {ring.Top().name}, "f"_s);
    }

    SUBCASE("clear") {
        ring.PushEvictOldest(make('a'));
        ring.PushEvictOldest(make('b'));
        ring.Clear();
        REQUIRE(!ring.Size());

        ring.PushEvictOldest(make('x'));
        REQUIRE_EQ(String {ring.At(0).name}, "x"_s);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterUndoHistoryTests) { REGISTER_TEST(TestUndoStepRing); }
