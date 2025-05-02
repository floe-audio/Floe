// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "os/threading.hpp"
#include "tests/framework.hpp"
#include "utils/cli_arg_parse.hpp"
#include "utils/error_notifications.hpp"
#include "utils/json/json_reader.hpp"
#include "utils/json/json_writer.hpp"
#include "utils/leak_detecting_allocator.hpp"
#include "utils/thread_extra/atomic_queue.hpp"
#include "utils/thread_extra/atomic_swap_buffer.hpp"

TEST_CASE(TestParseCommandLineArgs) {
    auto& a = tester.scratch_arena;

    SUBCASE("args to strings span") {
        char const* argv[] = {"program-name", "arg1", "arg2"};
        auto const argc = (int)ArraySize(argv);
        {
            auto args = ArgsToStringsSpan(a, {argc, argv}, false);
            CHECK(args.size == 2);
            CHECK_EQ(args[0], "arg1"_s);
            CHECK_EQ(args[1], "arg2"_s);
        }
        {
            auto args = ArgsToStringsSpan(a, {argc, argv}, true);
            CHECK(args.size == 3);
            CHECK_EQ(args[0], "program-name"_s);
            CHECK_EQ(args[1], "arg1"_s);
            CHECK_EQ(args[2], "arg2"_s);
        }
    }

    auto const check_arg = [&](HashTable<String, Span<String>> table, String arg, Span<String const> values) {
        CAPTURE(arg);
        CAPTURE(values);
        tester.log.Debug("Checking arg: {}, values: {}", arg, values);
        auto f = table.Find(arg);
        CHECK(f != nullptr);
        if (f) CHECK_EQ(*f, values);
    };

    SUBCASE("mutliple short and long args") {
        auto args = ArgsToKeyValueTable(a, Array {"-a"_s, "b", "--c", "d", "e", "-f", "--key=value"});
        CHECK_EQ(args.size, 4uz);
        check_arg(args, "a"_s, Array {"b"_s});
        check_arg(args, "c"_s, Array {"d"_s, "e"});
        check_arg(args, "f"_s, {});
        check_arg(args, "key"_s, Array {"value"_s});
    }

    SUBCASE("no args") {
        auto args = ArgsToKeyValueTable(a, Span<String> {});
        CHECK_EQ(args.size, 0uz);
    }

    SUBCASE("arg without value") {
        auto args = ArgsToKeyValueTable(a, Array {"--filter"_s});
        CHECK_EQ(args.size, 1uz);
        CHECK(args.Find("filter"_s));
    }

    SUBCASE("positional args are ignored") {
        auto args = ArgsToKeyValueTable(a, Array {"filter"_s});
        CHECK_EQ(args.size, 0uz);
    }

    SUBCASE("short arg with value") {
        auto args = ArgsToKeyValueTable(a, Array {"-a=b"_s});
        check_arg(args, "a"_s, Array {"b"_s});
        (void)args;
    }

    SUBCASE("long arg with value") {
        auto args = ArgsToKeyValueTable(a, Array {"--a=b"_s});
        check_arg(args, "a"_s, Array {"b"_s});
    }

    SUBCASE("parsing") {
        enum class ArgId {
            A,
            B,
            C,
            D,
            E,
            Count,
        };
        constexpr auto k_arg_defs = MakeCommandLineArgDefs<ArgId>({
            {
                .id = (u32)ArgId::A,
                .key = "a-arg",
                .description = "desc",
                .value_type = "type",
                .required = true,
                .num_values = 1,
            },
            {
                .id = (u32)ArgId::B,
                .key = "b-arg",
                .description = "desc",
                .value_type = "type",
                .required = false,
                .num_values = 0,
            },
            {
                .id = (u32)ArgId::C,
                .key = "c-arg",
                .description = "desc",
                .value_type = "type",
                .required = false,
                .num_values = 0,
            },
            {
                .id = (u32)ArgId::D,
                .key = "d-arg",
                .description = "desc",
                .value_type = "type",
                .required = false,
                .num_values = 2,
            },
            {
                .id = (u32)ArgId::E,
                .key = "e-arg",
                .description = "desc",
                .value_type = "type",
                .required = false,
                .num_values = -1,
            },

        });

        DynamicArray<char> buffer {a};
        auto writer = dyn::WriterFor(buffer);

        SUBCASE("valid args") {
            auto const o = ParseCommandLineArgs(writer,
                                                a,
                                                "my-program",
                                                Array {"--a-arg"_s, "value", "--c-arg"},
                                                k_arg_defs,
                                                {
                                                    .handle_help_option = false,
                                                    .print_usage_on_error = false,
                                                });
            auto const args = REQUIRE_UNWRAP(o);
            CHECK(args.size == k_arg_defs.size);

            auto a_arg = args[ToInt(ArgId::A)];
            CHECK(a_arg.values == Array {"value"_s});
            CHECK(a_arg.was_provided);
            CHECK(a_arg.info.id == (u32)ArgId::A);

            auto b_arg = args[ToInt(ArgId::B)];
            CHECK(!b_arg.was_provided);

            auto c_arg = args[ToInt(ArgId::C)];
            CHECK(c_arg.was_provided);
            CHECK(c_arg.values.size == 0);
        }

        SUBCASE("missing required args") {
            auto const o = ParseCommandLineArgs(writer,
                                                a,
                                                "my-program",
                                                Array {"--b-arg"_s, "value"},
                                                k_arg_defs,
                                                {
                                                    .handle_help_option = false,
                                                    .print_usage_on_error = false,
                                                });
            REQUIRE(o.HasError());
            CHECK(buffer.size > 0);
        }

        SUBCASE("help is handled when requested") {
            auto const o = ParseCommandLineArgs(writer,
                                                a,
                                                "my-program",
                                                Array {"--help"_s},
                                                k_arg_defs,
                                                {
                                                    .handle_help_option = true,
                                                    .print_usage_on_error = false,
                                                });
            REQUIRE(o.HasError());
            CHECK(o.Error() == CliError::HelpRequested);
            CHECK(buffer.size > 0);
        }

        SUBCASE("version is handled when requested") {
            auto const o = ParseCommandLineArgs(writer,
                                                a,
                                                "my-program",
                                                Array {"--version"_s},
                                                k_arg_defs,
                                                {
                                                    .handle_help_option = true,
                                                    .print_usage_on_error = false,
                                                    .version = "1.0.0"_s,
                                                });
            REQUIRE(o.HasError());
            CHECK(o.Error() == CliError::VersionRequested);
            CHECK(buffer.size > 0);
        }

        SUBCASE("arg that requires exactly 2 values") {
            auto const o = ParseCommandLineArgs(writer,
                                                a,
                                                "my-program",
                                                Array {"--a-arg=1"_s, "--d-arg", "1", "2"},
                                                k_arg_defs,
                                                {
                                                    .handle_help_option = false,
                                                    .print_usage_on_error = false,
                                                });
            auto const args = REQUIRE_UNWRAP(o);
            auto d_arg = args[ToInt(ArgId::D)];
            CHECK(d_arg.was_provided);
            CHECK(d_arg.values == Array {"1"_s, "2"_s});
        }

        SUBCASE("arg that can receive any number of arguments") {
            auto const o = ParseCommandLineArgs(writer,
                                                a,
                                                "my-program",
                                                Array {"--a-arg=1"_s, "--e-arg", "1", "2", "3", "4"},
                                                k_arg_defs,
                                                {
                                                    .handle_help_option = false,
                                                    .print_usage_on_error = false,
                                                });
            auto const args = REQUIRE_UNWRAP(o);
            auto e_arg = args[ToInt(ArgId::E)];
            CHECK(e_arg.was_provided);
            CHECK(e_arg.values == Array {"1"_s, "2"_s, "3"_s, "4"_s});
        }
    }

    return k_success;
}

struct StartingGun {
    void WaitUntilFired() {
        while (true) {
            WaitIfValueIsExpected(value, 0);
            if (value.Load(LoadMemoryOrder::Relaxed) == 1) return;
        }
    }
    void Fire() {
        value.Store(1, StoreMemoryOrder::Relaxed);
        WakeWaitingThreads(value, NumWaitingThreads::All);
    }
    Atomic<u32> value {0};
};

TEST_CASE(TestErrorNotifications) {
    ThreadsafeErrorNotifications no;

    Atomic<u32> iterations {0};
    constexpr u32 k_num_iterations = 10000;
    Array<Thread, 4> producers;
    Atomic<bool> thread_ready {false};
    StartingGun starting_gun;
    for (auto& p : producers) {
        p.Start(
            [&]() {
                thread_ready.Store(true, StoreMemoryOrder::Relaxed);
                starting_gun.WaitUntilFired();

                auto seed = (u64)NanosecondsSinceEpoch();
                while (iterations.Load(LoadMemoryOrder::Relaxed) < k_num_iterations) {
                    auto const id = RandomIntInRange<u64>(seed, 0, 20);
                    if (RandomIntInRange<u32>(seed, 0, 5) == 0) {
                        no.RemoveError(id);
                    } else {
                        auto item = no.NewError();
                        item->value = {
                            .title = "title"_s,
                            .message = "message"_s,
                            .error_code = {},
                            .id = id,
                        };
                        no.AddOrUpdateError(item);
                    }

                    iterations.FetchAdd(1, RmwMemoryOrder::Release);
                    YieldThisThread();
                }
            },
            "producer");
    }

    while (!thread_ready.Load(LoadMemoryOrder::Relaxed))
        YieldThisThread();

    starting_gun.Fire();
    auto seed = (u64)NanosecondsSinceEpoch();
    while (iterations.Load(LoadMemoryOrder::Relaxed) < k_num_iterations) {
        for (auto& n : no.items) {
            if (auto error = n.TryRetain()) {
                DEFER { n.Release(); };

                if (RandomIntInRange<u32>(seed, 0, 20)) {
                    no.RemoveError(error->id);
                } else {
                    CHECK_EQ(error->title, "title"_s);
                    CHECK_EQ(error->message, "message"_s);
                }
            }
        }
        YieldThisThread();
    }

    for (auto& p : producers)
        p.Join();

    return k_success;
}

TEST_CASE(TestAtomicSwapBuffer) {
    AtomicSwapBuffer<int, true> buffer;

    Thread producer;
    Thread consumer;
    StartingGun starting_gun;
    Atomic<u32> threads_ready {0};
    producer.Start(
        [&]() {
            threads_ready.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
            starting_gun.WaitUntilFired();
            for (auto const value : Range<int>(10000)) {
                auto& data = buffer.Write();
                data = value;
                buffer.Publish();
            }
        },
        "producer");
    consumer.Start(
        [&]() {
            threads_ready.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
            starting_gun.WaitUntilFired();
            for (auto const _ : Range<int>(10000))
                buffer.Consume();
        },
        "consumer");

    while (threads_ready.Load(LoadMemoryOrder::Relaxed) != 2)
        YieldThisThread();

    starting_gun.Fire();
    producer.Join();
    consumer.Join();

    return k_success;
}

template <usize k_size, NumProducers k_num_producers, NumConsumers k_num_consumers>
void DoAtomicQueueTest(tests::Tester& tester, String name) {
    SUBCASE(name) {
        SUBCASE("Basic operations") {
            AtomicQueue<int, k_size, k_num_producers, k_num_consumers> q;

            REQUIRE(q.Push(Array<int, 1> {99}));

            Array<int, 1> buf;
            REQUIRE(q.Pop(buf) == 1);
            REQUIRE(buf[0] == 99);
        }

        SUBCASE("Move operations") {
            SUBCASE("int") {
                AtomicQueue<int, k_size, k_num_producers, k_num_consumers> q;

                REQUIRE(q.Push(Array<int, 1> {99}));
                Array<int, 1> buf;
                REQUIRE(q.Pop(buf) == 1);
                REQUIRE(buf[0] == 99);
            }
        }

        SUBCASE("Push single elements until full") {
            AtomicQueue<int, k_size, k_num_producers, k_num_consumers> q;

            constexpr int k_val = 99;
            for (auto _ : Range(k_size))
                REQUIRE(q.Push(k_val));
            REQUIRE(!q.Push(k_val));

            for (auto _ : Range(k_size)) {
                int v;
                REQUIRE(q.Pop(v));
                REQUIRE(v == k_val);
            }
        }

        SUBCASE("Push large elements") {
            AtomicQueue<usize, k_size, k_num_producers, k_num_consumers> q;

            Array<usize, k_size / 2> items {};
            for (auto [index, i] : Enumerate(items))
                i = index;

            REQUIRE(q.Push(items));

            Array<usize, k_size / 2> out_items {};
            REQUIRE(q.Pop(out_items) == k_size / 2);

            for (auto [index, i] : Enumerate(out_items))
                REQUIRE(i == index);
        }

        SUBCASE("Push too many elements") {
            AtomicQueue<int, k_size, k_num_producers, k_num_consumers> q;
            Array<int, k_size * 2> items {};
            REQUIRE(!q.Push(items));
        }

        SUBCASE("Pop is clamped to number of elements") {
            AtomicQueue<int, k_size, k_num_producers, k_num_consumers> q;
            Array<int, k_size * 2> items {};
            int const val = 99;
            REQUIRE(q.Pop(items) == 0);
            REQUIRE(q.Push({&val, 1}));
            REQUIRE(q.Pop(items) == 1);
            REQUIRE(q.Push({&val, 1}));
            REQUIRE(q.Push({&val, 1}));
            REQUIRE(q.Pop(items) == 2);
        }

        auto const do_random_spamming = [](AtomicQueue<int, k_size, k_num_producers, k_num_consumers>& q,
                                           StartingGun& starting_gun,
                                           bool push) {
            starting_gun.WaitUntilFired();
            Array<int, 1> small_item {};
            Array<int, 4> big_item {};
            auto seed = (u64)NanosecondsSinceEpoch();
            for (auto _ : Range(10000)) {
                if (RandomIntInRange<int>(seed, 0, 1) == 0)
                    if (push)
                        q.Push(small_item);
                    else
                        q.Pop(small_item);
                else if (push)
                    q.Push(big_item);
                else
                    q.Pop(big_item);
            }
        };

        SUBCASE("2 threads spamming mindlessly") {
            AtomicQueue<int, k_size, k_num_producers, k_num_consumers> q;
            Thread producer;
            Thread consumer;
            StartingGun starting_gun;
            producer.Start([&]() { do_random_spamming(q, starting_gun, true); }, "producer");
            consumer.Start([&]() { do_random_spamming(q, starting_gun, false); }, "consumer");
            starting_gun.Fire();
            producer.Join();
            consumer.Join();
        }

        SUBCASE("2 threads: all push/pops are accounted for and in order") {
            constexpr int k_num_values = 10000;
            AtomicQueue<int, k_size, k_num_producers, k_num_consumers> q;

            // NOTE(Sam): Yieiding the thread is necessary here when running with Valgrind. It doesn't seem to
            // be nececssary normally though.

            Thread producer;
            StartingGun starting_gun;
            Atomic<bool> producer_ready {false};
            producer.Start(
                [&]() {
                    producer_ready.Store(true, StoreMemoryOrder::Relaxed);
                    starting_gun.WaitUntilFired();
                    for (auto const index : Range(k_num_values))
                        while (!q.Push(index))
                            YieldThisThread();
                },
                "producer");

            while (!producer_ready.Load(LoadMemoryOrder::Relaxed))
                YieldThisThread();

            tester.log.Debug("Producer ready");
            starting_gun.Fire();

            int index = 0;
            do {
                Array<int, 1> buf;
                if (auto num_popped = q.Pop(Span<int> {buf})) {
                    CHECK_EQ(num_popped, 1u);
                    CHECK_EQ(buf[0], index);
                    index++;
                } else {
                    YieldThisThread();
                }
            } while (index != k_num_values);

            producer.Join();
        }

        if constexpr (k_num_consumers == NumConsumers::Many || k_num_producers == NumProducers::Many) {
            SUBCASE("Multiple threads spamming mindlessly") {
                AtomicQueue<int, k_size, k_num_producers, k_num_consumers> q;
                Array<Thread, k_num_producers == NumProducers::One ? 1 : 4> producers;
                Array<Thread, k_num_consumers == NumConsumers::One ? 1 : 4> consumers;

                StartingGun starting_gun;

                for (auto& producer : producers)
                    producer.Start([&]() { do_random_spamming(q, starting_gun, true); }, "producer");

                for (auto& consumer : consumers)
                    consumer.Start([&]() { do_random_spamming(q, starting_gun, false); }, "consumer");

                starting_gun.Fire();

                for (auto& producer : producers)
                    producer.Join();
                for (auto& consumer : consumers)
                    consumer.Join();
            }
        }
    }
}

TEST_CASE(TestAtomicQueue) {
    DoAtomicQueueTest<64, NumProducers::One, NumConsumers::One>(tester, "1");
    DoAtomicQueueTest<8, NumProducers::One, NumConsumers::One>(tester, "2");
    DoAtomicQueueTest<64, NumProducers::Many, NumConsumers::One>(tester, "3");
    DoAtomicQueueTest<8, NumProducers::Many, NumConsumers::One>(tester, "4");
    DoAtomicQueueTest<64, NumProducers::One, NumConsumers::Many>(tester, "5");
    DoAtomicQueueTest<8, NumProducers::One, NumConsumers::Many>(tester, "6");
    DoAtomicQueueTest<4096, NumProducers::Many, NumConsumers::Many>(tester, "7");
    DoAtomicQueueTest<8, NumProducers::Many, NumConsumers::Many>(tester, "8");
    return k_success;
}

struct MallocedObj {
    MallocedObj(char c) : obj((char*)GpaAlloc(10)) { FillMemory({(u8*)obj, 10}, (u8)c); }
    ~MallocedObj() { GpaFree(obj); }
    char* obj;
};

TEST_CASE(TestAtomicRefList) {
    AtomicRefList<MallocedObj> map;

    SUBCASE("basics") {
        // Initially empty
        {
            CHECK(map.dead_list == nullptr);
            CHECK(map.free_list == nullptr);
            CHECK(map.live_list.Load(LoadMemoryOrder::Relaxed) == nullptr);
        }

        // Allocate and insert
        {
            auto node = map.AllocateUninitialised();
            REQUIRE(node);
            CHECK(map.dead_list == nullptr);
            CHECK(map.free_list == nullptr);
            CHECK(map.live_list.Load(LoadMemoryOrder::Relaxed) == nullptr);
            PLACEMENT_NEW(&node->value) MallocedObj('a');
            map.Insert(node);
            CHECK(map.dead_list == nullptr);
            CHECK(map.free_list == nullptr);
            CHECK(map.live_list.Load(LoadMemoryOrder::Relaxed) == node);
        }

        // Retained iterator
        {
            auto it = map.begin();
            CHECK(it->TryRetain());
            CHECK(it.node);
            it->Release();

            ++it;
            REQUIRE(!it.node);
        }

        // Remove
        {
            auto it = map.begin();
            REQUIRE(it.node);
            map.Remove(it);
            CHECK(map.begin().node == nullptr);
            CHECK(map.dead_list);
            CHECK(!map.free_list);
        }

        // Delete unreferenced
        {
            map.DeleteRemovedAndUnreferenced();
            CHECK(map.free_list);
            CHECK(!map.dead_list);
        }

        // Check multiple objects
        {
            auto const keys = Array {'a', 'b', 'c', 'd', 'e', 'f'};
            auto const count = [&]() {
                usize count = 0;
                for (auto& i : map) {
                    auto val = i.TryRetain();
                    REQUIRE(val);
                    DEFER { i.Release(); };
                    ++count;
                }
                return count;
            };

            // Insert and iterate
            {
                for (auto c : keys) {
                    auto n = map.AllocateUninitialised();
                    PLACEMENT_NEW(&n->value) MallocedObj(c);
                    map.Insert(n);
                }

                auto it = map.begin();
                REQUIRE(it.node);
                CHECK(Find(keys, it->value.obj[0]));
                usize num = 0;
                while (it != map.end()) {
                    ++num;
                    ++it;
                }
                CHECK_EQ(num, keys.size);
            }

            // Remove first and writer-iterate
            {
                auto writer_it = map.begin();
                map.Remove(writer_it);

                CHECK_EQ(count(), keys.size - 1);
            }

            // Remove while in a loop
            {
                usize pos = 0;
                for (auto it = map.begin(); it != map.end();) {
                    if (pos == 2)
                        it = map.Remove(it);
                    else
                        ++it;
                    ++pos;
                }
                CHECK_EQ(count(), keys.size - 2);
            }

            // Remove unref
            {
                map.DeleteRemovedAndUnreferenced();
                CHECK_EQ(count(), keys.size - 2);
                CHECK(map.free_list != nullptr);
            }

            // Remove all
            {
                map.RemoveAll();
                map.DeleteRemovedAndUnreferenced();
                CHECK(map.live_list.Load(LoadMemoryOrder::Relaxed) == nullptr);
                CHECK(map.dead_list == nullptr);
            }
        }
    }

    SUBCASE("multithreading") {
        Thread thread;
        Atomic<bool> done {false};

        StartingGun starting_gun;
        Atomic<bool> thread_ready {false};

        thread.Start(
            [&]() {
                thread_ready.Store(true, StoreMemoryOrder::Relaxed);
                starting_gun.WaitUntilFired();
                auto seed = (u64)NanosecondsSinceEpoch();
                for (auto _ : Range(5000)) {
                    for (char c = 'a'; c <= 'z'; ++c) {
                        auto const r = RandomIntInRange(seed, 0, 2);
                        if (r == 0) {
                            for (auto it = map.begin(); it != map.end();)
                                if (it->value.obj[0] == c) {
                                    it = map.Remove(it);
                                    break;
                                } else {
                                    ++it;
                                }
                        } else if (r == 1) {
                            bool found = false;
                            for (auto& it : map) {
                                if (it.value.obj[0] == c) {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                auto node = map.AllocateUninitialised();
                                PLACEMENT_NEW(&node->value) MallocedObj(c);
                                map.Insert(node);
                            }
                        } else if (r == 2) {
                            map.DeleteRemovedAndUnreferenced();
                        }
                    }
                    YieldThisThread();
                }
                done.Store(true, StoreMemoryOrder::Release);
            },
            "test-thread");

        while (!thread_ready.Load(LoadMemoryOrder::Relaxed))
            YieldThisThread();

        starting_gun.Fire();
        while (!done.Load(LoadMemoryOrder::Relaxed)) {
            for (auto& i : map)
                if (auto val = i.TryRetain()) {
                    CHECK(val->obj[0] >= 'a' && val->obj[0] <= 'z');
                    i.Release();
                }
            YieldThisThread();
        }

        thread.Join();

        for (auto n = map.live_list.Load(LoadMemoryOrder::Relaxed); n != nullptr;
             n = n->next.Load(LoadMemoryOrder::Relaxed))
            CHECK_EQ(n->reader_uses.Load(LoadMemoryOrder::Relaxed), 0u);

        map.RemoveAll();
        map.DeleteRemovedAndUnreferenced();
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

TEST_REGISTRATION(RegisterUtilsTests) {
    REGISTER_TEST(TestAtomicQueue);
    REGISTER_TEST(TestAtomicRefList);
    REGISTER_TEST(TestAtomicSwapBuffer);
    REGISTER_TEST(TestErrorNotifications);
    REGISTER_TEST(TestHasAddressesInCurrentModule);
    REGISTER_TEST(TestJsonReader);
    REGISTER_TEST(TestJsonWriter);
    REGISTER_TEST(TestParseCommandLineArgs);
    REGISTER_TEST(TestSprintfBuffer);
    REGISTER_TEST(TestStacktraceString);
}
