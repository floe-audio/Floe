// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"
#include "utils/debug/debug.hpp"
#include "utils/error_notifications.hpp"
#include "utils/logger/logger.hpp"
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
// - Manages loading, unloading and storage of sample libraries (including instruments, irs, etc)
// - Provides an asynchronous request-response API (we tend to call response 'result')
// - Very quick for resources that are already loaded
// - Scans library folders and watches for file changes in them
// - Has its own dedicated thread but also makes use of a thread pool for loading big files
// - Instantly aborts any pending loads that are no longer needed
// - No duplication of resources in memory
// - Provides progress/status metrics for other threads to read
//
// We use the term 'resource' for loadable things from a library, such as an Instrument, IR, audio data,
// image, etc.

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

// NOTE: this doesn't do reference counting automatically. You must use Retain() and Release() manually.
// We do this because things can get messy and inefficient doing ref-counting automatically in copy/move
// constructors and assignment operators. Instead, you will get assertion failures if you have mismatched
// retain/release.
template <typename Type>
struct RefCounted {
    RefCounted() = default;
    RefCounted(Type& t, Atomic<u32>& r, WorkSignaller* s) : data(&t), ref_count(&r), work_signaller(s) {}

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
                if (work_signaller) work_signaller->Signal();

                data = nullptr;
                ref_count = nullptr;
                work_signaller = nullptr;
            }
        }
    }

    void ChangeRefCount(RefCountChange t) {
        switch (t) {
            case RefCountChange::Retain: Retain(); break;
            case RefCountChange::Release: Release(); break;
        }
    }

    constexpr explicit operator bool() const { return data != nullptr; }
    constexpr Type const* operator->() const {
        ASSERT(data);
        return data;
    }
    constexpr Type const& operator*() const {
        ASSERT(data);
        return *data;
    }

    Type const* data {};
    Atomic<u32>* ref_count {};
    WorkSignaller* work_signaller {};
};

using Resource =
    TaggedUnion<LoadRequestType,
                TypeAndTag<RefCounted<sample_lib::LoadedInstrument>, LoadRequestType::Instrument>,
                TypeAndTag<RefCounted<sample_lib::LoadedIr>, LoadRequestType::Ir>>;

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

using LibrariesList = AtomicRefList<ListedLibrary>;

struct ScanFolder {
    enum class Source { AlwaysScannedFolder, ExtraFolder };
    enum class State { NotScanned, RescanRequested, Scanning, ScannedSuccessfully, ScanFailed };
    DynamicArray<char> path {Malloc::Instance()};
    Source source {};
    Atomic<State> state {State::NotScanned};
};

struct ScanFolders {
    using Folders = DynamicArrayBounded<ScanFolder*, k_max_extra_scan_folders + 1>;
    Mutex mutex;
    ArenaAllocator folder_arena {PageAllocator::Instance()};
    ArenaList<ScanFolder> folder_allocator {};
    Folders folders; // active folders
};

struct QueuedRequest {
    RequestId id;
    LoadRequest request;
    AsyncCommsChannel& async_comms_channel;
};

} // namespace detail

// Public API
// ==========================================================================================================

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
    Atomic<u32> is_scanning_libraries {}; // you can use WaitIfValueIsExpected

    // private
    detail::ScanFolders scan_folders;
    detail::LibrariesList libraries;
    Mutex libraries_by_id_mutex;
    DynamicHashTable<sample_lib::LibraryIdRef, detail::LibrariesList::Node*> libraries_by_id {
        Malloc::Instance()};
    // Connection-independent errors. If we have access to a channel, we post to the channel's
    // error_notifications instead of this.
    ThreadsafeErrorNotifications& error_notifications;
    Atomic<u32> num_uncompleted_library_jobs {0};
    ThreadPool& thread_pool;
    Atomic<RequestId> request_id_counter {};
    ArenaAllocator channels_arena {Malloc::Instance()};
    MutexProtected<ArenaList<AsyncCommsChannel>> channels {};
    Thread thread {};
    u64 server_thread_id {};
    Atomic<bool> end_thread {false};
    ThreadsafeQueue<detail::QueuedRequest> request_queue {PageAllocator::Instance()};
    WorkSignaller work_signaller {};
    Atomic<bool> request_debug_dump_current_state {false};
};

// IMPROVE: we can set limits: we know there's only going to be k_max_num_floe_instances.

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

// You'll receive a callback when the request is completed. After that you should consume all the results in
// your channel's 'results' field (threadsafe). Each result is already retained so you must Release() them
// when you're done with them. The server monitors the layer_index of each of your requests and works out if
// any currently-loading resources are no longer needed and aborts their loading.
// [threadsafe]
RequestId SendAsyncLoadRequest(Server& server, AsyncCommsChannel& channel, LoadRequest const& request);

// Change the set of extra folders that will be scanned for libraries.
// [threadsafe]
void SetExtraScanFolders(Server& server, Span<String const> folders);

// The server takes a lazy-loading approach. It only scans a folder when it's requested. Call this function to
// trigger a scan of any unscanned folders.
// [threadsafe]
void RequestScanningOfUnscannedFolders(Server& server);

// [threadsafe]
void RescanFolder(Server& server, String folder);

// You must call Release() on all results
// [threadsafe]
Span<RefCounted<sample_lib::Library>> AllLibrariesRetained(Server& server, ArenaAllocator& arena);
RefCounted<sample_lib::Library> FindLibraryRetained(Server& server, sample_lib::LibraryIdRef id);

inline void ReleaseAll(Span<RefCounted<sample_lib::Library>> libs) {
    for (auto& l : libs)
        l.Release();
}

} // namespace sample_lib_server

// Loaded instrument
using Instrument = TaggedUnion<
    InstrumentType,
    TypeAndTag<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>, InstrumentType::Sampler>,
    TypeAndTag<WaveformType, InstrumentType::WaveformSynth>>;
