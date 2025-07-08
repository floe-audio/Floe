// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"
#include "utils/thread_extra/atomic_swap_buffer.hpp"

struct CurveMap {
    struct Point {
        bool operator==(Point const& other) const = default;

        // Normalised 0.0-1.0.
        f32 x, y;
        // -1.0 to 1.0, where 0=linear, >0=exponential, <0=logarithmic.
        // Controls the line after this point.
        f32 curve;
    };

    // Used just to simplify the code.
    struct WorkingPoint : Point {
        bool is_virtual;
        s8 real_index;
    };

    // This constant controls how extreme the curves can get - it scales the curve parameter (-1.0 to 1.0).
    static constexpr float k_curve_exponent_multiplier = 6.0f;

    using FloatArray = Array<f32, 200>;
    using Points = DynamicArrayBounded<Point, 8>;
    using WorkingPoints = DynamicArrayBounded<WorkingPoint, Points::Capacity() + 2>;

    // Producer thread.
    static WorkingPoints CreateWorkingPoints(Points const& user_points) {
        WorkingPoints working;

        if (user_points.size) {
            // Add virtual (0,0) if first point isn't at origin
            if (user_points[0].x > 0.0f)
                dyn::Append(working, {{0.0f, 0.0f, 0.0f}, true, -1}); // curve=0 for linear

            // Add all user points
            for (auto const [index, p] : Enumerate<s8>(user_points))
                dyn::Append(working, {{p.x, p.y, p.curve}, false, index});

            // Add virtual (1,1) if last point isn't at end
            if (user_points[user_points.size - 1].x < 1.0f)
                dyn::Append(working, {{1.0f, 1.0f, 0.0f}, true, -2});
        } else {
            dyn::AppendSpan(working,
                            Array {
                                WorkingPoint {{0.0f, 0.0f, 0.0f}, true, -1},
                                WorkingPoint {{1.0f, 1.0f, 0.0f}, true, -2},
                            });
        }

        return working;
    }

    // Producer thread.
    void SetNewPoints(Points const& new_points) {
        points = new_points;
        RenderCurveToLookupTable();
    }

    static f32 ValueAt(Span<WorkingPoint const> working, f32 x) {
        usize segment = 0;
        for (usize j = 0; j < working.size - 1; ++j) {
            if (x >= working[j].x && x <= working[j + 1].x) {
                segment = j;
                break;
            }
        }

        auto const& p0 = working[segment];
        auto const& p1 = working[segment + 1];

        if (p0.x == p1.x) {
            return p0.y;
        } else {
            f32 t = (x - p0.x) / (p1.x - p0.x);
            if (p0.curve > 0.0f)
                t = Pow(t, 1.0f + (p0.curve * k_curve_exponent_multiplier)); // Exponential
            else if (p0.curve < 0.0f)
                t = 1.0f - Pow(1.0f - t, 1.0f - (p0.curve * k_curve_exponent_multiplier)); // Logarithmic
            return p0.y + ((p1.y - p0.y) * t);
        }
    }

    // Producer thread. Audio thread can read the lookup table.
    void RenderCurveToLookupTable() {
        auto& table = lookup_table.Write();
        DEFER { lookup_table.Publish(); };

        auto working = CreateWorkingPoints(points);

        for (usize i = 0; i < table.size; ++i) {
            f32 x = (f32)i / (f32)(table.size - 1);
            table[i] = ValueAt(working, x);
        }
    }

    AtomicSwapBuffer<FloatArray, true> lookup_table;
    Points points;
};
