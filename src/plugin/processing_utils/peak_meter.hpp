// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

#include "processing_utils/stereo_audio_frame.hpp"

struct StereoPeakMeter {
    static constexpr f32 k_hold_ms = 3000.0f;
    static constexpr f32 k_hold_decay_db_per_second = 60.0f;

    struct Snapshot {
        f32x2 levels {};
        f32x2 hold_levels {};
    };

    // not thread-safe
    void PrepareToPlay(f32 sample_rate) {
        constexpr f32 k_falldown_rate_ms = 500.0f;
        m_falldown_divisor = sample_rate * (k_falldown_rate_ms / 1000.0f);

        constexpr f32 k_clipping_detection_window_ms = 500.0f;
        m_clipping_detection_start_counter = (u32)(sample_rate * (k_clipping_detection_window_ms / 1000.0f));

        m_hold_frames = sample_rate * (k_hold_ms / 1000.0f);
        m_hold_decay_multiplier = Pow(10.0f, -k_hold_decay_db_per_second / (20.0f * sample_rate));

        Zero();
    }

    // not thread-safe
    void Zero() {
        m_levels = {};
        m_smoothed_levels = {};
        m_prev_levels = {};
        m_hold_levels = {};
        m_hold_frames_remaining = {};
        m_clipping_detection_counter = {};
        m_clipping_detection_counter_atomic.Store(0, StoreMemoryOrder::Relaxed);
        m_levels_snapshot.Store({}, StoreMemoryOrder::Relaxed);
        m_hold_snapshot.Store({}, StoreMemoryOrder::Relaxed);
    }

    // not thread-safe
    void AddBuffer(Span<f32x2> frames) {
        for (auto const i : Range(frames.size)) {
            auto const frame = frames[i];
            auto const abs_f = Abs(frame);

            auto const is_new_peak = abs_f > m_levels;

            m_levels = is_new_peak ? abs_f : Max<f32x2>(0.0f, m_levels - m_falldown_steps);
            m_falldown_steps = is_new_peak ? (abs_f / m_falldown_divisor) : m_falldown_steps;

            auto const is_new_hold = abs_f >= m_hold_levels;
            m_hold_frames_remaining =
                is_new_hold ? f32x2(m_hold_frames) : Max<f32x2>(0.0f, m_hold_frames_remaining - 1.0f);
            m_hold_levels =
                is_new_hold
                    ? abs_f
                    : (m_hold_frames_remaining > 0 ? m_hold_levels : m_hold_levels * m_hold_decay_multiplier);

            if (Any(abs_f > 1))
                m_clipping_detection_counter = m_clipping_detection_start_counter;
            else if (m_clipping_detection_counter != 0)
                --m_clipping_detection_counter;

            m_smoothed_levels = SmoothOutput(m_levels, m_prev_levels);
        }

        m_levels_snapshot.Store({m_smoothed_levels}, StoreMemoryOrder::Relaxed);
        m_hold_snapshot.Store({m_hold_levels}, StoreMemoryOrder::Relaxed);
        m_clipping_detection_counter_atomic.Store(m_clipping_detection_counter, StoreMemoryOrder::Relaxed);
    }

    // not thread-safe
    bool Silent() const { return All(m_levels == 0); }

    // thread-safe
    Snapshot GetSnapshot() const {
        return {
            .levels = m_levels_snapshot.Load(LoadMemoryOrder::Relaxed).v,
            .hold_levels = m_hold_snapshot.Load(LoadMemoryOrder::Relaxed).v,
        };
    }

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

    // A whole Snapshot is too big for a lock-free atomic, so the two halves are published separately;
    // tearing between them is harmless for a meter.
    struct AtomicLevels {
        f32x2 v {};
    };
    static_assert(__atomic_always_lock_free(sizeof(AtomicLevels), nullptr));

    f32x2 m_falldown_steps {};
    f32x2 m_levels {};
    f32x2 m_smoothed_levels {};
    f32x2 m_prev_levels {};
    f32x2 m_hold_levels {};
    f32x2 m_hold_frames_remaining {};
    f32 m_falldown_divisor {};
    f32 m_hold_frames {};
    f32 m_hold_decay_multiplier {};
    u32 m_clipping_detection_start_counter {};
    u32 m_clipping_detection_counter {};

    Atomic<u32> m_clipping_detection_counter_atomic {};
    Atomic<AtomicLevels> m_levels_snapshot {};
    Atomic<AtomicLevels> m_hold_snapshot {};
};
