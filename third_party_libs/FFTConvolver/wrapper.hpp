// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: MIT

#pragma once

struct StereoConvolver;

StereoConvolver* CreateStereoConvolver();
void DestroyStereoConvolver(StereoConvolver* convolver);

void Init(StereoConvolver& convolver, float const* samples, float gain_db, int num_frames, int num_channels);
int NumFrames(StereoConvolver& convolver);
void Process(StereoConvolver& convolver,
             float const* input_l,
             float const* input_r,
             float* output_l,
             float* output_r,
             int num_frames);
void Zero(StereoConvolver& convolver);
