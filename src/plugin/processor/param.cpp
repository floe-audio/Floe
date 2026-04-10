// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "param.hpp"

#include "tests/framework.hpp"

TEST_CASE(TestEnumParamWithLegacies) {
    // These might not actually be real legacy remapping, but it doesn't matter for the test.
    auto const legacy_v1 = ParamIndexFromLayerParamIndex(0, LayerParamIndex::LegacyLfoShape);
    auto const legacy_v2 = ParamIndexFromLayerParamIndex(0, LayerParamIndex::LfoShape);
    auto const current = ParamIndexFromLayerParamIndex(1, LayerParamIndex::LfoShape);

    EnumParamWithLegacies<param_values::LfoShape, 2> p {
        .current_idx = current,
        .legacies = {{{legacy_v1}, {legacy_v2}}},
    };

    {
        Parameters params {};
        ChangedParams cp {params, {}};
        REQUIRE(!p.Poll(cp).HasValue());
    }
    {
        Parameters params {};
        ChangedParams cp {params, {}};
        params.values[ToInt(current)] = 3.0f;
        cp.changed.Set(ToInt(current));
        REQUIRE_EQ(*p.Poll(cp), param_values::LfoShape::Square);
    }
    {
        Parameters params {};
        ChangedParams cp {params, {}};
        params.values[ToInt(legacy_v1)] = 1.0f;
        cp.changed.Set(ToInt(legacy_v1));
        params.values[ToInt(current)] = 3.0f;
        cp.changed.Set(ToInt(current));
        REQUIRE_EQ(*p.Poll(cp), param_values::LfoShape::Triangle);
    }
    {
        Parameters params {};
        ChangedParams cp {params, {}};
        params.values[ToInt(legacy_v1)] = 0.0f;
        cp.changed.Set(ToInt(legacy_v1));
        params.values[ToInt(current)] = 5.0f;
        cp.changed.Set(ToInt(current));
        REQUIRE_EQ(*p.Poll(cp), param_values::LfoShape::RandomGlide);
    }
    {
        Parameters params {};
        ChangedParams cp {params, {}};
        params.values[ToInt(legacy_v1)] = 1.0f;
        cp.changed.Set(ToInt(legacy_v1));
        params.values[ToInt(legacy_v2)] = 3.0f;
        cp.changed.Set(ToInt(legacy_v2));
        params.values[ToInt(current)] = 5.0f;
        cp.changed.Set(ToInt(current));
        REQUIRE_EQ(*p.Poll(cp), param_values::LfoShape::Triangle);
    }
    {
        Parameters params {};
        ChangedParams cp {params, {}};
        params.values[ToInt(legacy_v1)] = 0.0f;
        cp.changed.Set(ToInt(legacy_v1));
        params.values[ToInt(legacy_v2)] = 3.0f;
        cp.changed.Set(ToInt(legacy_v2));
        params.values[ToInt(current)] = 5.0f;
        cp.changed.Set(ToInt(current));
        REQUIRE_EQ(*p.Poll(cp), param_values::LfoShape::Square);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterParamTests) { REGISTER_TEST(TestEnumParamWithLegacies); }
