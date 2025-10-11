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

FolderNode* FirstCommonAncestor(Span<FolderNode*> nodes, ArenaAllocator& scratch_arena) {
    ASSERT(nodes.size);

    if (nodes.size == 1) return nodes[0];

    // Collect ancestor paths for all nodes
    DynamicArray<DynamicArray<FolderNode*>> ancestor_paths(scratch_arena);
    ASSERT(ancestor_paths.Reserve(nodes.size));

    for (auto node : nodes) {
        DynamicArray<FolderNode*> path(scratch_arena);

        // Build path from node to root
        for (auto* current = node; current; current = current->parent)
            ASSERT(dyn::Append(path, current));

        ASSERT(dyn::Append(ancestor_paths, Move(path)));
    }

    // Find the shortest path length to avoid going out of bounds
    usize min_path_length = ancestor_paths[0].size;
    for (usize i = 1; i < ancestor_paths.size; ++i)
        if (ancestor_paths[i].size < min_path_length) min_path_length = ancestor_paths[i].size;

    // Find common ancestors by checking from the root (end of paths) towards leaves
    FolderNode* common_ancestor = nullptr;
    for (usize depth = 0; depth < min_path_length; ++depth) {
        usize index_from_end = ancestor_paths[0].size - 1 - depth;
        FolderNode* candidate = ancestor_paths[0][index_from_end];

        // Check if this ancestor exists at the same depth in all other paths
        bool is_common = true;
        for (usize path_idx = 1; path_idx < ancestor_paths.size; ++path_idx) {
            usize other_index_from_end = ancestor_paths[path_idx].size - 1 - depth;
            if (ancestor_paths[path_idx][other_index_from_end] != candidate) {
                is_common = false;
                break;
            }
        }

        if (is_common)
            common_ancestor = candidate;
        else
            break;
    }

    ASSERT(common_ancestor);
    return common_ancestor;
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

TEST_CASE(TestFirstCommonAncestor) {
    auto& a = tester.scratch_arena;
    FolderNode root {
        .name = "root",
    };
    FolderNodeAllocators const allocators {
        .node_allocator = a,
    };

    SUBCASE("single node") {
        auto folder1 = FindOrInsertFolderNode(&root, Array {"Folder1"_s}, allocators);
        REQUIRE(folder1 != nullptr);

        Array<FolderNode*, 1> nodes {folder1};
        auto result = FirstCommonAncestor(nodes.Items(), a);
        CHECK(result == folder1);
    }

    SUBCASE("two sibling nodes") {
        auto folder1 = FindOrInsertFolderNode(&root, Array {"Parent"_s, "Child1"_s}, allocators);
        auto folder2 = FindOrInsertFolderNode(&root, Array {"Parent"_s, "Child2"_s}, allocators);
        REQUIRE(folder1 != nullptr);
        REQUIRE(folder2 != nullptr);

        Array<FolderNode*, 2> nodes {folder1, folder2};
        auto result = FirstCommonAncestor(nodes.Items(), a);
        CHECK(result->name == "Parent"_s);
        CHECK(result == folder1->parent);
        CHECK(result == folder2->parent);
    }

    SUBCASE("nodes at different depths") {
        auto folder1 = FindOrInsertFolderNode(&root, Array {"A"_s, "B"_s, "C"_s}, allocators);
        auto folder2 = FindOrInsertFolderNode(&root, Array {"A"_s, "D"_s}, allocators);
        REQUIRE(folder1 != nullptr);
        REQUIRE(folder2 != nullptr);

        Array<FolderNode*, 2> nodes {folder1, folder2};
        auto result = FirstCommonAncestor(nodes.Items(), a);
        CHECK(result->name == "A"_s);
    }

    SUBCASE("three nodes with common ancestor") {
        auto folder1 = FindOrInsertFolderNode(&root, Array {"Common"_s, "Branch1"_s, "Leaf1"_s}, allocators);
        auto folder2 = FindOrInsertFolderNode(&root, Array {"Common"_s, "Branch1"_s, "Leaf2"_s}, allocators);
        auto folder3 = FindOrInsertFolderNode(&root, Array {"Common"_s, "Branch2"_s, "Leaf3"_s}, allocators);
        REQUIRE(folder1 != nullptr);
        REQUIRE(folder2 != nullptr);
        REQUIRE(folder3 != nullptr);

        Array<FolderNode*, 3> nodes {folder1, folder2, folder3};
        auto result = FirstCommonAncestor(nodes.Items(), a);
        CHECK(result->name == "Common"_s);
    }

    SUBCASE("nodes where one is ancestor of another") {
        auto parent = FindOrInsertFolderNode(&root, Array {"Parent"_s}, allocators);
        auto child = FindOrInsertFolderNode(&root, Array {"Parent"_s, "Child"_s}, allocators);
        auto grandchild =
            FindOrInsertFolderNode(&root, Array {"Parent"_s, "Child"_s, "Grandchild"_s}, allocators);
        REQUIRE(parent != nullptr);
        REQUIRE(child != nullptr);
        REQUIRE(grandchild != nullptr);

        Array<FolderNode*, 3> nodes {parent, child, grandchild};
        auto result = FirstCommonAncestor(nodes.Items(), a);
        CHECK(result == parent);
        CHECK(result->name == "Parent"_s);
    }

    SUBCASE("all nodes are root") {
        Array<FolderNode*, 2> nodes {&root, &root};
        auto result = FirstCommonAncestor(nodes.Items(), a);
        CHECK(result == &root);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterFolderNodeTests) {
    REGISTER_TEST(TestFolderFromString);
    REGISTER_TEST(TestFirstCommonAncestor);
}
