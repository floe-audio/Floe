// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "os/threading.hpp"

#include "common_infrastructure/audio_utils.hpp"

#include "filters.hpp"
#include "peak_meter.hpp"
#include "true_peak_detector.hpp"

// Deliberately tiny to keep latency negligible.
constexpr f32 k_limiter_lookahead_ms = 0.5f;

constexpr f32 k_limiter_release_ms = 80.0f;

constexpr f32 k_limiter_param_smoothing_ms = 10.0f;

constexpr f32 k_limiter_gain_reduction_meter_falldown_db_per_second = 40.0f;

class SlidingWindowMin {
  public:
    ~SlidingWindowMin() { FreeBuffer(); }

    // window_size is inclusive of the current sample, i.e. Push() returns the minimum of the most
    // recent (window_size + 1) values pushed (the current one plus window_size prior values).
    void PrepareToPlay(u32 window_size) {
        FreeBuffer();
        m_window_size = window_size;
        // Just before a push, the deque can already hold up to (window_size + 1) live entries (the
        // invariant maintained after the previous push's front-eviction). The push itself needs a free
        // slot to write into before front-eviction runs again, so capacity must be (window_size + 2).
        m_buffer = PageAllocator::Instance().AllocateExactSizeUninitialised<Entry>(window_size + 2);
        Reset();
    }

    f32 Push(f32 value) {
        ASSERT_HOT(m_buffer.size > 0); // PrepareToPlay() must be called first
        auto const index = m_index;
        auto const capacity = (u32)m_buffer.size;

        while (m_count > 0) {
            auto const back_ring_index = (m_tail + capacity - 1) % capacity;
            if (m_buffer[back_ring_index].value < value) break;
            m_tail = back_ring_index;
            --m_count;
        }

        m_buffer[m_tail] = {value, index};
        m_tail = (m_tail + 1) % capacity;
        ++m_count;

        while (m_count > 0 && m_buffer[m_head].index + m_window_size < index) {
            m_head = (m_head + 1) % capacity;
            --m_count;
        }

        ++m_index;
        return m_buffer[m_head].value;
    }

    void Reset() {
        for (auto& e : m_buffer)
            e = {};
        m_head = 0;
        m_tail = 0;
        m_count = 0;
        m_index = 0;
    }

  private:
    struct Entry {
        f32 value;
        u64 index;
    };

    void FreeBuffer() {
        if (m_buffer.size) PageAllocator::Instance().Free(m_buffer.ToByteSpan());
        m_buffer = {};
    }

    u32 m_window_size = 0;
    Span<Entry> m_buffer {};
    u32 m_head = 0;
    u32 m_tail = 0;
    u32 m_count = 0;
    u64 m_index = 0;
};

// Feedforward lookahead true-peak brickwall limiter.
//
// Signal flow per sample:
//   1. Input gain.
//   2. True-peak estimation (TruePeakDetector) of the gained input, stereo-linked (max of the channels).
//   3. Required linear gain to keep that peak within the ceiling.
//   4. A sliding-window minimum of that required gain over the lookahead window.
//   5. An asymmetric envelope follower on that: applied as-is while decreasing, one-pole smoothed
//      while increasing (release).
//   6. A moving average of the same length as the lookahead window. Every downward step in the
//      released envelope therefore becomes a linear ramp over the lookahead window that reaches the
//      full reduction exactly as the peak arrives at the delayed output -- this is the "attack", which
//      is why there's no Attack control. The release must come before this average: if it came after,
//      a sustained reduction would sit below the average's ramp and only join it at the very end,
//      collapsing the attack into a one-sample step (audible as crackle under heavy limiting).
//   7. The gained input is delayed by the lookahead window plus the true-peak detector's group delay so
//      it stays time-aligned with that envelope.
//   8. A final hard clamp to the ceiling as a defensive backstop against true-peak estimation error and
//      float rounding.
//   9. Mix against the time-aligned (delayed) dry signal.
class LimiterDsp {
  public:
    struct ParamUpdate {
        Optional<f32> gain_db;
        Optional<f32> ceiling_db;
        Optional<f32> mix_01;
    };

    ~LimiterDsp() { FreeBuffers(); }

    void PrepareToPlay(f32 sample_rate) {
        ASSERT_HOT(sample_rate > 0);
        if (sample_rate == m_sample_rate) return;
        m_sample_rate = sample_rate;

        FreeBuffers();

        auto const lookahead_samples = Max(1u, (u32)Round(k_limiter_lookahead_ms * 0.001f * sample_rate));
        auto const delay_samples = lookahead_samples + TruePeakDetector::k_group_delay_samples;

        m_delay_buffer = PageAllocator::Instance().AllocateExactSizeUninitialised<f32x2>(delay_samples);
        m_attack_average_buffer =
            PageAllocator::Instance().AllocateExactSizeUninitialised<f32>(lookahead_samples);
        m_gain_window.PrepareToPlay(lookahead_samples);

        input_peak_meter.PrepareToPlay(sample_rate);
        output_peak_meter.PrepareToPlay(sample_rate);

        m_param_smoothing_cutoff01 =
            OnePoleLowPassFilter<f32>::MsToCutoff(k_limiter_param_smoothing_ms, sample_rate);
        m_release_cutoff01 = OnePoleLowPassFilter<f32>::MsToCutoff(k_limiter_release_ms, sample_rate);
        m_gain_reduction_meter_falldown_db_per_sample =
            k_limiter_gain_reduction_meter_falldown_db_per_second / sample_rate;

        Reset();
    }

    u32 LatencySamples() const { return (u32)m_delay_buffer.size; }

    // audio-thread: the gain envelope applied to the most recently processed sample (1 = no reduction).
    f32 CurrentGain() const { return m_prev_env; }

    // audio-thread: the fully-limited signal from the most recently processed sample, before the Mix
    // blend back towards dry. Metering this rather than Process()'s return value keeps the output meter
    // (and its ceiling marker) meaningful regardless of the Mix setting.
    f32x2 CurrentWet() const { return m_prev_wet; }

    void OnParamChange(ParamUpdate const& update) {
        if (update.gain_db) m_gain_lin = DbToAmp(*update.gain_db);
        if (update.ceiling_db) m_ceiling_lin = DbToAmp(*update.ceiling_db);
        if (update.mix_01) m_mix_01 = *update.mix_01;
    }

    f32x2 Process(f32x2 in) {
        ASSERT_HOT(m_delay_buffer.size > 0); // PrepareToPlay() must be called before Process()

        auto const smoothed_gain_lin = m_gain_smoother.LowPass(m_gain_lin, m_param_smoothing_cutoff01);
        auto const smoothed_ceiling_lin =
            m_ceiling_smoother.LowPass(m_ceiling_lin, m_param_smoothing_cutoff01);
        auto const smoothed_mix_01 = m_mix_smoother.LowPass(m_mix_01, m_param_smoothing_cutoff01);

        auto const gained_in = in * smoothed_gain_lin;

        auto const true_peak = m_true_peak_detector.EstimatePeak(gained_in);
        auto const peak = Max(true_peak.x, true_peak.y);

        constexpr f32 k_epsilon = 1e-8f;
        auto const required_gain = Min(1.0f, smoothed_ceiling_lin / Max(peak, k_epsilon));
        auto const windowed_min_gain = m_gain_window.Push(required_gain);

        f32 released_gain;
        if (windowed_min_gain < m_prev_released_gain) {
            // Attack: pass through, the moving average below turns this step into the ramp. Keep the
            // release filter's state in sync, otherwise the next release phase would smooth from a
            // stale starting point and glitch at the transition.
            released_gain = windowed_min_gain;
            m_release_filter.prev_output = windowed_min_gain;
        } else {
            released_gain = m_release_filter.LowPass(windowed_min_gain, m_release_cutoff01);
        }
        m_prev_released_gain = released_gain;

        auto const env = PushAttackAverage(released_gain);
        m_prev_env = env;

        auto const delayed_dry = m_delay_buffer[m_delay_write_pos];
        m_delay_buffer[m_delay_write_pos] = gained_in;
        m_delay_write_pos = (m_delay_write_pos + 1) % (u32)m_delay_buffer.size;

        m_worst_gain_this_block = Min(m_worst_gain_this_block, env);

        auto const ceiling_vec = f32x2 {smoothed_ceiling_lin, smoothed_ceiling_lin};
        auto const wet = Clamp(delayed_dry * env, -ceiling_vec, ceiling_vec);
        m_prev_wet = wet;

        return LinearInterpolate(smoothed_mix_01, delayed_dry, wet);
    }

    // Whether the delay line still holds audio that hasn't reached the output yet.
    bool HasPendingAudio() const {
        for (auto const& f : m_delay_buffer)
            if (!IsSilent(f)) return true;
        return false;
    }

    void Reset() {
        for (auto& f : m_delay_buffer)
            f = {};
        m_delay_write_pos = 0;
        for (auto& g : m_attack_average_buffer)
            g = 1.0f;
        m_attack_average_write_pos = 0;
        m_attack_average_sum = (f32)m_attack_average_buffer.size;
        m_gain_window.Reset();
        m_gain_smoother.Reset();
        m_ceiling_smoother.Reset();
        m_mix_smoother.Reset();
        m_release_filter.Reset();
        m_prev_released_gain = 1.0f;
        m_prev_env = 1.0f;
        m_prev_wet = {};
        m_true_peak_detector.Reset();
        m_worst_gain_this_block = 1.0f;
        m_gain_reduction_meter_db = 0.0f;
        m_gain_reduction_db_atomic.Store(0.0f, StoreMemoryOrder::Relaxed);
        input_peak_meter.Zero();
        output_peak_meter.Zero();
    }

    // audio-thread: call once per processed block, after all its frames, to publish that block's worst
    // gain reduction (peak-hold with a steady falldown, like a peak meter).
    void PublishGuiStats(u32 num_frames_in_block) {
        auto const worst_db = -AmpToDb(m_worst_gain_this_block);
        m_worst_gain_this_block = 1.0f;
        m_gain_reduction_meter_db =
            Max(worst_db,
                m_gain_reduction_meter_db -
                    ((f32)num_frames_in_block * m_gain_reduction_meter_falldown_db_per_sample));
        m_gain_reduction_db_atomic.Store(m_gain_reduction_meter_db, StoreMemoryOrder::Relaxed);
    }

    // thread-safe: current gain reduction in dB (0 = no reduction, positive = amount reduced by).
    f32 GainReductionDb() const { return m_gain_reduction_db_atomic.Load(LoadMemoryOrder::Relaxed); }

    // audio-thread: fed by the caller with this object's input and output; safe to read from another
    // thread for display.
    StereoPeakMeter input_peak_meter {};
    StereoPeakMeter output_peak_meter {};

  private:
    void FreeBuffers() {
        if (m_delay_buffer.size) PageAllocator::Instance().Free(m_delay_buffer.ToByteSpan());
        m_delay_buffer = {};
        if (m_attack_average_buffer.size)
            PageAllocator::Instance().Free(m_attack_average_buffer.ToByteSpan());
        m_attack_average_buffer = {};
    }

    // Moving average over the lookahead window: a running sum over a ring buffer.
    f32 PushAttackAverage(f32 gain) {
        auto const size = (u32)m_attack_average_buffer.size;
        m_attack_average_sum += gain - m_attack_average_buffer[m_attack_average_write_pos];
        m_attack_average_buffer[m_attack_average_write_pos] = gain;
        m_attack_average_write_pos = (m_attack_average_write_pos + 1) % size;
        if (m_attack_average_write_pos == 0) {
            // Recompute exactly once per lap so float drift in the running sum can't accumulate.
            m_attack_average_sum = 0;
            for (auto const g : m_attack_average_buffer)
                m_attack_average_sum += g;
        }
        return Min(m_attack_average_sum / (f32)size, 1.0f);
    }

    f32 m_sample_rate = 0;
    f32 m_gain_lin = 1.0f;
    f32 m_ceiling_lin = DbToAmp(-1.0f);
    f32 m_mix_01 = 1.0f;
    f32 m_param_smoothing_cutoff01 = 0.01f;
    f32 m_release_cutoff01 = 0.01f;
    OnePoleLowPassFilter<f32> m_gain_smoother {};
    OnePoleLowPassFilter<f32> m_ceiling_smoother {};
    OnePoleLowPassFilter<f32> m_mix_smoother {};

    TruePeakDetector m_true_peak_detector {};

    Span<f32x2> m_delay_buffer {};
    u32 m_delay_write_pos = 0;

    SlidingWindowMin m_gain_window {};
    Span<f32> m_attack_average_buffer {};
    u32 m_attack_average_write_pos = 0;
    f32 m_attack_average_sum = 0;

    OnePoleLowPassFilter<f32> m_release_filter {};
    f32 m_prev_released_gain = 1.0f;
    f32 m_prev_env = 1.0f;
    f32x2 m_prev_wet {};

    f32 m_worst_gain_this_block = 1.0f;
    f32 m_gain_reduction_meter_db = 0.0f;
    f32 m_gain_reduction_meter_falldown_db_per_sample = 0.0f;
    Atomic<f32> m_gain_reduction_db_atomic {0.0f};
};
