// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/debug//tracy_wrapped.hpp"
#include "utils/thread_extra/atomic_queue.hpp"

#include "common_infrastructure/audio_data.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"

#include "FFTConvolver/wrapper.hpp"
#include "effect.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/filters.hpp"

class ConvolutionReverb final : public Effect {
  public:
    ConvolutionReverb() : Effect(EffectType::ConvolutionReverb) {}
    ~ConvolutionReverb() {
        DeletedUnusedConvolvers();
        if (m_convolver) DestroyStereoConvolver(m_convolver);
    }

    // This effect's void *::effect_context.
    struct ConvoExtraContext {
        bool start_fade_out; // In parameter.
        bool changed_ir; // Out parameter.
    };

    void ProcessChangesInternal(ProcessBlockChanges const& changes,
                                AudioProcessingContext const& context) override {
        if (auto p = changes.changed_params.Param(ParamIndex::ConvolutionReverbHighpass))
            m_filter_coeffs.Set(rbj_filter::Type::HighPass, context.sample_rate, p->ProjectedValue(), 1, 0);
        if (auto p = changes.changed_params.Param(ParamIndex::ConvolutionReverbWet))
            m_wet_dry.SetWet(p->ProjectedValue());
        if (auto p = changes.changed_params.Param(ParamIndex::ConvolutionReverbDry))
            m_wet_dry.SetDry(p->ProjectedValue());
    }

    EffectProcessResult
    ProcessBlock(Span<f32x2> frames, AudioProcessingContext const& context, void* extra_context) override {
        ZoneScoped;
        auto result = EffectProcessResult::Done;

        ASSERT_HOT(extra_context);
        auto& conv_context = *(ConvoExtraContext*)extra_context;

        if (!ShouldProcessBlock()) {
            conv_context.changed_ir = SwapConvolversIfNeeded();
            return result;
        }

        alignas(16) f32 input_left[k_block_size_max];
        alignas(16) f32 input_right[k_block_size_max];
        Array<f32*, 2> input_channels = {input_left, input_right};

        CopyFramesToSeparateChannels(input_channels, frames);

        if (conv_context.start_fade_out) m_fade.SetAsFadeOut(context.sample_rate, 20);

        alignas(16) f32 wet_left[k_block_size_max] = {};
        alignas(16) f32 wet_right[k_block_size_max] = {};
        Array<f32*, 2> wet_channels = {wet_left, wet_right};

        if (m_convolver) {
            Process(*m_convolver,
                    input_channels[0],
                    input_channels[1],
                    wet_channels[0],
                    wet_channels[1],
                    (int)frames.size);
        } else {
            SimdZeroAlignedBuffer(wet_channels[0], frames.size * 2);
        }

        for (auto [frame_index, frame] : Enumerate<u32>(frames)) {
            f32x2 wet = {wet_channels[0][frame_index], wet_channels[1][frame_index]};
            auto const [filter_coeffs, mix] = m_filter_coeffs.Value();
            wet = Process(m_filter, filter_coeffs, wet * mix);
            wet = m_wet_dry.MixStereo(context, wet, frame);

            if (auto f = m_fade.GetFade(); f != 1) wet = LinearInterpolate(f, frame, wet);

            if (m_fade.IsSilent()) {
                m_remaining_tail_length = 0;
                conv_context.changed_ir = SwapConvolversIfNeeded();
                break;
            } else {
                UpdateRemainingTailLength(wet);
            }

            wet = MixOnOffSmoothing(context, wet, frame);
            frame = wet;
        }

        result = IsSilent() ? EffectProcessResult::Done : EffectProcessResult::ProcessingTail;
        return result;
    }

    // audio-thread
    bool IsSilent() const { return m_remaining_tail_length == 0; }

    // [audio-thread]
    bool SwapConvolversIfNeeded() {
        ZoneScoped;
        auto new_convolver = m_desired_convolver.Exchange((StereoConvolver*)k_desired_convolver_consumed,
                                                          RmwMemoryOrder::Acquire);
        if ((uintptr)new_convolver == k_desired_convolver_consumed) return false;

        auto old_convolver = Exchange(m_convolver, new_convolver);

        // Let another thread to the deleting. Adding null is OK.
        m_convolvers_to_delete.Push(old_convolver);

        m_remaining_tail_length = 0;
        m_filter = {};
        if (m_convolver)
            m_max_tail_length = (u32)NumFrames(*m_convolver);
        else
            m_max_tail_length = 0;

        m_fade.ForceSetFullVolume();
        return true;
    }

    // [main-thread]
    void ConvolutionIrDataLoaded(AudioData const* audio_data,
                                 sample_lib::ImpulseResponse::AudioProperties const& audio_props) {
        DeletedUnusedConvolvers();
        if (audio_data)
            m_desired_convolver.Store(CreateConvolver(*audio_data, audio_props), StoreMemoryOrder::Relaxed);
        else
            m_desired_convolver.Store(nullptr, StoreMemoryOrder::Relaxed);
    }

    // [main-thread]. Call this periodically
    void DeletedUnusedConvolvers() {
        for (auto c : m_convolvers_to_delete.PopAll())
            if (c) DestroyStereoConvolver(c);
    }

    // [main-thread]
    Optional<sample_lib::IrId> ir_id = k_nullopt; // May temporarily differ to what is actually loaded

  private:
    static StereoConvolver* CreateConvolver(AudioData const& audio_data,
                                            sample_lib::ImpulseResponse::AudioProperties const& audio_props) {
        auto num_channels = audio_data.channels;
        auto num_frames = audio_data.num_frames;

        ASSERT(num_frames);

        auto result = CreateStereoConvolver();
        Init(*result,
             audio_data.interleaved_samples.data,
             audio_props.gain_db,
             (int)num_frames,
             (int)num_channels);

        return result;
    }

    void UpdateRemainingTailLength(f32x2 frame) {
        if (!::IsSilent(frame))
            m_remaining_tail_length = m_max_tail_length;
        else if (m_remaining_tail_length)
            --m_remaining_tail_length;
    }

    void ResetInternal() override {
        m_filter = {};

        if (m_convolver) Zero(*m_convolver);

        m_remaining_tail_length = 0;
        m_wet_dry.Reset();
        m_filter_coeffs.ResetSmoothing();
    }

    u32 m_remaining_tail_length {};
    u32 m_max_tail_length {};

    VolumeFade m_fade {VolumeFade::State::FullVolume};

    StereoConvolver* m_convolver {}; // audio-thread only

    static constexpr uintptr k_desired_convolver_consumed =
        1; // must be an invalid m_desired_convolver pointer
    Atomic<StereoConvolver*> m_desired_convolver {};

    static constexpr usize k_max_num_convolvers = 8;
    AtomicQueue<StereoConvolver*, k_max_num_convolvers> m_convolvers_to_delete;

    rbj_filter::StereoData m_filter {};
    rbj_filter::SmoothedCoefficients m_filter_coeffs {};
    EffectWetDryHelper m_wet_dry;
};
