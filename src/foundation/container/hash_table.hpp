// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// This code is based on htab (https://github.com/rofl0r/htab/), itself based on musl's hsearch.
// Copyright Szabolcs Nagy A.K.A. nsz
// Copyright rofl0r
// SPDX-License-Identifier: MIT

#pragma once
#include "foundation/memory/allocators.hpp"

// IMPORTANT: don't set k_hash_function to a function that is header-only. It can result in the type of the
// HashTable being different across compilation units.

struct DummyValueType {};

template <typename T>
concept TriviallyCopyableOrDummy = TriviallyCopyable<T> || Same<DummyValueType, T>;

template <typename KeyType>
using HashFunction = u64 (*)(KeyType const&);

template <typename KeyType, typename ValueType>
using LessThanFunction = bool (*)(KeyType const&, ValueType const&, KeyType const&, ValueType const&);

u64 NoHash(u64 const&);

enum HashTableOrdering { Unordered, Ordered };

template <TriviallyCopyable KeyType_,
          TriviallyCopyableOrDummy ValueType_,
          HashFunction<KeyType_> k_hash_function_ = nullptr,
          HashTableOrdering k_ordering_ = HashTableOrdering::Unordered,
          LessThanFunction<KeyType_, ValueType_> k_less_than_function = nullptr>
struct HashTable {
    using KeyType = KeyType_;
    using ValueType = ValueType_;
    static constexpr HashFunction<KeyType> k_hash_function = k_hash_function_;
    static constexpr HashTableOrdering k_ordering = k_ordering_;

    struct Element {
        bool Active() const { return hash && hash != k_tombstone; }
        [[no_unique_address]] ValueType data {};
        KeyType key {};
        u64 hash {}; // 0 == empty, k_tombstone == deleted, otherwise valid
    };

    struct OrderIndicesArray {
        void Reserve(usize new_capacity, Allocator& allocator) {
            if (new_capacity <= capacity) return;
            new_capacity = Max<usize>(4, capacity + (capacity / 2), new_capacity);

            auto mem =
                allocator.Reallocate<usize>(new_capacity, {(u8*)items.data, capacity}, items.size, true);
            ASSERT(mem.data != nullptr);

            items = {CheckedPointerCast<usize*>(mem.data), items.size};
            capacity = mem.size;
        }

        Span<usize> items {};
        usize capacity {};
    };

    struct Iterator {
        friend bool operator==(Iterator const& a, Iterator const& b) {
            return &a.table == &b.table && a.index == b.index;
        }
        friend bool operator!=(Iterator const& a, Iterator const& b) {
            return &a.table != &b.table || a.index != b.index;
        }
        friend bool operator<(Iterator const& a, Iterator const& b) {
            ASSERT(&a.table == &b.table);
            return a.index < b.index;
        }
        auto operator*() const {
            Element* element;
            if constexpr (k_ordering == HashTableOrdering::Unordered) {
                element = &table.elems[index];
            } else {
                auto const elem_index = table.order_indices.items[index];
                element = &table.elems[elem_index];
            }
            ASSERT_HOT(element->Active());

            if constexpr (Same<DummyValueType, ValueType>) {
                struct Item {
                    KeyType const& key;
                    u64 hash;
                };
                return Item {.key = element->key, .hash = element->hash};
            } else {
                struct Item {
                    KeyType const& key;
                    ValueType& value;
                    u64 hash;
                };
                return Item {.key = element->key, .value = element->data, .hash = element->hash};
            }
        }
        Iterator& operator++() {
            ++index;

            if constexpr (k_ordering == HashTableOrdering::Unordered) {
                for (; index < table.mask + 1; ++index)
                    if (table.elems[index].Active()) break;
            }

            return *this;
        }
        Iterator& operator--() {
            static_assert(UnsignedInt<decltype(index)>, "we rely on wrap-around");
            --index;

            if constexpr (k_ordering == HashTableOrdering::Unordered) {
                for (; index < table.mask + 1; --index)
                    if (table.elems[index].Active()) break;
            }

            return *this;
        }

        HashTable const& table;
        usize index {};
    };

    static constexpr usize k_min_size = 8;
    static constexpr usize k_max_size = (((usize)-1 / 2) + 1);
    static constexpr u64 k_tombstone = 0xdeadc0de;

    static u64 Hash(KeyType k) {
        u64 result;

        // IMPORTANT: we don't set Hash as the k_hash_function in the template arguments because we don't know
        // if Hash is consistent accross different compilation units. It might be a header-only function in
        // which case the _type_ of the HashTable will vary depending on the compilation unit leading to
        // cryptic linker errors.
        if constexpr (k_hash_function == nullptr) {
            // if the KeyType has a Hash() method, use it
            if constexpr (requires { k.Hash(); })
                result = k.Hash();
            else
                result = ::Hash(k);
        } else
            result = k_hash_function(k);

        // Reserved hash values.
        // IMPROVE: remap these?
        ASSERT_HOT(result != 0);
        ASSERT_HOT(result != k_tombstone);
        return result;
    }

    // Quadratic probing is used if there's a hash collision
    Element* Lookup(KeyType key, u64 hash, u64 dead_hash_value) const {
        ASSERT(elems);
        ASSERT(size <= Capacity());

        Element* element;
        usize index = hash;
        usize step = 1;

        usize iterations = 0;
        constexpr usize k_max_iterations = 1000000;

        for (; iterations < k_max_iterations; ++iterations) {
            element = elems + (index & mask);

            if (element->hash == 0) break; // empty slot
            if (element->hash == dead_hash_value) break; // deleted slot
            if (element->hash == hash && element->key == key) break; // match

            index += step;
            step++;
        }

        ASSERT(iterations < k_max_iterations);

        return element;
    }

    Element* FindElement(KeyType key, u64 hash = 0) const {
        if (!elems) return nullptr;
        if (!hash) hash = Hash(key);
        Element* element = Lookup(key, hash, 0);
        if (element->Active()) return element;
        return nullptr;
    }

    bool Contains(KeyType key, u64 hash = 0) const { return FindElement(key, hash) != nullptr; }

    // Finds an element but doens't protect against hash collisions.
    bool ContainsSkipKeyCheck(u64 hash) const {
        ASSERT(hash);
        if (!elems) return false;

        usize index = hash;
        usize step = 1;

        usize iterations = 0;
        constexpr usize k_max_iterations = 1000000;

        for (; iterations < k_max_iterations; ++iterations) {
            auto element = elems + (index & mask);

            if (element->hash == 0) return false; // empty slot
            if (element->hash == k_tombstone) return false; // deleted slot
            if (element->hash == hash) return true; // match

            index += step;
            step++;
        }

        ASSERT(iterations < k_max_iterations);

        return false;
    }

    static usize PowerOf2Capacity(usize capacity) {
        if (capacity > k_max_size) capacity = k_max_size;
        usize new_capacity;
        for (new_capacity = k_min_size; new_capacity < capacity; new_capacity *= 2)
            ;
        return new_capacity;
    }

    static usize RecommendedCapacity(usize num_items) { return PowerOf2Capacity(num_items * 2); }

    [[nodiscard]] static HashTable Create(Allocator& a, usize size) {
        auto const cap = RecommendedCapacity(size);
        HashTable table {};
        table.elems = a.NewMultiple<Element>(cap).data;
        table.mask = cap - 1;

        if constexpr (k_ordering == HashTableOrdering::Ordered) {
            auto allocation = a.AllocateExactSizeUninitialised<usize>(size);
            table.order_indices.items = {allocation.data, 0};
            table.order_indices.capacity = size;
        }

        return table;
    }

    usize Capacity() const { return mask + 1; }

    void Free(Allocator& a) {
        auto element = Elements();
        if (element.size) a.Free(element.ToByteSpan());

        if constexpr (k_ordering == HashTableOrdering::Ordered) {
            if (order_indices.items.size) a.Free(order_indices.items.ToByteSpan());
            order_indices = {};
        }
    }

    Span<Element const> Elements() const {
        return elems ? Span<Element const> {elems, mask + 1} : Span<Element const> {};
    }
    Span<Element> Elements() { return elems ? Span<Element> {elems, mask + 1} : Span<Element> {}; }

    ValueType* Find(KeyType key, u64 hash = 0) const {
        static_assert(!Same<ValueType, DummyValueType>,
                      "HashTable::Find called on a set, use FindElement instead");
        Element* element = FindElement(key, hash);
        if (!element) return nullptr;
        return &element->data;
    }

    void DeleteIndex(usize index) {
        RemoveFromOrderedIndicesIfNeeded(index);
        elems[index].hash = k_tombstone;
        --size;
        ++num_dead;
    }

    bool Delete(KeyType key) {
        Element* element = FindElement(key);
        if (!element) return false;

        auto const index = (usize)(element - elems);
        DeleteIndex(index);
        return true;
    }

    void DeleteElement(Element* element) {
        auto const index = (usize)(element - elems);
        DeleteIndex(index);
    }

    void DeleteAll() {
        if (!elems) return;

        for (auto& element : Elements())
            element.hash = 0;

        if constexpr (k_ordering == HashTableOrdering::Ordered) order_indices.items.size = 0;
        size = 0;
        num_dead = 0;
    }

    // allocator must be the same as created this table
    void IncreaseCapacity(Allocator& allocator, usize capacity) {
        auto old_elements = Elements();

        if constexpr (k_ordering == HashTableOrdering::Ordered) {
            order_indices.items.size = 0;
            order_indices.Reserve(capacity, allocator);
        }

        capacity = PowerOf2Capacity(capacity);
        elems = allocator.template NewMultiple<Element>(capacity).data;
        mask = capacity - 1;

        if (old_elements.size) {
            Element* new_element;
            for (auto& old_element : old_elements)
                if (old_element.Active()) {
                    for (u64 i = old_element.hash, j = 1;; i += j++) {
                        new_element = elems + (i & mask);
                        if (!new_element->Active()) break;
                    }
                    *new_element = old_element;
                    AddToOrderedIndicesIfNeeded((usize)(new_element - elems), nullptr);
                }
            allocator.Free(old_elements.ToByteSpan());
        }
        return;
    }

    bool InsertWithoutGrowing(KeyType key, ValueType value, u64 hash = 0) {
        if (!elems) {
            PanicIfReached();
            return false;
        }
        if (!hash) hash = Hash(key);
        Element* element = Lookup(key, hash, k_tombstone);

        if (element->Active()) return false; // already exists
        if (size + num_dead > mask - mask / 4) {
            PanicIfReached();
            return false; // too full
        }

        if constexpr (k_ordering == HashTableOrdering::Ordered) {
            if (order_indices.items.size == order_indices.capacity) {
                PanicIfReached();
                return false; // too full
            }
        }

        if (element->hash == k_tombstone) --num_dead;
        ++size;
        element->key = key;
        element->data = value;
        element->hash = hash;

        AddToOrderedIndicesIfNeeded((usize)(element - elems), nullptr);

        return true;
    }

    // allocator must be the same as created this table
    bool InsertGrowIfNeeded(Allocator& allocator, KeyType key, ValueType value, u64 hash = 0) {
        if (!elems) IncreaseCapacity(allocator, k_min_size);
        if (!hash) hash = Hash(key);
        Element* element = Lookup(key, hash, k_tombstone);
        if (element->Active()) return false; // already exists

        auto const old_hash = element->hash; // save old hash in case it's tombstone marker
        element->key = key;
        element->data = value;
        element->hash = hash;
        AddToOrderedIndicesIfNeeded((usize)(element - elems), &allocator);

        if (++size + num_dead > mask - mask / 4) {
            IncreaseCapacity(allocator, 2 * size);
            num_dead = 0;
        } else if (old_hash == k_tombstone) {
            // re-used tomb
            --num_dead;
        }

        return true;
    }

    struct FindOrInsertResult {
        Element& element;
        bool inserted;
    };

    FindOrInsertResult FindOrInsertWithoutGrowing(KeyType key, ValueType value, u64 hash = 0) {
        if (!elems) PanicIfReached(); // not initialized
        if (!hash) hash = Hash(key);
        Element* element = Lookup(key, hash, k_tombstone);
        if (element->Active()) return {.element = *element, .inserted = false};

        if (size + num_dead > mask - mask / 4) PanicIfReached(); // too full
        if constexpr (k_ordering == HashTableOrdering::Ordered) {
            if (order_indices.items.size == order_indices.capacity) PanicIfReached(); // too full
        }

        if (element->hash == k_tombstone) --num_dead;
        ++size;
        element->key = key;
        element->data = value;
        element->hash = hash;

        AddToOrderedIndicesIfNeeded((usize)(element - elems), nullptr);

        return {.element = *element, .inserted = true};
    }

    FindOrInsertResult
    FindOrInsertGrowIfNeeded(Allocator& allocator, KeyType key, ValueType value, u64 hash = 0) {
        if (!elems) IncreaseCapacity(allocator, k_min_size);
        if (!hash) hash = Hash(key);
        Element* element = Lookup(key, hash, k_tombstone);
        if (element->Active()) return {.element = *element, .inserted = false};

        auto const old_hash = element->hash; // save old hash in case it's tombstone marker
        element->key = key;
        element->data = value;
        element->hash = hash;
        AddToOrderedIndicesIfNeeded((usize)(element - elems), &allocator);

        if (++size + num_dead > mask - mask / 4) {
            IncreaseCapacity(allocator, 2 * size);
            num_dead = 0;
        } else if (old_hash == k_tombstone) {
            // re-used tomb
            --num_dead;
        }

        return {.element = *element, .inserted = true};
    }

    Iterator begin() const {
        if (!elems) return end();

        if constexpr (k_ordering == HashTableOrdering::Unordered) {
            usize index = 0;
            for (; index < mask + 1; ++index)
                if (elems[index].Active()) return Iterator {*this, index};

            return end();
        } else {
            if (order_indices.items.size == 0) return end();
            {
                auto const element_index = order_indices.items[0];
                auto& element = elems[element_index];
                ASSERT_HOT(element.Active());
            }
            return Iterator {*this, 0};
        }
    }
    Iterator end() const {
        if constexpr (k_ordering == HashTableOrdering::Unordered)
            return Iterator {*this, mask + 1};
        else
            return Iterator {*this, order_indices.items.size};
    }

    HashTable Clone(Allocator& allocator, CloneType type) const {
        auto cloned_elements = allocator.Clone(Elements(), type);
        return {
            .elems = cloned_elements.data,
            .mask = mask,
            .size = size,
            .num_dead = num_dead,
        };
    }

    void Assign(HashTable const& other, Allocator& allocator) {
        if (this == &other) return; // self-assign

        Free(allocator);
        elems = allocator.Clone(other.Elements(), CloneType::Deep).data;
        mask = other.mask;
        size = other.size;
        num_dead = other.num_dead;
        if constexpr (k_ordering == HashTableOrdering::Ordered) {
            auto new_arr = allocator.AllocateExactSizeUninitialised<usize>(other.order_indices.capacity);
            for (usize i = 0; i < other.order_indices.items.size; ++i)
                new_arr[i] = other.order_indices.items[i];
            order_indices.items = {new_arr.data, other.order_indices.items.size};
            order_indices.capacity = new_arr.size;
        }
    }

    // Takes another HashTable and intersects it with this one: only elements that are present in both tables
    // will remain.
    void IntersectWith(HashTable const& other) {
        if (!elems || !other.elems) return;

        for (usize i = 0; i < mask + 1; ++i) {
            auto& element = elems[i];
            if (element.Active()) {
                Element* other_element = other.Lookup(element.key, element.hash, 0);
                if (!other_element || !other_element->Active()) DeleteIndex(i);
            }
        }
    }

    void RemoveFromOrderedIndicesIfNeeded(usize elem_index) {
        if constexpr (k_ordering == HashTableOrdering::Ordered) {
            bool found = false;
            // Find the element in the ordering array
            for (usize i = 0; i < order_indices.items.size; ++i) {
                if (order_indices.items[i] == elem_index) {
                    // Shift remaining elements left
                    for (usize j = i; j < order_indices.items.size - 1; ++j)
                        order_indices.items[j] = order_indices.items[j + 1];
                    --order_indices.items.size;
                    found = true;
                    break;
                }
            }
            ASSERT_HOT(found);
        }
    }

    void AddToOrderedIndicesIfNeeded(usize elem_index, Allocator* a) {
        if constexpr (k_ordering == HashTableOrdering::Ordered) {
            if (a) order_indices.Reserve(order_indices.items.size + 1, *a);

            ASSERT_HOT(elems[elem_index].Active());

            auto const& new_elem = elems[elem_index];

            auto const insert_index = BinarySearchForSlotToInsert(order_indices.items, [&](usize elem_index) {
                auto const& elem = elems[elem_index];
                if constexpr (k_less_than_function != nullptr) {
                    if (k_less_than_function(elem.key, elem.data, new_elem.key, new_elem.data)) return -1;
                } else if (elem.key < new_elem.key)
                    return -1;
                // keys are unique in a hash table, so we don't need to check for equality
                return 1;
            });

            MakeRoomForInsertion(order_indices.items, insert_index, 1); // size is increased by 1
            order_indices.items[insert_index] = elem_index;
        }
    }

    Element* elems {};
    usize mask {};
    usize size {};
    usize num_dead {};

    [[no_unique_address]] Conditional<k_ordering == HashTableOrdering::Ordered,
                                      OrderIndicesArray,
                                      DummyValueType> order_indices {};
};

template <TriviallyCopyable KeyType_,
          TriviallyCopyableOrDummy ValueType_,
          HashFunction<KeyType_> k_hash_function_ = nullptr,
          HashTableOrdering k_ordering_ = HashTableOrdering::Unordered,
          LessThanFunction<KeyType_, ValueType_> k_less_than_function = nullptr>
struct DynamicHashTable {
    using KeyType = KeyType_;
    using ValueType = ValueType_;
    static constexpr HashFunction<KeyType> k_hash_function = k_hash_function_;
    static constexpr HashTableOrdering k_ordering = k_ordering_;
    using Table = HashTable<KeyType, ValueType, k_hash_function, k_ordering, k_less_than_function>;

    DynamicHashTable(Allocator& alloc, usize initial_capacity = 0) : allocator(alloc) {
        if (initial_capacity) IncreaseCapacity(initial_capacity);
    }

    ~DynamicHashTable() { Free(); }

    DynamicHashTable(DynamicHashTable&& other) : allocator(other.allocator), table(other.table) {
        other.table = {};
    }

    DynamicHashTable& operator=(DynamicHashTable&& other) {
        Free();

        if (&other.allocator == &allocator) {
            table = other.table;
        } else {
            table.Assign(other.table, allocator);
            other.Free();
        }

        other.table = {};

        return *this;
    }

    NON_COPYABLE(DynamicHashTable);

    Table ToOwnedTable() {
        auto result = table;
        table = {};
        return result;
    }

    // table must have been created with allocator
    static constexpr DynamicHashTable FromOwnedTable(Table table, Allocator& allocator) {
        DynamicHashTable result {allocator};
        result.table = table;
        return result;
    }

    void Free() { table.Free(allocator); }

    void IncreaseCapacity(usize capacity) { table.IncreaseCapacity(allocator, capacity); }

    ValueType* Find(KeyType key) const { return table.Find(key); }
    Table::Element* FindElement(KeyType key) const { return table.FindElement(key); }

    bool Delete(KeyType key) { return table.Delete(key); }
    void DeleteIndex(usize i) { table.DeleteIndex(i); }
    void DeleteAll() { table.DeleteAll(); }

    void Assign(Table const& other) { table.Assign(other, allocator); }

    Span<typename Table::Element const> Elements() const { return table.Elements(); }

    auto Insert(KeyType key, ValueType value, u64 hash = 0) {
        return table.InsertGrowIfNeeded(allocator, key, value, hash);
    }
    Table::FindOrInsertResult FindOrInsert(KeyType key, ValueType value, u64 hash = 0) {
        return table.FindOrInsertGrowIfNeeded(allocator, key, value, hash);
    }
    bool Contains(KeyType key, u64 hash = 0) const { return table.Contains(key, hash); }

    auto begin() const { return table.begin(); }
    auto end() const { return table.end(); }

    operator Table() const { return this->table; }

    Allocator& allocator;
    Table table {};
};

template <TriviallyCopyable KeyType_,
          HashFunction<KeyType_> k_hash_function_ = nullptr,
          HashTableOrdering k_ordering_ = HashTableOrdering::Unordered,
          LessThanFunction<KeyType_, DummyValueType> k_less_than_function = nullptr>
struct Set : HashTable<KeyType_, DummyValueType, k_hash_function_, k_ordering_> {
    using KeyType = KeyType_;
    static constexpr HashFunction<KeyType> k_hash_function = k_hash_function_;
    static constexpr HashTableOrdering k_ordering = k_ordering_;
    using Table = HashTable<KeyType, DummyValueType, k_hash_function, k_ordering, k_less_than_function>;

    // delete methods that don't make sense for a set
    bool InsertWithoutGrowing(KeyType key, DummyValueType, u64 hash = 0) = delete;
    bool InsertGrowIfNeeded(Allocator& allocator, KeyType key, DummyValueType, u64 hash = 0) = delete;
    Table::FindOrInsertResult FindOrInsertWithoutGrowing(KeyType key, DummyValueType, u64 hash = 0) = delete;
    Table::FindOrInsertResult
    FindOrInsertGrowIfNeeded(Allocator& allocator, KeyType key, DummyValueType, u64 hash = 0) = delete;
    DummyValueType* Find(KeyType key) const = delete;

    // replace with methods that make sense for a set
    static Set Create(Allocator& a, usize size) { return Set {Table::Create(a, size)}; }
    auto InsertWithoutGrowing(KeyType key, u64 hash = 0) {
        return Table::InsertWithoutGrowing(key, {}, hash);
    }
    // allocator must be the same as created this table
    auto InsertGrowIfNeeded(Allocator& allocator, KeyType key, u64 hash = 0) {
        return Table::InsertGrowIfNeeded(allocator, key, {}, hash);
    }
    auto FindOrInsertWithoutGrowing(KeyType key, u64 hash = 0) {
        return Table::FindOrInsertWithoutGrowing(key, {}, hash);
    }
    auto FindOrInsertGrowIfNeeded(Allocator& allocator, KeyType key, u64 hash = 0) {
        return Table::FindOrInsertGrowIfNeeded(allocator, key, {}, hash);
    }
};

template <TriviallyCopyable KeyType_,
          HashFunction<KeyType_> k_hash_function_ = nullptr,
          HashTableOrdering k_ordering_ = HashTableOrdering::Unordered,
          LessThanFunction<KeyType_, DummyValueType> k_less_than_function = nullptr>
struct DynamicSet : DynamicHashTable<KeyType_, DummyValueType, k_hash_function_, k_ordering_> {
    using KeyType = KeyType_;
    static constexpr HashFunction<KeyType> k_hash_function = k_hash_function_;
    static constexpr HashTableOrdering k_ordering = k_ordering_;
    using Set = Set<KeyType, k_hash_function, k_ordering, k_less_than_function>;

    DynamicSet(Allocator& alloc, usize initial_capacity = 0)
        : DynamicHashTable<KeyType, DummyValueType, k_hash_function>(alloc, initial_capacity) {}

    auto Insert(KeyType key, u64 hash = 0) {
        return DynamicHashTable<KeyType, DummyValueType, k_hash_function>::Insert(key, {}, hash);
    }

    Set ToOwnedSet() {
        auto result = this->table;
        this->table = {};
        return (Set)result;
    }

    operator Set() const { return this->table; }

    DummyValueType* Find(KeyType key) const = delete;
    Set::Element* FindElement(KeyType key) const = delete;
};

// Ordered versions
template <TriviallyCopyable KeyType,
          TriviallyCopyableOrDummy ValueType,
          HashFunction<KeyType> k_hash_function = nullptr,
          LessThanFunction<KeyType, ValueType> k_less_than_function = nullptr>
using OrderedHashTable =
    HashTable<KeyType, ValueType, k_hash_function, HashTableOrdering::Ordered, k_less_than_function>;

template <TriviallyCopyable KeyType,
          TriviallyCopyableOrDummy ValueType,
          HashFunction<KeyType> k_hash_function = nullptr,
          LessThanFunction<KeyType, ValueType> k_less_than_function = nullptr>
using DynamicOrderedHashTable =
    DynamicHashTable<KeyType, ValueType, k_hash_function, HashTableOrdering::Ordered, k_less_than_function>;

template <TriviallyCopyable KeyType,
          HashFunction<KeyType> k_hash_function = nullptr,
          LessThanFunction<KeyType, DummyValueType> k_less_than_function = nullptr>
using OrderedSet = Set<KeyType, k_hash_function, HashTableOrdering::Ordered, k_less_than_function>;

template <TriviallyCopyable KeyType,
          HashFunction<KeyType> k_hash_function = nullptr,
          LessThanFunction<KeyType, DummyValueType> k_less_than_function = nullptr>
using DynamicOrderedSet = DynamicHashTable<KeyType,
                                           DummyValueType,
                                           k_hash_function,
                                           HashTableOrdering::Ordered,
                                           k_less_than_function>;
