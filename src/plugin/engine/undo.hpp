// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/state/state_snapshot.hpp"

struct Engine;

constexpr u32 k_undo_max_entries = 200;
constexpr u32 k_redo_max_entries = 50;
constexpr usize k_undoable_step_name_max_size = 64;

struct UndoableStep {
    DynamicArrayBounded<char, k_undoable_step_name_max_size> name;
    StateSnapshot snapshot;

    // Marks a baseline-establishing event (preset load, DAW state load, default reset, save). When undo/redo
    // navigation lands on or crosses such an entry, the engine's pinned snapshot moves to the most recent
    // anchor at or before the current position. The anchor's own snapshot serves as the pinned snapshot —
    // no duplicate copy.
    bool is_pinned_snapshot_anchor = false;
};

static_assert(TriviallyCopyable<UndoableStep>);

struct UndoStepRing {
    NON_COPYABLE(UndoStepRing);

    UndoStepRing(Allocator& a, u32 capacity);
    ~UndoStepRing();

    void PushEvictOldest(UndoableStep const& entry);
    UndoableStep PopTop();
    UndoableStep const& Top() const;

    void Clear();
    u32 Size() const { return size; }

    UndoableStep& At(u32 i);
    UndoableStep const& At(u32 i) const;

    Allocator& allocator;
    UndoableStep* items {};
    u32 capacity {};
    u32 head {0};
    u32 size {0};
};

struct UndoHistory {
    UndoHistory(Allocator& a) : undo(a, k_undo_max_entries), redo(a, k_redo_max_entries) {}

    bool CanUndo() const;
    bool CanRedo() const;
    Optional<String> NextUndoName() const;
    Optional<String> NextRedoName() const;
    void Clear();

    UndoableStep Undo();
    UndoableStep Redo();
    void Record(UndoableStep const& step);

    UndoStepRing undo;
    UndoStepRing redo;
};
