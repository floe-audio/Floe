// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "param.hpp"

#include "tests/framework.hpp"

TEST_CASE(TestIntValueLegacyAware) {
    auto const legacy_v1 = ParamIndexFromLayerParamIndex(0, LayerParamIndex::LegacyLfoShape);
    auto const legacy_v2 = ParamIndexFromLayerParamIndex(0, LayerParamIndex::LegacyLfoShapeV2);
    auto const current = ParamIndexFromLayerParamIndex(0, LayerParamIndex::LfoShape);

    auto const poll = [&](Parameters const& params, Bitset<k_num_parameters> const& changed) {
        return ChangedParams {params, changed}.IntValueLegacyAware<param_values::LfoShape>(current);
    };

    {
        Parameters params {};
        REQUIRE(!poll(params, {}).HasValue());
    }
    {
        Parameters params {};
        Bitset<k_num_parameters> changed {};
        params.values[ToInt(current)] = 3.0f;
        changed.Set(ToInt(current));
        REQUIRE_EQ(*poll(params, changed), param_values::LfoShape::Square);
    }
    {
        // V1 overrides — V1 value walks V1 → V2 → modern (identity for first 4 enum values).
        Parameters params {};
        Bitset<k_num_parameters> changed {};
        params.values[ToInt(legacy_v1)] = 1.0f;
        changed.Set(ToInt(legacy_v1));
        params.values[ToInt(current)] = 3.0f;
        changed.Set(ToInt(current));
        REQUIRE_EQ(*poll(params, changed), param_values::LfoShape::Triangle);
    }
    {
        // V1 at default — V2 wins. V2 value 5 (RandomGlide) walks V2 → modern unchanged.
        Parameters params {};
        Bitset<k_num_parameters> changed {};
        params.values[ToInt(legacy_v1)] = 0.0f;
        changed.Set(ToInt(legacy_v1));
        params.values[ToInt(legacy_v2)] = 5.0f;
        changed.Set(ToInt(legacy_v2));
        params.values[ToInt(current)] = 0.0f;
        changed.Set(ToInt(current));
        REQUIRE_EQ(*poll(params, changed), param_values::LfoShape::RandomGlide);
    }
    {
        // V1 and V2 both overriding — oldest wins (V1).
        Parameters params {};
        Bitset<k_num_parameters> changed {};
        params.values[ToInt(legacy_v1)] = 1.0f;
        changed.Set(ToInt(legacy_v1));
        params.values[ToInt(legacy_v2)] = 3.0f;
        changed.Set(ToInt(legacy_v2));
        params.values[ToInt(current)] = 5.0f;
        changed.Set(ToInt(current));
        REQUIRE_EQ(*poll(params, changed), param_values::LfoShape::Triangle);
    }
    {
        // Only V2 overrides.
        Parameters params {};
        Bitset<k_num_parameters> changed {};
        params.values[ToInt(legacy_v1)] = 0.0f;
        changed.Set(ToInt(legacy_v1));
        params.values[ToInt(legacy_v2)] = 3.0f;
        changed.Set(ToInt(legacy_v2));
        params.values[ToInt(current)] = 5.0f;
        changed.Set(ToInt(current));
        REQUIRE_EQ(*poll(params, changed), param_values::LfoShape::Square);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterParamTests) { REGISTER_TEST(TestIntValueLegacyAware); }
