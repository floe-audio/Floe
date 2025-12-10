// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"
#include "utils/error_notifications.hpp"
#include "utils/thread_extra/atomic_ref_list.hpp"
#include "utils/thread_extra/thread_extra.hpp"
#include "utils/thread_extra/thread_pool.hpp"

#include "common_infrastructure/audio_data.hpp"
#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"
#include "common_infrastructure/state/instrument.hpp"

// Sample library server
// A centralised manager for sample libraries that multiple plugins/systems can use at once.
//
// We use the term 'resource' for loadable things from a library, such as an Instrument, IR, audio data,
// image, etc.
//
// The server provides convenient access and lookup of all available sample libraries information. However,
// for specific resources (instruments and audio data), a request-response API is used. The request triggers
// an asynchronous load of the resource, and the response containing the resource is provided via a
// 'communication channel'. Resources are kept alive using reference counting.
//
// - Manages loading, unloading and storage of sample libraries (including instruments, irs, etc)
// - Provides an asynchronous request-response API (we tend to call response 'result')
// - Very quick for resources that are already loaded
// - Scans library folders and watches for file changes in them
// - Has its own dedicated thread but also makes use of a thread pool for loading big files
// - Instantly aborts any pending loads that are no longer needed
// - No duplication of resources in memory
// - Provides progress/status metrics for other threads to read
//

namespace sample_lib_server {

// Request
// ==========================================================================================================
using RequestId = u64;

enum class LoadRequestType { Instrument, Ir };

struct LoadRequestInstrumentIdWithLayer {
    sample_lib::InstrumentId id;
    u32 layer_index;
};

using LoadRequest = TaggedUnion<LoadRequestType,
                                TypeAndTag<LoadRequestInstrumentIdWithLayer, LoadRequestType::Instrument>,
                                TypeAndTag<sample_lib::IrId, LoadRequestType::Ir>>;

// Result
// ==========================================================================================================
enum class RefCountChange { Retain, Release };

// A reference counted handle to a resource. This object is just like a pointer really. The resource will stay
// valid for as long as you are between Retain/Release pairs. The server gives you one of these with a Retain
// already called for you - you must Release it if you don't want to keep it.
//
// NOTE: this doesn't do reference counting automatically using C++ constructors/operators. You must use
// Retain() and Release() manually. We do this because things can get messy and inefficient doing ref-counting
// automatically in copy/move constructors and assignment operators. Instead, you will get assertion failures
// if you have mismatched retain/release.
template <typename Type>
struct ResourcePointer {
    ResourcePointer() = default;
    ResourcePointer(Type& t, Atomic<u32>& r) : resource(&t), ref_count(&r) {}

    void Retain() const {
        if (ref_count) {
            auto const prev = ref_count->FetchAdd(1, RmwMemoryOrder::Relaxed);

            // The special case where the ref count is 0 is only meant to be handled internally where we can
            // be more certain of lifetimes. For general use, a 0 ref count suggests a bug, because if its 0,
            // `data` could be deleted or about to be deleted.
            ASSERT(prev != 0);
        }
    }

    void Release() {
        if (ref_count) {
            auto const curr = ref_count->SubFetch(1, RmwMemoryOrder::AcquireRelease);

            ASSERT(curr != ~(u32)0);

            if (curr == 0) {
                // For easier debugging, we null out the pointers here because the object could be freed as
                // soon as the ref count is 0.
                resource = nullptr;
                ref_count = nullptr;
            }
        }
    }

    void ChangeRefCount(RefCountChange t) {
        switch (t) {
            case RefCountChange::Retain: Retain(); break;
            case RefCountChange::Release: Release(); break;
        }
    }

    constexpr explicit operator bool() const { return resource != nullptr; }
    constexpr Type const* operator->() const {
        ASSERT(resource);
        return resource;
    }
    constexpr Type const& operator*() const {
        ASSERT(resource);
        return *resource;
    }

    Type const* resource {};
    Atomic<u32>* ref_count {};
};

using Resource =
    TaggedUnion<LoadRequestType,
                TypeAndTag<ResourcePointer<sample_lib::LoadedInstrument>, LoadRequestType::Instrument>,
                TypeAndTag<ResourcePointer<sample_lib::LoadedIr>, LoadRequestType::Ir>>;

struct LoadResult {
    enum class ResultType { Success, Error, Cancelled };
    using Result = TaggedUnion<ResultType,
                               TypeAndTag<Resource, ResultType::Success>,
                               TypeAndTag<ErrorCode, ResultType::Error>>;

    void ChangeRefCount(RefCountChange t);
    void ChangeRefCount(RefCountChange t) const;
    void Retain() const { ChangeRefCount(RefCountChange::Retain); }
    void Release() { ChangeRefCount(RefCountChange::Release); }

    template <typename T>
    T const* TryExtract() const {
        if (result.tag == ResultType::Success) return result.Get<Resource>().TryGet<T>();
        return nullptr;
    }

    RequestId id;
    Result result;
};

namespace detail {
struct ListedInstrument;
}

// Asynchronous communication channel
// ==========================================================================================================
struct AsyncCommsChannel {
    using ResultAddedCallback = TrivialFixedSizeFunction<8, void()>;
    using LibraryChangedCallback = TrivialFixedSizeFunction<8, void(sample_lib::LibraryIdRef)>;

    // -1 if not valid, else 0 to 100
    Array<Atomic<s32>, k_num_layers> instrument_loading_percents {};

    // Threadsafe. These are the retained results. You should pop these and then Release() when you're done
    // with them.
    ThreadsafeQueue<LoadResult> results {Malloc::Instance()};

    // private
    ThreadsafeErrorNotifications& error_notifications;
    Array<detail::ListedInstrument*, k_num_layers> desired_inst {};
    ResultAddedCallback result_added_callback;
    LibraryChangedCallback library_changed_callback;
    Atomic<bool> used {};
    AsyncCommsChannel* next {};
};

// Internal details
// ==========================================================================================================
namespace detail {

enum class FileLoadingState : u32 {
    PendingLoad,
    PendingCancel,
    Loading,
    CompletedSucessfully,
    CompletedWithError,
    CompletedCancelled,
    Count,
};

struct ListedAudioData {
    ~ListedAudioData();

    sample_lib::LibraryPath path;
    bool file_modified {};
    AudioData audio_data;
    Atomic<u32> ref_count {};
    Atomic<u32>& library_ref_count;
    Atomic<FileLoadingState> state {FileLoadingState::PendingLoad};
    Optional<ErrorCode> error {};
};

struct ListedInstrument {
    ~ListedInstrument();

    u32 debug_id;
    sample_lib::LoadedInstrument inst;
    Atomic<u32> ref_count {};
    Span<ListedAudioData*> audio_data_set {};
    ArenaAllocator arena {PageAllocator::Instance()};
};

struct ListedImpulseResponse {
    ~ListedImpulseResponse();

    sample_lib::LoadedIr ir;
    ListedAudioData* audio_data {};
    Atomic<u32> ref_count {};
};

struct ListedLibrary {
    ~ListedLibrary() { ASSERT(instruments.Empty(), "missing instrument dereference"); }

    ArenaAllocator arena;
    sample_lib::Library* lib {};
    TimePoint scan_timepoint {};

    ArenaList<ListedAudioData> audio_datas {};
    ArenaList<ListedInstrument> instruments {};
    ArenaList<ListedImpulseResponse> irs {};
};

using LibrariesAtomicList = AtomicRefList<ListedLibrary>;

struct ScanFolder {
    enum class Source { AlwaysScannedFolder, ExtraFolder };
    enum State : u32 { Inactive, NotScanned, RescanRequested, Scanning, ScannedSuccessfully, ScanFailed };
    DynamicArray<char> path {Malloc::Instance()};
    Source source {};
    Atomic<u32> state {Inactive}; // Only modified under mutex, but atomic for futex waits.
};

// The first folder is the AlwaysScannedFolder.
using ScanFolders = Array<MutexProtected<ScanFolder>, k_max_extra_scan_folders + 1>;

struct QueuedRequest {
    RequestId id;
    LoadRequest request;
    AsyncCommsChannel& async_comms_channel;
};

} // namespace detail

// Public API
// ==========================================================================================================
// You may directly access some fields of the server, but most things are done through functions.

struct Server {
    Server(ThreadPool& pool,
           String always_scanned_folder,
           ThreadsafeErrorNotifications& connection_independent_error_notif);
    ~Server();

    // public
    Atomic<bool> disable_file_watching {false}; // set to true/false as needed
    Atomic<u64> total_bytes_used_by_samples {}; // filled by the server thread
    Atomic<u32> num_insts_loaded {};
    Atomic<u32> num_samples_loaded {};

    // private
    detail::ScanFolders scan_folders;
    detail::LibrariesAtomicList libraries;
    Mutex libraries_by_id_mutex;
    DynamicHashTable<sample_lib::LibraryIdRef, detail::LibrariesAtomicList::Node*> libraries_by_id {
        Malloc::Instance()};
    // Connection-independent errors. If we have access to a channel, we post to the channel's
    // error_notifications instead of this.
    ThreadsafeErrorNotifications& error_notifications;
    Atomic<u32> libraries_loading {false};
    ThreadPool& thread_pool;
    Atomic<RequestId> request_id_counter {};
    ArenaAllocator channels_arena {Malloc::Instance()};
    MutexProtected<ArenaList<AsyncCommsChannel>> channels {};
    Thread thread {};
    u64 server_thread_id {};
    Atomic<bool> end_thread {false};
    ThreadsafeQueue<detail::QueuedRequest> request_queue {PageAllocator::Instance()};
    Semaphore work_signaller {};
    Atomic<bool> request_debug_dump_current_state {false};

    // IMPROVE: we can set limits on some things here: we know there's only going to be
    // k_max_num_floe_instances for instance.
    // IMPROVE: would a Future-based API instead of request-response be better?
};

// ACCESSING LIBRARY INFORMATION
// ==========================================================================================================
// At any time, you can access library information from the server using these functions.

bool LibraryLessThan(sample_lib::LibraryIdRef const& key_a,
                     ResourcePointer<sample_lib::Library> const& val_a,
                     sample_lib::LibraryIdRef const& key_b,
                     ResourcePointer<sample_lib::Library> const& val_b);

using LibrariesTable = OrderedHashTable<sample_lib::LibraryIdRef,
                                        ResourcePointer<sample_lib::Library>,
                                        nullptr,
                                        LibraryLessThan>;
using LibrariesSpan = Span<ResourcePointer<sample_lib::Library>>;

//
// Thread-safe. You must call Release() on all results.
LibrariesSpan AllLibrariesRetained(Server& server, ArenaAllocator& arena);

// Thread-safe.
inline void ReleaseAll(Span<ResourcePointer<sample_lib::Library>> libs) {
    for (auto& l : libs)
        l.Release();
}
inline void ReleaseAll(LibrariesTable& table) {
    for (auto [key, lib, _] : table)
        lib.Release();
    table.DeleteAll();
}

LibrariesTable MakeTable(LibrariesSpan libs, ArenaAllocator& arena); // Does not retain.

// Thread-safe.
LibrariesTable AllLibrariesRetainedAsTable(Server& server, ArenaAllocator& arena);

// Thread-safe.
ResourcePointer<sample_lib::Library> FindLibraryRetained(Server& server, sample_lib::LibraryIdRef id);

// Thread-safe. You need understand the AtomicRefList API to use this properly.
detail::LibrariesAtomicList& LibrariesList(Server& server);

// LOADING RESOURCES
// ==========================================================================================================
// To load resources you need to open a communication channel with the server and then send requests.

// STEP 1: open a 'communications channel' with the server
//
// The server owns the channel, you just get a reference to it that will be valid until you close it. The
// callback will be called whenever a request from this channel is completed. If you want to keep any of
// the resources that are contained in the LoadResult, you must 'retain' them in the callback. You can release
// them at any point after that. The callback is called from the server thread; you should not do any really
// slow operations in it because it will block the server thread from processing other requests.
// [threadsafe]
struct OpenAsyncCommsChannelArgs {
    ThreadsafeErrorNotifications& error_notifications;
    AsyncCommsChannel::ResultAddedCallback result_added_callback;
    AsyncCommsChannel::LibraryChangedCallback library_changed_callback;
};
AsyncCommsChannel& OpenAsyncCommsChannel(Server& server, OpenAsyncCommsChannelArgs const& args);

// You will not receive any more results after this is called. Results that are still in the channel's queue
// will be released at some point after this is called.
// [threadsafe]
void CloseAsyncCommsChannel(Server& server, AsyncCommsChannel& channel);

// STEP 2: send requests for resources
//
// You'll receive a callback when the request is completed. After that you should consume all the results in
// your channel's 'results' field (threadsafe). Each result is already retained so you must Release() them
// when you're done with them. The server monitors the layer_index of each of your requests and works out if
// any currently-loading resources are no longer needed and aborts their loading.
// [threadsafe]
RequestId SendAsyncLoadRequest(Server& server, AsyncCommsChannel& channel, LoadRequest const& request);

// SERVER CONFIGURATION
// =========================================================================================================

// Change the set of extra folders that will be scanned for libraries.
// [threadsafe]
void SetExtraScanFolders(Server& server, Span<String const> folders);

// The server takes a lazy-loading approach. It only scans a folder when it's requested. Call this function to
// trigger a scan of any unscanned folders.
// [threadsafe]
void RequestScanningOfUnscannedFolders(Server& server);

// [threadsafe]
void RescanFolder(Server& server, String folder);

// Waits until all libraries have finished loading and all scan folders have finished scanning.
// Returns true if loading completed, false if timeout was reached. If timeout is nullopt, waits indefinitely.
// If timeout is 0, just returns 'is loading' status immediately.
// [threadsafe]
bool WaitIfLibrariesAreLoading(Server& server, Optional<u32> timeout);

bool AreLibrariesLoading(Server& server);

} // namespace sample_lib_server

// Loaded instrument
using Instrument = TaggedUnion<
    InstrumentType,
    TypeAndTag<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>, InstrumentType::Sampler>,
    TypeAndTag<WaveformType, InstrumentType::WaveformSynth>>;
