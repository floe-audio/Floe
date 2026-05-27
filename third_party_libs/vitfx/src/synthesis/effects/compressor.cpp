// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compressor.h"
#include "futils.h"
#include "synth_constants.h"

namespace vital {

  namespace {
    constexpr mono_float kRmsTime = 0.025f;
    constexpr mono_float kMaxExpandMult = 32.0f;

    constexpr mono_float kMinGain = -30.0f;
    constexpr mono_float kMaxGain = 30.0f;
    constexpr mono_float kMinThreshold = -100.0f;
    constexpr mono_float kMaxThreshold = 12.0f;
    constexpr mono_float kMinSampleEnvelope = 5.0f;
  } // namespace

  Compressor::Compressor(mono_float base_attack_ms_first, mono_float base_release_ms_first,
                         mono_float base_attack_ms_second, mono_float base_release_ms_second) :
      Processor(kNumInputs, kNumOutputs), input_mean_squared_(0.0f) {
    base_attack_ms_ = utils::maskLoad(poly_float(base_attack_ms_second), base_attack_ms_first, constants::kFirstMask);
    poly_float second_release = base_release_ms_second;
    base_release_ms_ = utils::maskLoad(second_release, base_release_ms_first, constants::kFirstMask);
    output_mult_ = 0.0f;
    mix_ = 0.0f;
  }

  void Compressor::process(int num_samples) {
    processWithInput(input(kAudio)->source->buffer, num_samples);
  }

  void Compressor::processWithInput(const poly_float* audio_in, int num_samples) {
    processRms(audio_in, num_samples);

    input_mean_squared_ = computeMeanSquared(audio_in, num_samples, input_mean_squared_);
    output_mean_squared_ = computeMeanSquared(output(kAudioOut)->buffer, num_samples, output_mean_squared_);
    scaleOutput(audio_in, num_samples);
  }

  void Compressor::processRms(const poly_float* audio_in, int num_samples) {
    poly_float* audio_out = output(kAudioOut)->buffer;

    mono_float samples_per_ms = (1.0f * getSampleRate()) / kMsPerSec;
    poly_float attack_mult = base_attack_ms_ * samples_per_ms;
    poly_float release_mult = base_release_ms_ * samples_per_ms;
    poly_float attack_exponent = utils::clamp(input(kAttack)->at(0), 0.0f, 1.0f) * 8.0f - 4.0f;
    poly_float release_exponent = utils::clamp(input(kRelease)->at(0), 0.0f, 1.0f) * 8.0f - 4.0f;
    poly_float envelope_attack_samples = futils::exp(attack_exponent) * attack_mult;
    poly_float envelope_release_samples = futils::exp(release_exponent) * release_mult;
    envelope_attack_samples = utils::max(envelope_attack_samples, kMinSampleEnvelope);
    envelope_release_samples = utils::max(envelope_release_samples, kMinSampleEnvelope);

    poly_float attack_scale = poly_float(1.0f) / (envelope_attack_samples + 1.0f);
    poly_float release_scale = poly_float(1.0f) / (envelope_release_samples + 1.0f);

    poly_float upper_threshold = utils::clamp(input(kUpperThreshold)->at(0), kMinThreshold, kMaxThreshold);
    upper_threshold = futils::dbToMagnitude(upper_threshold);
    upper_threshold *= upper_threshold;
    poly_float lower_threshold = utils::clamp(input(kLowerThreshold)->at(0), kMinThreshold, kMaxThreshold);
    lower_threshold = futils::dbToMagnitude(lower_threshold);
    lower_threshold *= lower_threshold;

    poly_float upper_ratio = utils::clamp(input(kUpperRatio)->at(0), 0.0f, 1.0f) * 0.5f;
    poly_float lower_ratio = utils::clamp(input(kLowerRatio)->at(0), -1.0f, 1.0f) * 0.5f;

    poly_float low_enveloped_mean_squared = low_enveloped_mean_squared_;
    poly_float high_enveloped_mean_squared = high_enveloped_mean_squared_;

    for (int i = 0; i < num_samples; ++i) {
      poly_float sample = audio_in[i];
      poly_float sample_squared = sample * sample;

      poly_mask high_attack_mask = poly_float::greaterThan(sample_squared, high_enveloped_mean_squared);
      poly_float high_samples = utils::maskLoad(envelope_release_samples, envelope_attack_samples, high_attack_mask);
      poly_float high_scale = utils::maskLoad(release_scale, attack_scale, high_attack_mask);

      high_enveloped_mean_squared = (sample_squared + high_enveloped_mean_squared * high_samples) * high_scale;
      high_enveloped_mean_squared = utils::max(high_enveloped_mean_squared, upper_threshold);

      poly_float upper_mag_delta = upper_threshold / high_enveloped_mean_squared;
      poly_float upper_mult = futils::pow(upper_mag_delta, upper_ratio);

      poly_mask low_attack_mask = poly_float::greaterThan(sample_squared, low_enveloped_mean_squared);
      poly_float low_samples = utils::maskLoad(envelope_release_samples, envelope_attack_samples, low_attack_mask);
      poly_float low_scale = utils::maskLoad(release_scale, attack_scale, low_attack_mask);

      low_enveloped_mean_squared = (sample_squared + low_enveloped_mean_squared * low_samples) * low_scale;
      low_enveloped_mean_squared = utils::min(low_enveloped_mean_squared, lower_threshold);

      poly_float lower_mag_delta = lower_threshold / low_enveloped_mean_squared;
      poly_float lower_mult = futils::pow(lower_mag_delta, lower_ratio);

      poly_float gain_compression = utils::clamp(upper_mult * lower_mult, 0.0f, kMaxExpandMult);
      audio_out[i] = gain_compression * sample;
      VITAL_ASSERT(utils::isContained(audio_out[i]));
    }

    low_enveloped_mean_squared_ = low_enveloped_mean_squared;
    high_enveloped_mean_squared_ = high_enveloped_mean_squared;
  }

  void Compressor::scaleOutput(const poly_float* audio_input, int num_samples) {
    poly_float* audio_out = output(kAudioOut)->buffer;

    poly_float current_output_mult = output_mult_;
    poly_float gain = utils::clamp(input(kOutputGain)->at(0), kMinGain, kMaxGain);
    output_mult_ = futils::dbToMagnitude(gain);
    poly_float delta_output_mult = (output_mult_ - current_output_mult) * (1.0f / num_samples);

    poly_float current_mix = mix_;
    mix_ = utils::clamp(input(kMix)->at(0), 0.0f, 1.0f);
    poly_float delta_mix = (mix_ - current_mix) * (1.0f / num_samples);

    for (int i = 0; i < num_samples; ++i) {
      current_output_mult += delta_output_mult;
      current_mix += delta_mix;
      audio_out[i] = utils::interpolate(audio_input[i], audio_out[i] * current_output_mult, current_mix);
      VITAL_ASSERT(utils::isContained(audio_out[i]));
    }
  }

  void Compressor::reset(poly_mask reset_mask) {
    input_mean_squared_ = 0.0f;
    output_mean_squared_ = 0.0f;
    output_mult_ = 0.0f;
    mix_ = 0.0f;

    high_enveloped_mean_squared_ = 0.0f;
    low_enveloped_mean_squared_ = 0.0f;
  }

  poly_float Compressor::computeMeanSquared(const poly_float* audio_in, int num_samples, poly_float mean_squared) {
    int rms_samples = kRmsTime * getSampleRate();
    float rms_adjusted = rms_samples - 1.0f;
    mono_float input_scale = 1.0f / rms_samples;

    for (int i = 0; i < num_samples; ++i) {
      poly_float sample = audio_in[i];
      poly_float sample_squared = sample * sample;
      mean_squared = (mean_squared * rms_adjusted + sample_squared) * input_scale;
    }
    return mean_squared;
  }

} // namespace vital
