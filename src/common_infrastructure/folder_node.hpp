// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"

struct FolderNode {
    friend bool operator<(FolderNode const& a, FolderNode const& b);
    u64 Hash() const;

    String name {};
    String abbreviated_name {};
    FolderNode* parent {};
    FolderNode* first_child {};
    FolderNode* next {};
};

struct FolderNodeAllocators {
    struct NameAllocator {
        PathPool& path_pool;
        Allocator& path_pool_arena;
    };

    Allocator& node_allocator;
    Optional<NameAllocator> name_allocator; // if null, then names are just referenced
};

void ForEachNode(FolderNode* tree, FunctionRef<void(FolderNode*)> func);

FolderNode*
FindOrInsertFolderNode(FolderNode* root, Span<String const> parts, FolderNodeAllocators const& allocators);

// Returns null if the there are more parts than allowed.
FolderNode* FindOrInsertFolderNode(FolderNode* root,
                                   String subpath,
                                   usize max_sub_parts,
                                   FolderNodeAllocators const& allocators);

void FreeFolderNode(FolderNode const* folder, FolderNodeAllocators const& allocators);
void SetParent(FolderNode* folder, FolderNode* parent);
void SortFolderTree(FolderNode* root);

PUBLIC bool IsInsideFolder(FolderNode const* node, usize folder_hash) {
    for (auto f = node; f; f = f->parent)
        if (f->Hash() == folder_hash) return true;
    return false;
}
