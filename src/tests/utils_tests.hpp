// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <math.h>

#include "os/threading.hpp"
#include "tests/framework.hpp"
#include "utils/error_notifications.hpp"
#include "utils/json/json_reader.hpp"
#include "utils/json/json_writer.hpp"
#include "utils/leak_detecting_allocator.hpp"
#include "utils/thread_extra/atomic_ref_list.hpp"
#include "utils/thread_extra/atomic_swap_buffer.hpp"
#include "utils/thread_extra/starting_gun.hpp"
#include "utils/thread_extra/thread_pool.hpp"

TEST_CASE(TestErrorNotifications) {
    ThreadsafeErrorNotifications n;
    u64 const id1 = 54301239845687;
    u64 const id2 = 61398210056122;

    SUBCASE("basic operations") {
        // Add an item.
        {
            auto item = n.BeginWriteError(id1);
            REQUIRE(item);
            DEFER { n.EndWriteError(*item); };
            item->title = "Error"_s;
        }

        // Check we can read it.
        {
            usize count = 0;
            n.ForEach([&](ThreadsafeErrorNotifications::Item const& item) {
                CHECK_EQ(item.title, "Error"_s);
                ++count;
                return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
            });
            CHECK_EQ(count, 1uz);
        }

        // Remove it.
        CHECK(n.RemoveError(id1));

        // Removing a non-existing item does't work.
        CHECK(!n.RemoveError(100));

        // Check it is gone.
        {
            usize count = 0;
            n.ForEach([&](ThreadsafeErrorNotifications::Item const&) {
                ++count;
                return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
            });
            CHECK_EQ(count, 0uz);
        }
    }

    SUBCASE("update error") {
        // Add an item.
        {
            auto item = n.BeginWriteError(id1);
            REQUIRE(item);
            DEFER { n.EndWriteError(*item); };
            item->title = "Error"_s;
        }

        // Update it.
        {
            auto item = n.BeginWriteError(id1);
            REQUIRE(item);
            DEFER { n.EndWriteError(*item); };
            item->title = "Updated Error"_s;
        }

        // Check we can read it.
        {
            usize count = 0;
            n.ForEach([&](ThreadsafeErrorNotifications::Item const& item) {
                CHECK_EQ(item.title, "Updated Error"_s);
                ++count;
                return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
            });
            CHECK_EQ(count, 1uz);
        }
    }

    SUBCASE("remove an error while it's in begin/end section") {
        // Begin.
        auto item = n.BeginWriteError(id1);
        REQUIRE(item);
        item->title = "Error"_s;

        // Remove it.
        CHECK(n.RemoveError(id1));

        // End.
        n.EndWriteError(*item);

        // This is allowed behaviour. It should be empty now.
        {
            usize count = 0;
            n.ForEach([&](ThreadsafeErrorNotifications::Item const&) {
                ++count;
                return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
            });
            CHECK_EQ(count, 0uz);
        }
    }

    SUBCASE("multiple begin/end sections simultaneously") {
        auto item1 = n.BeginWriteError(id1);
        REQUIRE(item1);
        item1->title = "Error 1"_s;

        auto item2 = n.BeginWriteError(id2);
        REQUIRE(item2);
        item2->title = "Error 2"_s;

        n.EndWriteError(*item1);
        n.EndWriteError(*item2);

        // Check both are present
        {
            usize count = 0;
            n.ForEach([&](ThreadsafeErrorNotifications::Item const& item) {
                auto const id = item.id.Load(LoadMemoryOrder::Acquire);
                if (id == id1)
                    CHECK_EQ(item.title, "Error 1"_s);
                else if (id == id2)
                    CHECK_EQ(item.title, "Error 2"_s);
                else
                    TEST_FAILED("Unexpected item ID: {}", id);
                ++count;
                return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
            });
            CHECK_EQ(count, 2uz);
        }
    }

    SUBCASE("multiple threads") {
        Atomic<u32> iterations {0};
        constexpr u32 k_num_iterations = 10000;
        Array<Thread, 4> producers;
        Atomic<bool> thread_ready {false};
        StartingGun starting_gun;
        Atomic<u64> next_id {1};

        for (auto& p : producers) {
            p.Start(
                [&]() {
                    auto seed = RandomSeed();

                    thread_ready.Store(true, StoreMemoryOrder::Release);
                    starting_gun.WaitUntilFired();

                    while (iterations.Load(LoadMemoryOrder::Acquire) < k_num_iterations) {
                        auto const id = next_id.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
                        if (RandomIntInRange<u32>(seed, 0, 5) == 0) {
                            n.RemoveError(Max(id - 2, (u64)1));
                        } else if (auto item = n.BeginWriteError(id)) {
                            DEFER { n.EndWriteError(*item); };
                            item->title = "title"_s;

                            // Simulate an amount of work
                            auto const volatile work_size = RandomIntInRange<usize>(seed, 0, 500);
                            u32 volatile work = 0;
                            for (; work < work_size; work += 1)
                                (void)work;

                            item->message = "message"_s;
                            item->error_code = {};
                        }

                        iterations.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
                        YieldThisThread();
                    }
                },
                "producer");
        }

        auto seed = RandomSeed();

        while (!thread_ready.Load(LoadMemoryOrder::Acquire))
            YieldThisThread();

        starting_gun.Fire();
        while (iterations.Load(LoadMemoryOrder::Acquire) < k_num_iterations) {
            n.ForEach([&](ThreadsafeErrorNotifications::Item const& item) {
                // Let's occasionally remove an item.
                if (RandomIntInRange<u32>(seed, 0, 3) == 0)
                    return ThreadsafeErrorNotifications::ItemIterationResult::Remove;

                CHECK_EQ(item.title, "title"_s);

                // Simulate an amount of work
                auto const volatile work_size = RandomIntInRange<usize>(seed, 0, 500);
                u32 volatile work = 0;
                for (; work < work_size; work += 1)
                    (void)work;

                CHECK_EQ(item.message, "message"_s);

                return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
            });
            YieldThisThread();
        }

        for (auto& p : producers)
            p.Join();
    }

    return k_success;
}

TEST_CASE(TestJsonWriter) {
    using namespace json;

    json::WriteContext write_ctx {};
    DynamicArray<char> output {Malloc::Instance()};

    SUBCASE("basics") {
        write_ctx = {.out = dyn::WriterFor(output), .add_whitespace = true};

        {
            TRY(WriteObjectBegin(write_ctx));
            DEFER { REQUIRE_UNWRAP(WriteObjectEnd(write_ctx)); };

            u8 const v1 {};
            u16 const v2 {};
            u32 const v3 {};
            u64 const v4 {};
            s8 const v5 {};
            s16 const v6 {};
            s32 const v7 {};
            s64 const v8 {};
            f32 const v10 {};
            f64 const v11 {};
            bool const v12 {};

            TRY(WriteKeyValue(write_ctx, "smol", 1.0 / 7.0));
            TRY(WriteKeyValue(write_ctx, "big", Pow(k_pi<>, 25.0f)));

            TRY(WriteKeyValue(write_ctx, "v1", v1));
            TRY(WriteKeyValue(write_ctx, "v2", v2));
            TRY(WriteKeyValue(write_ctx, "v3", v3));
            TRY(WriteKeyValue(write_ctx, "v4", v4));
            TRY(WriteKeyValue(write_ctx, "v5", v5));
            TRY(WriteKeyValue(write_ctx, "v6", v6));
            TRY(WriteKeyValue(write_ctx, "v7", v7));
            TRY(WriteKeyValue(write_ctx, "v8", v8));
            TRY(WriteKeyValue(write_ctx, "v10", v10));
            TRY(WriteKeyValue(write_ctx, "v11", v11));
            TRY(WriteKeyValue(write_ctx, "v12", v12));
            TRY(WriteKeyNull(write_ctx, "null"));

            TRY(WriteKeyValue(write_ctx, "key", 100));
            TRY(WriteKeyValue(write_ctx, "key2", 0.4));
            TRY(WriteKeyValue(write_ctx, "key", "string"));

            DynamicArray<String> strs {Malloc::Instance()};
            dyn::Assign(strs, ArrayT<String>({"hey", "ho", "yo"}));
            TRY(WriteKeyValue(write_ctx, "string array", strs.Items()));

            {
                TRY(WriteKeyArrayBegin(write_ctx, "array"));
                DEFER { REQUIRE_UNWRAP(WriteArrayEnd(write_ctx)); };

                TRY(WriteValue(write_ctx, v1));
                TRY(WriteValue(write_ctx, v2));
                TRY(WriteValue(write_ctx, v3));
                TRY(WriteValue(write_ctx, v4));
                TRY(WriteValue(write_ctx, v5));
                TRY(WriteValue(write_ctx, v6));
                TRY(WriteValue(write_ctx, v7));
                TRY(WriteValue(write_ctx, v8));
                TRY(WriteValue(write_ctx, v10));
                TRY(WriteValue(write_ctx, v11));
                TRY(WriteValue(write_ctx, v12));
                TRY(WriteNull(write_ctx));

                TRY(WriteValue(write_ctx, "string"));

                TRY(WriteValue(write_ctx, strs.Items()));
            }
        }

        tester.log.Debug("{}", output);

        CHECK(Parse(output, [](EventHandlerStack&, Event const&) { return true; }, tester.scratch_arena, {})
                  .Succeeded());
    }

    SUBCASE("utf8") {
        write_ctx = {.out = dyn::WriterFor(output), .add_whitespace = false};
        TRY(WriteArrayBegin(write_ctx));
        TRY(WriteValue(write_ctx, "H:/Floe PresetsÉe"));
        TRY(WriteArrayEnd(write_ctx));

        tester.log.Debug("{}", output);
        CHECK_EQ(output.Items(), "[\"H:/Floe PresetsÉe\"]"_s);
    }
    return k_success;
}

TEST_CASE(TestJsonReader) {
    using namespace json;

    LeakDetectingAllocator const leak_detecting_a;
    ReaderSettings settings {};

    auto callback = [](EventHandlerStack&, Event const& event) {
        if constexpr (0) {
            switch (event.type) {
                case EventType::String:
                    tester.log.Debug("JSON event String: {} -> {}", event.key, event.string);
                    break;
                case EventType::Double:
                    tester.log.Debug("JSON event f64: {} -> {}", event.key, event.real);
                    break;
                case EventType::Int:
                    tester.log.Debug("JSON event Int: {} -> {}", event.key, event.integer);
                    break;
                case EventType::Bool:
                    tester.log.Debug("JSON event Bool: {} -> {}", event.key, event.boolean);
                    break;
                case EventType::Null: tester.log.Debug("JSON event Null: {}", event.key); break;
                case EventType::ObjectStart: tester.log.Debug("JSON event ObjectStart: {}", event.key); break;
                case EventType::ObjectEnd: tester.log.Debug("JSON event ObjectEnd"); break;
                case EventType::ArrayStart: tester.log.Debug("JSON event ArrayStart, {}", event.key); break;
                case EventType::ArrayEnd: tester.log.Debug("JSON event ArrayEnd"); break;
                case EventType::HandlingStarted: tester.log.Debug("Json event HandlingStarting"); break;
                case EventType::HandlingEnded: tester.log.Debug("Json event HandlingEnded"); break;
            }
        }
        return true;
    };

    SUBCASE("foo") {
        String const test = "{\"description\":\"Essential data for Floe\",\"name\":\"Core\",\"version\":1}";

        struct Data {
            u32 id_magic = {};
            Array<char, 64> name {};
            u32 version;
            u32 name_hash {0};
            String description {};
            String url {};
            String default_inst_path {};
            Version required_floe_version {};
            String file_extension {};
        };

        Data data;
        auto& a = tester.scratch_arena;
        auto parsed = Parse(
            test,
            [&data, &a](EventHandlerStack&, Event const& event) {
                if (SetIfMatching(event, "description", data.description, a)) return true;
                if (SetIfMatching(event, "url", data.url, a)) return true;
                if (SetIfMatching(event, "default_inst_relative_folder", data.default_inst_path, a))
                    return true;
                if (SetIfMatching(event, "file_extension", data.file_extension, a)) return true;
                if (SetIfMatching(event, "required_floe_version_major", data.required_floe_version.major))
                    return true;
                if (SetIfMatching(event, "required_floe_version_minor", data.required_floe_version.minor))
                    return true;
                if (SetIfMatching(event, "required_floe_version_patch", data.required_floe_version.patch))
                    return true;
                return false;
            },
            tester.scratch_arena,
            {});

        CHECK(!parsed.HasError());
    }

    SUBCASE("test1") {
        String const test = R"foo(
        {
            "name" : "Wraith",
            "param" : {
                "value" : 0.1,
                "hash" : 987234
            },
            "packs" : [
                {
                    "name" : "abc",
                    "hash" : 923847
                },
                {
                    "name" : "def",
                    "hash" : 58467
                }
            ],
            "numbers" : [ 0, 5, 6, 7, 8 ],
            "boolean" : false
        }
        )foo";

        REQUIRE(Parse(test, callback, tester.scratch_arena, settings).Succeeded());
    }

    SUBCASE("test2") {
        // http://json.org/JSON_checker/
        String const test = R"foo(
        [
            "JSON Test Pattern pass1",
            {"object with 1 member":["array with 1 element"]},
            {},
            [],
            -42,
            true,
            false,
            null,
            {
                "integer": 1234567890,
                "real": -9876.543210,
                "e": 0.123456789e-12,
                "E": 1.234567890E+34,
                "":  23456789012E66,
                "zero": 0,
                "one": 1,
                "space": " ",
                "quote": "\"",
                "backslash": "\\",
                "controls": "\b\f\n\r\t",
                "slash": "/ & \/",
                "alpha": "abcdefghijklmnopqrstuvwyz",
                "ALPHA": "ABCDEFGHIJKLMNOPQRSTUVWYZ",
                "digit": "0123456789",
                "0123456789": "digit",
                "special": "`1~!@#$%^&*()_+-={':[,]}|;.</>?",
                "hex": "\u0123\u4567\u89AB\uCDEF\uabcd\uef4A",
                "true": true,
                "false": false,
                "null": null,
                "array":[  ],
                "object":{  },
                "address": "50 St. James Street",
                "url": "http://www.JSON.org/",
                "comment": "// /* <!-- --",
                "# -- --> */": " ",
                " s p a c e d " :[1,2 , 3

        ,

        4 , 5        ,          6           ,7        ],"compact":[1,2,3,4,5,6,7],
                "jsontext": "{\"object with 1 member\":[\"array with 1 element\"]}",
                "quotes": "&#34; \u0022 %22 0x22 034 &#x22;",
                "\/\\\"\uCAFE\uBABE\uAB98\uFCDE\ubcda\uef4A\b\f\n\r\t`1~!@#$%^&*()_+-=[]{}|;:',./<>?"
        : "A key can be any string"
            },
            0.5 ,98.6
        ,
        99.44
        ,

        1066,
        1e1,
        0.1e1,
        1e-1,
        1e00,2e+00,2e-00
        ,"rosebud"]

        )foo";

        REQUIRE(Parse(test, callback, tester.scratch_arena, settings).Succeeded());
    }

    SUBCASE("nested test") {
        REQUIRE(Parse("[[[[[[[[[[[[[[[[[[[[[[[[[\"hello\"]]]]]]]]]]]]]]]]]]]]]]]]]",
                      callback,
                      tester.scratch_arena,
                      settings)
                    .Succeeded());
    }

    SUBCASE("should fail") {
        auto should_fail = [&](String test) {
            auto r = Parse(test, callback, tester.scratch_arena, settings);
            REQUIRE(r.HasError());
            tester.log.Debug("{}", r.Error().message);
        };

        should_fail("[\"mismatch\"}");
        should_fail("{\"nope\"}");
        should_fail("[0e]");
        should_fail("0.");
        should_fail("0.0e");
        should_fail("0.0e-");
        should_fail("0.0e+");
        should_fail("1e+");
        should_fail("{e}");
        should_fail("{1}");
        should_fail("[\"Colon instead of comma\": false]");
        should_fail("[0,]");
        should_fail("{\"key\":\"value\",}");
        should_fail("{no_quotes:\"str\"}");
    }

    SUBCASE("extra settings") {
        String const test = R"foo(
        {
            // "name" : "Wraith",
            /* "param" : {
                "value" : 0.1, 
                "hash" : 987234,
            }, */
            "packs" : [
                {
                    "name" : "abc",
                    "hash" : 923847
                },
                {
                    "name" : "def",
                    "hash" : 58467
                }
            ],
            "numbers" : [ 0, 5, 6, 7, 8, ],
            "boolean" : false,
            key_without_quotes : 10
        }
        )foo";
        settings.allow_comments = true;
        settings.allow_trailing_commas = true;
        settings.allow_keys_without_quotes = true;
        REQUIRE(Parse(test, callback, tester.scratch_arena, settings).Succeeded());
    }

    SUBCASE("newlines") {
        String const test = "{\"foo\":\r\n\"val\"}";
        REQUIRE(Parse(test, callback, tester.scratch_arena, settings).Succeeded());
    }

    SUBCASE("escape codes") {
        String const test = R"foo({ 
            "item": "value  \u000f \uFFFF \n \r \t \\ \" \/"
        })foo";
        REQUIRE(Parse(
                    test,
                    [&](EventHandlerStack&, Event const& event) {
                        if (event.type == EventType::String)
                            REQUIRE(event.string == "value  \u000f \uFFFF \n \r \t \\ \" /"_s);
                        return true;
                    },
                    tester.scratch_arena,
                    settings)
                    .Succeeded());
    }
    return k_success;
}

TEST_CASE(TestStacktraceString) {
    SUBCASE("stacktrace 1") {
        auto f = [&]() {
            auto str = CurrentStacktraceString(tester.scratch_arena,
                                               {
                                                   .ansi_colours = true,
                                               });
            tester.log.Debug("\n{}", str);
        };
        f();
    }

    SUBCASE("stacktrace 2") {
        auto f = [&]() {
            auto str = CurrentStacktraceString(tester.scratch_arena);
            tester.log.Debug("\n{}", str);
        };
        f();
    }

    SUBCASE("stacktrace 3") {
        auto f = [&]() {
            auto o = CurrentStacktrace(StacktraceFrames {1});
            if (!o.HasValue())
                LOG_WARNING("Failed to get stacktrace");
            else {
                auto str = StacktraceString(o.Value(), tester.scratch_arena);
                tester.log.Debug("\n{}", str);
            }
        };
        f();
    }

    SUBCASE("stacktrace 4") {
        auto f = [&]() {
            auto o = CurrentStacktrace(ProgramCounter {CALL_SITE_PROGRAM_COUNTER});
            if (!o.HasValue())
                LOG_WARNING("Failed to get stacktrace");
            else {
                auto str = StacktraceString(o.Value(), tester.scratch_arena);
                tester.log.Debug("\n{}", str);
            }
        };
        f();
    }

    SUBCASE("stacktrace 5") {
        bool stacktrace_has_this_function = false;
        constexpr String k_this_function = __FUNCTION__;
        CurrentStacktraceToCallback([&](FrameInfo const& frame) {
            if (ContainsSpan(frame.function_name, k_this_function)) stacktrace_has_this_function = true;
        });
        CHECK(stacktrace_has_this_function);
    }

    return k_success;
}

TEST_CASE(TestHasAddressesInCurrentModule) {
    CHECK(IsAddressInCurrentModule((uintptr)&TestHasAddressesInCurrentModule));
    CHECK(!IsAddressInCurrentModule(0));
    CHECK(!IsAddressInCurrentModule(LargestRepresentableValue<usize>()));

    auto addrs = Array {0uz, 0};
    CHECK(!HasAddressesInCurrentModule(addrs));

    addrs[0] = CALL_SITE_PROGRAM_COUNTER;
    CHECK(HasAddressesInCurrentModule(addrs));

    // This doesn't work on Windows, perhaps because we're using mingw which means it actually is in the
    // current module?
    if constexpr (!IS_WINDOWS) CHECK(!IsAddressInCurrentModule((uintptr)&powf));

    return k_success;
}

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

TEST_CASE(TestFutureAndAsync) {
    ThreadPool pool;
    pool.Init("test", 2u);

    auto cleanup = []() {};

    SUBCASE("basic async with return value") {
        Future<int> future;
        CHECK(!future.IsFinished());
        pool.Async(future, []() { return 42; }, cleanup);
        CHECK(future.WaitUntilFinished());
        REQUIRE(future.IsFinished());
        CHECK_EQ(future.Result(), 42);
    }

    SUBCASE("type with no default constructor") {
        struct NoDefault {
            NoDefault(int v) : value(v) {}
            int value;
        };
        Future<NoDefault> future;
        pool.Async(future, []() { return NoDefault(99); }, cleanup);
        CHECK(future.WaitUntilFinished());
        REQUIRE(future.IsFinished());
        CHECK_EQ(future.Result().value, 99);
    }

    SUBCASE("future lifecycle states") {
        Future<int> future;

        // Initially inactive
        CHECK(future.IsInactive());
        CHECK(!future.IsFinished());
        CHECK(!future.IsInProgress());
        CHECK(!future.IsCancelled());

        // Set to pending
        future.SetPending();
        CHECK(!future.IsInactive());
        CHECK(future.IsInProgress());
        CHECK(!future.IsFinished());

        // Simulate TrySetRunning success
        CHECK(future.TrySetRunning());
        CHECK(future.IsInProgress());
        CHECK(!future.IsFinished());

        // Set result
        future.SetResult(123);
        CHECK(!future.IsInProgress());
        CHECK(future.IsFinished());
        CHECK(future.HasResult());
        CHECK_EQ(future.Result(), 123);

        // Reset back to inactive
        future.Reset();
        CHECK(future.IsInactive());
        CHECK(!future.IsFinished());
    }

    SUBCASE("future cancellation before running") {
        Future<int> future;
        future.SetPending();

        CHECK(future.Cancel());
        CHECK(future.IsCancelled());
        CHECK(future.IsInProgress());

        // TrySetRunning should fail
        CHECK(!future.TrySetRunning());
        CHECK(future.IsCancelled());
        CHECK(future.IsInactive());
    }

    SUBCASE("future cancellation after finishing") {
        Future<int> future;
        future.SetPending();
        CHECK(future.TrySetRunning());
        future.SetResult(456);

        // Cancel after finishing should return false
        CHECK(!future.Cancel());
        CHECK(future.IsFinished());
        CHECK_EQ(future.Result(), 456);
    }

    SUBCASE("multiple cancel calls") {
        Future<int> future;
        future.SetPending();

        CHECK(future.Cancel());
        CHECK(future.IsCancelled());

        // Second cancel should still return true (already cancelled)
        CHECK(future.Cancel());
        CHECK(future.IsCancelled());

        CHECK(!future.TrySetRunning());

        CHECK(!future.ShutdownAndRelease());
    }

    SUBCASE("try release result") {
        Future<int> future;

        // No result available initially
        CHECK(!future.TryReleaseResult().HasValue());

        pool.Async(future, []() { return 789; }, cleanup);
        CHECK(future.WaitUntilFinished());

        auto result = future.TryReleaseResult();
        REQUIRE(result.HasValue());
        CHECK_EQ(*result, 789);

        // After releasing, should be inactive
        CHECK(future.IsInactive());

        // Second try should return nullopt
        CHECK(!future.TryReleaseResult().HasValue());
    }

    SUBCASE("release result") {
        Future<int> future;
        pool.Async(future, []() { return 321; }, cleanup);
        CHECK(future.WaitUntilFinished());

        int result = future.ReleaseResult();
        CHECK_EQ(result, 321);
        CHECK(future.IsInactive());
    }

    SUBCASE("shutdown with active future") {
        Future<int> future;
        Atomic<bool> work_started {false};
        Atomic<bool> do_work {false};

        pool.Async(
            future,
            [&]() {
                work_started.Store(true, StoreMemoryOrder::Release);
                while (!do_work.Load(LoadMemoryOrder::Acquire))
                    YieldThisThread();
                return 999;
            },
            cleanup);

        // Wait for work to start
        while (!work_started.Load(LoadMemoryOrder::Acquire))
            YieldThisThread();

        // Do the work
        do_work.Store(true, StoreMemoryOrder::Release);

        CHECK_EQ(*future.ShutdownAndRelease(), 999);
        CHECK(future.IsInactive());
    }

    SUBCASE("multiple futures concurrently") {
        Array<Future<int>, 5> futures;
        Atomic<int> counter {0};

        for (auto [i, future] : Enumerate(futures)) {
            pool.Async(
                future,
                [&counter, i]() {
                    return counter.FetchAdd((int)i + 1, RmwMemoryOrder::AcquireRelease) + (int)i + 1;
                },
                cleanup);
        }

        // Wait for all to complete
        for (auto& future : futures) {
            CHECK(future.WaitUntilFinished());
            CHECK(future.IsFinished());
        }

        // Results should be accumulated
        int total = 0;
        for (auto& future : futures)
            total += future.Result();

        CHECK(total > 0);
    }

    SUBCASE("rapid operations with starting gun") {
        constexpr usize k_num_operations = 1000;
        constexpr usize k_num_worker_threads = 4;

        Array<Thread, k_num_worker_threads> worker_threads;
        Array<Future<int>, k_num_operations> futures;
        StartingGun starting_gun;
        Atomic<usize> threads_ready {0};
        Atomic<usize> total_completed {0};
        Atomic<usize> total_cancelled {0};

        // Start worker threads that wait for the starting gun
        for (auto [i, thread] : Enumerate(worker_threads)) {
            thread.Start(
                [&starting_gun, &threads_ready]() {
                    threads_ready.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
                    starting_gun.WaitUntilFired();
                    // Thread is now ready to process async jobs
                },
                fmt::FormatInline<k_max_thread_name_size>("rapid-{}", i));
        }

        // Wait for all threads to be ready
        while (threads_ready.Load(LoadMemoryOrder::Acquire) < k_num_worker_threads)
            YieldThisThread();

        // Fire starting gun and rapidly create many async operations
        starting_gun.Fire();

        for (auto [i, future] : Enumerate(futures)) {
            pool.Async(
                future,
                [i, &total_completed]() {
                    // Simulate some work with random duration
                    auto seed = RandomSeed();
                    auto work_cycles = RandomIntInRange(seed, 1, 100);
                    for (auto _ : Range(work_cycles))
                        SpinLoopPause();

                    total_completed.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
                    return (int)i;
                },
                []() {} // empty cleanup
            );
        }

        // Rapidly check status of all futures, randomly cancel some
        auto seed = RandomSeed();
        usize checks_completed = 0;

        while (checks_completed < futures.size) {
            checks_completed = 0;

            for (auto [i, future] : Enumerate(futures)) {
                if (future.IsFinished()) {
                    ++checks_completed;
                    continue;
                }

                if (future.IsInactive()) {
                    ++checks_completed;
                    continue;
                }

                // Randomly cancel some in-progress futures (stress test cancellation)
                if (RandomIntInRange(seed, 0, 1000) < 5) { // ~0.5% chance
                    if (future.Cancel()) total_cancelled.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
                }
            }

            // Brief yield to allow worker threads to make progress
            YieldThisThread();
        }

        // Collect all results and verify consistency
        usize results_collected = 0;
        for (auto [i, future] : Enumerate(futures)) {
            if (auto result = future.TryReleaseResult()) {
                CHECK_EQ(*result, (int)i);
                ++results_collected;
            } else {
                // Future was cancelled or never completed
                CHECK(future.IsInactive());
            }
        }

        // Join worker threads
        for (auto& thread : worker_threads)
            if (thread.Joinable()) thread.Join();

        auto completed = total_completed.Load(LoadMemoryOrder::Acquire);
        auto cancelled = total_cancelled.Load(LoadMemoryOrder::Acquire);

        tester.log.Debug("Rapid operations: {} completed, {} cancelled, {} results collected",
                         completed,
                         cancelled,
                         results_collected);

        // Verify that completed operations equals results collected
        CHECK_EQ(completed, results_collected);

        // Verify that we don't have more results than operations
        CHECK(results_collected <= k_num_operations);
        CHECK(completed <= k_num_operations);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterUtilsTests) {
    REGISTER_TEST(TestErrorNotifications);
    REGISTER_TEST(TestFutureAndAsync);
    REGISTER_TEST(TestHasAddressesInCurrentModule);
    REGISTER_TEST(TestJsonReader);
    REGISTER_TEST(TestJsonWriter);
    REGISTER_TEST(TestSprintfBuffer);
    REGISTER_TEST(TestStacktraceString);
}
