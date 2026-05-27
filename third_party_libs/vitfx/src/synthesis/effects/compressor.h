// Copyright 2013-2019 Matt Tytel

//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "processor.h"

namespace vital {

  class Compressor : public Processor {
    public:
      enum {
        kAudio,
        kUpperThreshold,
        kLowerThreshold,
        kUpperRatio,
        kLowerRatio,
        kOutputGain,
        kAttack,
        kRelease,
        kMix,
        kNumInputs
      };

      enum {
        kAudioOut,
        kNumOutputs
      };

      Compressor(mono_float base_attack_ms_first, mono_float base_release_ms_first,
                 mono_float base_attack_ms_second, mono_float base_release_ms_second);
      virtual ~Compressor() { }

      virtual Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }
      virtual void process(int num_samples) override;
      virtual void processWithInput(const poly_float* audio_in, int num_samples) override;
      void processRms(const poly_float* audio_in, int num_samples);
      void scaleOutput(const poly_float* audio_input, int num_samples);
      void reset(poly_mask reset_mask) override;

      force_inline poly_float getInputMeanSquared() { return input_mean_squared_; }
      force_inline poly_float getOutputMeanSquared() { return output_mean_squared_; }

    protected:
      poly_float computeMeanSquared(const poly_float* audio_in, int num_samples, poly_float mean_squared);

      poly_float input_mean_squared_;
      poly_float output_mean_squared_;
      poly_float high_enveloped_mean_squared_;
      poly_float low_enveloped_mean_squared_;

      poly_float mix_;

      poly_float base_attack_ms_;
      poly_float base_release_ms_;

      poly_float output_mult_;
  };
} // namespace vital
