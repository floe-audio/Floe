// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/debug/tracy_wrapped.hpp"

#include "filters.hpp"

template <usize k_max_num_float_smoothers, usize k_max_num_double_smoothers, usize k_max_num_filter_smoothers>
class SmoothedValueSystem {
  public:
    template <int Tag>
    class Id {
      public:
        explicit Id(u16 val) : m_value(val) {}
        explicit operator u16() const noexcept { return m_value; }

      private:
        u16 m_value;
    };

    enum { FloatIDTag, DoubleIDTag, FilterIDTag };

    using FloatId = Id<FloatIDTag>;
    using DoubleId = Id<DoubleIDTag>;
    using FilterId = Id<FilterIDTag>;

    void PrepareToPlay(u32 block_size, f32 sample_rate, ArenaAllocator& arena) {
        m_sample_rate = sample_rate;
        m_float_smoothers.PrepareToPlay(block_size, arena);
        m_double_smoothers.PrepareToPlay(block_size, arena);

        m_filter_result_buffer = arena.template NewMultiple<rbj_filter::SmoothedCoefficients::State>(
            m_num_smoothed_filters * (usize)block_size);
    }

    FloatId CreateSmoother() { return m_float_smoothers.CreateSmoother(); }

    FilterId CreateFilterSmoother() {
        ASSERT((usize)m_num_smoothed_filters != m_smoothed_filters.size);
        return FilterId {m_num_smoothed_filters++};
    }

    DoubleId CreateDoubleSmoother() { return m_double_smoothers.CreateSmoother(); }

    f32 Value(FloatId smoother, u32 frame_index) const {
        return m_float_smoothers.Value(m_num_valid_frames, smoother, frame_index);
    }

    f64 Value(DoubleId smoother, u32 frame_index) const {
        return m_double_smoothers.Value(m_num_valid_frames, smoother, frame_index);
    }

    bool IsSmoothing(FloatId smoother, u32 frame_index) const {
        return m_float_smoothers.IsSmoothing(smoother, frame_index);
    }

    rbj_filter::SmoothedCoefficients::State Value(FilterId smoother, u32 frame_index) const {
        ASSERT(frame_index < m_num_valid_frames);

        if (m_processed_filter_this_frame[u16(smoother)])
            return m_filter_result_buffer[(u16(smoother) * m_num_valid_frames) + frame_index];
        else
            return {m_smoothed_filters[u16(smoother)].Coeffs(), 1};
    }

    f32* AllValues(FloatId smoother) { return m_float_smoothers.AllValues(m_num_valid_frames, smoother); }

    f32 TargetValue(FloatId smoother) const { return m_float_smoothers.TargetValue(smoother); }

    void SetVariableLength(FloatId smoother,
                           f32 value,
                           f32 min_transition_ms,
                           f32 max_transition_ms,
                           f32 max_expected_change) {
        m_float_smoothers.SetVariableLength(smoother,
                                            value,
                                            min_transition_ms,
                                            max_transition_ms,
                                            max_expected_change,
                                            m_sample_rate);
    }

    void Set(FloatId smoother, f32 value, f32 transition_ms) {
        m_float_smoothers.Set(smoother, value, transition_ms, m_sample_rate);
    }

    void Set(DoubleId smoother, f64 value, f32 transition_ms) {
        m_double_smoothers.Set(smoother, value, (f64)transition_ms, m_sample_rate);
    }

    void HardSet(FloatId smoother, f32 value) { m_float_smoothers.HardSet(smoother, value); }
    void HardSet(DoubleId smoother, f64 value) { m_double_smoothers.HardSet(smoother, value); }

    void Set(FilterId smoother, rbj_filter::Params& p) { m_smoothed_filters[u16(smoother)].Set(p); }

    void Set(FilterId smoother, rbj_filter::Type type, f32 sample_rate, f32 fc, f32 Q, f32 gain_db) {
        m_smoothed_filters[u16(smoother)].Set(type, sample_rate, fc, Q, gain_db);
    }

    void ResetAll() {
        m_float_smoothers.ResetAll();
        m_double_smoothers.ResetAll();

        for (auto& f : m_smoothed_filters)
            f.ResetSmoothing();

        for (auto& n : m_processed_filter_this_frame)
            n = false;
    }

    void ProcessBlock(u32 block_size) {
        ZoneNamedN(process_block, "SmoothedValueSystem ProcessBlock", true);
        m_float_smoothers.ProcessBlock(block_size);
        m_double_smoothers.ProcessBlock(block_size);

        if (m_smoothed_filters.size) {
            for (auto& n : m_processed_filter_this_frame)
                n = false;

            for (usize filter_smoother_id = 0; filter_smoother_id < m_num_smoothed_filters;
                 ++filter_smoother_id) {
                if (m_smoothed_filters[filter_smoother_id].NeedsUpdate()) {
                    m_processed_filter_this_frame[filter_smoother_id] = true;
                    for (auto const i : Range(block_size)) {
                        m_filter_result_buffer[(filter_smoother_id * block_size) + i] =
                            m_smoothed_filters[filter_smoother_id].Value();
                    }
                }
            }
        }

        m_num_valid_frames = block_size;
    }

  private:
    template <typename Type, typename IdType, usize k_max_num_smoothers>
    struct ValueSmoother {
        void PrepareToPlay(u32 block_size, ArenaAllocator& arena) {
            result_buffer = arena.template NewMultiple<Type>(block_size * num_smoothers);
        }

        IdType CreateSmoother() {
            ASSERT((usize)num_smoothers != smoothed_values.size);
            return IdType {num_smoothers++};
        }

        void SetVariableLength(IdType smoother,
                               Type value,
                               Type min_transition_ms,
                               Type max_transition_ms,
                               Type max_expected_change,
                               f32 sample_rate) {
            auto delta = Abs(value - smoothed_values[u16(smoother)].current);
            auto transition_ms = Map(Min(delta, max_expected_change),
                                     0,
                                     max_expected_change,
                                     min_transition_ms,
                                     max_transition_ms);
            Set(smoother, value, transition_ms, sample_rate);
        }

        void Set(IdType smoother, Type value, Type transition_ms, f32 sample_rate) {
            if (value == smoothed_values[u16(smoother)].target) return;
            smoothed_values[u16(smoother)].target = value;
            auto const num = (u32)((Type)sample_rate * (transition_ms / (Type)1000.0));
            if (!num) return;
            remaining_smoothing_steps[u16(smoother)] = num;
        }

        void HardSet(IdType smoother, Type value) {
            smoothed_values[u16(smoother)].target = value;
            smoothed_values[u16(smoother)].current = value;
            remaining_smoothing_steps[u16(smoother)] = 0;
        }

        void ResetAll() {
            for (auto& v : smoothed_values.Items().SubSpan(0, num_smoothers))
                v.current = v.target;

            for (auto& r : remaining_smoothing_steps.Items().SubSpan(0, num_smoothers))
                r = 0;

            for (auto& n : num_frames_smoothed_this_block.Items().SubSpan(0, num_smoothers))
                n = 0;
        }

        Type Value(u32 block_size, IdType smoother, u32 frame_index) const {
            ASSERT(frame_index < block_size);

            if (frame_index < num_frames_smoothed_this_block[u16(smoother)])
                return result_buffer[(u16(smoother) * block_size) + frame_index];
            else
                return smoothed_values[u16(smoother)].target;
        }

        bool IsSmoothing(IdType smoother, u32 frame_index) const {
            return frame_index < num_frames_smoothed_this_block[u16(smoother)];
        }

        Type* AllValues(u32 block_size, IdType smoother) {
            auto const result_buffer_offset = u16(smoother) * block_size;

            if (num_frames_smoothed_this_block[u16(smoother)] < block_size) {
                auto const start_index =
                    result_buffer_offset + Max(num_frames_smoothed_this_block[u16(smoother)], 0u);
                auto const end_index = result_buffer_offset + block_size;
                auto const target = smoothed_values[u16(smoother)].target;
                for (auto i = start_index; i < end_index; ++i)
                    result_buffer[i] = target;
            }

            return result_buffer.data + result_buffer_offset;
        }

        Type TargetValue(IdType smoother) const { return smoothed_values[u16(smoother)].target; }

        void ProcessBlock(u32 block_size) {
            if (!num_smoothers) return;

            for (auto& n : num_frames_smoothed_this_block.Items().SubSpan(0, num_smoothers))
                n = 0;

            for (auto const smoother_index : Range(num_smoothers)) {
                auto& remaining = remaining_smoothing_steps[smoother_index];
                if (remaining) {
                    auto const initial_remaining = remaining;
                    for (usize i = 0; i < block_size && remaining; ++i) {
                        auto& v = smoothed_values[smoother_index];
                        v.current += (v.target - v.current) / (Type)remaining;
                        --remaining;
                        result_buffer[(smoother_index * (usize)block_size) + i] = v.current;
                    }
                    num_frames_smoothed_this_block[smoother_index] = initial_remaining - remaining;
                }
            }
        }

        Span<Type> result_buffer {};

        struct SmoothedValue {
            Type current {}, target {};
        };
        u16 num_smoothers {};
        Array<SmoothedValue, k_max_num_smoothers> smoothed_values {};
        Array<u32, k_max_num_smoothers> remaining_smoothing_steps {};
        Array<u32, k_max_num_smoothers> num_frames_smoothed_this_block {};
    };

    u32 m_num_valid_frames {};
    f32 m_sample_rate {1};

    // Value smoothers
    ValueSmoother<f32, FloatId, k_max_num_float_smoothers> m_float_smoothers;
    ValueSmoother<f64, DoubleId, k_max_num_double_smoothers> m_double_smoothers;

    // Filter smoothers
    u16 m_num_smoothed_filters {};
    Array<rbj_filter::SmoothedCoefficients, k_max_num_filter_smoothers> m_smoothed_filters {};
    Array<bool, k_max_num_filter_smoothers> m_processed_filter_this_frame {};

    Span<rbj_filter::SmoothedCoefficients::State> m_filter_result_buffer {};
};

using FloeSmoothedValueSystem = SmoothedValueSystem<60, 0, 20>;
