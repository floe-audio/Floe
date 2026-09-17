// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <vitfx/wrapper.hpp>

#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "dsp_stillwell_majortom.hpp"
#include "effect.hpp"
#include "processing_utils/peak_meter.hpp"

constexpr f32 k_compressor_gain_reduction_meter_falldown_db_per_second = 40.0f;

class Compressor final : public Effect {
  public:
    Compressor() : Effect(EffectType::Compressor), m_vital(vitfx::compressor::Create()) {}
    ~Compressor() override { vitfx::compressor::Destroy(m_vital); }

    // thread-safe: current gain reduction in dB (0 = no reduction, positive = amount reduced by). Both
    // compressor types report their compression envelope alone, so this is unaffected by the Gain and
    // Mix params.
    f32 GainReductionDb() const { return m_gain_reduction_db_atomic.Load(LoadMemoryOrder::Relaxed); }

    // audio-thread: fed with the block's input and its fully-compressed output; safe to read from another
    // thread for display.
    StereoPeakMeter input_peak_meter {};
    StereoPeakMeter output_peak_meter {};

  private:
    void ProcessChangesInternal(ProcessBlockChanges const& changes,
                                AudioProcessingContext const& context) override {
        if (auto p =
                changes.changed_params.IntValue<param_values::CompressorType>(ParamIndex::CompressorType))
            m_type = *p;

        bool major_tom_changed = false;

        if (auto p = changes.changed_params.ProjectedValueLegacyAware(ParamIndex::CompressorThreshold)) {
            m_major_tom.slider_threshold = *p;
            m_target_threshold_db = *p;
            major_tom_changed = true;
        }
        if (auto p = changes.changed_params.ProjectedValueLegacyAware(ParamIndex::CompressorRatio)) {
            m_major_tom.slider_ratio = *p;
            // Map traditional ratio (1..20) to Vital's 0..1 normalised ratio: 1 - 1/r.
            m_target_ratio = 1.0f - (1.0f / *p);
            major_tom_changed = true;
        }
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::CompressorGain)) {
            m_target_gain_db = *p;
            m_vital_args.params[ToInt(vitfx::compressor::Params::OutputGainDb)] = *p;
            major_tom_changed = true;
        }
        if (auto p = changes.changed_params.BoolValue(ParamIndex::CompressorAutoGain)) {
            m_major_tom.slider_auto_gain = *p;
            major_tom_changed = true;
        }
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::CompressorAttack))
            m_vital_args.params[ToInt(vitfx::compressor::Params::Attack)] = *p;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::CompressorRelease))
            m_vital_args.params[ToInt(vitfx::compressor::Params::Release)] = *p;
        if (auto p = changes.changed_params.ProjectedValue(ParamIndex::CompressorMix)) mix_param = *p;

        if (major_tom_changed) m_major_tom.Update(context.sample_rate);
    }

    EffectProcessResult
    ProcessBlock(Span<f32x2> io_frames, AudioProcessingContext const& context, void*) override {
        if (!ShouldProcessBlock()) {
            ResetMeters();
            return EffectProcessResult::Done;
        }

        input_peak_meter.AddBuffer(io_frames);

        switch (m_type) {
            case param_values::CompressorType::Vintage: {
                f32x2 wet[k_block_size_max];
                u32 wet_index = 0;
                auto const result = ProcessBlockByFrame(
                    io_frames,
                    [&](f32x2 in) {
                        alignas(f32x2) f32 out[2];
                        m_major_tom.slider_gain =
                            m_gain_smoother.LowPass(m_target_gain_db, context.one_pole_smoothing_cutoff_10ms);
                        m_major_tom.UpdateMakeupGain();
                        m_major_tom.Process(context.sample_rate, in.x, in.y, out[0], out[1]);
                        m_worst_gain_reduction_db =
                            Max(m_worst_gain_reduction_db, m_major_tom.gain_reduction_db);
                        auto const wet_frame = LoadAlignedToType<f32x2>(out);
                        wet[wet_index++] = wet_frame;
                        return wet_frame;
                    },
                    context);
                PublishMeterStats({wet, io_frames.size}, context.sample_rate);
                return result;
            }

            case param_values::CompressorType::Modern: {
                f32x2 wet[k_block_size_max];
                CopyMemory(wet, io_frames.data, io_frames.size * sizeof(f32x2));

                auto num_frames = (u32)io_frames.size;
                u32 pos = 0;
                f32 chunk_min_gain_mult = 1;
                m_vital_args.out_min_gain_mult = &chunk_min_gain_mult;
                while (num_frames) {
                    u32 const chunk_size = Min(num_frames, 64u);

                    // vitfx::compressor::Process holds threshold/ratio fixed for the whole chunk
                    // (no internal ramping), so smooth them here to avoid stepping the gain
                    // multiplier abruptly when the params change quickly (e.g. dragging a knob).
                    // The cutoff is compensated for being applied once per chunk rather than once
                    // per sample.
                    auto const chunk_cutoff =
                        1 - Pow(1 - context.one_pole_smoothing_cutoff_10ms, (f32)chunk_size);
                    m_vital_args.params[ToInt(vitfx::compressor::Params::UpperThresholdDb)] =
                        m_threshold_smoother.LowPass(m_target_threshold_db, chunk_cutoff);
                    m_vital_args.params[ToInt(vitfx::compressor::Params::UpperRatio)] =
                        m_ratio_smoother.LowPass(m_target_ratio, chunk_cutoff);

                    m_vital_args.num_frames = (int)chunk_size;
                    m_vital_args.in_interleaved = (f32*)(io_frames.data + pos);
                    m_vital_args.out_interleaved = (f32*)(wet + pos);
                    // Lower threshold/ratio pinned to neutral so this acts as a downward-only
                    // compressor.
                    m_vital_args.params[ToInt(vitfx::compressor::Params::LowerThresholdDb)] = -100.0f;
                    m_vital_args.params[ToInt(vitfx::compressor::Params::LowerRatio)] = 0.0f;
                    // Wet/dry blend is handled outside via the Effect base's user_mix.
                    m_vital_args.params[ToInt(vitfx::compressor::Params::Mix)] = 1.0f;

                    vitfx::compressor::Process(*m_vital, m_vital_args);
                    m_worst_gain_reduction_db = Max(m_worst_gain_reduction_db, -AmpToDb(chunk_min_gain_mult));

                    num_frames -= chunk_size;
                    pos += chunk_size;
                }
                m_vital_args.out_min_gain_mult = nullptr;

                for (auto const frame_index : Range((u32)io_frames.size))
                    io_frames[frame_index] =
                        ApplyBypassCrossfade(context, wet[frame_index], io_frames[frame_index]);

                PublishMeterStats({wet, io_frames.size}, context.sample_rate);
                return EffectProcessResult::Done;
            }

            case param_values::CompressorType::Count: break;
        }
        PanicIfReached();
        return EffectProcessResult::Done;
    }

    // audio-thread: call once per processed block, after all its frames. Metering the fully-compressed
    // signal rather than io_frames keeps the output meter meaningful regardless of the Mix setting.
    void PublishMeterStats(Span<f32x2> wet_frames, f32 sample_rate) {
        output_peak_meter.AddBuffer(wet_frames);

        // Peak-hold with a steady falldown, like a peak meter.
        auto const num_frames_in_block = (u32)wet_frames.size;
        auto const worst_db = m_worst_gain_reduction_db;
        m_worst_gain_reduction_db = 0;
        m_gain_reduction_meter_db =
            Max(worst_db,
                m_gain_reduction_meter_db - (((f32)num_frames_in_block / sample_rate) *
                                             k_compressor_gain_reduction_meter_falldown_db_per_second));
        m_gain_reduction_db_atomic.Store(m_gain_reduction_meter_db, StoreMemoryOrder::Relaxed);
    }

    void ResetMeters() {
        m_worst_gain_reduction_db = 0;
        m_gain_reduction_meter_db = 0;
        m_gain_reduction_db_atomic.Store(0.0f, StoreMemoryOrder::Relaxed);
        input_peak_meter.Zero();
        output_peak_meter.Zero();
    }

    void ResetInternal() override {
        m_major_tom.Reset();
        vitfx::compressor::HardReset(*m_vital);
        m_threshold_smoother.Reset();
        m_ratio_smoother.Reset();
        m_gain_smoother.Reset();
        ResetMeters();
    }

    void PrepareToPlay(AudioProcessingContext const& context) override {
        m_major_tom.SetSampleRate(context.sample_rate);
        vitfx::compressor::SetSampleRate(*m_vital, (int)context.sample_rate);
        input_peak_meter.PrepareToPlay(context.sample_rate);
        output_peak_meter.PrepareToPlay(context.sample_rate);
    }

    param_values::CompressorType m_type {param_values::CompressorType::Modern};
    StillwellMajorTom m_major_tom;
    vitfx::compressor::Compressor* m_vital {};
    vitfx::compressor::ProcessCompressorArgs m_vital_args {};
    f32 m_target_threshold_db {};
    f32 m_target_ratio {};
    f32 m_target_gain_db {};
    OnePoleLowPassFilter<f32> m_threshold_smoother {};
    OnePoleLowPassFilter<f32> m_ratio_smoother {};
    OnePoleLowPassFilter<f32> m_gain_smoother {};

    f32 m_worst_gain_reduction_db {};
    f32 m_gain_reduction_meter_db {};
    Atomic<f32> m_gain_reduction_db_atomic {0.0f};
};
