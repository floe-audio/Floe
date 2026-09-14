// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"

#include "param_descriptors.hpp"

// These are not ordered
enum class EffectType : u8 {
    Distortion,
    BitCrush,
    Compressor,
    FilterEffect,
    StereoWiden,
    Chorus,
    Reverb,
    Delay,
    ConvolutionReverb,
    Phaser,
    Eq,
    Limiter,
    Count,
};

constexpr auto k_num_effect_types = ToInt(EffectType::Count);

struct EffectInfo {
    String description;
    String name;
    u8 id;
    ParamIndex on_param_index;
    ParamIndex mix_param_index;
};

constexpr auto k_effect_info = []() {
    // We use a switch statement so we get warnings for missing values
    Array<EffectInfo, k_num_effect_types> result {};
    for (auto const i : Range(k_num_effect_types)) {
        auto& info = result[i];
        switch ((EffectType)i) {
            case EffectType::Distortion:
                info = {
                    .description =
                        "The distortion effect pushes the signal through a shaping curve, for anything from gentle tape warmth to outright destruction. Oversampled and anti-aliased for a clean sound.",
                    .name = "Distortion",
                    .id = 1, // never change
                    .on_param_index = ParamIndex::DistortionOn,
                    .mix_param_index = ParamIndex::DistortionMix,
                };
                break;
            case EffectType::BitCrush:
                info = {
                    .description =
                        "The Bit Crush is a lo-fi effect that degrades the signal in two ways: dropping the sample rate for ringing, metallic aliasing, and reducing the bit depth for gritty quantisation noise. Both controls start at full quality, so lower them to hear the effect.",
                    .name = "Bit Crush",
                    .id = 2, // never change
                    .on_param_index = ParamIndex::BitCrushOn,
                    .mix_param_index = ParamIndex::BitCrushMix,
                };
                break;
            case EffectType::Compressor:
                info = {
                    .description =
                        "The compressor can be used to shape the dynamics: controlling dynamics and making quiet sections louder.",
                    .name = "Compressor",
                    .id = 3, // never change
                    .on_param_index = ParamIndex::CompressorOn,
                    .mix_param_index = ParamIndex::CompressorMix,
                };
                break;
            case EffectType::FilterEffect:
                info = {
                    .description =
                        "This effect filters the signal, either cutting away a region of the frequency range or boosting and dipping it.",
                    .name = "Filter",
                    .id = 4, // never change
                    .on_param_index = ParamIndex::FilterOn,
                    .mix_param_index = ParamIndex::FilterMix,
                };
                break;
            case EffectType::StereoWiden:
                info = {
                    .description =
                        "This effect allows for both narrowing the signal towards mono, or spreading it out wider. There's also a Bass Mono mode that holds the low end in the centre while everything above it widens.",
                    .name = "Stereo Widen",
                    .id = 5, // never change
                    .on_param_index = ParamIndex::StereoWidenOn,
                    .mix_param_index = ParamIndex::StereoWidenMix,
                };
                break;
            case EffectType::Chorus:
                info = {
                    .description =
                        "The chorus effect thickens the sound by layering it with delayed copies that drift in pitch. Gentle settings add a subtle shimmer and movement, while deeper settings give an obvious, tape-like wobble.",
                    .name = "Chorus",
                    .id = 6, // never change
                    .on_param_index = ParamIndex::ChorusOn,
                    .mix_param_index = ParamIndex::ChorusMix,
                };
                break;
            case EffectType::Reverb:
                info = {
                    .description =
                        "Reverb algorithmically simulates the reflections and reverberations of a real space, from a small, tight room to a vast hall that takes many seconds to fade. Features modulation options for creating shimmering tails.",
                    .name = "Reverb",
                    .id = 7, // never change
                    .on_param_index = ParamIndex::ReverbOn,
                    .mix_param_index = ParamIndex::ReverbMix,
                };
                break;
            case EffectType::Delay:
                info = {
                    .description =
                        "A fully-featured stereo echo, with separate left and right times, free or tempo-synced, a choice of ping-pong modes, and a filter that thins the repeats as they fade.",
                    .name = "Delay",
                    .id = 11, // never change
                    .on_param_index = ParamIndex::DelayOn,
                    .mix_param_index = ParamIndex::DelayMix,
                };
                break;
            case EffectType::ConvolutionReverb:
                info = {
                    .description =
                        "This convolution reverb effect's character comes entirely from an impulse response (IR): a sample of how a space or object responds to sound. Most of the IRs on offer are strange and characterful, making this as much a sound-design tool as a reverb.",
                    .name = "Convol Reverb",
                    .id = 10, // never change
                    .on_param_index = ParamIndex::ConvolutionReverbOn,
                    .mix_param_index = ParamIndex::ConvolutionReverbMix,
                };
                break;
            case EffectType::Phaser:
                info = {
                    .description =
                        "The phaser can sweep a series of peaks and notches through the sound, giving it the classic swooshing, jet-like motion. Gentle settings add a subtle sense of movement to sustained sounds, while faster or more resonant ones become an unmistakable whoosh.",
                    .name = "Phaser",
                    .id = 9, // never change
                    .on_param_index = ParamIndex::PhaserOn,
                    .mix_param_index = ParamIndex::PhaserMix,
                };
                break;
            case EffectType::Eq:
                info = {
                    .description =
                        "This is a three-band equaliser for lifting or taming particular parts of the frequency range, from broad tonal shaping to surgical cuts.",
                    .name = "EQ",
                    .id = 8, // never change
                    .on_param_index = ParamIndex::EqOn,
                    .mix_param_index = ParamIndex::EqMix,
                };
                break;
            case EffectType::Limiter:
                info = {
                    .description =
                        "The limiter can hold the signal below a set ceiling, either to catch stray peaks or to push the overall level up without clipping. It's a true-peak brickwall limiter with a very short lookahead, and usually belongs at the end of the effects chain.",
                    .name = "Limiter",
                    .id = 12, // never change
                    .on_param_index = ParamIndex::LimiterOn,
                    .mix_param_index = ParamIndex::LimiterMix,
                };
                break;

            case EffectType::Count: break;
        }

        if (i != 0) {
            for (int j = (int)i - 1; j >= 0; --j)
                if (result[(usize)j].id == info.id) throw "id must be unique";
        }
    }
    return result;
}();

constexpr ParameterModule EffectTypeToParameterModule(EffectType type) {
    switch (type) {
        case EffectType::Distortion: return ParameterModule::Distortion;
        case EffectType::BitCrush: return ParameterModule::Bitcrush;
        case EffectType::Compressor: return ParameterModule::Compressor;
        case EffectType::FilterEffect: return ParameterModule::Filter;
        case EffectType::StereoWiden: return ParameterModule::StereoWiden;
        case EffectType::Chorus: return ParameterModule::Chorus;
        case EffectType::Reverb: return ParameterModule::Reverb;
        case EffectType::Delay: return ParameterModule::Delay;
        case EffectType::ConvolutionReverb: return ParameterModule::ConvolutionReverb;
        case EffectType::Phaser: return ParameterModule::Phaser;
        case EffectType::Eq: return ParameterModule::Eq;
        case EffectType::Limiter: return ParameterModule::Limiter;
        case EffectType::Count: break;
    }
    return ParameterModule::None;
}
