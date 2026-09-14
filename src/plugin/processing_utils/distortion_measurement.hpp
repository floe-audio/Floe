// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "os/misc.hpp"
#include "os/threading.hpp"

#include "common_infrastructure/audio_utils.hpp"

#include "distortion.hpp"

// ITU-R BS.1770 K-weighting at 48 kHz.
struct KWeighting {
    static constexpr f32 k_sample_rate = 48000;

    f32 Process(f32 x) {
        static constexpr rbj_filter::Coeffs k_shelf {
            .b0 = 1.53512485958697f,
            .b1 = -2.69169618940638f,
            .b2 = 1.19839281085285f,
            .a1 = -1.69065929318241f,
            .a2 = 0.73248077421585f,
        };
        static constexpr rbj_filter::Coeffs k_high_pass {
            .b0 = 1.0f,
            .b1 = -2.0f,
            .b2 = 1.0f,
            .a1 = -1.99004745483398f,
            .a2 = 0.99007225036621f,
        };
        return rbj_filter::Process(high_pass, k_high_pass, rbj_filter::Process(shelf, k_shelf, x));
    }

    rbj_filter::Data shelf {};
    rbj_filter::Data high_pass {};
};

struct ReferenceSine {
    static constexpr u32 k_warmup_frames = 3072; // outlasts the 10 Hz DC blockers settling (~764 samples)
    static constexpr u32 k_measure_frames = 2048;
    static constexpr u32 k_cycles = 10; // whole cycles in the window: exact RMS, fundamental on one DFT bin
    static constexpr f32 k_freq = KWeighting::k_sample_rate * k_cycles / k_measure_frames;

    f32 Next() {
        auto const value = amplitude * Sin(phase);
        phase += k_tau<> * k_freq / KWeighting::k_sample_rate;
        if (phase > k_tau<>) phase -= k_tau<>;
        return value;
    }

    f32 amplitude = DistortionDsp::k_reference_peak_amplitude;
    f32 phase = 0;
};

// Partials with no shared period. Dwells near zero like real material, where a shaper that collapses when
// hot is still wide open. Its peak, not RMS, is set to the amplitude.
struct ReferenceDenseTone {
    static constexpr auto k_cycles = Array {10u, 23u, 41u, 67u};

    ReferenceDenseTone(f32 amplitude) {
        f32 peak = 0;
        for (auto const period_index : Range(ReferenceSine::k_measure_frames))
            peak = Max(peak, Fabs(Sum((u32)period_index)));
        scale = amplitude / peak;
    }

    f32 Next() {
        auto const value = scale * Sum(frame_index);
        ++frame_index;
        return value;
    }

    static f32 Sum(u32 frame_index) {
        f32 sum = 0;
        for (auto const partial_index : Range(k_cycles.size)) {
            auto const phase = k_tau<> * (f32)k_cycles[partial_index] *
                               (f32)(frame_index % ReferenceSine::k_measure_frames) /
                               (f32)ReferenceSine::k_measure_frames;
            sum += Sin(phase + (f32)partial_index);
        }
        return sum;
    }

    f32 scale;
    u32 frame_index = 0;
};

struct KWeightedMeanSquares {
    f64 in;
    f64 out;
};

template <typename Signal>
KWeightedMeanSquares
MeasureKWeightedMeanSquares(DistortionDsp& dsp, DistortionDsp::Controls controls, Signal signal) {
    KWeighting in_weighting;
    KWeighting out_weighting;
    KWeightedMeanSquares result {};
    for (auto const frame_index : Range(ReferenceSine::k_warmup_frames + ReferenceSine::k_measure_frames)) {
        auto const in = signal.Next();
        auto const out = dsp.Process(f32x2(in), controls).x;
        auto const in_weighted = in_weighting.Process(in);
        auto const out_weighted = out_weighting.Process(out);
        if (frame_index >= ReferenceSine::k_warmup_frames) {
            result.in += (f64)in_weighted * (f64)in_weighted;
            result.out += (f64)out_weighted * (f64)out_weighted;
        }
    }
    return result;
}

inline f32 MeasureLoudnessChangeDb(DistortionDsp& dsp,
                                   f32 drive01,
                                   f32 amplitude = DistortionDsp::k_reference_peak_amplitude) {
    auto const mean_squares =
        MeasureKWeightedMeanSquares(dsp, {.drive01 = drive01}, ReferenceSine {.amplitude = amplitude});
    if (mean_squares.out < 1e-12) return 0;
    return (f32)(10 * Log10(mean_squares.out / mean_squares.in));
}

struct CalibrationPoint {
    DistortionType type;
    f32 punish;
    bool compensate;
    DistortionNormTable const* norm_table = nullptr;
};

inline DistortionDsp MakeMeasurementDsp(CalibrationPoint point) {
    DistortionDsp dsp;
    dsp.SetSettings({.type = point.type, .compensate = point.compensate});
    dsp.SetSampleRate(KWeighting::k_sample_rate);
    if (point.norm_table) dsp.norm_table = point.norm_table;
    return dsp;
}

// Loudest output of any probe at any level up to the reference, relative to the reference sine's input.
// For a shaper that collapses when hot, the reference sine is near its quietest case.
inline f32 MeasureCalibrationLoudnessDb(CalibrationPoint point, f32 drive01) {
    f64 reference_in = 0;
    f64 loudest_out = 0;
    for (auto const level_db : Array {0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f}) {
        auto const amplitude = DistortionDsp::k_reference_peak_amplitude * DbToAmp(level_db);
        auto const measure = [&](auto signal) {
            auto dsp = MakeMeasurementDsp(point);
            return MeasureKWeightedMeanSquares(dsp, {.drive01 = drive01, .punish01 = point.punish}, signal);
        };
        auto const sine = measure(ReferenceSine {.amplitude = amplitude});
        if (level_db == 0) reference_in = sine.in;
        loudest_out = Max(loudest_out, sine.out, measure(ReferenceDenseTone {amplitude}).out);
    }
    if (loudest_out < 1e-12) return 0;
    return (f32)(10 * Log10(loudest_out / reference_in));
}

inline f32 BucketDrive(usize bucket, usize num_buckets) {
    return DistortionNormTable::DriveFromBucketPosition((f32)bucket / (f32)(num_buckets - 1));
}

template <typename Function>
void ForEachDistortionTypeInParallel(Function&& function) {
    Atomic<u32> next_type {0};
    auto const run_worker = [&next_type, &function]() {
        while (true) {
            auto const type_index = next_type.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
            if (type_index >= k_num_distortion_types) return;
            function((DistortionType)type_index);
        }
    };

    auto const num_threads = Min((u32)k_num_distortion_types, Max(1u, CachedSystemStats().num_logical_cpus));

    Array<Thread, k_num_distortion_types - 1> threads {};
    for (auto const thread_index : Range(num_threads - 1))
        threads[thread_index].Start(run_worker, "dist-table-gen");

    run_worker();

    for (auto const thread_index : Range(num_threads - 1))
        threads[thread_index].Join();
}

inline DistortionNormTable ComputeDistortionNormTable() {
    DistortionNormTable table {};
    ForEachDistortionTypeInParallel([&table](DistortionType type) {
        auto const type_index = ToInt(type);
        for (auto const bucket : Range((usize)DistortionNormTable::k_num_buckets)) {
            table.stage_gain[type_index][bucket] = DbToAmp(
                -MeasureCalibrationLoudnessDb({.type = type, .punish = 0, .compensate = false},
                                              BucketDrive(bucket, DistortionNormTable::k_num_buckets)));
        }
    });

    // A separate table so a residual filled in earlier never bleeds into a later one through interpolation.
    auto measurement_table = table;
    for (auto& surface : measurement_table.punish_gain)
        for (auto& row : surface)
            for (auto& gain : row)
                gain = 1;

    ForEachDistortionTypeInParallel([&table, &measurement_table](DistortionType type) {
        auto const type_index = ToInt(type);

        for (auto const punish_bucket : Range((usize)DistortionNormTable::k_num_punish_buckets)) {
            auto const punish = (f32)punish_bucket / (f32)(DistortionNormTable::k_num_punish_buckets - 1);
            for (auto const drive_bucket : Range((usize)DistortionNormTable::k_num_punish_drive_buckets)) {
                auto const drive = BucketDrive(drive_bucket, DistortionNormTable::k_num_punish_drive_buckets);
                table.punish_gain[type_index][punish_bucket][drive_bucket] =
                    DbToAmp(-MeasureCalibrationLoudnessDb({.type = type,
                                                           .punish = punish,
                                                           .compensate = true,
                                                           .norm_table = &measurement_table},
                                                          drive));
            }
        }
    });
    return table;
}
