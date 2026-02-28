// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

enum class MidiMessageType {
    None = 0,
    NoteOff = 8,
    NoteOn = 9,
    PolyAftertouch = 10,
    ControlChange = 11,
    ProgramChange = 12,
    ChannelAftertouch = 13,
    PitchWheel = 14,
    SystemMessage = 15,
};

constexpr u7 k_midi_learn_controller_numbers[] = {
    0,   1,   2,   3,   4,   5,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19, 20, 21,
    22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  64,  65,  66,  67,  68,  69,  70,  71,  72, 73, 74,
    75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93, 94, 95,
    102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
};

constexpr usize k_num_midi_learn_ccs = ArraySize(k_midi_learn_controller_numbers);

consteval Bitset<128> CreateMidiLearnControllerBitset() {
    Bitset<128> result;
    for (auto b : k_midi_learn_controller_numbers)
        result.Set(b);
    return result;
}

constexpr auto k_midi_learn_controller_bitset = CreateMidiLearnControllerBitset();

struct MidiChannelNote {
    constexpr bool operator==(MidiChannelNote const& other) const {
        return note == other.note && channel == other.channel;
    }
    u7 note;
    u4 channel;
};

struct MidiMessage {
    static constexpr u7 k_u7_max = LargestRepresentableValue<u7>();
    static constexpr u4 k_u4_max = LargestRepresentableValue<u4>();

    MidiMessageType Type() const {
        auto t = (MidiMessageType)(status >> 4);
        if (t == MidiMessageType::NoteOn && Velocity() == 0) t = MidiMessageType::NoteOff;
        return t;
    }
    u7 NoteNum() const { return data1 & k_u7_max; }
    u7 CCNum() const { return data1 & k_u7_max; }
    u7 Velocity() const { return data2 & k_u7_max; }
    u7 CCValue() const { return data2 & k_u7_max; }
    u7 PolyAftertouch() const { return data2 & k_u7_max; }
    u7 ChannelPressure() const { return data1 & k_u7_max; }
    u14 PitchBend() const { // 14 bit value, 0 to 16383. 8192 is centre
        return CheckedCast<u14>(data1 | (data2 << 7));
    }
    u4 ChannelNum() const { return 0xf & status; }

    MidiChannelNote ChannelNote() const { return {.note = NoteNum(), .channel = ChannelNum()}; }

    void SetNoteNum(u7 num) { data1 = num; }
    void SetVelocity(u7 velo) { data2 = velo; }
    void SetCCNum(u7 cc) { data1 = cc; }
    void SetCCValue(u7 val) { data2 = val; }
    void SetType(MidiMessageType type) { SetTypeAndChannelNum(type, ChannelNum()); }
    void SetTypeAndChannelNum(MidiMessageType type, u4 chan) { status = (u8)((int(type) << 4) | chan); }
    void SetChannelNum(u4 chan) {
        auto type = Type();
        SetTypeAndChannelNum(type, chan);
    }

    static MidiMessage NoteOn(u7 note, u7 velo, u4 channel = 0) {
        MidiMessage message {};
        message.SetTypeAndChannelNum(MidiMessageType::NoteOn, channel);
        message.SetVelocity(velo);
        message.SetNoteNum(note);
        return message;
    }

    static MidiMessage NoteOff(u7 note, u4 channel = 0) {
        MidiMessage message {};
        message.SetTypeAndChannelNum(MidiMessageType::NoteOff, channel);
        message.SetNoteNum(note);
        return message;
    }

    u8 status {};
    u8 data1 {};
    u8 data2 {};
};

// We copy JUCE's rules:
// "The detector uses the following parsing rules: the parameter number LSB/MSB can be sent/received in
// either order and must both come before the parameter value; for the parameter value, LSB always has to
// be sent/received before the value MSB, otherwise it will be treated as 7-bit (MSB only)."

struct RpnDetector {
    enum class State {
        ExpectingFirstParamNum,
        ExpectingParamNumLsb,
        ExpectingParamNumMsb,
        ExpectingParamValueLsbOrMsb,
        ExpectingParamValueMsb,
    };

    static constexpr u7 k_midi_cc_rpn_msb = 101;
    static constexpr u7 k_midi_cc_rpn_lsb = 100;
    static constexpr u7 k_midi_cc_data_entry_msb = 6;
    static constexpr u7 k_midi_cc_data_entry_lsb = 38;

    struct Rpn {
        u14 param_num;
        u14 param_val;
        bool param_val_is_7_bit;
    };

    Optional<Rpn> DetectRpnFromCcMessage(MidiMessage msg);

    State state {State::ExpectingFirstParamNum};
    u7 param_num_msb;
    u7 param_num_lsb;
    u7 param_val_msb;
    u7 param_val_lsb;
};
