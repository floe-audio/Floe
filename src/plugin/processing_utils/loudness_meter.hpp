// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

// EBU R128 loudness meter.
class LufsMeter {
  public:
    // The EBU R128 absolute silence gate: values quieter than this (including true silence, which libebur128
    // reports as -HUGE_VAL) are clamped here rather than shown as -inf.
    static constexpr f32 k_silence_floor_lufs = -70.0f;

    struct Snapshot {
        f32 momentary_lufs = k_silence_floor_lufs;
        f32 short_term_lufs = k_silence_floor_lufs;
    };

    ~LufsMeter();

    void PrepareToPlay(f32 sample_rate);

    // [audio-thread]
    void AddBuffer(Span<f32x2> frames);

    // Discards the accumulated loudness history, e.g. when resuming metering after it was paused: without
    // this, stale momentary/short-term values would otherwise be reported until enough new audio has been
    // processed to overwrite them.
    // [audio-thread]
    void Reset();

    // [audio-thread]
    void Zero() { m_snapshot.Store({}, StoreMemoryOrder::Relaxed); }

    // thread-safe
    Snapshot GetSnapshot() const { return m_snapshot.Load(LoadMemoryOrder::Relaxed); }

  private:
    void InitEbur128();

    void* m_state = nullptr; // ebur128_state*
    f32 m_sample_rate = 0;
    u32 m_query_interval_frames = 0;
    u32 m_frames_since_last_query = 0;

    Atomic<Snapshot> m_snapshot {};
};
