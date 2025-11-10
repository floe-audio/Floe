// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

TEST_CASE(TestSprintfBuffer) {
    InlineSprintfBuffer buffer {};
    CHECK_EQ(buffer.AsString(), String {});
    buffer.Append("%s", "foo");
    CHECK_EQ(buffer.AsString(), "foo"_s);
    buffer.Append("%d", 1);
    CHECK_EQ(buffer.AsString(), "foo1"_s);

    char b[2048];
    FillMemory({(u8*)b, ArraySize(b)}, 'a');
    b[ArraySize(b) - 1] = 0;
    buffer.Append("%s", b);
    CHECK_EQ(buffer.AsString().size, ArraySize(buffer.buffer));

    return k_success;
}

TEST_REGISTRATION(RegisterAssertFTests) { REGISTER_TEST(TestSprintfBuffer); }
