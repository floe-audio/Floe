// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"
#include "utils/leak_detecting_allocator.hpp"

TEST_CASE(TestWriter) {
    SUBCASE("alloced") {
        LeakDetectingAllocator a;
        DynamicArray<char> buf {a};
        auto writer = dyn::WriterFor(buf);
        TRY(writer.WriteBytes(Array {(u8)'a'}));
        CHECK_EQ(buf.Items(), "a"_s);
    }

    SUBCASE("inline") {
        DynamicArrayBounded<char, 128> buf {};
        auto writer = dyn::WriterFor(buf);
        TRY(writer.WriteBytes(Array {(u8)'a'}));
        CHECK_EQ(buf.Items(), "a"_s);
    }

    SUBCASE("BufferedWriter") {
        LeakDetectingAllocator a;
        DynamicArray<char> buf {a};

        BufferedWriter<16> buffered_writer {dyn::WriterFor(buf)};
        auto writer = buffered_writer.Writer();

        TRY(writer.WriteChars("hello"_s));
        TRY(buffered_writer.Flush());
        CHECK_EQ(buf.Items(), "hello"_s);

        TRY(writer.WriteChars(" world"_s));
        CHECK_EQ(buf.Items(), "hello"_s);
        TRY(buffered_writer.Flush());
        CHECK_EQ(buf.Items(), "hello world"_s);

        TRY(writer.WriteChars("01234567890123456789"_s));
        TRY(buffered_writer.Flush());
        CHECK_EQ(buf.Items(), "hello world01234567890123456789"_s);

        dyn::Clear(buf);

        // ensure that when it reaches the end of the buffer it correctly flushs and continues without missing
        // any characters
        for (auto ch : Range<char>('a', 'z' + 1))
            TRY(writer.WriteChar(ch));
        TRY(buffered_writer.Flush());

        CHECK_EQ(buf.Items(), "abcdefghijklmnopqrstuvwxyz"_s);
    }
    return k_success;
}

TEST_REGISTRATION(RegisterWriterTests) { REGISTER_TEST(TestWriter); }
