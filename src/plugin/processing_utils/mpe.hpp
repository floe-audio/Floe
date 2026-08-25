// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "midi.hpp"

// MIDI Polyphonic Expression (MPE). When enabled, MIDI channels are grouped into zones: a master channel
// carries zone-wide messages while each sounding note gets its own member channel, allowing per-note pitch
// bend, channel pressure ('press') and CC74 ('slide').

constexpr f32 k_mpe_default_per_note_bend_range_semitones = 48;

struct MpeZone {
    bool active {};
    u8 num_member_channels {}; // 1-15
    f32 per_note_bend_range_semitones {k_mpe_default_per_note_bend_range_semitones};
};

struct MpeState {
    static constexpr u4 k_lower_master_channel = 0;
    static constexpr u4 k_upper_master_channel = 15;

    // A zone's master channel, if the channel is a member of an active zone. Returns nullopt when MPE is
    // disabled or the channel is a master channel or outside all zones.
    Optional<u4> MasterChannelForMember(u4 channel) const {
        if (!enabled) return k_nullopt;
        if (lower.active && channel >= 1 && channel <= lower.num_member_channels)
            return k_lower_master_channel;
        if (upper.active && channel <= 14 && channel >= 15 - upper.num_member_channels)
            return k_upper_master_channel;
        return k_nullopt;
    }

    bool IsMemberChannel(u4 channel) const { return MasterChannelForMember(channel).HasValue(); }

    bool IsMasterChannel(u4 channel) const {
        if (!enabled) return false;
        return (channel == k_lower_master_channel && lower.active) ||
               (channel == k_upper_master_channel && upper.active);
    }

    f32 PerNoteBendRangeSemitones(u4 member_channel) const {
        if (auto const master = MasterChannelForMember(member_channel)) {
            return *master == k_lower_master_channel ? lower.per_note_bend_range_semitones
                                                     : upper.per_note_bend_range_semitones;
        }
        return 0;
    }

    Bitset<16> AllZoneChannels() const {
        Bitset<16> result {};
        if (!enabled) return result;
        if (lower.active) result |= ZoneChannels(k_lower_master_channel);
        if (upper.active) result |= ZoneChannels(k_upper_master_channel);
        return result;
    }

    // A note's initial press/slide must be sent before its note-on, so reset at note-off to stop the next
    // note on this channel inheriting the previous note's values if none are sent.
    void ResetChannelExpression(u4 channel) {
        pressure_01[channel] = 0;
        slide_01[channel] = 0.5f;
    }

    // All channels of a zone, master included. Only valid for a channel where IsMasterChannel is true.
    Bitset<16> ZoneChannels(u4 master_channel) const {
        ASSERT(IsMasterChannel(master_channel));
        Bitset<16> result {};
        result.Set(master_channel);
        if (master_channel == k_lower_master_channel)
            for (auto const channel : Range(1u, 1u + lower.num_member_channels))
                result.Set(channel);
        else
            for (auto const channel : Range(15u - upper.num_member_channels, 15u))
                result.Set(channel);
        return result;
    }

    void HandleRpn(u4 channel, RpnDetector::Rpn const& rpn) {
        constexpr u14 k_rpn_pitch_bend_sensitivity = 0;
        constexpr u14 k_rpn_mpe_configuration = 6;

        auto const value_msb = (u7)(rpn.param_val_is_7_bit ? rpn.param_val : rpn.param_val >> 7);

        switch (rpn.param_num) {
            case k_rpn_mpe_configuration: {
                // Configuring a zone resets its per-note bend range and shrinks the other zone if the two
                // would overlap.
                auto const num_member_channels = (u8)Min<u7>(value_msb, 15);

                auto const configure = [num_member_channels](MpeZone& zone, MpeZone& other_zone) {
                    zone = {
                        .active = num_member_channels != 0,
                        .num_member_channels = num_member_channels,
                    };
                    if (other_zone.active) {
                        auto const max_other = 14 - (int)num_member_channels;
                        if ((int)other_zone.num_member_channels > max_other) {
                            if (max_other <= 0)
                                other_zone.active = false;
                            else
                                other_zone.num_member_channels = (u8)max_other;
                        }
                    }
                };

                if (channel == k_lower_master_channel)
                    configure(lower, upper);
                else if (channel == k_upper_master_channel)
                    configure(upper, lower);
                break;
            }
            case k_rpn_pitch_bend_sensitivity: {
                // On a member channel this sets the whole zone's per-note bend range. Master-channel bend
                // range is governed by the per-layer Pitch Bend Range parameter, so we ignore it here.
                if (auto const master = MasterChannelForMember(channel)) {
                    auto& zone = *master == k_lower_master_channel ? lower : upper;
                    zone.per_note_bend_range_semitones = (f32)Min<u7>(value_msb, 96);
                }
                break;
            }
            default: break;
        }
    }

    // Synced from the instance config at the start of each process call.
    bool enabled {};
    f32 press_slide_smoothing_ms {100};

    // Until an MPE Configuration Message says otherwise, default to a lower zone spanning all 15 member
    // channels - many hosts/controllers never forward the configuration message.
    MpeZone lower {.active = true, .num_member_channels = 15};
    MpeZone upper {};

    Array<RpnDetector, 16> rpn_detectors {};

    // Latest per-channel controller values, captured by voices when they start so that values sent just
    // before note-on (as the MPE spec requires of controllers) are picked up.
    Array<f32, 16> pressure_01 {};
    InitialisedArray<f32, 16> slide_01 {0.5f};
};
