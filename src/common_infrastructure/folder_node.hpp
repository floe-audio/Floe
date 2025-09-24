// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"

struct TypeErasedUserData {
    void* data;
    u64 type_hash;

    template <typename T>
    static TypeErasedUserData Create(T* ptr) {
        using BaseType = RemoveCV<T>;
        return {(void*)ptr, TypeHashFor<BaseType>()};
    }

    template <typename T>
    T* As() const {
        using BaseType = RemoveCV<T>;
        return (type_hash == TypeHashFor<BaseType>()) ? (T*)data : nullptr;
    }

    operator bool() const { return data != nullptr; }

  private:
    template <typename T>
    static constexpr u64 TypeHashFor() {
        return HashComptime(__PRETTY_FUNCTION__);
    }
};

struct FolderNode {
    u64 Hash() const;

    String name {};
    String display_name {}; // Optional name for display purposes.
    FolderNode* parent {};
    FolderNode* first_child {};
    FolderNode* next {};
    TypeErasedUserData user_data {};
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

// Returns the node that is the first common ancestor of all the nodes. IMPORTANT: all nodes must have the
// same single top-level node.
FolderNode* FirstCommonAncestor(Span<FolderNode*> nodes, ArenaAllocator& scratch_arena);

PUBLIC bool IsInsideFolder(FolderNode const* node, usize folder_hash) {
    for (auto f = node; f; f = f->parent)
        if (f->Hash() == folder_hash) return true;
    return false;
}
