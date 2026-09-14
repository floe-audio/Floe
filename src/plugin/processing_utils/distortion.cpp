// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

#include "benchmarks/framework.hpp"
#include "distortion_measurement.hpp"

TEST_CASE(TestNormTableIsUpToDate) {
    constexpr usize k_bucket_step = 8;
    constexpr usize k_num_checked_buckets =
        (DistortionNormTable::k_num_buckets + k_bucket_step - 1) / k_bucket_step;
    Array<Array<f32, k_num_checked_buckets>, k_num_distortion_types> expected_gains {};
    ForEachDistortionTypeInParallel([&expected_gains](DistortionType type) {
        for (auto const checked_index : Range(k_num_checked_buckets)) {
            expected_gains[ToInt(type)][checked_index] = DbToAmp(-MeasureCalibrationLoudnessDb(
                {.type = type, .punish = 0, .compensate = false},
                BucketDrive(checked_index * k_bucket_step, DistortionNormTable::k_num_buckets)));
        }
    });

    for (auto const type_index : Range(k_num_distortion_types)) {
        CAPTURE(k_distortion_type_strings[type_index]);
        for (auto const checked_index : Range(k_num_checked_buckets)) {
            auto const bucket = checked_index * k_bucket_step;
            CAPTURE(bucket);
            auto const embedded_gain = k_distortion_norm_table.stage_gain[type_index][bucket];
            CHECK_LT(Abs(AmpToDb(expected_gains[type_index][checked_index]) - AmpToDb(embedded_gain)), 0.2f);
        }
    }
    return k_success;
}

TEST_CASE(TestCompensatedLevelIsUnityAtEveryDriveAndPunish) {
    struct PunishPoint {
        f32 punish;
        f32 tolerance_db;
    };
    static constexpr auto k_punish_points = Array {
        PunishPoint {0, 0.3f},
        PunishPoint {1, 0.3f},
        PunishPoint {0.2f, 0.8f}, // off-grid: interpolated
    };
    static constexpr auto k_drives = Array {0.0f, 0.5f, 1.0f};

    Array<Array<Array<f32, k_drives.size>, k_punish_points.size>, k_num_distortion_types> deviations_db {};
    ForEachDistortionTypeInParallel([&deviations_db](DistortionType type) {
        for (auto const punish_index : Range(k_punish_points.size)) {
            for (auto const drive_index : Range(k_drives.size)) {
                deviations_db[ToInt(type)][punish_index][drive_index] = MeasureCalibrationLoudnessDb(
                    {.type = type, .punish = k_punish_points[punish_index].punish, .compensate = true},
                    k_drives[drive_index]);
            }
        }
    });

    for (auto const punish_index : Range(k_punish_points.size)) {
        auto const point = k_punish_points[punish_index];
        for (auto const type_index : Range(k_num_distortion_types)) {
            CAPTURE(k_distortion_type_strings[type_index]);
            CAPTURE(point.punish);
            for (auto const drive_index : Range(k_drives.size)) {
                CAPTURE(k_drives[drive_index]);
                auto const type = (DistortionType)type_index;
                // Ring mod sidebands don't fit whole cycles in the window.
                auto const is_ring_mod =
                    type == DistortionType::LegacyRingMod || type == DistortionType::RingMod;
                auto const tolerance_db = is_ring_mod ? Max(point.tolerance_db, 1.0f) : point.tolerance_db;
                CHECK_LT(Abs(deviations_db[type_index][punish_index][drive_index]), tolerance_db);
            }
        }
    }
    return k_success;
}

// Assumes an integer number of cycles in the window.
static f64 DftMagnitude(Span<f32 const> signal, u32 bin) {
    f64 re = 0;
    f64 im = 0;
    for (auto const n : Range(signal.size)) {
        auto const angle = k_tau<f64> * (f64)bin * (f64)n / (f64)signal.size;
        re += (f64)signal[n] * Cos(angle);
        im -= (f64)signal[n] * Sin(angle);
    }
    return Sqrt((re * re) + (im * im)) * 2 / (f64)signal.size;
}

struct RenderedSine {
    static constexpr f32 k_sample_rate = 44100;
    static constexpr u32 k_window = 4410; // 10 Hz bins
    static constexpr u32 k_warmup = 2048;
    Array<f32, k_window> in;
    Array<f32, k_window> out;
};

static RenderedSine
RenderSine(DistortionDsp& dsp, f32 freq_hz, f32 amplitude, DistortionDsp::Controls controls) {
    RenderedSine result;
    f32 phase = 0;
    for (auto const frame_index : Range(RenderedSine::k_warmup + RenderedSine::k_window)) {
        auto const in = amplitude * Sin(phase);
        phase += k_tau<> * freq_hz / RenderedSine::k_sample_rate;
        if (phase > k_tau<>) phase -= k_tau<>;
        auto const out = dsp.Process(f32x2(in), controls).x;
        if (frame_index >= RenderedSine::k_warmup) {
            result.in[frame_index - RenderedSine::k_warmup] = in;
            result.out[frame_index - RenderedSine::k_warmup] = out;
        }
    }
    return result;
}

TEST_CASE(TestOversamplingSuppressesAliases) {
    RenderedSine rendered;
    for (auto const type :
         Array {DistortionType::LegacyClip, DistortionType::Overdrive, DistortionType::Valve}) {
        CAPTURE((int)type);
        DistortionDsp dsp;
        dsp.SetSettings({.type = type, .compensate = false});
        dsp.SetSampleRate(RenderedSine::k_sample_rate);
        rendered = RenderSine(dsp, 5000, 0.25f, {.drive01 = 1.0f});

        // A 5 kHz sine's 25 kHz and 35 kHz harmonics fold down to 19.1 kHz and 9.1 kHz.
        auto const fundamental = DftMagnitude(rendered.out, 500);
        auto const alias_from_5th_db = 20 * Log10(DftMagnitude(rendered.out, 1910) / fundamental);
        auto const alias_from_7th_db = 20 * Log10(DftMagnitude(rendered.out, 910) / fundamental);
        tester.log.Debug("type {} 4x: alias from 5th {.1} dB, from 7th {.1} dB",
                         (int)type,
                         alias_from_5th_db,
                         alias_from_7th_db);
        CHECK_GT(fundamental, 0.5);
        CHECK_LT(alias_from_5th_db, -35.0); // 25 kHz is in the downsampler's transition band
        CHECK_LT(alias_from_7th_db, -55.0);
    }

    DistortionShaper shaper;
    Array<f32, RenderedSine::k_window> base_rate_out;
    for (auto const n : Range(RenderedSine::k_window))
        base_rate_out[n] = shaper
                               .Shape(f32x2(rendered.in[n]),
                                      DistortionType::LegacyClip,
                                      1.0f,
                                      DistortionInputGain(DistortionType::LegacyClip, 1),
                                      0)
                               .x;
    auto const base_rate_alias_db =
        20 * Log10(DftMagnitude(base_rate_out, 1910) / DftMagnitude(base_rate_out, 500));
    tester.log.Debug("1x: alias from 5th {.1} dB", base_rate_alias_db);
    CHECK_GT(base_rate_alias_db, -20.0);
    return k_success;
}

TEST_CASE(TestLogWrightOmega) {
    for (f32 u = -20; u < 5000; u += u < 20 ? 0.037f : 7.3f) {
        CAPTURE(u);
        auto const v = DiodeCurve::LogWrightOmega(f32x2(u)).x;
        auto const residual = Abs(Exp(v) + v - u);
        CHECK_LT(residual, 1e-4f * Max(1.0f, Abs(u)));
    }
    for (auto const type : Array {DistortionType::Valve, DistortionType::Overdrive}) {
        CAPTURE((int)type);
        DistortionShaper shaper;
        CHECK_EQ(shaper.Shape(f32x2(0), type, 1.0f, 1, 0).x, 0.0f);
    }
    return k_success;
}

TEST_CASE(TestDcIsBlocked) {
    for (auto const type : Array {DistortionType::Octave, DistortionType::Valve}) {
        CAPTURE((int)type);
        DistortionDsp dsp;
        dsp.SetSettings({.type = type});
        dsp.SetSampleRate(RenderedSine::k_sample_rate);
        constexpr f32 k_drive = 0.3f;
        auto const rendered = RenderSine(dsp, 220, 0.25f, {.drive01 = k_drive});

        f64 sum = 0;
        for (auto const v : rendered.out)
            sum += (f64)v;
        auto const mean = Abs(sum / RenderedSine::k_window);

        DistortionShaper shaper;
        f64 raw_sum = 0;
        for (auto const v : rendered.in)
            raw_sum += (f64)shaper.Shape(f32x2(v), type, k_drive, DistortionInputGain(type, k_drive), 0).x;
        auto const raw_mean = Abs(raw_sum / RenderedSine::k_window);
        tester.log.Debug("type {} raw DC {.4}, blocked DC {.5}", (int)type, raw_mean, mean);
        CHECK_GT(raw_mean, 0.01);
        CHECK_LT(mean, 0.005);
    }
    return k_success;
}

TEST_CASE(TestBitcrushHoldsUnlikeLegacyDecimate) {
    // Reads decimate_y rather than the output: saturated Tanh would hide distinct held values.
    auto count_value_changes = [](DistortionType type) -> u32 {
        DistortionShaper shaper;
        f32 phase = 0;
        f32 last = shaper.decimate_y.x;
        u32 changes = 0;
        for (auto n = 0u; n < 2000u; ++n) {
            auto const in = 0.5f * Sin(phase);
            phase += 0.15f;
            shaper.Shape(f32x2(in), type, 1.0f, DistortionInputGain(type, 1.0f), 0);
            if (shaper.decimate_y.x != last) ++changes;
            last = shaper.decimate_y.x;
        }
        return changes;
    };

    auto const decimate_changes = count_value_changes(DistortionType::LegacyDecimate);
    auto const bitcrush_changes = count_value_changes(DistortionType::Bitcrush);
    tester.log.Debug("Decimate value changes {}, Bitcrush value changes {}",
                     decimate_changes,
                     bitcrush_changes);

    CHECK_GT(decimate_changes, 1900u);
    CHECK_LT(bitcrush_changes, 50u);
    return k_success;
}

TEST_CASE(TestRingModTracksSampleRateUnlikeLegacy) {
    auto phase_after_one_step = [](DistortionType type, f32 sample_rate) -> f32 {
        DistortionShaper shaper;
        shaper.SetSampleRate(sample_rate);
        shaper.Shape(f32x2(0), type, 1.0f, DistortionInputGain(type, 1.0f), 0);
        return shaper.ring_phase;
    };

    constexpr f32 k_freq_hz = 250; // modulator frequency at full drive
    auto const legacy_phase = phase_after_one_step(DistortionType::LegacyRingMod, 48000);
    auto const synced_phase = phase_after_one_step(DistortionType::RingMod, 48000);
    auto const legacy_expected = k_freq_hz * k_tau<> / 44100.0f;
    auto const synced_expected = k_freq_hz * k_tau<> / 48000.0f;
    tester.log.Debug("legacy phase step {}, synced phase step {}", legacy_phase, synced_phase);
    CHECK_LT(Abs(legacy_phase - legacy_expected), 0.0001f);
    CHECK_LT(Abs(synced_phase - synced_expected), 0.0001f);
    return k_success;
}

TEST_CASE(TestHardClipReachesQuietSignalsUnlikeLegacy) {
    auto third_harmonic_db = [](DistortionType type, f32 amplitude, f32 drive) {
        DistortionDsp dsp;
        dsp.SetSettings({.type = type, .compensate = false});
        dsp.SetSampleRate(RenderedSine::k_sample_rate);
        auto const rendered = RenderSine(dsp, 220, amplitude, {.drive01 = drive});
        return 20 * Log10(DftMagnitude(rendered.out, 66) / DftMagnitude(rendered.out, 22));
    };

    constexpr f32 k_quiet_amplitude = 0.01f; // -40 dBFS
    auto const legacy_full = third_harmonic_db(DistortionType::LegacyClip, k_quiet_amplitude, 1);
    auto const modern_half = third_harmonic_db(DistortionType::HardClip, k_quiet_amplitude, 0.5f);
    auto const modern_full = third_harmonic_db(DistortionType::HardClip, k_quiet_amplitude, 1);
    auto const modern_zero = third_harmonic_db(DistortionType::HardClip, 0.5f, 0);
    tester.log.Debug("quiet sine 3rd harmonic: legacy full {.1} dB, hard clip half {.1} dB, full {.1} dB",
                     legacy_full,
                     modern_half,
                     modern_full);
    CHECK_LT(legacy_full, -80.0);
    CHECK_LT(modern_zero, -80.0);
    CHECK_LT(modern_half, -80.0);
    CHECK_GT(modern_full, -15.0); // a square wave's 3rd harmonic sits at -9.5 dB
    return k_success;
}

TEST_CASE(TestHardClipAdaaAliasesLessAndMatchesInsideKnee) {
    auto alias_db = [](DistortionType type) {
        DistortionShaper shaper;
        Array<f32, RenderedSine::k_window> out;
        f32 phase = 0;
        auto const gain = DistortionInputGain(DistortionType::LegacyClip, 1);
        for (auto const n : Range(RenderedSine::k_warmup + RenderedSine::k_window)) {
            auto const in = 0.25f * Sin(phase);
            phase += k_tau<> * 5000 / RenderedSine::k_sample_rate;
            if (phase > k_tau<>) phase -= k_tau<>;
            auto const shaped = shaper.Shape(f32x2(in), type, 1.0f, gain, 0).x;
            if (n >= RenderedSine::k_warmup) out[n - RenderedSine::k_warmup] = shaped;
        }
        return 20 * Log10(DftMagnitude(out, 1910) / DftMagnitude(out, 500));
    };
    auto const plain = alias_db(DistortionType::LegacyClip);
    auto const adaa = alias_db(DistortionType::HardClip);
    tester.log.Debug("1x alias from 5th: plain clip {.1} dB, anti-derivative clip {.1} dB", plain, adaa);
    CHECK_LT(adaa, plain - 3);

    // Inside the knee the average along the path is the midpoint.
    DistortionShaper shaper;
    f32 prev = 0;
    for (auto const n : Range(200u)) {
        auto const in = 0.9f * Sin((f32)n * 0.05f);
        auto const shaped = shaper.Shape(f32x2(in), DistortionType::HardClip, 1.0f, 1, 0).x;
        if (n > 0) CHECK_LT(Abs(shaped - ((in + prev) * 0.5f)), 1e-5f);
        prev = in;
    }
    return k_success;
}

TEST_CASE(TestSineFoldKeepsFundamentalUnlikeLegacy) {
    auto render = [](DistortionType type, f32 amplitude, f32 drive) {
        DistortionDsp dsp;
        dsp.SetSettings({.type = type, .compensate = false});
        dsp.SetSampleRate(RenderedSine::k_sample_rate);
        return RenderSine(dsp, 220, amplitude, {.drive01 = drive});
    };

    auto const zero_drive = render(DistortionType::SineFold, DistortionDsp::k_reference_peak_amplitude, 0);
    CHECK_LT(20 * Log10(DftMagnitude(zero_drive.out, 66) / DftMagnitude(zero_drive.out, 22)), -50.0);

    auto const legacy_hot = DftMagnitude(render(DistortionType::LegacySine, 1, 1).out, 22);
    auto const modern_hot = DftMagnitude(render(DistortionType::SineFold, 1, 1).out, 22);
    tester.log.Debug("hot sine fundamental: legacy {.3}, sine fold {.3}", legacy_hot, modern_hot);
    CHECK_LT(legacy_hot, 0.3);
    CHECK_GT(modern_hot, 0.8);
    return k_success;
}

TEST_CASE(TestSineFoldAdaaAliasesLessAndMatchesWhenSlow) {
    auto plain_sine_fold = [](f32 input) { return SineFoldCurve::Value(f32x2(input)).x; };

    // Low enough that a 5 kHz sine moves less than one fold period per sample.
    auto const gain = DistortionInputGain(DistortionType::SineFold, 0.3f);
    DistortionShaper shaper;
    Array<f32, RenderedSine::k_window> adaa_out;
    Array<f32, RenderedSine::k_window> plain_out;
    f32 phase = 0;
    for (auto const n : Range(RenderedSine::k_warmup + RenderedSine::k_window)) {
        auto const in = 0.25f * Sin(phase);
        phase += k_tau<> * 5000 / RenderedSine::k_sample_rate;
        if (phase > k_tau<>) phase -= k_tau<>;
        auto const adaa = shaper.Shape(f32x2(in), DistortionType::SineFold, 1.0f, gain, 0).x;
        if (n >= RenderedSine::k_warmup) {
            adaa_out[n - RenderedSine::k_warmup] = adaa;
            plain_out[n - RenderedSine::k_warmup] = plain_sine_fold(in * gain);
        }
    }
    auto alias_db = [](Span<f32 const> out) {
        return 20 * Log10(DftMagnitude(out, 1910) / DftMagnitude(out, 500));
    };
    auto const plain = alias_db(plain_out);
    auto const adaa = alias_db(adaa_out);
    tester.log.Debug("1x alias from 5th: plain sine fold {.1} dB, anti-derivative {.1} dB", plain, adaa);
    CHECK_LT(adaa, plain - 3);

    // Slow enough that the average along the path is the midpoint value.
    shaper.Reset();
    f32 prev = 0;
    for (auto const n : Range(400u)) {
        auto const in = 10.0f * Sin((f32)n * 0.01f);
        auto const shaped = shaper.Shape(f32x2(in), DistortionType::SineFold, 1.0f, 1, 0).x;
        if (n > 0) CHECK_LT(Abs(shaped - plain_sine_fold((in + prev) * 0.5f)), 1e-3f);
        prev = in;
    }
    return k_success;
}

struct LevelTracking {
    f32 worst_boost_db; // the most any input level comes out louder than it went in
    f32 inversion_db; // how far the loudest input falls short of the loudest output
};
static LevelTracking MeasureLevelTracking(DistortionType type, f32 drive) {
    f32 worst_boost_db = -1000;
    f32 loudest_out_db = -1000;
    f32 out_db_at_loudest_input = 0;
    for (auto const level_db : Array {-45.0f, -35.0f, -25.0f, -15.0f, -6.0f, 0.0f}) {
        auto dsp = MakeMeasurementDsp({.type = type, .punish = 0, .compensate = true});
        auto const change_db = MeasureLoudnessChangeDb(dsp, drive, DbToAmp(level_db));
        worst_boost_db = Max(worst_boost_db, change_db);
        out_db_at_loudest_input = level_db + change_db;
        loudest_out_db = Max(loudest_out_db, out_db_at_loudest_input);
    }
    return {worst_boost_db, loudest_out_db - out_db_at_loudest_input};
}

TEST_CASE(TestWavefolderLevelTracksInputUnlikeLegacyFoldback) {
    for (auto const drive : Array {0.1f, 0.5f, 1.0f}) {
        CAPTURE(drive);
        auto const legacy = MeasureLevelTracking(DistortionType::LegacyFoldback, drive);
        auto const wavefolder = MeasureLevelTracking(DistortionType::Wavefolder, drive);
        auto const hard_clip = MeasureLevelTracking(DistortionType::HardClip, drive);
        tester.log.Debug(
            "drive {.2}: boost/inversion legacy {.1}/{.1}, wavefolder {.1}/{.1}, hard clip {.1}/{.1} dB",
            drive,
            legacy.worst_boost_db,
            legacy.inversion_db,
            wavefolder.worst_boost_db,
            wavefolder.inversion_db,
            hard_clip.worst_boost_db,
            hard_clip.inversion_db);
        CHECK_GT(legacy.inversion_db, 4.0f);
        CHECK_LT(wavefolder.inversion_db, 2.0f);
        CHECK_LT(wavefolder.worst_boost_db, hard_clip.worst_boost_db + 3);
    }

    // The legacy curve is worst at low drive, where its pre-gain moves fastest.
    auto const legacy_low = MeasureLevelTracking(DistortionType::LegacyFoldback, 0.1f);
    auto const wavefolder_low = MeasureLevelTracking(DistortionType::Wavefolder, 0.1f);
    CHECK_GT(legacy_low.worst_boost_db, 5.0f);
    CHECK_LT(wavefolder_low.worst_boost_db, 3.0f);
    return k_success;
}

TEST_CASE(TestOctaveLevelTracksInputUnlikeLegacyRectifier) {
    // The legacy curve only inverts in the top half of the drive.
    for (auto const drive : Array {0.75f, 1.0f}) {
        CAPTURE(drive);
        auto const legacy = MeasureLevelTracking(DistortionType::LegacyRectifier, drive);
        auto const octave = MeasureLevelTracking(DistortionType::Octave, drive);
        auto const hard_clip = MeasureLevelTracking(DistortionType::HardClip, drive);
        tester.log.Debug(
            "drive {.2}: boost/inversion legacy {.1}/{.1}, octave {.1}/{.1}, hard clip {.1}/{.1} dB",
            drive,
            legacy.worst_boost_db,
            legacy.inversion_db,
            octave.worst_boost_db,
            octave.inversion_db,
            hard_clip.worst_boost_db,
            hard_clip.inversion_db);
        CHECK_GT(legacy.inversion_db, 4.0f);
        CHECK_LT(octave.inversion_db, 1.0f);
        CHECK_LT(octave.worst_boost_db, hard_clip.worst_boost_db + 2);
    }
    return k_success;
}

// Energy in the non-harmonic bins relative to the fundamental, for a 5 kHz sine at 44.1 kHz.
static f64 AliasEnergyDb(Span<f32 const> signal) {
    auto const fundamental = DftMagnitude(signal, 500);
    f64 alias = 0;
    for (u32 bin = 10; bin < RenderedSine::k_window / 2; bin += 10) {
        if (bin % 500 == 0) continue;
        auto const magnitude = DftMagnitude(signal, bin);
        alias += magnitude * magnitude;
    }
    return 10 * Log10(alias / (fundamental * fundamental));
}

TEST_CASE(TestWavefolderAdaaAliasesLessAndMatchesInsideFirstSegment) {
    auto plain_wavefolder = [](f32 input) { return WavefolderCurve::Value(f32x2(input)).x; };

    for (auto const drive : Array {0.3f, 0.6f, 1.0f}) {
        CAPTURE(drive);
        auto const gain = DistortionInputGain(DistortionType::Wavefolder, drive);
        DistortionShaper shaper;
        Array<f32, RenderedSine::k_window> adaa_out;
        Array<f32, RenderedSine::k_window> plain_out;
        f32 phase = 0;
        for (auto const n : Range(RenderedSine::k_warmup + RenderedSine::k_window)) {
            auto const in = DistortionDsp::k_reference_peak_amplitude * Sin(phase);
            phase += k_tau<> * 5000 / RenderedSine::k_sample_rate;
            if (phase > k_tau<>) phase -= k_tau<>;
            auto const adaa = shaper.Shape(f32x2(in), DistortionType::Wavefolder, drive, gain, 0).x;
            if (n >= RenderedSine::k_warmup) {
                adaa_out[n - RenderedSine::k_warmup] = adaa;
                plain_out[n - RenderedSine::k_warmup] = plain_wavefolder(in * gain);
            }
        }
        auto const plain = AliasEnergyDb(plain_out);
        auto const adaa = AliasEnergyDb(adaa_out);
        DistortionDsp dsp;
        dsp.SetSettings({.type = DistortionType::Wavefolder, .compensate = false});
        dsp.SetSampleRate(RenderedSine::k_sample_rate);
        auto const oversampled = AliasEnergyDb(
            RenderSine(dsp, 5000, DistortionDsp::k_reference_peak_amplitude, {.drive01 = drive}).out);
        tester.log.Debug(
            "alias energy at drive {.2}: 1x plain {.1} dB, 1x anti-derivative {.1} dB, full path {.1} dB",
            drive,
            plain,
            adaa,
            oversampled);
        // ADAA is a faithful anti-aliased version of the pointwise curve, so it never aliases worse.
        CHECK_LT(adaa, plain);

        // First-order ADAA falls back to the pointwise midpoint when the signal barely moves between
        // samples, so it stops suppressing aliasing where the sine peak grazes the top fold corner
        // (input peak ~= WavefolderCurve::k_limit): the sharpest feature then sits exactly where the
        // signal is slowest. Away from that alignment the suppression is large. The shipping
        // oversampled path stays clean either way, which is the guarantee that matters.
        auto const peak_grazes_fold_limit =
            Abs((DistortionDsp::k_reference_peak_amplitude * gain) - WavefolderCurve::k_limit) < 0.5f;
        if (!peak_grazes_fold_limit) CHECK_LT(adaa, plain - 6);
        CHECK_LT(oversampled, -20.0);
    }

    // Inside the first segment the average along the path is the midpoint.
    DistortionShaper shaper;
    f32 prev = 0;
    for (auto const n : Range(400u)) {
        auto const in = 0.9f * Sin((f32)n * 0.1f);
        auto const shaped = shaper.Shape(f32x2(in), DistortionType::Wavefolder, 1.0f, 1, 0).x;
        if (n > 0) CHECK_LT(Abs(shaped - ((in + prev) * 0.5f)), 1e-4f);
        prev = in;
    }
    return k_success;
}

TEST_CASE(TestDiodeAdaaAliasesLessAndMatchesPathAverage) {
    auto pointwise = [](DistortionType type, f32 input, f32 gain) {
        return DiodeCurve::ForType(type).Shape(f32x2(input * gain)).x;
    };

    auto const gain = DistortionInputGain(DistortionType::Overdrive, 0.6f);
    DistortionShaper shaper;
    Array<f32, RenderedSine::k_window> adaa_out;
    Array<f32, RenderedSine::k_window> plain_out;
    f32 phase = 0;
    for (auto const n : Range(RenderedSine::k_warmup + RenderedSine::k_window)) {
        auto const in = 0.25f * Sin(phase);
        phase += k_tau<> * 5000 / RenderedSine::k_sample_rate;
        if (phase > k_tau<>) phase -= k_tau<>;
        auto const adaa = shaper.Shape(f32x2(in), DistortionType::Overdrive, 1.0f, gain, 0).x;
        if (n >= RenderedSine::k_warmup) {
            adaa_out[n - RenderedSine::k_warmup] = adaa;
            plain_out[n - RenderedSine::k_warmup] = pointwise(DistortionType::Overdrive, in, gain);
        }
    }
    auto const plain = AliasEnergyDb(plain_out);
    auto const adaa = AliasEnergyDb(adaa_out);
    tester.log.Debug("1x alias energy: plain diode {.1} dB, anti-derivative {.1} dB", plain, adaa);
    CHECK_LT(adaa, plain - 6);

    auto average_along_path = [&](DistortionType type, f32 from, f32 to) {
        constexpr u32 k_steps = 64;
        f64 sum = 0;
        for (auto const step : Range(k_steps + 1)) {
            auto const weight = (step == 0 || step == k_steps) ? 0.5 : 1.0;
            auto const x = from + ((to - from) * (f32)step / k_steps);
            sum += weight * (f64)pointwise(type, x, 1);
        }
        return sum / k_steps;
    };
    for (auto const type : Array {DistortionType::Overdrive, DistortionType::Valve}) {
        CAPTURE((int)type);
        for (auto const amplitude : Array {2.0f, 200.0f}) {
            CAPTURE(amplitude);
            shaper.Reset();
            f32 prev = 0;
            for (auto const n : Range(400u)) {
                auto const in = amplitude * Sin((f32)n * 0.05f);
                auto const shaped = shaper.Shape(f32x2(in), type, 1.0f, 1, 0).x;
                if (n > 0) CHECK_LT(Abs((f64)shaped - average_along_path(type, prev, in)), 2e-3);
                prev = in;
            }
        }
    }
    return k_success;
}

TEST_CASE(TestHotInputProducesNoNan) {
    DistortionDsp dsp;
    dsp.SetSettings({.type = DistortionType::Valve});
    dsp.SetSampleRate(RenderedSine::k_sample_rate);
    auto const rendered = RenderSine(dsp, 220, 2.0f, {.drive01 = 1, .punish01 = 1}); // +6 dBFS
    for (auto const v : rendered.out) {
        CHECK(!__builtin_isnan(v));
        CHECK(Abs(v) < 20);
    }
    return k_success;
}

TEST_CASE(TestEmphasisFilterPairCancels) {
    for (auto const type : Array {DistortionType::Tape, DistortionType::Overdrive}) {
        CAPTURE((int)type);
        DistortionDsp dsp;
        dsp.SetSettings({.type = type});
        dsp.SetSampleRate(RenderedSine::k_sample_rate);
        CHECK(dsp.emphasis_filter_active);
        auto const oversampled_rate = RenderedSine::k_sample_rate * (f32)Oversampler4x::k_factor;
        f32 max_emphasis_db = 0;
        for (auto const freq : Array {100.0f, 400.0f, 750.0f, 1500.0f, 3000.0f, 8000.0f, 16000.0f}) {
            CAPTURE(freq);
            auto const pre_db = rbj_filter::MagnitudeDb(dsp.emphasis_pre_coeffs, freq, oversampled_rate);
            auto const post_db = rbj_filter::MagnitudeDb(dsp.emphasis_post_coeffs, freq, oversampled_rate);
            CHECK_LT(Abs(pre_db + post_db), 0.01f);
            max_emphasis_db = Max(max_emphasis_db, Abs(pre_db));
        }
        CHECK_GT(max_emphasis_db, 6.0f);
    }
    return k_success;
}

TEST_CASE(TestAutoGainFadeAvoidsLevelStep) {
    constexpr f32 k_sample_rate = 44100;
    constexpr f32 k_sine_hz = 50;
    constexpr u32 k_period_frames = (u32)(k_sample_rate / k_sine_hz);
    // The switch lands on the sine's peak so the step is the level jump itself.
    constexpr u32 k_settle_frames = (5 * k_period_frames) + (k_period_frames / 4);
    constexpr u32 k_fade_frames = 441;
    constexpr f32 k_drive = 0.2f;

    struct LargestSteps {
        f32 during_switch;
        f32 steady;
    };
    auto const largest_steps = [&](bool fade) {
        DistortionDsp dsp;
        dsp.SetSettings({.type = DistortionType::LegacyTubeLog});
        dsp.SetSampleRate(k_sample_rate);
        LargestSteps steps {};
        f32 phase = 0;
        f32 prev_out = 0;
        for (auto const n : Range(3 * k_settle_frames)) {
            auto const in = DistortionDsp::k_reference_peak_amplitude * Sin(phase);
            phase += k_tau<> * k_sine_hz / k_sample_rate;
            if (phase > k_tau<>) phase -= k_tau<>;

            auto const switching = n >= k_settle_frames && n < (k_settle_frames + k_fade_frames);
            auto const auto_gain = n < k_settle_frames   ? 0.0f
                                   : (switching && fade) ? (f32)(n - k_settle_frames) / (f32)k_fade_frames
                                                         : 1.0f;
            auto const out = dsp.Process(f32x2(in), {.drive01 = k_drive, .auto_gain01 = auto_gain}).x;
            auto const step = Abs(out - prev_out);
            prev_out = out;

            if (n < k_settle_frames / 2) continue;
            auto const settling_after_switch =
                n >= k_settle_frames && n < (k_settle_frames + k_fade_frames + 64);
            if (settling_after_switch)
                steps.during_switch = Max(steps.during_switch, step);
            else
                steps.steady = Max(steps.steady, step);
        }
        return steps;
    };

    auto const hard = largest_steps(false);
    auto const faded = largest_steps(true);
    tester.log.Debug("largest step: steady {.4}, hard switch {.4}, faded switch {.4}",
                     hard.steady,
                     hard.during_switch,
                     faded.during_switch);
    CHECK_GT(hard.during_switch, hard.steady * 5);
    CHECK_LT(faded.during_switch, faded.steady * 1.5f);
    return k_success;
}

TEST_CASE(TestDryDelayMatchesWetLatency) {
    DistortionDsp dsp;
    dsp.SetSettings({.type = DistortionType::LegacyClip, .compensate = false});
    dsp.SetSampleRate(RenderedSine::k_sample_rate);

    // Same ring-buffer scheme the distortion effect uses to align its dry signal.
    constexpr u32 k_delay_size = NextPowerOf2(DistortionDsp::k_latency_base_samples + 1);
    Array<f32, k_delay_size> delay_buffer {};
    u32 delay_pos = 0;
    auto const delay_dry = [&](f32 in) {
        delay_buffer[delay_pos & (k_delay_size - 1)] = in;
        auto const delayed =
            delay_buffer[(delay_pos - DistortionDsp::k_latency_base_samples) & (k_delay_size - 1)];
        ++delay_pos;
        return delayed;
    };

    Array<f32, 64> impulse_response {};
    for (auto const n : Range(64u)) {
        auto const in = n == 0 ? 0.5f : 0.0f;
        auto const dry = delay_dry(in);
        impulse_response[n] = dsp.Process(f32x2(in), {.drive01 = 0}).x;
        CHECK_EQ(dry, n == DistortionDsp::k_latency_base_samples ? 0.5f : 0.0f);
    }
    usize peak_index = 0;
    for (auto const n : Range(64u))
        if (Abs(impulse_response[n]) > Abs(impulse_response[peak_index])) peak_index = n;
    CHECK_EQ(peak_index, (usize)DistortionDsp::k_latency_base_samples);

    dsp.Reset();
    delay_buffer = {};
    delay_pos = 0;
    f32 phase = 0;
    f64 mix_sum_sq = 0;
    f64 in_sum_sq = 0;
    for (auto const n : Range(RenderedSine::k_warmup + RenderedSine::k_window)) {
        auto const in = 0.25f * Sin(phase);
        phase += k_tau<> * 10000 / RenderedSine::k_sample_rate;
        if (phase > k_tau<>) phase -= k_tau<>;
        auto const dry = delay_dry(in);
        auto const wet = dsp.Process(f32x2(in), {.drive01 = 0}).x;
        if (n >= RenderedSine::k_warmup) {
            auto const mixed = (0.5f * dry) + (0.5f * wet);
            mix_sum_sq += (f64)mixed * (f64)mixed;
            in_sum_sq += (f64)in * (f64)in;
        }
    }
    CHECK_LT(Abs(10 * Log10(mix_sum_sq / in_sum_sq)), 0.5);
    return k_success;
}

TEST_CASE(TestOversamplerPassesBandAndSuppressesImages) {
    Oversampler4x oversampler;
    constexpr u32 k_window = 4410;
    constexpr u32 k_warmup = 512;
    Array<f32, k_window * Oversampler4x::k_factor> upsampled;
    Array<f32, k_window> round_trip;

    f32 phase = 0;
    for (auto const n : Range(k_warmup + k_window)) {
        auto const in = Sin(phase);
        phase += k_tau<> * 10000 / 44100.0f;
        if (phase > k_tau<>) phase -= k_tau<>;
        f32x2 up[Oversampler4x::k_factor];
        oversampler.Upsample(f32x2(in), up);
        auto const down = oversampler.Downsample(up).x;
        if (n >= k_warmup) {
            for (auto const i : Range(Oversampler4x::k_factor))
                upsampled[((n - k_warmup) * Oversampler4x::k_factor) + i] = up[i].x;
            round_trip[n - k_warmup] = down;
        }
    }

    // 10 Hz bins at the 4x rate too; the images sit at 34.1, 78.2 and 98.2 kHz.
    auto const fundamental = DftMagnitude(upsampled, 1000);
    CHECK_LT(Abs(20 * Log10(fundamental)), 0.2);
    for (auto const image_bin : Array {3410u, 7820u, 9820u}) {
        CAPTURE(image_bin);
        CHECK_LT(20 * Log10(DftMagnitude(upsampled, image_bin) / fundamental), -55.0);
    }
    CHECK_LT(Abs(20 * Log10(DftMagnitude(round_trip, 1000))), 0.3);
    return k_success;
}

TEST_REGISTRATION(RegisterDistortionTests) {
    REGISTER_TEST(TestNormTableIsUpToDate);
    REGISTER_TEST(TestCompensatedLevelIsUnityAtEveryDriveAndPunish);
    REGISTER_TEST(TestOversamplingSuppressesAliases);
    REGISTER_TEST(TestBitcrushHoldsUnlikeLegacyDecimate);
    REGISTER_TEST(TestRingModTracksSampleRateUnlikeLegacy);
    REGISTER_TEST(TestHardClipReachesQuietSignalsUnlikeLegacy);
    REGISTER_TEST(TestHardClipAdaaAliasesLessAndMatchesInsideKnee);
    REGISTER_TEST(TestSineFoldKeepsFundamentalUnlikeLegacy);
    REGISTER_TEST(TestSineFoldAdaaAliasesLessAndMatchesWhenSlow);
    REGISTER_TEST(TestWavefolderLevelTracksInputUnlikeLegacyFoldback);
    REGISTER_TEST(TestWavefolderAdaaAliasesLessAndMatchesInsideFirstSegment);
    REGISTER_TEST(TestOctaveLevelTracksInputUnlikeLegacyRectifier);
    REGISTER_TEST(TestLogWrightOmega);
    REGISTER_TEST(TestDcIsBlocked);
    REGISTER_TEST(TestDiodeAdaaAliasesLessAndMatchesPathAverage);
    REGISTER_TEST(TestHotInputProducesNoNan);
    REGISTER_TEST(TestEmphasisFilterPairCancels);
    REGISTER_TEST(TestAutoGainFadeAvoidsLevelStep);
    REGISTER_TEST(TestDryDelayMatchesWetLatency);
    REGISTER_TEST(TestOversamplerPassesBandAndSuppressesImages);
}

// ======================================================================================
// Benchmarks

// The full wet path for one type at a fixed drive, single stage (no punish), so each run isolates that
// type's shaper cost rather than the shared Tape cascade the punish stages fall back to.
BENCHMARK_FN void BenchmarkDistortionType(DistortionType type) {
    constexpr u32 k_sample_rate = 44100;
    constexpr u32 k_block_frames = 512;

    // A couple of detuned partials so the ADAA and fold curves see a realistically varying slope.
    Array<f32x2, k_block_frames> input;
    for (auto const frame_index : Range(k_block_frames)) {
        auto const t = (f32)frame_index / (f32)k_sample_rate;
        auto const sample = 0.5f * (Sin(k_two_pi<f32> * 220 * t) + (0.3f * Sin(k_two_pi<f32> * 557 * t)));
        input[frame_index] = f32x2 {sample, sample * 0.9f};
    }

    DistortionDsp dsp;
    dsp.SetSampleRate(k_sample_rate);
    dsp.SetSettings({.type = type});

    constexpr DistortionDsp::Controls k_controls {.drive01 = 0.7f, .punish01 = 0};

    constexpr int k_num_iterations = 1000;
    for (auto _ : Range(k_num_iterations)) {
        for (auto const frame_index : Range(k_block_frames)) {
            auto out = dsp.Process(input[frame_index], k_controls);
            benchmarks::DoNotOptimise(out);
        }
    }
}

BENCHMARK_REGISTRATION(RegisterDistortionBenchmarks) {
#define BENCHMARK_DISTORTION_TYPE(name, enumerator)                                                          \
    REGISTER_BENCHMARK_NAMED([]() { BenchmarkDistortionType(DistortionType::enumerator); },                  \
                             "BenchmarkDistortion" name)

    BENCHMARK_DISTORTION_TYPE("Tape", Tape);
    BENCHMARK_DISTORTION_TYPE("Valve", Valve);
    BENCHMARK_DISTORTION_TYPE("Overdrive", Overdrive);
    BENCHMARK_DISTORTION_TYPE("HardClip", HardClip);
    BENCHMARK_DISTORTION_TYPE("Octave", Octave);
    BENCHMARK_DISTORTION_TYPE("Bitcrush", Bitcrush);
    BENCHMARK_DISTORTION_TYPE("RingMod", RingMod);
    BENCHMARK_DISTORTION_TYPE("SineFold", SineFold);
    BENCHMARK_DISTORTION_TYPE("Warp", Warp);
    BENCHMARK_DISTORTION_TYPE("Wavefolder", Wavefolder);

    BENCHMARK_DISTORTION_TYPE("LegacyTubeLog", LegacyTubeLog);
    BENCHMARK_DISTORTION_TYPE("LegacyTubeAsym3", LegacyTubeAsym3);
    BENCHMARK_DISTORTION_TYPE("LegacySine", LegacySine);
    BENCHMARK_DISTORTION_TYPE("LegacyRaph1", LegacyRaph1);
    BENCHMARK_DISTORTION_TYPE("LegacyDecimate", LegacyDecimate);
    BENCHMARK_DISTORTION_TYPE("LegacyAtan", LegacyAtan);
    BENCHMARK_DISTORTION_TYPE("LegacyClip", LegacyClip);
    BENCHMARK_DISTORTION_TYPE("LegacyFoldback", LegacyFoldback);
    BENCHMARK_DISTORTION_TYPE("LegacyRectifier", LegacyRectifier);
    BENCHMARK_DISTORTION_TYPE("LegacyRingMod", LegacyRingMod);

#undef BENCHMARK_DISTORTION_TYPE
}
