// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "folder_node.hpp"

#include "tests/framework.hpp"

u64 FolderNode::Hash() const {
    auto hash = HashInit();
    for (auto f = this; f; f = f->parent)
        HashUpdate(hash, f->name);
    return hash;
}

void ForEachNode(FolderNode* tree, FunctionRef<void(FolderNode*)> func) {
    if (!tree) return;

    func(tree);

    for (auto* child = tree->first_child; child; child = child->next)
        ForEachNode(child, func);
}

static FolderNode* MatchNode(FolderNode* root, FolderNode* n, Span<String const> parts) {
    auto* part_cursor = parts.data + parts.size - 1;
    auto* folder_cursor = n;

    while (true) {
        // Compare name strings.
        if (*part_cursor != folder_cursor->name) return nullptr;

        // Increment cursors.
        --part_cursor;
        folder_cursor = folder_cursor->parent;

        // Check if either cursor is at its end.
        if (!folder_cursor) return nullptr; //
        if (part_cursor < parts.data) {
            // We have matched all parts, now check if the folder_cursor is the root node
            if (folder_cursor == root)
                return n;
            else
                return nullptr; // The parts matched, but there's a different parent.
        }
    }

    return nullptr;
}

static FolderNode* FindNodeWithParts(FolderNode* root, FolderNode* n, Span<String const> parts) {
    ASSERT_HOT(parts.size);
    if (!n) return nullptr;

    if (MatchNode(root, n, parts)) return n;

    for (auto child = n->first_child; child; child = child->next) {
        auto found = FindNodeWithParts(root, child, parts);
        if (found) return found;
    }

    return nullptr;
}

FolderNode*
FindOrInsertFolderNode(FolderNode* root, Span<String const> parts, FolderNodeAllocators const& allocators) {
    if (parts.size == 0) return root;

    FolderNode* folder = root;
    for (usize part_count = 1; part_count <= parts.size; ++part_count) {
        auto f = FindNodeWithParts(root, root, parts.SubSpan(0, part_count));
        if (!f) {
            auto const name = parts[part_count - 1];
            f = allocators.node_allocator.New<FolderNode>(FolderNode {
                .name = allocators.name_allocator ? allocators.name_allocator->path_pool.Clone(
                                                        name,
                                                        allocators.name_allocator->path_pool_arena)
                                                  : name,
            });
            if (!f) return nullptr;
            SetParent(f, folder);
        }
        folder = f;
    }

    return folder;
}

FolderNode* FindOrInsertFolderNode(FolderNode* root,
                                   String subpath,
                                   usize max_sub_parts,
                                   FolderNodeAllocators const& allocators) {
    using PartsArray = Array<String, 12>;
    ASSERT(max_sub_parts <= PartsArray::size);
    usize num_parts = 0;
    auto& parts = *(PartsArray*)__builtin_alloca(sizeof(PartsArray));

    usize cursor = 0;
    while (auto part = SplitWithIterator(subpath, cursor, '/')) {
        if (num_parts == PartsArray::size) return nullptr;
        if (num_parts == max_sub_parts) return nullptr;
        parts[num_parts++] = *part;
    }

    return FindOrInsertFolderNode(root, Span {parts.data, num_parts}, allocators);
}

void FreeFolderNode(FolderNode const* folder, FolderNodeAllocators const& allocators) {
    if (!folder) return;

    if (allocators.name_allocator) allocators.name_allocator->path_pool.Free(folder->name);
    allocators.node_allocator.Delete(folder);
}

void SetParent(FolderNode* folder, FolderNode* parent) {
    ASSERT(!folder->parent);
    for (auto* ancestor = parent; ancestor; ancestor = ancestor->parent)
        ASSERT(ancestor != folder);

    folder->parent = parent;

    if (parent) {
        if (!parent->first_child) {
            parent->first_child = folder;
        } else {
            for (auto* child = parent->first_child; child; child = child->next) {
                if (!child->next) {
                    child->next = folder;
                    break;
                }
            }
        }
    }
}

void SortFolderTree(FolderNode* root) {
    if (!root) return;

    bool swapped;
    do {
        swapped = false;
        FolderNode** current = &root->first_child;

        while (*current && (*current)->next) {
            if (!((*current)->name < (*current)->next->name)) {
                // Swap nodes
                FolderNode* first = *current;
                FolderNode* second = first->next;

                first->next = second->next;
                second->next = first;
                *current = second;

                swapped = true;
            }
            current = &(*current)->next;
        }
    } while (swapped);

    // Recursively sort children
    for (auto* child = root->first_child; child; child = child->next)
        SortFolderTree(child);
}

TEST_CASE(TestFolderFromString) {
    struct FolderNodeAllocator : Allocator {
        FolderNodeAllocator(ArenaAllocator& arena) : arena(arena) {}
        Span<u8> DoCommand(AllocatorCommandUnion const& command) override {
            CheckAllocatorCommandIsValid(command);

            switch (command.tag) {
                case AllocatorCommand::Allocate: {
                    auto const& cmd = command.Get<AllocateCommand>();
                    ASSERT(cmd.size == sizeof(FolderNode));
                    ++used;
                    return arena.DoCommand(command);
                }

                case AllocatorCommand::Free: {
                    PanicIfReached();
                    break;
                }

                case AllocatorCommand::Resize: {
                    PanicIfReached();
                    break;
                }
            }
            return {};
        }
        ArenaAllocator& arena;
        usize used = 0;
    };

    auto& a = tester.scratch_arena;
    FolderNode root {
        .name = "root",
    };
    FolderNodeAllocator folder_node_allocator(a);
    FolderNodeAllocators const allocators {
        .node_allocator = folder_node_allocator,
    };

    SUBCASE("empty string") {
        auto folder = FindOrInsertFolderNode(&root, {}, allocators);
        CHECK(folder == &root);
    }

    SUBCASE("single folder") {
        auto folder = FindOrInsertFolderNode(&root, Array {"Folder1"_s}, allocators);
        REQUIRE(folder != nullptr);
        CHECK(folder->name == "Folder1"_s);
        CHECK(folder->parent == &root);
        CHECK(folder->first_child == nullptr);
        CHECK(folder->next == nullptr);
        CHECK_EQ(folder_node_allocator.used, 1uz);
    }

    SUBCASE("nested folders") {
        auto folder3 =
            FindOrInsertFolderNode(&root, Array {"Folder1"_s, "Folder2"_s, "Folder3"_s}, allocators);

        REQUIRE(folder3 != nullptr);

        CHECK(folder3->name == "Folder3"_s);
        CHECK(folder3->parent != nullptr);
        CHECK(folder3->parent->name == "Folder2"_s);
        CHECK(folder3->parent->parent != nullptr);
        CHECK(folder3->parent->parent->name == "Folder1"_s);
        CHECK(folder3->first_child == nullptr);
        CHECK(folder3->next == nullptr);

        auto folder2 = folder3->parent;
        REQUIRE(folder2 != nullptr);
        CHECK(folder2->name == "Folder2"_s);
        CHECK(folder2->parent != nullptr);
        CHECK(folder2->first_child != nullptr);
        CHECK(folder2->first_child == folder3);
        CHECK(folder2->next == nullptr);

        auto folder1 = folder2->parent;
        REQUIRE(folder1 != nullptr);
        CHECK(folder1->name == "Folder1"_s);
        CHECK(folder1->parent == &root);
        CHECK(folder1->first_child != nullptr);
        CHECK(folder1->first_child == folder2);

        CHECK(root.parent == nullptr);
        CHECK(root.next == nullptr);

        CHECK_EQ(folder_node_allocator.used, 3uz);
    }

    SUBCASE("siblings") {
        auto folder1 = FindOrInsertFolderNode(&root, Array {"Folder1"_s}, allocators);
        auto folder2 = FindOrInsertFolderNode(&root, Array {"Folder1"_s, "Folder2"_s}, allocators);
        auto folder3 = FindOrInsertFolderNode(&root, Array {"Folder1"_s, "Folder3"_s}, allocators);

        REQUIRE(folder1 != nullptr);
        CHECK(folder1->name == "Folder1"_s);
        CHECK(folder1->parent == &root);
        CHECK(folder1->first_child != nullptr);
        CHECK(folder1->first_child == folder2);
        CHECK(folder1->next == nullptr);

        REQUIRE(folder2 != nullptr);
        CHECK(folder2->name == "Folder2"_s);
        CHECK(folder2->parent != nullptr);
        CHECK(folder2->parent == folder1);
        CHECK(folder2->first_child == nullptr);
        CHECK(folder2->next != nullptr);
        CHECK(folder2->next == folder3);

        REQUIRE(folder3 != nullptr);
        CHECK(folder3->name == "Folder3"_s);
        CHECK(folder3->parent != nullptr);
        CHECK(folder3->parent == folder1);
        CHECK(folder3->first_child == nullptr);
        CHECK(folder3->next == nullptr);

        CHECK_EQ(folder_node_allocator.used, 3uz);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterFolderNodeTests) { REGISTER_TEST(TestFolderFromString); }
