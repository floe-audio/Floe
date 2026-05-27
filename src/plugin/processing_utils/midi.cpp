// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "midi.hpp"

Optional<RpnDetector::Rpn> RpnDetector::DetectRpnFromCcMessage(MidiMessage msg) {
    ASSERT_EQ(msg.Type(), MidiMessageType::ControlChange);

    auto const cc_num = msg.CCNum();
    auto const cc_value = msg.CCValue();
    switch (state) {
        case State::ExpectingFirstParamNum: {
            if (cc_num == k_midi_cc_rpn_lsb) {
                param_num_lsb = cc_value;
                state = State::ExpectingParamNumMsb;
            } else if (cc_num == k_midi_cc_rpn_msb) {
                param_num_msb = cc_value;
                state = State::ExpectingParamNumLsb;
            }
            break;
        }
        case State::ExpectingParamNumLsb: {
            if (cc_num == k_midi_cc_rpn_lsb) {
                param_num_lsb = cc_value;
                state = State::ExpectingParamValueLsbOrMsb;
            } else {
                state = State::ExpectingFirstParamNum;
            }
            break;
        }
        case State::ExpectingParamNumMsb: {
            if (cc_num == k_midi_cc_rpn_msb) {
                param_num_msb = cc_value;
                state = State::ExpectingParamValueLsbOrMsb;
            } else {
                state = State::ExpectingFirstParamNum;
            }
            break;
        }
        case State::ExpectingParamValueLsbOrMsb: {
            if (cc_num == k_midi_cc_data_entry_lsb) {
                param_val_lsb = cc_value;
                state = State::ExpectingParamValueMsb;
            } else if (cc_num == k_midi_cc_data_entry_msb) {
                param_val_msb = cc_value;
                return Rpn {
                    .param_num = ((u14)param_num_msb << 7) | (u14)param_num_lsb,
                    .param_val = param_val_msb,
                    .param_val_is_7_bit = true,
                };
            }
            break;
        }
        case State::ExpectingParamValueMsb: {
            if (cc_num == k_midi_cc_data_entry_msb) {
                param_val_msb = cc_value;
                return Rpn {
                    .param_num = ((u14)param_num_msb << 7) | (u14)param_num_lsb,
                    .param_val = ((u14)param_val_msb << 7) | (u14)param_val_lsb,
                    .param_val_is_7_bit = false,
                };
            }
            state = State::ExpectingFirstParamNum;
            break;
        }
    }

    return k_nullopt;
}
