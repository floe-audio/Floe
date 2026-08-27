// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio_processing_context.hpp"

#include "tests/framework.hpp"

TEST_CASE(TestMidiNoteStateSustainPedal) {
    MidiNoteState state {};

    SUBCASE("pedal captures only its own channel's held notes") {
        state.NoteOn({.note = 60, .channel = 5}, 0.5f);
        state.HandleSustainPedalOn(0);
        state.NoteOff({.note = 60, .channel = 5});
        CHECK(!state.NotesHeldIncludingSustained(5).AnyValuesSet());
        CHECK(!state.NotesCurrentlyHeldAllChannels().AnyValuesSet());
    }

    SUBCASE("pedal-down on one channel preserves another channel's sustained notes") {
        state.NoteOn({.note = 60, .channel = 0}, 0.5f);
        state.HandleSustainPedalOn(0);
        state.NoteOff({.note = 60, .channel = 0});
        CHECK(state.NotesHeldIncludingSustained(0).Get(60));

        state.HandleSustainPedalOn(1);
        CHECK(state.NotesHeldIncludingSustained(0).Get(60));
        CHECK(state.HandleSustainPedalOff(0).Get(60));
    }

    SUBCASE("notes played while the pedal is down are sustained") {
        state.HandleSustainPedalOn(2);
        state.NoteOn({.note = 61, .channel = 2}, 0.5f);
        state.NoteOff({.note = 61, .channel = 2});
        CHECK(state.NotesHeldIncludingSustained(2).Get(61));
        CHECK(state.HandleSustainPedalOff(2).Get(61));
        CHECK(!state.NotesHeldIncludingSustained(2).AnyValuesSet());
    }

    SUBCASE("releasing the pedal on every channel leaves no sustain state") {
        // Mirrors the cleanup run when MPE is toggled: pedal state may be spread across all zone channels.
        for (auto const channel : Range(16u)) {
            state.HandleSustainPedalOn((u4)channel);
            state.NoteOn({.note = (u7)(20 + channel), .channel = (u4)channel}, 0.5f);
            state.NoteOff({.note = (u7)(20 + channel), .channel = (u4)channel});
        }
        for (auto const channel : Range(16u))
            CHECK(state.HandleSustainPedalOff((u4)channel).Get(20 + channel));
        CHECK(!state.sustain_pedal_on.AnyValuesSet());
        CHECK(!state.NotesCurrentlyHeldAllChannels().AnyValuesSet());
    }

    SUBCASE("all-notes-off clears held, sustained and pedal state") {
        state.NoteOn({.note = 40, .channel = 3}, 0.5f);
        state.HandleSustainPedalOn(3);
        state.NoteOn({.note = 41, .channel = 3}, 0.5f);
        state.NoteOff({.note = 41, .channel = 3});
        auto const ended = state.HandleAllNotesOff(3);
        CHECK(ended.Get(40));
        CHECK(ended.Get(41));
        CHECK(!state.sustain_pedal_on.Get(3));
        CHECK(!state.NotesHeldIncludingSustained(3).AnyValuesSet());
    }

    return k_success;
}

TEST_REGISTRATION(RegisterMidiNoteStateTests) { REGISTER_TEST(TestMidiNoteStateSustainPedal); }
