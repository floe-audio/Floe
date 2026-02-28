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
    Count,
};

constexpr auto k_num_effect_types = ToInt(EffectType::Count);

struct EffectInfo {
    String description;
    String name;
    u8 id;
    ParamIndex on_param_index;
};

constexpr auto k_effect_info = []() {
    // We use a switch statement so we get warnings for missing values
    Array<EffectInfo, k_num_effect_types> result {};
    for (auto const i : Range(k_num_effect_types)) {
        auto& info = result[i];
        switch ((EffectType)i) {
            case EffectType::Distortion:
                info = {
                    .description = "Distort the audio using various algorithms.",
                    .name = "Distortion",
                    .id = 1, // never change
                    .on_param_index = ParamIndex::DistortionOn,
                };
                break;
            case EffectType::BitCrush:
                info = {
                    .description =
                        "Apply a lo-fi effect to the signal by either reducing the sample rate or by reducing the sample resolution. Doing either distorts the signal.",
                    .name = "Bit Crush",
                    .id = 2, // never change
                    .on_param_index = ParamIndex::BitCrushOn,
                };
                break;
            case EffectType::Compressor:
                info = {
                    .description = "Compress the signal to make the quiet sections louder.",
                    .name = "Compressor",
                    .id = 3, // never change
                    .on_param_index = ParamIndex::CompressorOn,
                };
                break;
            case EffectType::FilterEffect:
                info = {
                    .description =
                        "Adjust the volume frequency bands in the signal, or cut out frequency bands altogether. The filter type can be selected with the menu.",
                    .name = "Filter",
                    .id = 4, // never change
                    .on_param_index = ParamIndex::FilterOn,
                };
                break;
            case EffectType::StereoWiden:
                info = {
                    .description = "Increase or decrease the stereo width of the signal.",
                    .name = "Stereo Widen",
                    .id = 5, // never change
                    .on_param_index = ParamIndex::StereoWidenOn,
                };
                break;
            case EffectType::Chorus:
                info = {
                    .description =
                        "An effect that changes the character of the signal by adding a modulated and pitch-varying duplicate signal.",
                    .name = "Chorus",
                    .id = 6, // never change
                    .on_param_index = ParamIndex::ChorusOn,
                };
                break;
            case EffectType::Reverb:
                info = {
                    .description =
                        "Algorithmically simulate the reflections and reverberations of a real room.",
                    .name = "Reverb",
                    .id = 7, // never change
                    .on_param_index = ParamIndex::ReverbOn,
                };
                break;
            case EffectType::Delay:
                info = {
                    .description =
                        "Simulate an echo effect, as if the sound is reflecting off of a distant surface.",
                    .name = "Delay",
                    .id = 11, // never change
                    .on_param_index = ParamIndex::DelayOn,
                };
                break;
            case EffectType::ConvolutionReverb:
                info = {
                    .description =
                        "The Convolution reverb effect applies a reverb to the signal. The characteristic of the reverb is determined by the impulse response (IR). The IR can be selected from the menu.",
                    .name = "Convol Reverb",
                    .id = 10, // never change
                    .on_param_index = ParamIndex::ConvolutionReverbOn,
                };
                break;
            case EffectType::Phaser:
                info = {
                    .description = "Modulate the sound using a series of moving filters",
                    .name = "Phaser",
                    .id = 9, // never change
                    .on_param_index = ParamIndex::PhaserOn,
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
