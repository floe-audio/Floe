// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

struct VolumeFade {
    enum class State {
        FullVolume,
        Silent,
        FadeIn,
        FadeOut,

        NoStateChanged, // Special value used for function returns
    };

    VolumeFade(State initial_state = State::Silent) : m_state(initial_state) {
        if (m_state == State::FullVolume || m_state == State::FadeOut)
            m_phase = 1;
        else
            m_phase = 0;
    }

    void ForceSetAsFadeIn(f32 sample_rate, f32 ms_for_fade_in = 0.25f) {
        m_state = State::FadeIn;
        m_phase = 0;
        auto const samples_for_fade = sample_rate * (ms_for_fade_in / 1000.0f);
        m_increment = 1.0f / samples_for_fade;
    }

    void ForceSetFullVolume() {
        m_state = State::FullVolume;
        m_phase = 1;
        m_increment = 0;
    }

    void SetAsFadeIn(f32 sample_rate, f32 ms_for_fade_in = 0.25f) {
        if (IsFullVolume()) return;

        auto const samples_for_fade = sample_rate * (ms_for_fade_in / 1000.0f);
        ASSERT(m_phase >= 0 && m_phase <= 1);
        m_increment = 1.0f / samples_for_fade;
        m_state = State::FadeIn;
    }

    void SetAsFadeOut(f32 sample_rate, f32 ms_for_fade_out = 10) {
        if (IsSilent()) return;

        auto const samples_for_fade = sample_rate * (ms_for_fade_out / 1000.0f);
        m_increment = 1.0f / samples_for_fade;
        m_state = State::FadeOut;
    }

    void SetAsFadeOutIfNotAlready(f32 sample_rate, f32 ms_for_fade_out = 10) {
        if (m_state == State::FadeOut) return;
        SetAsFadeOut(sample_rate, ms_for_fade_out);
    }

    f32 GetFade() {
        switch (m_state) {
            case State::FullVolume: return 1;
            case State::Silent: return 0;
            case State::FadeIn: {
                if (m_phase >= 1) {
                    m_state = State::FullVolume;
                    m_phase = 1;
                    return 1;
                }
                break;
            }
            case State::FadeOut: {
                if (m_phase <= 0) {
                    m_state = State::Silent;
                    m_phase = 0;
                    return 0;
                }
                break;
            }
            case State::NoStateChanged: break;
        }
        auto result = QuarterSineFade(Clamp(m_phase, 0.0f, 1.0f));
        m_phase += (m_state == State::FadeIn) ? m_increment : -m_increment;
        return result;
    }

    struct FadeResult {
        f32 value;
        State state_changed;
    };

    FadeResult GetFadeAndStateChange() {
        auto const initial_state = m_state;
        FadeResult r;
        r.value = GetFade();
        r.state_changed = (initial_state != m_state) ? m_state : State::NoStateChanged;
        return r;
    }

    State JumpMultipleSteps(u32 steps) {
        if (m_state == State::FullVolume || m_state == State::Silent) return State::NoStateChanged;

        auto const delta = m_increment * (f32)steps;
        m_phase += (m_state == State::FadeIn) ? delta : -delta;

        if (m_state == State::FadeOut && m_phase <= 0) {
            m_state = State::Silent;
            m_phase = 0;
            return m_state;
        }

        if (m_state == State::FadeIn && m_phase >= 1) {
            m_state = State::FullVolume;
            m_phase = 1;
            return m_state;
        }

        return State::NoStateChanged;
    }

    bool IsSilent() const { return m_state == State::Silent; }
    bool IsFullVolume() const { return m_state == State::FullVolume; }
    bool IsFadingIn() const { return m_state == State::FadeIn; }
    bool IsFadingOut() const { return m_state == State::FadeOut; }
    State GetCurrentState() const { return m_state; }

  private:
    State m_state {State::Silent};
    f32 m_increment {};
    f32 m_phase {};
};
