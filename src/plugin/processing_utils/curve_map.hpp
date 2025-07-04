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

    // This constant controls how extreme the curves can get - it scales the curve parameter (-1.0 to 1.0) to
    // create exponents ranging from 0.25 to 4.0, which gives a good balance between subtle and dramatic curve
    // shapes without becoming unusably extreme.
    static constexpr float k_curve_exponent_multiplier = 3.0f;

    using FloatArray = Array<f32, 200>;
    using Points = DynamicArrayBounded<Point, 8>;

    void SetNewPoints(Points const& new_points) {
        points = new_points;
        RenderCurveToLookupTable();
    }

    // Producer thread. Audio thread can read the lookup table.
    void RenderCurveToLookupTable() {
        auto& table = lookup_table.Write();
        DEFER { lookup_table.Publish(); };

        if (points.size == 0) {
            // Fill with linear curve if no points
            for (usize i = 0; i < table.size; ++i)
                table[i] = (f32)i / (f32)(table.size - 1);
            return;
        }

        if (points.size == 1) {
            auto const& p = points[0];

            if (p.x == 0.0f) {
                // Point at start - constant value from start, then linear to (1,1)
                for (usize i = 0; i < table.size; ++i) {
                    f32 x = (f32)i / (f32)(table.size - 1);
                    if (x == 0.0f)
                        table[i] = p.y;
                    else
                        table[i] = p.y + ((1.0f - p.y) / 1.0f) * x; // Linear from point to (1,1)
                }
            } else if (p.x == 1.0f) {
                // Point at end - linear from (0,0) to point
                for (usize i = 0; i < table.size; ++i) {
                    f32 x = (f32)i / (f32)(table.size - 1);
                    table[i] = p.y * x; // Linear from (0,0) to point
                }
            } else {
                // Point in middle - linear from (0,0) through point to (1,1)
                for (usize i = 0; i < table.size; ++i) {
                    f32 x = (f32)i / (f32)(table.size - 1);
                    if (x <= p.x)
                        table[i] = (p.y / p.x) * x; // Linear from (0,0) to point
                    else
                        table[i] =
                            p.y + ((1.0f - p.y) / (1.0f - p.x)) * (x - p.x); // Linear from point to (1,1)
                }
            }
            return;
        }

        for (usize i = 0; i < table.size; ++i) {
            f32 x = (f32)i / (f32)(table.size - 1);

            // Handle before first point
            if (x < points[0].x) {
                table[i] = (points[0].y / points[0].x) * x; // Linear from (0,0)
                continue;
            }

            // Handle after last point
            if (x > points[points.size - 1].x) {
                auto const& last = points[points.size - 1];
                table[i] = last.y + ((1.0f - last.y) / (1.0f - last.x)) * (x - last.x); // Linear to (1,1)
                continue;
            }

            // Find the segment this x falls into
            usize segment = 0;
            for (usize j = 0; j < points.size - 1; ++j) {
                if (x >= points[j].x && x <= points[j + 1].x) {
                    segment = j;
                    break;
                }
            }

            // Interpolate within the segment
            auto const& p0 = points[segment];
            auto const& p1 = points[segment + 1];

            if (p0.x == p1.x) {
                table[i] = p0.y;
            } else {
                f32 t = (x - p0.x) / (p1.x - p0.x);
                // Apply curve
                if (p0.curve > 0.0f)
                    t = Pow(t, 1.0f + (p0.curve * k_curve_exponent_multiplier)); // Exponential
                else if (p0.curve < 0.0f)
                    t = 1.0f - Pow(1.0f - t, 1.0f - (p0.curve * k_curve_exponent_multiplier)); // Logarithmic
                table[i] = p0.y + (p1.y - p0.y) * t;
            }
        }
    }

    AtomicSwapBuffer<FloatArray, true> lookup_table;
    Points points;
};
