// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"
#include "utils/leak_detecting_allocator.hpp"

template <HashTableOrdering k_ordering>
TEST_CASE(TestHashTable) {
    auto& a = tester.scratch_arena;

    SUBCASE("table") {
        DynamicHashTable<String, usize, nullptr, k_ordering> tab {a, 16u};

        CHECK(tab.table.size == 0);
        CHECK(tab.table.Elements().size >= 16);

        {
            usize count = 0;
            for (auto item : tab) {
                (void)item;
                ++count;
            }
            CHECK(count == 0);
        }

        CHECK(tab.Insert("foo", 42));
        CHECK(tab.Insert("bar", 31337));
        CHECK(tab.Insert("qux", 64));
        CHECK(tab.Insert("900", 900));
        CHECK(tab.Insert("112", 112));

        CHECK(tab.Find("foo"));
        CHECK(tab.Find("bar"));
        CHECK(!tab.Find("baz"));

        CHECK(tab.table.Elements().size > 5);
        CHECK(tab.table.size == 5);

        {
            auto v = tab.Find("bar");
            REQUIRE(v);
            tester.log.Debug("{}", *v);
        }

        {
            usize count = 0;
            for (auto item : tab) {
                CHECK(item.key.size);
                tester.log.Debug("{} -> {}", item.key, item.value);
                if (item.key == "112") item.value++;
                ++count;
            }
            CHECK(count == 5);
            auto v = tab.Find("112");
            CHECK(v && *v == 113);
        }

        for (auto const i : Range(10000uz))
            CHECK(tab.Insert(fmt::Format(a, "key{}", i), i));

        // test Assign()
        DynamicHashTable<String, usize, nullptr, k_ordering> other {a, 16u};
        CHECK(other.table.size == 0);
        CHECK(other.Insert("foo", 42));

        tab.Assign(other);
        CHECK(tab.table.size == 1);
    }

    SUBCASE("grow and delete") {
        for (auto insertions : Range(4uz, 32uz)) {
            HashTable<usize, usize, nullptr, k_ordering> tab {};
            for (auto i : Range(insertions)) {
                auto const result = tab.FindOrInsertGrowIfNeeded(a, i, i * 2);
                CHECK(result.inserted);
            }
            CHECK_EQ(tab.size, insertions);
            for (auto i : Range(insertions))
                tab.Delete(i);
            CHECK_EQ(tab.size, 0uz);
            for (auto i : Range(insertions * 4)) {
                auto const result = tab.FindOrInsertGrowIfNeeded(a, i, i * 2);
                CHECK(result.inserted);
            }
            CHECK_EQ(tab.size, insertions * 4);
        }
    }

    SUBCASE("reserve") {
        for (auto count : Range(4uz, 32uz)) {
            HashTable<usize, usize, nullptr, k_ordering> tab {};
            tab.Reserve(a, count);
            CHECK_EQ(tab.size, 0uz);
            for (auto i : Range(count)) {
                auto const result = tab.FindOrInsertWithoutGrowing(i, i * 2);
                CHECK(result.inserted);
            }
            CHECK_EQ(tab.size, count);
        }
    }

    SUBCASE("no initial size") {
        DynamicHashTable<String, int, nullptr, k_ordering> tab {a};
        CHECK(tab.Insert("foo", 100));
        for (auto item : tab)
            CHECK_EQ(item.value, 100);
        auto v = tab.Find("foo");
        REQUIRE(v);
        CHECK_EQ(*v, 100);
        *v = 200;
        v = tab.Find("foo");
        REQUIRE(v);
        CHECK_EQ(*v, 200);

        CHECK(tab.table.size == 1);

        CHECK(tab.Delete("foo"));

        CHECK(tab.table.size == 0);
    }

    SUBCASE("move") {
        LeakDetectingAllocator a2;

        SUBCASE("construct") {
            DynamicHashTable<String, int, nullptr, k_ordering> tab1 {a2};
            CHECK(tab1.Insert("foo", 100));
            DynamicHashTable<String, int, nullptr, k_ordering> const tab2 {Move(tab1)};
            auto v = tab2.Find("foo");
            REQUIRE(v);
        }
        SUBCASE("assign same allocator") {
            DynamicHashTable<String, int, nullptr, k_ordering> tab1 {a2};
            CHECK(tab1.Insert("foo", 100));
            DynamicHashTable<String, int, nullptr, k_ordering> tab2 {a2};
            tab2 = Move(tab1);
            auto v = tab2.Find("foo");
            REQUIRE(v);
        }
        SUBCASE("assign different allocator") {
            DynamicHashTable<String, int, nullptr, k_ordering> tab1 {a2};
            CHECK(tab1.Insert("foo", 100));
            DynamicHashTable<String, int, nullptr, k_ordering> tab2 {Malloc::Instance()};
            tab2 = Move(tab1);
            auto v = tab2.Find("foo");
            REQUIRE(v);
        }
    }

    SUBCASE("Intersect") {
        DynamicHashTable<String, int, nullptr, k_ordering> tab1 {a};
        CHECK(tab1.Insert("foo", 100));
        CHECK(tab1.Insert("bar", 200));

        DynamicHashTable<String, int, nullptr, k_ordering> tab2 {a};
        CHECK(tab2.Insert("bar", 200));
        CHECK(tab2.Insert("baz", 400));

        tab1.table.IntersectWith(tab2);
        CHECK(tab1.table.size == 1);
        auto v = tab1.Find("bar");
        REQUIRE(v);
    }

    if constexpr (k_ordering == HashTableOrdering::Ordered) {
        SUBCASE("Ordered") {
            DynamicHashTable<String, int, nullptr, k_ordering> tab1 {a};
            CHECK(tab1.Insert("b", 0));
            CHECK(tab1.Insert("c", 0));
            CHECK(tab1.Insert("a", 0));
            CHECK(tab1.Insert("d", 0));

            CHECK(tab1.table.size == 4);

            {
                auto it = tab1.begin();
                CHECK_EQ((*it).key, "a"_s);
                ++it;
                CHECK_EQ((*it).key, "b"_s);
                ++it;
                CHECK_EQ((*it).key, "c"_s);
                ++it;
                CHECK_EQ((*it).key, "d"_s);
                ++it;
                CHECK(it == tab1.end());
            }

            // Remove "b" and re-check
            {
                CHECK(tab1.Delete("b"));
                CHECK(tab1.table.size == 3);
                auto it = tab1.begin();
                CHECK_EQ((*it).key, "a"_s);
                ++it;
                CHECK_EQ((*it).key, "c"_s);
                ++it;
                CHECK_EQ((*it).key, "d"_s);
                ++it;
                CHECK(it == tab1.end());
            }

            // Delete all, then add a couple items back
            {
                tab1.DeleteAll();
                CHECK(tab1.table.size == 0);

                CHECK(tab1.Insert("x", 100));
                CHECK(tab1.Insert("y", 200));
                CHECK(tab1.table.size == 2);

                auto it = tab1.begin();
                CHECK_EQ((*it).key, "x"_s);
                ++it;
                CHECK_EQ((*it).key, "y"_s);
                ++it;
                CHECK(it == tab1.end());
            }
        }
    }

    SUBCASE("correct retrieval") {
        HashTable<int, int, nullptr, k_ordering> table {};
        for (int i = 0; i < 1000; ++i) {
            auto const result = table.FindOrInsertGrowIfNeeded(a, i, i * 2);
            CHECK(result.inserted);
        }

        CHECK(table.size == 1000);
        for (auto const& item : table)
            CHECK(item.value == item.key * 2);
    }

    SUBCASE("find or insert") {
        // Test when a table is grown - is the correct element still returned?
        HashTable<String, usize, nullptr, k_ordering> table {};

        usize index = 0;
        for (auto const str : Array {
                 "Vocal Ohh"_s,
                 "Vocal Eee"_s,
                 "Air - Restless Canopy"_s,
                 "Low - Alien Kerogen"_s,
                 "Low - Bass Arena"_s,
                 "Mid - Tickseed Ambience"_s,
                 "Noise - Electric Hiss"_s,
                 "Noise - Static"_s,
                 "Vocal Ooo"_s,
                 "New value"_s,
                 "Other"_s,
                 "New"_s,
                 "String"_s,
                 "Link"_s,
                 "List"_s,
                 "Text"_s,
                 "aaaa"_s,
                 "bbbb"_s,
                 "cccc"_s,
                 "dddd"_s,
                 "e"_s,
                 "1"_s,
                 "2"_s,
             }) {
            auto const result = table.FindOrInsertGrowIfNeeded(a, str, index);
            CHECK(result.inserted);
            CHECK(result.element.data == index);
            ++index;
            CHECK(table.size == index);
        };
    }

    SUBCASE("iteration") {
        auto tags = HashTable<String, Set<String, nullptr, k_ordering>, nullptr, k_ordering>::Create(a, 16);

        auto check = [&]() {
            for (auto const [name, set, name_hash] : tags) {
                CHECK(name.size);
                CHECK(IsValidUtf8(name));
                CHECK(name_hash != 0);
                CHECK(set.size);

                for (auto const [tag, tag_hash] : set) {
                    REQUIRE(tag.size);
                    REQUIRE(tag.size < 64);
                    CHECK(IsValidUtf8(tag));
                }
            }
        };

        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Air - Tephra", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "bittersweet"_s,
                     "bright",
                     "chillout",
                     "dreamy",
                     "fuzzy",
                     "nostalgic",
                     "smooth",
                     "strings-like",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Scattered World", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "dreamy",
                     "eerie",
                     "ethereal",
                     "full-spectrum",
                     "lush",
                     "multi-pitched",
                     "nostalgic",
                     "sci-fi",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Noise - Rumble Hiss", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient",
                     "noise",
                     "non-pitched",
                     "resonant",
                     "rumbly",
                     "synthesized",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Division", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient",
                     "choir-like",
                     "ethereal",
                     "pad",
                     "peaceful",
                     "pure",
                     "smooth",
                     "synthesized",
                     "warm",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Tickseed Ambience", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient",
                     "dreamy",
                     "eerie",
                     "ethereal",
                     "glassy",
                     "pad",
                     "resonant",
                     "saturated",
                     "strings-like",
                     "synthesized",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - Drifter", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "cinematic",
                     "dark",
                     "disturbing",
                     "dreamy",
                     "eerie",
                     "menacing",
                     "muddy",
                     "mysterious",
                     "resonant",
                     "rumbly",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - Alien Kerogen", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "bittersweet",
                     "dreamy",
                     "eerie",
                     "ethereal",
                     "hopeful",
                     "nostalgic",
                     "pad",
                     "smooth",
                     "synthesized",
                     "warm",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - Bass Arena", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "cinematic",
                     "cold",
                     "eerie",
                     "hypnotic",
                     "muddy",
                     "mysterious",
                     "rumbly",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Boreal", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "bright",
                     "glassy",
                     "hopeful",
                     "pad",
                     "sci-fi",
                     "strings-like",
                     "synthesized",
                     "thin",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - Heavenly Rumble", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "bittersweet",
                     "cinematic",
                     "dark",
                     "dystopian",
                     "eerie",
                     "ethereal",
                     "muddy",
                     "mysterious",
                     "nostalgic",
                     "rumbly",
                     "smooth",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Noise - Static", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient"_s,
                     "cold",
                     "noise",
                     "non-pitched",
                     "reedy",
                     "resonant",
                     "synthesized",
                     "thin",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - Warmth Cycles", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "bittersweet",
                     "cinematic",
                     "dreamy",
                     "dystopian",
                     "eerie",
                     "metallic",
                     "muffled",
                     "nostalgic",
                     "pulsing",
                     "pure",
                     "sci-fi",
                     "smooth",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Noise - Electric Hiss", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient"_s,
                     "hissing",
                     "noise",
                     "synthesized",
                     "thin",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Noise - Hollow Noise", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "dreamy",
                     "eerie",
                     "mysterious",
                     "noise",
                     "non-pitched",
                     "resonant",
                     "synthesized",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Air - Restless Canopy", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "bittersweet",
                     "breathy",
                     "dreamy",
                     "ethereal",
                     "nostalgic",
                     "resonant",
                     "smooth",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Misty Nightfall", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "dreamy",
                     "ethereal",
                     "full-spectrum",
                     "glassy",
                     "lush",
                     "metallic",
                     "multi-pitched",
                     "mysterious",
                     "organ-like",
                     "synthesized",
                     "texture",
                     "warm",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Vocal Ahh", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "pad"_s,
                     "synthesized",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - Dark Aurora", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "breathy"_s,
                     "cinematic",
                     "dark",
                     "disturbing",
                     "ethereal",
                     "muddy",
                     "resonant",
                     "rumbly",
                     "synthesized",
                     "tense",
                     "texture",
                     "thin",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Noise - Atonal Void", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient"_s,
                     "breathy",
                     "eerie",
                     "ethereal",
                     "mysterious",
                     "noise",
                     "synthesized",
                     "thin",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Nectareous", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "choir-like",
                     "ethereal",
                     "muffled",
                     "pad",
                     "resonant",
                     "smooth",
                     "synthesized",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Goldenrods", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "dreamy",
                     "muddy",
                     "multi-pitched",
                     "mysterious",
                     "resonant",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - First Twilight", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "bittersweet",
                     "dreamy",
                     "ethereal",
                     "hopeful",
                     "organ-like",
                     "pad",
                     "pure",
                     "resonant",
                     "smooth",
                     "synthesized",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Greek Moon", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient",
                     "choir-like",
                     "dreamy",
                     "ethereal",
                     "hopeful",
                     "pad",
                     "peaceful",
                     "pure",
                     "smooth",
                     "strings-like",
                     "synthesized",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Earthly Effigies", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient"_s,
                     "choir-like",
                     "cinematic",
                     "dystopian",
                     "eerie",
                     "ethereal",
                     "hypnotic",
                     "muddy",
                     "muffled",
                     "resonant",
                     "sci-fi",
                     "smooth",
                     "synthesized",
                     "tense",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - The Actuator", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "bittersweet",
                     "cinematic",
                     "dark",
                     "dreamy",
                     "nostalgic",
                     "rumbly",
                     "smooth",
                     "synthesized",
                     "texture",
                     "warm",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - Ether Wraith", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient"_s,
                     "cinematic",
                     "cold",
                     "dark",
                     "disturbing",
                     "dystopian",
                     "eerie",
                     "melancholic",
                     "menacing",
                     "muddy",
                     "noisy",
                     "rumbly",
                     "synthesized",
                     "tense",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }

        if (auto i = tags.Find("Vocal Ahh"))
            for (auto const [tag, _] : *i)
                CHECK(tag == "pad"_s || tag == "synthesized"_s);
    }

    SUBCASE("RemoveIf") {
        // Test DynamicHashTable RemoveIf
        DynamicHashTable<String, int, nullptr, k_ordering> tab {a};
        CHECK(tab.Insert("keep1", 100));
        CHECK(tab.Insert("remove1", 200));
        CHECK(tab.Insert("keep2", 300));
        CHECK(tab.Insert("remove2", 400));
        CHECK(tab.Insert("keep3", 500));

        CHECK(tab.table.size == 5);

        // Remove entries with "remove" in the key
        auto removed =
            tab.RemoveIf([](String const& key, int const&) { return ContainsSpan(key, "remove"_s); });

        CHECK(removed == 2);
        CHECK(tab.table.size == 3);
        CHECK(tab.Find("keep1") && *tab.Find("keep1") == 100);
        CHECK(tab.Find("keep2") && *tab.Find("keep2") == 300);
        CHECK(tab.Find("keep3") && *tab.Find("keep3") == 500);
        CHECK(!tab.Find("remove1"));
        CHECK(!tab.Find("remove2"));

        // Remove entries with value > 300
        removed = tab.RemoveIf([](String const&, int const& value) { return value > 300; });

        CHECK(removed == 1);
        CHECK(tab.table.size == 2);
        CHECK(tab.Find("keep1") && *tab.Find("keep1") == 100);
        CHECK(tab.Find("keep2") && *tab.Find("keep2") == 300);
        CHECK(!tab.Find("keep3"));

        // Remove all remaining
        removed = tab.RemoveIf([](String const&, int const&) { return true; });
        CHECK(removed == 2);
        CHECK(tab.table.size == 0);

        // Test removing from empty table
        removed = tab.RemoveIf([](String const&, int const&) { return true; });
        CHECK(removed == 0);
        CHECK(tab.table.size == 0);
    }

    SUBCASE("Set RemoveIf") {
        // Test DynamicSet RemoveIf
        DynamicSet<String> set {a};
        CHECK(set.Insert("apple"));
        CHECK(set.Insert("banana"));
        CHECK(set.Insert("cherry"));
        CHECK(set.Insert("apricot"));
        CHECK(set.Insert("blueberry"));

        CHECK(set.table.size == 5);

        // Remove entries starting with "a"
        auto removed = set.RemoveIf([](String const& key) { return StartsWithSpan(key, "a"_s); });

        CHECK(removed == 2);
        CHECK(set.table.size == 3);
        CHECK(set.table.Contains("banana"));
        CHECK(set.table.Contains("cherry"));
        CHECK(set.table.Contains("blueberry"));
        CHECK(!set.table.Contains("apple"));
        CHECK(!set.table.Contains("apricot"));

        // Remove entries with length >= 6
        removed = set.RemoveIf([](String const& key) { return key.size >= 6; });

        CHECK(removed == 3);
        CHECK(set.table.size == 0);

        // Test Set (non-dynamic) RemoveIf
        auto static_set = Set<String>::Create(a, 10);
        CHECK(static_set.FindOrInsertGrowIfNeeded(a, "test1").inserted);
        CHECK(static_set.FindOrInsertGrowIfNeeded(a, "test2").inserted);
        CHECK(static_set.FindOrInsertGrowIfNeeded(a, "keep").inserted);

        removed = static_set.RemoveIf([](String const& key) { return StartsWithSpan(key, "test"_s); });

        CHECK(removed == 2);
        CHECK(static_set.size == 1);
        CHECK(static_set.Contains("keep"));
        CHECK(!static_set.Contains("test1"));
        CHECK(!static_set.Contains("test2"));
    }

    return k_success;
}

TEST_REGISTRATION(RegisterHashTableTests) {
    REGISTER_TEST(TestHashTable<HashTableOrdering::Ordered>);
    REGISTER_TEST(TestHashTable<HashTableOrdering::Unordered>);
}
