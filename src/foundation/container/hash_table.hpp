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

u64 NoHash(u64 const&);

enum HashTableOrdering { Unordered, Ordered };

template <TriviallyCopyable KeyType_,
          TriviallyCopyableOrDummy ValueType_,
          HashFunction<KeyType_> k_hash_function_ = nullptr,
          HashTableOrdering k_ordering_ = HashTableOrdering::Unordered>
struct HashTable {
    using KeyType = KeyType_;
    using ValueType = ValueType_;
    static constexpr HashFunction<KeyType> k_hash_function = k_hash_function_;
    static constexpr HashTableOrdering k_ordering = k_ordering_;

    struct Element {
        [[no_unique_address]] ValueType data {};
        KeyType key {};
        u64 hash {};
        bool active {};
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
        struct Item {
            KeyType key;
            [[no_unique_address]] Conditional<Same<DummyValueType, ValueType>, DummyValueType, ValueType*>
                value_ptr;
        };
        Item operator*() const { return item; }
        Iterator& operator++() {
            ++index;

            if constexpr (k_ordering == HashTableOrdering::Unordered) {
                for (; index < table.mask + 1; ++index) {
                    auto& element = table.elems[index];
                    if (element.active) {
                        item.key = element.key;
                        if constexpr (!Same<DummyValueType, ValueType>) item.value_ptr = &element.data;
                        break;
                    }
                }
            } else if (index < table.order_indices.items.size) {
                auto const elem_index = table.order_indices.items[index];
                auto& element = table.elems[elem_index];
                item.key = element.key;
                if constexpr (!Same<DummyValueType, ValueType>) item.value_ptr = &element.data;
            }

            return *this;
        }

        HashTable const& table;
        Item item {};
        usize index {};
    };

    static constexpr usize k_min_size = 8;
    static constexpr usize k_max_size = (((usize)-1 / 2) + 1);
    static constexpr u64 k_tombstone = 0xdeadc0de;

    static u64 Hash(KeyType k) {
        // IMPORTANT: we don't set Hash as the k_hash_function in the template arguments because we don't know
        // if Hash is consistent accross different compilation units. It might be a header-only function in
        // which case the _type_ of the HashTable will vary depending on the compilation unit leading to
        // cryptic linker errors.
        if constexpr (k_hash_function == nullptr)
            return ::Hash(k);
        else
            return k_hash_function(k);
    }

    // Quadratic probing is used if there's a hash collision
    Element* Lookup(KeyType key, u64 hash, u64 dead_hash_value) const {
        ASSERT(elems);

        Element* element;
        usize index = hash;
        usize step = 1;

        while (true) {
            element = elems + (index & mask);

            if (!element->active) {
                if (!element->hash) break;
                if (element->hash == dead_hash_value) break;
            }

            if (element->hash == hash && element->key == key) break;

            index += step;
            step++;
        }

        return element;
    }

    Element* FindElement(KeyType key) const {
        if (!elems) return nullptr;
        Element* element = Lookup(key, Hash(key), 0);
        if (element->active) return element;
        return nullptr;
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
            table.order_indices.items = a.AllocateExactSizeUninitialised<usize>(size);
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

    ValueType* Find(KeyType key) const {
        static_assert(!Same<ValueType, DummyValueType>,
                      "HashTable::Find called on a set, use FindElement instead");
        Element* element = FindElement(key);
        if (!element) return nullptr;
        return &element->data;
    }

    void DeleteIndex(usize index) {
        RemoveFromOrderedIndicesIfNeeded(index);
        elems[index].active = false;
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

        for (auto& element : Elements()) {
            if (element.active) {
                element.active = false;
                element.hash = k_tombstone;
            }
        }

        if constexpr (k_ordering == HashTableOrdering::Ordered) order_indices.items.size = 0;
        size = 0;
        num_dead = 0;
    }

    // allocator must be the same as created this table
    void IncreaseCapacity(Allocator& allocator, usize capacity) {
        auto old_elements = Elements();

        if constexpr (k_ordering == HashTableOrdering::Ordered) order_indices.Reserve(capacity, allocator);

        capacity = PowerOf2Capacity(capacity);
        elems = allocator.template NewMultiple<Element>(capacity).data;
        mask = capacity - 1;

        if (old_elements.size) {
            Element* new_element;
            for (auto& old_element : old_elements)
                if (old_element.active) {
                    for (u64 i = old_element.hash, j = 1;; i += j++) {
                        new_element = elems + (i & mask);
                        if (!new_element->active) break;
                    }
                    *new_element = old_element;
                }
            allocator.Free(old_elements.ToByteSpan());
        }
        return;
    }

    bool InsertWithoutGrowing(KeyType key, ValueType value) {
        if (!elems) {
            PanicIfReached();
            return false;
        }
        auto const hash = Hash(key);
        Element* element = Lookup(key, hash, k_tombstone);

        if (element->active) return false; // already exists
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
        element->active = true;
        element->data = value;
        element->hash = hash;

        AddToOrderedIndicesIfNeeded((usize)(element - elems), nullptr);

        return true;
    }

    // allocator must be the same as created this table
    bool InsertGrowIfNeeded(Allocator& allocator, KeyType key, ValueType value) {
        if (!elems) IncreaseCapacity(allocator, k_min_size);
        auto const hash = Hash(key);
        Element* element = Lookup(key, hash, k_tombstone);
        if (element->active) return false; // already exists

        auto const old_hash = element->hash; // save old hash in case it's tombstone marker
        element->active = true;
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
        Element* element;
        bool inserted;
    };

    FindOrInsertResult FindOrInsertWithoutGrowing(KeyType key, ValueType value) {
        auto const hash = Hash(key);
        Element* element = Lookup(key, hash, k_tombstone);
        if (element->active) return {.element = element, .inserted = false};

        if (size + num_dead > mask - mask / 4) {
            PanicIfReached();
            return {}; // too full
        }
        if constexpr (k_ordering == HashTableOrdering::Ordered) {
            if (order_indices.items.size == order_indices.capacity) {
                PanicIfReached();
                return {}; // too full
            }
        }

        if (element->hash == k_tombstone) --num_dead;
        ++size;
        element->key = key;
        element->active = true;
        element->data = value;
        element->hash = hash;

        AddToOrderedIndicesIfNeeded((usize)(element - elems), nullptr);

        return {.element = element, .inserted = true};
    }

    FindOrInsertResult FindOrInsertGrowIfNeeded(Allocator& allocator, KeyType key, ValueType value) {
        if (!elems) IncreaseCapacity(allocator, k_min_size);
        auto const hash = Hash(key);
        Element* element = Lookup(key, hash, k_tombstone);
        if (element->active) return {.element = element, .inserted = false};

        auto const old_hash = element->hash; // save old hash in case it's tombstone marker
        element->active = true;
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

        return {.element = element, .inserted = true};
    }

    Iterator begin() const {
        if (!elems) return end();
        typename Iterator::Item item {};

        if constexpr (k_ordering == HashTableOrdering::Unordered) {
            usize index = 0;
            for (; index < mask + 1; ++index) {
                auto& element = elems[index];
                if (element.active) {
                    item.key = element.key;
                    if constexpr (!Same<DummyValueType, ValueType>) item.value_ptr = &element.data;
                    break;
                }
            }

            return Iterator {*this, item, index};
        } else {
            if (order_indices.items.size == 0) return end();
            auto const element_index = order_indices.items[0];
            auto& element = elems[element_index];
            item.key = element.key;
            if constexpr (!Same<DummyValueType, ValueType>) item.value_ptr = &element.data;

            return Iterator {*this, item, 0};
        }
    }
    Iterator end() const {
        if constexpr (k_ordering == HashTableOrdering::Unordered)
            return Iterator {*this, {}, mask + 1};
        else
            return Iterator {*this, {}, order_indices.items.size};
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
            if (element.active) {
                Element* other_element = other.Lookup(element.key, element.hash, 0);
                if (!other_element || !other_element->active) DeleteIndex(i);
            }
        }
    }

    void RemoveFromOrderedIndicesIfNeeded(usize elem_index) {
        if constexpr (k_ordering == HashTableOrdering::Ordered) {
            // Find the element in the ordering array
            for (usize i = 0; i < order_indices.items.size; ++i) {
                if (order_indices.items[i] == elem_index) {
                    // Shift remaining elements left
                    for (usize j = i; j < order_indices.items.size - 1; ++j)
                        order_indices.items[j] = order_indices.items[j + 1];
                    --order_indices.items.size;
                    break;
                }
            }
        }
    }

    void AddToOrderedIndicesIfNeeded(usize elem_index, Allocator* a) {
        if constexpr (k_ordering == HashTableOrdering::Ordered) {
            if (a) order_indices.Reserve(order_indices.items.size + 1, *a);

            auto const& new_key = elems[elem_index].key;

            auto const insert_index = BinarySearchForSlotToInsert(order_indices.items, [&](usize elem_index) {
                auto const& key = elems[elem_index].key;
                if (key < new_key) return -1;
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
          HashTableOrdering k_ordering_ = HashTableOrdering::Unordered>
struct DynamicHashTable {
    using KeyType = KeyType_;
    using ValueType = ValueType_;
    static constexpr HashFunction<KeyType> k_hash_function = k_hash_function_;
    static constexpr HashTableOrdering k_ordering = k_ordering_;
    using Table = HashTable<KeyType, ValueType, k_hash_function, k_ordering>;

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

    auto Insert(KeyType key, ValueType value) { return table.InsertGrowIfNeeded(allocator, key, value); }
    Table::FindOrInsertResult FindOrInsert(KeyType key, ValueType value) {
        return table.FindOrInsertGrowIfNeeded(allocator, key, value);
    }

    auto begin() const { return table.begin(); }
    auto end() const { return table.end(); }

    operator Table() const { return this->table; }

    Allocator& allocator;
    Table table {};
};

template <TriviallyCopyable KeyType_,
          HashFunction<KeyType_> k_hash_function_ = nullptr,
          HashTableOrdering k_ordering_ = HashTableOrdering::Unordered>
struct Set : HashTable<KeyType_, DummyValueType, k_hash_function_, k_ordering_> {
    using KeyType = KeyType_;
    static constexpr HashFunction<KeyType> k_hash_function = k_hash_function_;
    static constexpr HashTableOrdering k_ordering = k_ordering_;
    using Table = HashTable<KeyType, DummyValueType, k_hash_function, k_ordering>;

    // delete methods that don't make sense for a set
    Table::Element* InsertWithoutGrowing(KeyType key, DummyValueType) = delete;
    Table::Element* InsertGrowIfNeeded(Allocator& allocator, KeyType key, DummyValueType) = delete;
    DummyValueType* Find(KeyType key) const = delete;

    // replace with methods that make sense for a set
    static Set Create(Allocator& a, usize size) { return Set {Table::Create(a, size)}; }
    auto InsertWithoutGrowing(KeyType key) { return Table::InsertWithoutGrowing(key, {}); }
    // allocator must be the same as created this table
    auto InsertGrowIfNeeded(Allocator& allocator, KeyType key) {
        return Table::InsertGrowIfNeeded(allocator, key, {});
    }
    bool Contains(KeyType key) const { return this->FindElement(key); }
};

template <TriviallyCopyable KeyType_,
          HashFunction<KeyType_> k_hash_function_ = nullptr,
          HashTableOrdering k_ordering_ = HashTableOrdering::Unordered>
struct DynamicSet : DynamicHashTable<KeyType_, DummyValueType, k_hash_function_, k_ordering_> {
    using KeyType = KeyType_;
    static constexpr HashFunction<KeyType> k_hash_function = k_hash_function_;
    static constexpr HashTableOrdering k_ordering = k_ordering_;
    using Set = Set<KeyType, k_hash_function, k_ordering>;

    DynamicSet(Allocator& alloc, usize initial_capacity = 0)
        : DynamicHashTable<KeyType, DummyValueType, k_hash_function>(alloc, initial_capacity) {}

    auto Insert(KeyType key) {
        return DynamicHashTable<KeyType, DummyValueType, k_hash_function>::Insert(key, {});
    }

    Set ToOwnedSet() {
        auto result = this->table;
        this->table = {};
        return (Set)result;
    }

    operator Set() const { return this->table; }

    DummyValueType* Find(KeyType key) const = delete;
    Set::Element* FindElement(KeyType key) const = delete;

    bool Contains(KeyType key) const { return this->table.FindElement(key); }
};
