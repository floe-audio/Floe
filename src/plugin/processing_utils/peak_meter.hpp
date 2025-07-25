// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

#include "processing_utils/stereo_audio_frame.hpp"

struct StereoPeakMeter {
    struct Snapshot {
        f32x2 levels {};
    };

    // not thread-safe
    void PrepareToPlay(f32 sample_rate, ArenaAllocator&) {
        constexpr f32 k_falldown_rate_ms = 500.0f;
        m_falldown_divisor = sample_rate * (k_falldown_rate_ms / 1000.0f);

        constexpr f32 k_clipping_detection_window_ms = 500.0f;
        m_clipping_detection_start_counter = (u32)(sample_rate * (k_clipping_detection_window_ms / 1000.0f));

        Zero();
    }

    // not thread-safe
    void Zero() {
        m_levels = {};
        m_smoothed_levels = {};
        m_prev_levels = {};
        m_clipping_detection_counter = {};
        m_clipping_detection_counter_atomic.Store(0, StoreMemoryOrder::Relaxed);
        m_snapshot.Store({}, StoreMemoryOrder::Relaxed);
    }

    // not thread-safe
    void AddBuffer(Span<f32x2> frames) {
        for (auto const i : Range(frames.size)) {
            auto const frame = frames[i];
            auto const abs_f = Abs(frame);

            auto const is_new_peak = abs_f > m_levels;

            m_levels = is_new_peak ? abs_f : Max<f32x2>(0.0f, m_levels - m_falldown_steps);
            m_falldown_steps = is_new_peak ? (abs_f / m_falldown_divisor) : m_falldown_steps;

            if (Any(abs_f > 1))
                m_clipping_detection_counter = m_clipping_detection_start_counter;
            else if (m_clipping_detection_counter != 0)
                --m_clipping_detection_counter;

            m_smoothed_levels = SmoothOutput(m_levels, m_prev_levels);
        }

        m_snapshot.Store(
            Snapshot {
                .levels = m_smoothed_levels,
            },
            StoreMemoryOrder::Relaxed);
        m_clipping_detection_counter_atomic.Store(m_clipping_detection_counter, StoreMemoryOrder::Relaxed);
    }

    // not thread-safe
    bool Silent() const { return All(m_levels == 0); }

    // thread-safe
    Snapshot GetSnapshot() const { return m_snapshot.Load(LoadMemoryOrder::Relaxed); }

    bool DidClipRecently() const {
        return m_clipping_detection_counter_atomic.Load(LoadMemoryOrder::Relaxed) != 0;
    }

  private:
    static f32x2 SmoothOutput(f32x2 output, f32x2& prev_output) {
        static constexpr f32 k_smoothing_amount = 0.001f;
        auto const result = prev_output + (k_smoothing_amount * (output - prev_output));
        prev_output = result;
        return result;
    }

    f32x2 m_falldown_steps {};
    f32x2 m_levels {};
    f32x2 m_smoothed_levels {};
    f32x2 m_prev_levels {};
    f32 m_falldown_divisor {};
    u32 m_clipping_detection_start_counter {};
    u32 m_clipping_detection_counter {};

    Atomic<u32> m_clipping_detection_counter_atomic {};
    Atomic<Snapshot> m_snapshot {};
};
