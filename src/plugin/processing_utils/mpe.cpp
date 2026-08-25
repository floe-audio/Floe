// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mpe.hpp"

#include "tests/framework.hpp"

static void SendRpn(MpeState& mpe, u4 channel, u14 param_num, u7 value_msb) {
    auto const cc = [&](u7 cc_num, u7 cc_value) {
        MidiMessage message {};
        message.SetTypeAndChannelNum(MidiMessageType::ControlChange, channel);
        message.SetCCNum(cc_num);
        message.SetCCValue(cc_value);
        if (auto const rpn = mpe.rpn_detectors[channel].DetectRpnFromCcMessage(message))
            mpe.HandleRpn(channel, *rpn);
    };
    cc(RpnDetector::k_midi_cc_rpn_msb, (u7)(param_num >> 7));
    cc(RpnDetector::k_midi_cc_rpn_lsb, (u7)(param_num & 0x7f));
    cc(RpnDetector::k_midi_cc_data_entry_msb, value_msb);
}

TEST_CASE(TestMpeChannelClassification) {
    MpeState mpe {};

    // Disabled: nothing is classified as MPE.
    CHECK(!mpe.IsMasterChannel(0));
    CHECK(!mpe.IsMemberChannel(1));

    mpe.enabled = true;

    // Default layout: lower zone, master channel 0, members 1-15.
    CHECK(mpe.IsMasterChannel(0));
    CHECK(!mpe.IsMemberChannel(0));
    for (u8 channel = 1; channel <= 15; ++channel) {
        CAPTURE(channel);
        CHECK(mpe.IsMemberChannel((u4)channel));
        CHECK_EQ((int)*mpe.MasterChannelForMember((u4)channel), 0);
    }
    CHECK(!mpe.IsMasterChannel(15));
    CHECK_APPROX_EQ(mpe.PerNoteBendRangeSemitones(3), k_mpe_default_per_note_bend_range_semitones, 0.001f);
    CHECK_EQ(mpe.ZoneChannels(0).NumSet(), 16uz);

    return k_success;
}

TEST_CASE(TestMpeConfigurationMessage) {
    MpeState mpe {};
    mpe.enabled = true;

    // Configure the lower zone with 7 members.
    SendRpn(mpe, 0, 6, 7);
    CHECK(mpe.lower.active);
    CHECK_EQ(mpe.lower.num_member_channels, (u8)7);
    CHECK(mpe.IsMemberChannel(7));
    CHECK(!mpe.IsMemberChannel(8));

    // Configure the upper zone with 5 members: channels 10-14, master 15.
    SendRpn(mpe, 15, 6, 5);
    CHECK(mpe.upper.active);
    CHECK(mpe.IsMasterChannel(15));
    CHECK_EQ((int)*mpe.MasterChannelForMember(10), 15);
    CHECK(!mpe.IsMemberChannel(9));
    CHECK_EQ(mpe.ZoneChannels(15).NumSet(), 6uz);
    CHECK_EQ(mpe.AllZoneChannels().NumSet(), 14uz); // lower: 0-7, upper: 10-15

    // Growing the upper zone shrinks the lower zone to avoid overlap.
    SendRpn(mpe, 15, 6, 10);
    CHECK_EQ(mpe.lower.num_member_channels, (u8)4);
    CHECK_EQ((int)*mpe.MasterChannelForMember(5), 15);

    // A zone spanning everything deactivates the other.
    SendRpn(mpe, 0, 6, 15);
    CHECK(!mpe.upper.active);
    CHECK(mpe.IsMemberChannel(15));

    // Zero members deactivates the zone.
    SendRpn(mpe, 0, 6, 0);
    CHECK(!mpe.lower.active);
    CHECK(!mpe.IsMasterChannel(0));
    CHECK(!mpe.IsMemberChannel(1));

    // Configuration messages are only valid on master channels.
    SendRpn(mpe, 3, 6, 4);
    CHECK(!mpe.lower.active);
    CHECK(!mpe.upper.active);

    return k_success;
}

TEST_CASE(TestMpePitchBendSensitivity) {
    MpeState mpe {};
    mpe.enabled = true;

    // On a member channel: sets the zone's per-note bend range.
    SendRpn(mpe, 4, 0, 12);
    CHECK_APPROX_EQ(mpe.PerNoteBendRangeSemitones(9), 12.0f, 0.001f);

    // On the master channel: ignored (the per-layer Pitch Bend Range param governs master bend).
    SendRpn(mpe, 0, 0, 24);
    CHECK_APPROX_EQ(mpe.PerNoteBendRangeSemitones(9), 12.0f, 0.001f);

    // Reconfiguring the zone resets the per-note bend range to the default.
    SendRpn(mpe, 0, 6, 15);
    CHECK_APPROX_EQ(mpe.PerNoteBendRangeSemitones(9), k_mpe_default_per_note_bend_range_semitones, 0.001f);

    return k_success;
}

TEST_REGISTRATION(RegisterMpeTests) {
    REGISTER_TEST(TestMpeChannelClassification);
    REGISTER_TEST(TestMpeConfigurationMessage);
    REGISTER_TEST(TestMpePitchBendSensitivity);
}
