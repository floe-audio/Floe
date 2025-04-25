// This file a c++ port of the Stillwell Major Tom compressor reaper plugin.
// Copyright 2006 Thomas Scott Stillwell
// SPDX-License-Identifier: BSD-3-Clause
//
// Modified by Sam Windell

#pragma once
#include "foundation/foundation.hpp"

constexpr auto k_attack_times = []() {
    Array<f32, 121> result;
    result[0] = 1;
    for (usize i = 1; i < result.size; ++i) {
        auto const fi = (f32)i;
        result[i] = ((0.08924f / fi) + (0.60755f / (fi * fi)) - 0.00006f);
    }
    return result;
}();

struct StillwellMajorTom {
    f32 slider_threshold {}; //: 0<-60,0,0.1>Threshold (dB)
    f32 slider_ratio {}; //: 1<1,20,0.1>Ratio
    f32 slider_gain {}; //: 0<-20,20,0.1>Gain
    static constexpr int k_slider_knee_type {0}; //: 0<0,1,1{Hard,Soft}>Knee
    bool slider_auto_gain {}; //: 0<0,1,1{No,Yes}>Automatic Make-Up
    static constexpr int k_slider_detection_mode {0}; //: 0<0,1,1{Peak,RMS}>Detection
    static constexpr int k_slider_detection_source {0}; //: 0<0,1,1{Feedforward,Feedback}>Detection Source
    static constexpr f32 k_log2db = 8.6858896380650365530225783783321f; // 20 / ln(10)
    static constexpr f32 k_db2log = 0.11512925464970228420089957273422f; // ln(10) / 20

    f32 attime {};
    f32 reltime {};
    f32 maxover {};
    f32 cratio {};
    f32 rundb {};
    f32 atcoef {};
    f32 relcoef {};

    f32 sample_rate {44100};

    void SetSampleRate(f32 s) {
        sample_rate = s;
        Reset();
    }

    void Reset() {
        attime = 0.010f;
        reltime = 0.100f;
        cratio = 0;
        rundb = 0;
        maxover = 0;
        atcoef = Exp(-1 / (attime * sample_rate));
        relcoef = Exp(-1 / (reltime * sample_rate));

        ospl0 = {};
        ospl1 = {};
        runospl = {};
        ospl = {};
        aspl0 = {};
        aspl1 = {};
        runmax = {};
        runave = {};
    }

    f32 cthreshv {};
    f32 makeupv {};
    f32 rmscoef {};

    void Update(f32 srate) {
        auto thresh = slider_threshold;
        auto cthresh = (k_slider_knee_type ? (thresh - 3) : thresh);
        cthreshv = Exp(cthresh * k_db2log);

        f32 autogain;
        if (slider_auto_gain)
            autogain = (Fabs(thresh) - (Fabs(thresh) / Max(1.0f, slider_ratio - 1))) / 2;
        else
            autogain = 0;
        auto makeup = slider_gain;
        makeupv = Exp((makeup + autogain) * k_db2log);

        if constexpr (k_slider_detection_mode)
            rmscoef = Exp(-1000 / (10 * srate)); // 10 ms RMS window
        else
            rmscoef = Exp(-1000 / (0.0025f * srate)); // 2.5 us Peak detector
    }

    f32 ospl0 {};
    f32 ospl1 {};
    f32 runospl {};
    f32 ospl {};
    f32 aspl0 {};
    f32 aspl1 {};
    f32 runmax {};
    f32 runave {};

    void Process(f32 srate, f32 spl0, f32 spl1, f32& out_spl0, f32& out_spl1) {
        if constexpr (k_slider_detection_source) {
            ospl = ospl0 * ospl0 + ospl1 * ospl1;
            if (ospl > runospl)
                runospl = ospl + atcoef * (runospl - ospl);
            else
                runospl = ospl + relcoef * (runospl - ospl);
            ospl = Sqrt(Max(0.0f, runospl));
            ospl *= 0.5f;

            aspl0 = Fabs(ospl);
            aspl1 = Fabs(ospl);
        } else {
            aspl0 = Fabs(spl0);
            aspl1 = Fabs(spl1);
        }

        f32 ave;
        f32 det;
        if constexpr (k_slider_detection_mode) {
            ave = (aspl0 * aspl0) + (aspl1 * aspl1);
            runave = ave + rmscoef * (runave - ave);
            det = Sqrt(Max(0.0f, runave));
        } else {
            auto maxspl = Max(aspl0, aspl1);
            maxspl = maxspl * maxspl;
            runave = maxspl + rmscoef * (runave - maxspl);
            det = Sqrt(Max(0.0f, runave));
        }
        auto overdb = 2.08136898f * Log(det / cthreshv) * k_log2db;
        if (overdb > maxover) {
            maxover = overdb;
            attime = k_attack_times[(usize)Max(0.0f, Floor(Fabs(overdb)))]; // attack time per formula
            atcoef = Exp(-1 / (attime * srate));
            reltime = overdb / 125; // release at constant 125 dB/sec.
            relcoef = Exp(-1 / (reltime * srate));
        }
        overdb = Max(0.0f, overdb);

        if (overdb > rundb)
            rundb = overdb + atcoef * (rundb - overdb);
        else
            rundb = overdb + relcoef * (rundb - overdb);
        overdb = rundb;

        cratio = (k_slider_knee_type ? (1 + ((slider_ratio - 1) * Min(overdb, 6.0f) / 6)) : slider_ratio);

        auto gr = -overdb * (cratio - 1) / cratio;
        auto grv = Exp(gr * k_db2log);

        runmax = maxover + relcoef * (runmax - maxover); // highest peak for setting att/rel decays in reltime
        maxover = runmax;

        spl0 *= grv * makeupv;
        spl1 *= grv * makeupv;

        ospl0 = spl0;
        ospl1 = spl1;

        out_spl0 = ospl0;
        out_spl1 = ospl1;
    }
};
