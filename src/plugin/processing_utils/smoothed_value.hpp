// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

class SmoothedValueFilter {
  public:
    static constexpr f32 k_default_cutoff = 0.05f;

    void ResetWithValue(f32 v) {
        SetValue(v);
        ResetSmoothing();
    }
    void ResetSmoothing() { m_prev = m_value; }
    void SetValue(f32 v) { m_value = v; }
    f32 GetUnsmoothedValue() const { return m_value; }
    void SetPreviousValue(f32 v) { m_prev = v; }

    f32 Get01Value(f32 const cutoff01 = k_default_cutoff) {
        auto local_value_instance = m_value;
        auto result = m_prev;
        m_prev = m_prev + cutoff01 * (local_value_instance - m_prev);
        if (m_prev < 0 || m_prev > 1) m_prev = local_value_instance;
        return result;
    }

    f32 GetValue(f32 const cutoff01 = k_default_cutoff) {
        f32 const result = m_prev + (cutoff01 * (m_value - m_prev));
        m_prev = result;
        return result;
    }

  private:
    f32 m_prev {0.0f};
    f32 m_value {0.0f};
};
