// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preset_server.hpp"

#include <xxhash.h>

#include "utils/logger/logger.hpp"

#include "common_infrastructure/state/state_coding.hpp"

constexpr bool k_skip_duplicate_presets = false;

// If all presets in this folder and all subfolders use the same single library, return that library.
static bool AllPresetsSingleLibrary(FolderNode const* node,
                                    Optional<sample_lib::LibraryIdRef>& single_library) {
    if (auto const folder = node->user_data.As<PresetFolderListing const>()->folder) {
        if (folder->used_libraries.size > 3) return false;
        if (folder->used_libraries.size != 0) {
            ASSERT(folder->used_libraries.size == 1 || folder->used_libraries.size == 2 ||
                   folder->used_libraries.size == 3);

            Optional<sample_lib::LibraryIdRef> library;
            if (folder->used_libraries.size != 1) {
                u8 num_proper_libraries = 0;
                for (auto const& lib : folder->used_libraries) {
                    if (lib.key != sample_lib::k_mirage_compat_library_id &&
                        lib.key != sample_lib::k_builtin_library_id) {
                        ++num_proper_libraries;
                        if (num_proper_libraries == 2) return false;
                        library = lib.key;
                    }
                }
            } else {
                library = (*folder->used_libraries.begin()).key;
            }

            if (library) {
                if (single_library) {
                    if (*single_library != *library) return false;
                } else {
                    single_library = *library;
                }
            }
        }
    }

    for (auto* child = node->first_child; child; child = child->next)
        if (!AllPresetsSingleLibrary(child, single_library)) return false;

    return true;
}

Optional<sample_lib::LibraryIdRef> AllPresetsSingleLibrary(FolderNode const& node) {
    Optional<sample_lib::LibraryIdRef> single_library {};
    if (AllPresetsSingleLibrary(&node, single_library)) return single_library;
    return k_nullopt;
}

PresetBank const* PresetBankInfoAtNode(FolderNode const& node) {
    PresetBank const* metadata {};
    auto const listing = node.user_data.As<PresetFolderListing const>();
    if (listing->folder && listing->folder->preset_bank_info)
        metadata = &*listing->folder->preset_bank_info;
    else if (listing->fallback_preset_bank_info)
        metadata = listing->fallback_preset_bank_info;
    return metadata;
}

PresetBank const* ContainingPresetBank(FolderNode const* node) {
    for (auto f = node; f; f = f->parent)
        if (auto const info = PresetBankInfoAtNode(*f); info) return info;
    return nullptr;
}

bool IsInsideFolder(PresetFolderListing const* node, usize folder_node_hash) {
    FolderNode const* possible_parent = nullptr;
    for (auto f = &node->node; f; f = f->parent)
        if (f->Hash() == folder_node_hash) {
            possible_parent = f;
            break;
        }
    if (!possible_parent) return false;

    // The node and the possible parent must be in the same preset bank.
    if (ContainingPresetBank(&node->node) != ContainingPresetBank(possible_parent)) return false;
    return true;
}

static u64 FolderContentsHash(FolderNode const* node) {
    // Using XOR and only when we have an all_presets_hash means it doesn't matter about the order or exact
    // hierarchy of the tree.
    u64 hash = 0;
    if (auto const folder = node->user_data.As<PresetFolderListing const>()->folder)
        hash ^= folder->all_presets_hash;
    for (auto n = node->first_child; n; n = n->next)
        hash ^= FolderContentsHash(n);
    return hash;
}

static String ExtensionForPreset(PresetFolder::Preset const& preset) {
    switch (preset.file_format) {
        case PresetFormat::Mirage: return preset.file_extension;
        case PresetFormat::Floe: return FLOE_PRESET_FILE_EXTENSION;
        case PresetFormat::Count: PanicIfReached();
    }
}

Optional<usize> PresetFolder::MatchFullPresetPath(String p) const {
    if (!path::IsWithinDirectory(p, scan_folder)) return k_nullopt;

    PathArena scratch_arena {PageAllocator::Instance()};

    DynamicArray<char> path {scan_folder, scratch_arena};
    path::JoinAppend(path, folder);
    auto const path_len = path.size;

    for (auto const [i, preset] : Enumerate(presets)) {
        path::JoinAppend(path, preset.name);
        dyn::AppendSpan(path, ExtensionForPreset(preset));

        if (path == p) return i;

        dyn::Resize(path, path_len);
    }

    return k_nullopt;
}

String PresetFolder::FullPathForPreset(PresetFolder::Preset const& preset, Allocator& a) const {
    auto path = path::Join(a, Array {scan_folder, folder, preset.name});
    path = fmt::JoinAppendResizeAllocation(a, path, Array {ExtensionForPreset(preset)});
    return path;
}

static Span<FolderNode> CloneFolderNodes(Span<FolderNode> folders, ArenaAllocator& arena) {
    auto result = arena.AllocateExactSizeUninitialised<FolderNode>(folders.size);
    auto listings = arena.AllocateExactSizeUninitialised<PresetFolderListing>(folders.size);

    auto const old_pointer_to_new_pointer = [&](FolderNode const* old_node) -> FolderNode* {
        if (!old_node) return nullptr;
        return result.data + (old_node - folders.data);
    };

    for (usize i = 0; i < folders.size; ++i) {
        result[i] = folders[i];
        result[i].parent = old_pointer_to_new_pointer(folders[i].parent);
        result[i].first_child = old_pointer_to_new_pointer(folders[i].first_child);
        result[i].next = old_pointer_to_new_pointer(folders[i].next);

        ASSERT(folders[i].user_data.As<PresetFolderListing>());
        auto old_listing = folders[i].user_data.As<PresetFolderListing>();
        PLACEMENT_NEW(&listings[i])
        PresetFolderListing {
            .folder = nullptr, // The folder points to the old arena, so we can't copy it.
            .fallback_preset_bank_info = old_listing->fallback_preset_bank_info, // Static data.
            .node = result[i],
        };
        result[i].user_data = TypeErasedUserData::Create(&listings[i]);
    }

    return result;
}

// Reader thread
void StartScanningIfNeeded(PresetServer& server) {
    server.enable_scanning.Store(true, StoreMemoryOrder::Relaxed);
}

static u64 OldestVersion(Span<u64> versions) {
    auto oldest = versions[0];
    for (auto const v : versions)
        oldest = Min(oldest, v);
    return oldest;
}

// Reader thread
static void BeginReaderUsingVersion(MutexProtected<DynamicArray<u64>>& active_versions,
                                    Atomic<u64>& oldest_version_in_use,
                                    u64 version) {
    active_versions.Use([&](auto& array) {
        dyn::Append(array, version);
        oldest_version_in_use.Store(OldestVersion(array), StoreMemoryOrder::Release);
    });
}

// Reader thread
static void EndReaderUsingVersion(MutexProtected<DynamicArray<u64>>& active_versions,
                                  Atomic<u64>& oldest_version_in_use,
                                  u64 version) {
    active_versions.Use([&](auto& array) {
        dyn::RemoveValueSwapLast(array, version);
        if (array.size == 0)
            oldest_version_in_use.Store(PresetServer::k_no_version, StoreMemoryOrder::Release);
        else
            oldest_version_in_use.Store(OldestVersion(array), StoreMemoryOrder::Release);
    });
}

// Reader thread
BeginReadFoldersResult BeginReadFolders(PresetServer& server, ArenaAllocator& arena) {
    // Trigger the server to start the scanning process if its not already doing so.
    StartScanningIfNeeded(server);

    // We tell the server that we're reading the current version so that it knows not to delete any folders
    // that we might be using.
    auto const current_version = server.published_version.Load(LoadMemoryOrder::Acquire);
    BeginReaderUsingVersion(server.active_reader_versions, server.oldest_version_in_use, current_version);

    // We take a snapshot of the folders list so that the server can continue to modify it while we're
    // reading and we don't have to do locking or reference counting. We only copy pointers.
    server.mutex.Lock();
    DEFER { server.mutex.Unlock(); };

    ASSERT_EQ(server.folder_node_order_indices.size, server.folders.size);

    auto folders_nodes = CloneFolderNodes(server.folder_nodes, arena);
    auto preset_folders =
        arena.AllocateExactSizeUninitialised<PresetFolderListing const*>(server.folders.size);
    for (auto const i : Range(server.folder_node_order_indices.size)) {
        auto& node = folders_nodes[server.folder_node_order_indices[i]];
        auto node_listing = node.user_data.As<PresetFolderListing>();
        node_listing->folder = server.folders[i];
        preset_folders[i] = node_listing;
    }

    auto preset_banks =
        arena.AllocateExactSizeUninitialised<FolderNode const*>(server.folder_node_preset_bank_indices.size);
    for (auto const i : Range(server.folder_node_preset_bank_indices.size))
        preset_banks[i] = &folders_nodes[server.folder_node_preset_bank_indices[i]];

    return {
        .snapshot =
            {
                .folders = preset_folders,
                .preset_banks = preset_banks,
                .used_tags = {server.used_tags.table.Clone(arena, CloneType::Deep)},
                .used_libraries = {server.used_libraries.table.Clone(arena, CloneType::Deep)},
                .authors = {server.authors.table.Clone(arena, CloneType::Deep)},
                .has_preset_type = server.has_preset_type,
            },
        .handle = (PresetServerReadHandle)current_version,
    };
}

void EndReadFolders(PresetServer& server, PresetServerReadHandle handle) {
    EndReaderUsingVersion(server.active_reader_versions, server.oldest_version_in_use, (u64)handle);
}

static bool FolderIsSafeForDeletion(PresetFolder const& folder, u64 current_version, u64 in_use_version) {
    if (!folder.delete_after_version) return false;

    // If the folder was removed in a previous version, we would like to delete it if we can.
    if (*folder.delete_after_version < current_version) {

        // If there was no reader at the time we checked we can delete it because if a new
        // reader were to have started, it would have seen the current version and used that.
        if (in_use_version == PresetServer::k_no_version) return true;

        // If there were readers at the time we checked, we just need to make sure that the readers
        // are using a version after the folder was removed.
        if (in_use_version > *folder.delete_after_version) return true;
    }

    return false;
}

static void DeleteUnusedFolders(PresetServer& server) {
    ASSERT(CurrentThreadId() == server.server_thread_id);

    auto const current_version = server.published_version.Load(LoadMemoryOrder::Relaxed);
    auto const in_use_version = server.oldest_version_in_use.Load(LoadMemoryOrder::Acquire);

    server.folder_pool.RemoveIf([&](PresetFolder const& folder) {
        if (FolderIsSafeForDeletion(folder, current_version, in_use_version)) {
            LogDebug(
                ModuleName::PresetServer,
                "Deleting folder: {}, current_version: {}, in_use_version: {}, folder_deleted_version: {}",
                folder.folder,
                current_version,
                in_use_version,
                *folder.delete_after_version);
            return true;
        }
        return false;
    });
}

static sample_lib::LibraryIdRef FindOrCloneLibraryIdRef(PresetFolder& folder,
                                                        sample_lib::LibraryIdRef const& lib_id) {
    // If we are the first to use this library, we need to clone it into the folder's arena.
    auto found_result = folder.used_libraries.FindOrInsertGrowIfNeeded(folder.arena, lib_id);
    if (found_result.inserted) found_result.element.key = lib_id.Clone(folder.arena);
    return found_result.element.key;
}

static String FindOrCloneTag(PresetFolder& folder, String tag) {
    // If we are the first to use this tag, we need to clone it into the folder's arena.
    auto found_result = folder.used_tags.FindOrInsertGrowIfNeeded(folder.arena, tag);
    if (found_result.inserted) found_result.element.key = folder.arena.Clone(tag);
    return found_result.element.key;
}

static void AddPresetToFolder(PresetFolder& folder,
                              dir_iterator::Entry const& entry,
                              StateSnapshot const& state,
                              u64 file_hash,
                              PresetFormat file_format) {
    auto presets = DynamicArray<PresetFolder::Preset>::FromOwnedSpan(folder.presets,
                                                                     folder.preset_array_capacity,
                                                                     folder.arena);

    auto used_libraries = OrderedSet<sample_lib::LibraryIdRef>::Create(folder.arena, k_num_layers + 1);

    for (auto const& inst_id : state.inst_ids) {
        if (auto const& sampled_inst = inst_id.TryGet<sample_lib::InstrumentId>()) {
            auto const lib_id =
                FindOrCloneLibraryIdRef(folder, (sample_lib::LibraryIdRef)sampled_inst->library);
            used_libraries.InsertWithoutGrowing(lib_id);
        }
    }

    if (state.ir_id) {
        auto const lib_id = FindOrCloneLibraryIdRef(folder, (sample_lib::LibraryIdRef)state.ir_id->library);
        if (lib_id != sample_lib::k_builtin_library_id) used_libraries.InsertWithoutGrowing(lib_id);
    }

    dyn::Append(presets,
                PresetFolder::Preset {
                    .name = folder.arena.Clone(path::FilenameWithoutExtension(entry.subpath)),
                    .metadata {
                        .tags = ({
                            auto tags = Set<String>::Create(folder.arena, state.metadata.tags.size);
                            for (auto const tag : state.metadata.tags)
                                tags.InsertWithoutGrowing(FindOrCloneTag(folder, tag));
                            tags;
                        }),
                        .author = folder.arena.Clone(state.metadata.author),
                        .description = folder.arena.Clone(state.metadata.description),
                    },
                    .used_libraries = used_libraries,
                    .file_hash = file_hash,
                    .full_path_hash = HashMultiple(Array {folder.scan_folder, folder.folder, entry.subpath}),
                    .file_extension = file_format == PresetFormat::Mirage
                                          ? (String)folder.arena.Clone(path::Extension(entry.subpath))
                                          : ""_s,
                    .file_format = file_format,
                });

    auto const [items, cap] = presets.ToOwnedSpanUnchangedCapacity();
    folder.presets = items;
    folder.preset_array_capacity = cap;
}

constexpr usize k_max_nested_folders = 10;

// There's a reasonable amount of aggregating work that needs to be done. We do this separately so that under
// the mutex all we need is to copy some contiguous data.
struct FoldersAggregateInfo {
    struct FolderNodeAllocator : Allocator {
        Span<u8> DoCommand(AllocatorCommandUnion const& command) override {
            CheckAllocatorCommandIsValid(command);

            switch (command.tag) {
                case AllocatorCommand::Allocate: {
                    auto const& cmd = command.Get<AllocateCommand>();
                    ASSERT(cmd.size == sizeof(FolderNode));
                    if (used == folders.size) return {};
                    return Span<u8> {(u8*)&folders[used++], sizeof(FolderNode)};
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
        Span<FolderNode> folders;
        usize used = 0;
    };

    struct ListingAllocator : Allocator {
        Span<u8> DoCommand(AllocatorCommandUnion const& command) override {
            CheckAllocatorCommandIsValid(command);

            switch (command.tag) {
                case AllocatorCommand::Allocate: {
                    auto const& cmd = command.Get<AllocateCommand>();
                    ASSERT(cmd.size == sizeof(PresetFolderListing));
                    if (used == folders.size) return {};
                    return Span<u8> {(u8*)&folders[used++], sizeof(PresetFolderListing)};
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
        Span<PresetFolderListing> folders;
        usize used = 0;
    };

    FoldersAggregateInfo(ArenaAllocator& arena, usize folders_used)
        : used_tags {arena}
        , used_libraries {arena}
        , authors {arena}
        , scan_folder_nodes {arena}
        , folder_node_indices {arena}
        , folder_node_preset_bank_indices {arena} {
        // We must know the full size up front so no reallocation happens.
        folder_node_allocator.folders = arena.AllocateExactSizeUninitialised<FolderNode>(folders_used);
        listing_allocator.folders = arena.AllocateExactSizeUninitialised<PresetFolderListing>(folders_used);
    }

    // IMPORTANT: you must call this in sorted folder order so that the nodes are created in the same order as
    // the folders.
    void AddFolder(PresetFolder const& folder) {
        {
            auto found = scan_folder_nodes.FindOrInsert(folder.scan_folder, nullptr);
            if (found.inserted) {
                found.element.data = folder_node_allocator.New<FolderNode>(FolderNode {
                    .name = folder.scan_folder,
                    .display_name = folder.abbreviated_scan_folder,
                });
            }
            auto& root = found.element.data;

            auto node = FindOrInsertFolderNode(root,
                                               folder.folder,
                                               k_max_nested_folders,
                                               {
                                                   .node_allocator = folder_node_allocator,
                                               });
            // It's possible that the folder is too nested, in which case we fallback to putting it inside the
            // root.
            if (!node) node = root;

            {
                auto listing = listing_allocator.NewUninitialised<PresetFolderListing>();
                PLACEMENT_NEW(listing)
                PresetFolderListing {
                    .folder = &folder,
                    .node = *node,
                };
                node->user_data = TypeErasedUserData::Create(listing);
            }

            for (auto n = node->parent; n; n = n->parent) {
                if (!n->user_data) {
                    auto listing = listing_allocator.NewUninitialised<PresetFolderListing>();
                    PLACEMENT_NEW(listing)
                    PresetFolderListing {
                        .folder = nullptr,
                        .fallback_preset_bank_info = nullptr,
                        .node = *n,
                    };
                    n->user_data = TypeErasedUserData::Create(listing);
                }
            }

            auto const index = CheckedCast<usize>(node - folder_node_allocator.folders.data);
            ASSERT(index < folder_node_allocator.used);
            dyn::Append(folder_node_indices, index);
        }

        for (auto const& preset : folder.presets)
            AddPreset(preset);
    }

    void AddPreset(PresetFolder::Preset const& preset) {
        // Tags and libraries point to memory within each folder, so they share the same versioning as the
        // folders.

        for (auto const [tag, tag_hash] : preset.metadata.tags)
            used_tags.Insert(tag, tag_hash);

        for (auto const [lib_id, lib_id_hash] : preset.used_libraries)
            used_libraries.Insert(lib_id, lib_id_hash);

        if (preset.metadata.author.size) authors.Insert(preset.metadata.author);

        has_preset_type.Set(ToInt(preset.file_format));
    }

    // Floe didn't use to have preset banks. To smooth the transition for users, we detect all the preset
    // banks that existed before the Floe update and fill in the metadata for them.
    static PresetBank const* KnownPresetBank(FolderNode const* node) {
        auto const hash = FolderContentsHash(node);
        switch (hash) {
            case 17797709789825583399ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.AbstractEnergy.Mirage"),
                    .subtitle = "Factory presets for Abstract Energy (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 17678716117694255396ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.Wraith.Mirage"),
                    .subtitle = "Factory presets for Wraith (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 4522276088530940864ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.ArcticStrings.Mirage"),
                    .subtitle = "Factory presets for Arctic Strings (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 17067796986821586660ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.CinematicAtmosphereToolkit.Mirage"),
                    .subtitle = "Factory presets for Cinematic Atmosphere Toolkit (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 1113295807784802420ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.DeepConjuring.Mirage"),
                    .subtitle = "Factory presets for Deep Conjuring (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 14194170911065684425ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.FeedbackLoops.Mirage"),
                    .subtitle = "Factory presets for Feedback Loops (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 10657727448210940357ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.IsolatedSignals.Mirage"),
                    .subtitle = "Factory presets for Isolated Signals (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 5014338070805093321ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.LostReveries.Mirage"),
                    .subtitle = "Factory presets for Lost Reveries (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 13346224102117216586ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.MusicBoxSuiteFree.Mirage"),
                    .subtitle = "Factory presets for Music Box Suite Free (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 10450269504034189798ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.MusicBoxSuite.Mirage"),
                    .subtitle = "Factory presets for Music Box Suite (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 12314029761590835424ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.Phoenix.Mirage"),
                    .subtitle = "Factory presets for Phoenix (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 1979436314251425427ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.ScenicVibrations.Mirage"),
                    .subtitle = "Factory presets for Scenic Vibrations (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 5617954846491642181ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.Slow.Mirage"),
                    .subtitle = "Factory presets for Slow (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 4523343789936516079ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.SqueakyGate.Mirage"),
                    .subtitle = "Factory presets for Squeaky Gate (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 15901798520857468560ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.Dreamstates.Mirage"),
                    .subtitle = "Factory presets for Dreamstates (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 9622774010603600999ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.Paranormal.Mirage"),
                    .subtitle = "Factory presets for Paranormal (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 2299133524087718373ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.ScareTactics.Mirage"),
                    .subtitle = "Factory presets for Scare Tactics (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 3960283021267125531ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.SignalInterference.Mirage"),
                    .subtitle = "Factory presets for Signal Interference (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 2834298600494183622ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.Terracotta.Mirage"),
                    .subtitle = "Factory presets for Terracotta (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 7286607532220839066ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.WraithDemo.Mirage"),
                    .subtitle = "Factory presets for Wraith Demo (Mirage presets)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 3719497291850758672ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.Dulcitone"),
                    .subtitle = "Factory presets for Dulcitone"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 6899967127661925909ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.MusicBoxSuite"),
                    .subtitle = "Factory presets for Music Box Suite (Floe edition)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 9336774792391258852ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.MusicBoxSuiteFree"),
                    .subtitle = "Factory presets for Music Box Suite Free (Floe edition)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
            case 11142846282151865892ull: {
                static constexpr PresetBank k_metadata {
                    .id = HashComptime("com.FrozenPlain.MusicBoxSuiteFree.Beta"),
                    .subtitle = "Factory presets for Music Box Suite Free (Floe beta edition)"_s,
                    .minor_version = 1,
                };
                return &k_metadata;
            }
        }
        return nullptr;
    }

    void Finalise(ArenaAllocator& scratch_arena) {
        for (auto [_, root, _] : scan_folder_nodes) {
            // Add preset bank info for banks that we know existed before Floe had metadata files.
            ForEachNode(root, [&](FolderNode* node) {
                auto listing = node->user_data.As<PresetFolderListing>();
                ASSERT(listing);
                if (auto const bank = KnownPresetBank(node)) listing->fallback_preset_bank_info = bank;
            });

            DynamicArray<FolderNode*> miscellaneous_banks {scratch_arena};

            // Add orphaned PresetFolder nodes to new "Miscellaneous" banks.
            ForEachNode(root, [&](FolderNode* node) {
                if (!node->user_data.As<PresetFolderListing>()->folder) return;

                for (auto n = node; n; n = n->parent)
                    if (PresetBankInfoAtNode(*n)) return;

                // The node is not part of any bank. We should see if we should create metadata for it
                // by again walking up the tree, this time looking for the topmost parent that has a
                // PresetFolder; we will put the metadata there.
                DynamicArrayBounded<FolderNode*, k_max_nested_folders> lineage {};
                for (auto n = node; n; n = n->parent)
                    dyn::Append(lineage, n);

                // Walk back down the lineage looking for a PresetFolder, we use the topmost one we find.
                for (usize i = lineage.size; i-- > 0;) {
                    if (auto listing = lineage[i]->user_data.As<PresetFolderListing>(); listing->folder) {
                        dyn::AppendIfNotAlreadyThere(miscellaneous_banks, lineage[i]);
                        break;
                    }
                }
            });

            if (miscellaneous_banks.size) {
                static constexpr PresetBank k_miscellaneous_info {
                    .id = HashComptime("misc"),
                    .subtitle = ""_s,
                };
                auto const node = FirstCommonAncestor(miscellaneous_banks, scratch_arena);
                auto listing = node->user_data.As<PresetFolderListing>();
                listing->fallback_preset_bank_info = &k_miscellaneous_info;
            }

            ForEachNode(root, [&](FolderNode* node) {
                if (auto m = PresetBankInfoAtNode(*node)) {
                    // Since we consider nesting of folders to be unimportant when identifying legacy banks,
                    // we can end up with the subfolder having the same metadata as the parent. We don't want
                    // to list both as separate banks so we walk up the tree to find the topmost folder with
                    // the same metadata. This was quite common with the old Mirage factory presets which had
                    // folders like LibraryName/Factory.
                    for (; node->parent && PresetBankInfoAtNode(*node->parent) == m; node = node->parent)
                        ;
                    auto const index = CheckedCast<usize>(node - folder_node_allocator.folders.data);
                    dyn::AppendIfNotAlreadyThere(folder_node_preset_bank_indices, index);
                }
            });
        }
    }

    // Call under the mutex.
    void CopyToServer(PresetServer& server) const {
        server.used_tags.Assign(used_tags);
        server.used_libraries.Assign(used_libraries);
        server.authors.Assign(authors);

        server.has_preset_type = has_preset_type;

        server.folder_node_arena.ResetCursorAndConsolidateRegions();
        server.folder_nodes =
            CloneFolderNodes(Span {folder_node_allocator.folders.data, folder_node_allocator.used},
                             server.folder_node_arena);
        server.folder_node_order_indices = server.folder_node_arena.Clone(folder_node_indices);
        server.folder_node_preset_bank_indices =
            server.folder_node_arena.Clone(folder_node_preset_bank_indices);
    }

    DynamicSet<String> used_tags;
    DynamicSet<sample_lib::LibraryIdRef> used_libraries;
    DynamicSet<String> authors;
    FolderNodeAllocator folder_node_allocator;
    ListingAllocator listing_allocator;
    DynamicOrderedHashTable<String, FolderNode*> scan_folder_nodes;
    DynamicArray<usize> folder_node_indices;
    DynamicArray<usize> folder_node_preset_bank_indices;
    Bitset<ToInt(PresetFormat::Count)> has_preset_type {};
};

static void
AppendFolderAndPublish(PresetServer& server, PresetFolder* new_preset_folder, ArenaAllocator& scratch_arena) {
    ASSERT(CurrentThreadId() == server.server_thread_id);

    auto const insert_point = BinarySearchForSlotToInsert(server.folders, [&](PresetFolder const* folder) {
        if (folder->scan_folder == new_preset_folder->scan_folder) {
            if (folder->folder < new_preset_folder->folder) return -1;
            return 1;
        } else {
            if (folder->scan_folder < new_preset_folder->scan_folder) return -1;
            return 1;
        }
    });

    FoldersAggregateInfo info {scratch_arena,
                               ((server.folders.size + 1) * k_max_nested_folders) + server.scan_folders.size};
    for (auto const folder_index : Range(server.folders.size)) {
        // We call AddFolder at the correct ordered index.
        if (folder_index == insert_point) info.AddFolder(*new_preset_folder);
        info.AddFolder(*server.folders[folder_index]);
    }
    if (insert_point == server.folders.size) info.AddFolder(*new_preset_folder);
    info.Finalise(scratch_arena);

    server.mutex.Lock();
    DEFER { server.mutex.Unlock(); };

    dyn::MakeUninitialisedGap(server.folders, insert_point, 1);
    server.folders[insert_point] = new_preset_folder;

    info.CopyToServer(server);
    ASSERT_EQ(server.folders.size, server.folder_node_order_indices.size);

    server.published_version.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
}

static void RemoveFolderAndPublish(PresetServer& server, usize index, ArenaAllocator& scratch_arena) {
    ASSERT(CurrentThreadId() == server.server_thread_id);

    auto& folder = *server.folders[index];
    folder.delete_after_version = server.published_version.Load(LoadMemoryOrder::Relaxed);
    if constexpr (k_skip_duplicate_presets)
        for (auto const& preset : folder.presets)
            server.preset_file_hashes.Delete(preset.file_hash);

    FoldersAggregateInfo info {scratch_arena,
                               ((server.folders.size + 1) * k_max_nested_folders) + server.scan_folders.size};
    for (auto const& existing_folder : server.folders) {
        if (existing_folder == &folder) continue;
        info.AddFolder(*existing_folder);
    }
    info.Finalise(scratch_arena);

    server.mutex.Lock();
    DEFER { server.mutex.Unlock(); };

    dyn::Remove(server.folders, index);

    info.CopyToServer(server);

    server.published_version.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
}

static PresetFolder*
CreatePresetFolder(PresetServer& server, String scan_folder, String subfolder_of_scan_folder) {
    auto preset_folder = server.folder_pool.PrependUninitialised(server.arena);
    PLACEMENT_NEW(preset_folder) PresetFolder();
    preset_folder->scan_folder = preset_folder->arena.Clone(scan_folder);
    preset_folder->abbreviated_scan_folder = path::MakeDisplayPath(preset_folder->scan_folder,
                                                                   {
                                                                       .stylize_dir_separators = true,
                                                                       .compact_middle_sections = true,
                                                                   },
                                                                   preset_folder->arena);
    preset_folder->folder = ({
        auto f = preset_folder->arena.Clone(subfolder_of_scan_folder);
        if constexpr (IS_WINDOWS) Replace(f, '\\', '/');
        f;
    });
    return preset_folder;
}

static ErrorCodeOr<PresetBank>
ReadPresetBankFile(String path, ArenaAllocator& arena, ArenaAllocator& scratch_arena) {
    auto const file_data = TRY(ReadEntireFile(path, scratch_arena));
    return ParsePresetBankFile(file_data, arena);
}

static ErrorCodeOr<void> ScanFolder(PresetServer& server,
                                    String subfolder_of_scan_folder,
                                    ArenaAllocator& scratch_arena,
                                    PresetServer::ScanFolder& scan_folder,
                                    u32 depth = 0) {
    ASSERT(CurrentThreadId() == server.server_thread_id);

    if (depth > k_max_nested_folders) {
        LogError(ModuleName::PresetServer, "Too many nested folders in scan folder");
        return ErrorCode {FilesystemError::FolderContainsTooManyFiles};
    }

    auto const absolute_folder =
        (String)path::Join(scratch_arena, Array {(String)scan_folder.path, subfolder_of_scan_folder});

    auto const entries = TRY(FindEntriesInFolder(scratch_arena,
                                                 absolute_folder,
                                                 {
                                                     .options =
                                                         {
                                                             .wildcard = "*",
                                                             .get_file_size = false,
                                                             .skip_dot_files = true,
                                                         },
                                                     .recursive = false,
                                                     .only_file_type = k_nullopt,
                                                 }));

    PresetFolder* preset_folder {};

    for (auto const& entry : entries) {
        if (entry.type != FileType::File) continue;

        if (path::Equal(entry.subpath, k_metadata_filename)) {
            if (!preset_folder)
                preset_folder = CreatePresetFolder(server, scan_folder.path, subfolder_of_scan_folder);
            preset_folder->preset_bank_info =
                TRY_OR(ReadPresetBankFile(path::Join(scratch_arena, Array {absolute_folder, entry.subpath}),
                                          preset_folder->arena,
                                          scratch_arena),
                       continue);
            continue;
        }

        auto const preset_format = PresetFormatFromPath(entry.subpath);
        if (!preset_format) continue;

        if constexpr (IS_WINDOWS) Replace(entry.subpath, '\\', '/');

        auto const file_data = TRY_OR(
            ReadEntireFile(path::Join(scratch_arena, Array {absolute_folder, entry.subpath}), scratch_arena),
            continue);
        DEFER {
            if (file_data.size) scratch_arena.Free(file_data.ToByteSpan());
        };

        auto const file_hash = XXH3_64bits(file_data.data, file_data.size) + Hash((String)entry.subpath);

        if constexpr (k_skip_duplicate_presets) {
            if (server.preset_file_hashes.Contains(file_hash)) continue;
            server.preset_file_hashes.Insert(file_hash);
        }

        auto reader = Reader::FromMemory(file_data);
        auto const snapshot = TRY_OR(LoadPresetFile(*preset_format, reader, scratch_arena, true), continue);

        if (!preset_folder)
            preset_folder = CreatePresetFolder(server, scan_folder.path, subfolder_of_scan_folder);

        AddPresetToFolder(*preset_folder, entry, snapshot, file_hash, *preset_format);
    }

    if (preset_folder) {
        Sort(preset_folder->presets,
             [](PresetFolder::Preset const& a, PresetFolder::Preset const& b) { return a.name < b.name; });

        // After sorting, we can compute the overall hash.
        preset_folder->all_presets_hash = HashInitFnv1a();
        for (auto const& preset : preset_folder->presets)
            HashUpdateFnv1a(preset_folder->all_presets_hash, preset.file_hash);

        AppendFolderAndPublish(server, preset_folder, scratch_arena);
    }

    for (auto const& entry : entries) {
        if (entry.type == FileType::Directory) {
            TRY(ScanFolder(server,
                           path::Join(scratch_arena, Array {subfolder_of_scan_folder, entry.subpath}),
                           scratch_arena,
                           scan_folder,
                           depth + 1));
        }
    }

    return k_success;
}

static ErrorCodeOr<void>
ScanFolder(PresetServer& server, ArenaAllocator& scratch_arena, PresetServer::ScanFolder& scan_folder) {
    ASSERT(CurrentThreadId() == server.server_thread_id);
    if (scan_folder.scanned) return k_success;
    scan_folder.scanned = true;
    TRY(ScanFolder(server, "", scratch_arena, scan_folder));
    return k_success;
}

static void ServerThread(PresetServer& server) {
    server.server_thread_id = CurrentThreadId();

    auto watcher = ({
        Optional<DirectoryWatcher> w {};
        auto o = CreateDirectoryWatcher(PageAllocator::Instance());
        if (o.HasValue()) w = o.ReleaseValue();
        Move(w);
    });
    DEFER {
        if (PanicOccurred()) return;
        if (watcher) DestoryDirectoryWatcher(*watcher);
    };

    ArenaAllocator scratch_arena {PageAllocator::Instance()};

    while (!server.end_thread.Load(LoadMemoryOrder::Relaxed)) {
        scratch_arena.ResetCursorAndConsolidateRegions();

        server.work_signaller.WaitUntilSignalledOrSpurious(250u);

        if (!server.enable_scanning.Load(LoadMemoryOrder::Relaxed)) continue;

        // Consume scan folder request
        {
            server.scan_folders_request_mutex.Lock();
            DEFER { server.scan_folders_request_mutex.Unlock(); };

            if (server.scan_folders_request) {
                dyn::RemoveValueIfSwapLast(
                    server.scan_folders,
                    [&](PresetServer::ScanFolder const& scan_folder) {
                        // Never remove the always scanned folder.
                        if (scan_folder.always_scanned_folder) return false;

                        // We don't remove the folder if it's in the new set of folders.
                        if (path::Contains(*server.scan_folders_request, scan_folder.path)) return false;

                        // The folder is not in the new set of folders. We should remove the preset
                        // folders that relate to it so they disappear from the listing.
                        for (usize i = 0; i < server.folders.size;) {
                            auto& preset_folder = *server.folders[i];
                            if (preset_folder.scan_folder == scan_folder.path)
                                RemoveFolderAndPublish(server, i, scratch_arena);
                            else
                                ++i;
                        }

                        return true;
                    });

                for (auto const& path : *server.scan_folders_request) {
                    bool already_exists = false;
                    for (auto& f : server.scan_folders) {
                        if (path::Equal(f.path, path)) {
                            already_exists = true;
                            break;
                        }
                    }

                    if (already_exists) continue;

                    auto cloned_path = server.arena.Clone(path);
                    dyn::Append(server.scan_folders,
                                PresetServer::ScanFolder {
                                    .always_scanned_folder = false,
                                    .path = {cloned_path},
                                    .scanned = false,
                                });
                }

                server.scan_folders_request = k_nullopt;
            }

            server.scan_folders_request_arena.FreeAll();
        }

        if (watcher) {
            auto const dirs_to_watch = ({
                DynamicArray<DirectoryToWatch> dirs {scratch_arena};
                for (auto& f : server.scan_folders) {
                    dyn::Append(dirs,
                                {
                                    .path = f.path,
                                    .recursive = true,
                                    .user_data = &f,
                                });
                }
                dirs.ToOwnedSpan();
            });

            // Batch up changes.
            DynamicArray<PresetServer::ScanFolder*> rescan_folders {scratch_arena};

            if (auto const outcome = PollDirectoryChanges(*watcher,
                                                          {
                                                              .dirs_to_watch = dirs_to_watch,
                                                              .retry_failed_directories = false,
                                                              .result_arena = scratch_arena,
                                                              .scratch_arena = scratch_arena,
                                                          });
                outcome.HasError()) {
                // IMPROVE: handle error
                LogDebug(ModuleName::SampleLibraryServer,
                         "Reading directory changes failed: {}",
                         outcome.Error());
            } else {
                auto const dir_changes_span = outcome.Value();
                for (auto const& dir_changes : dir_changes_span) {
                    bool found = false;
                    for (auto& f : server.scan_folders) {
                        if ((void*)&f == dir_changes.linked_dir_to_watch->user_data) {
                            found = true;
                            break;
                        }
                    }
                    ASSERT(found);

                    auto& scan_folder =
                        *(PresetServer::ScanFolder*)dir_changes.linked_dir_to_watch->user_data;

                    if (dir_changes.error) {
                        // IMPROVE: handle this
                        LogDebug(ModuleName::SampleLibraryServer,
                                 "Reading directory changes failed for {}: {}",
                                 scan_folder.path,
                                 dir_changes.error);
                        continue;
                    }

                    for (auto const& subpath_changeset : dir_changes.subpath_changesets) {
                        // Changes to the watched directory itself.
                        if (subpath_changeset.subpath.size == 0) continue;

                        // For now, we ignore the granularity of the changes and just rescan the whole
                        // folder. IMPROVE: handle changes more granularly
                        dyn::AppendIfNotAlreadyThere(rescan_folders, &scan_folder);
                    }
                }
            }

            for (auto scan_folder : rescan_folders) {
                for (usize i = 0; i < server.folders.size;) {
                    auto& preset_folder = *server.folders[i];
                    if (preset_folder.scan_folder == scan_folder->path)
                        RemoveFolderAndPublish(server, i, scratch_arena);
                    else
                        ++i;
                }

                scan_folder->scanned = false; // force a rescan
            }
        }

        for (auto& scan_folder : server.scan_folders) {
            auto const o = ScanFolder(server, scratch_arena, scan_folder);
            auto const error_id = HashMultiple(Array {"preset-server"_s, scan_folder.path});
            if (o.HasError()) {
                if (!scan_folder.always_scanned_folder) {
                    if (auto err = server.error_notifications.BeginWriteError(error_id)) {
                        DEFER { server.error_notifications.EndWriteError(*err); };
                        dyn::AssignFitInCapacity(err->title, "Failed to scan presets folder"_s);
                        dyn::AssignFitInCapacity(err->message, scan_folder.path);
                        err->error_code = o.Error();
                    }
                }
            } else {
                server.error_notifications.RemoveError(error_id);
            }
        }

        // At the end of the tick, check if we can set is_scanning to false.
        {
            server.scan_folders_request_mutex.Lock();
            DEFER { server.scan_folders_request_mutex.Unlock(); };
            auto const any_needs_scan = FindIf(server.scan_folders, [](auto const& f) { return !f.scanned; });
            auto const has_rescan_request = server.scan_folders_request.HasValue();
            if (!any_needs_scan && !has_rescan_request) {
                if (server.is_scanning.Exchange(false, RmwMemoryOrder::AcquireRelease))
                    WakeWaitingThreads(server.is_scanning, NumWaitingThreads::All);
            }
        }

        DeleteUnusedFolders(server);
    }

    ASSERT(server.oldest_version_in_use.Load(LoadMemoryOrder::Relaxed) == PresetServer::k_no_version);
    server.folder_pool.Clear();
}

bool WaitIfFoldersAreScanning(PresetServer& server, Optional<u32> timeout) {
    ASSERT(server.enable_scanning.Load(LoadMemoryOrder::Acquire));

    Stopwatch stopwatch;
    while (true) {
        auto const elapsed = stopwatch.MicrosecondsElapsed();
        if (timeout && *timeout != 0 && elapsed >= *timeout) return false;

        if (server.is_scanning.Load(LoadMemoryOrder::Acquire)) {
            if (timeout && *timeout == 0) return false;
            WaitIfValueIsExpected(server.is_scanning,
                                  true,
                                  timeout ? CheckedCast<u32>(*timeout - elapsed) : k_nullopt);
            continue;
        } else {
            break;
        }
    }

    return true;
}

bool AreFoldersScanning(PresetServer& server) { return !WaitIfFoldersAreScanning(server, 0u); }

void SetExtraScanFolders(PresetServer& server, Span<String const> folders) {
    {
        server.scan_folders_request_mutex.Lock();
        DEFER { server.scan_folders_request_mutex.Unlock(); };

        server.scan_folders_request = server.scan_folders_request_arena.Clone(folders, CloneType::Deep);
        server.is_scanning.Store(true, StoreMemoryOrder::Release);
    }
    server.work_signaller.Signal();
}

void InitPresetServer(PresetServer& server, String always_scanned_folder) {
    dyn::Append(server.scan_folders,
                {
                    .always_scanned_folder = true,
                    // We can use the server arena directly because the server thread isn't running yet.
                    .path = {server.arena.Clone(always_scanned_folder)},
                });
    server.is_scanning.Store(true, StoreMemoryOrder::Release);

    server.thread.Start([&server]() { ServerThread(server); }, "presets");
}

void ShutdownPresetServer(PresetServer& server) {
    server.end_thread.Store(true, StoreMemoryOrder::Release);
    server.work_signaller.Signal();
    server.thread.Join();
    if (server.is_scanning.Exchange(false, RmwMemoryOrder::AcquireRelease))
        WakeWaitingThreads(server.is_scanning, NumWaitingThreads::All);
}
