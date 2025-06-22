// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#define Rect MacRect
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wextra-semi"
#include <AppKit/AppKit.h>
#include <CoreServices/CoreServices.h>
#include <dirent.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <sys/event.h>
#include <sys/stat.h>
#pragma clang diagnostic pop
#undef Rect

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "os/misc_mac.hpp"
#include "os/threading.hpp"
#include "utils/debug/debug.hpp"
#include "utils/logger/logger.hpp"

#include "filesystem.hpp"

static ErrorCode FilesystemErrorFromNSError(NSError* error,
                                            char const* extra_debug_info = nullptr,
                                            SourceLocation loc = SourceLocation::Current()) {
    ASSERT(error != nil);
    if ([error.domain isEqualToString:NSCocoaErrorDomain]) {
        if (error.code == NSFileNoSuchFileError || error.code == NSFileReadNoSuchFileError)
            return ErrorCode(FilesystemError::PathDoesNotExist, extra_debug_info, loc);
        else if (error.code == NSURLErrorUserAuthenticationRequired)
            return ErrorCode(FilesystemError::AccessDenied, extra_debug_info, loc);
    } else if ([error.domain isEqualToString:NSURLErrorDomain]) {
        if (error.code == NSURLErrorUserAuthenticationRequired)
            return ErrorCode(FilesystemError::AccessDenied, extra_debug_info, loc);
    }
    return ErrorFromNSError(error, extra_debug_info, loc);
}

ErrorCodeOr<void> CopyFile(String from, String to, ExistingDestinationHandling existing) {
    auto dest = StringToNSString(to);
    auto source = StringToNSString(from);

    NSError* error = nil;
    if (![[NSFileManager defaultManager] copyItemAtPath:source toPath:dest error:&error]) {
        if (error.code == NSFileWriteFileExistsError) {
            if (existing == ExistingDestinationHandling::Skip) return k_success;
            if (existing == ExistingDestinationHandling::Overwrite) {
                // IMPROVE: use replaceItemAtURL? Though it may require first creating a temp directory and
                // copying the file there
                if (![[NSFileManager defaultManager] removeItemAtPath:dest error:&error])
                    return FilesystemErrorFromNSError(error);
                if (![[NSFileManager defaultManager] copyItemAtPath:source toPath:dest error:&error])
                    return FilesystemErrorFromNSError(error);
                return k_success;
            }
            return ErrorCode {FilesystemError::PathAlreadyExists};
        }
        return FilesystemErrorFromNSError(error);
    }
    return k_success;
}

ErrorCodeOr<MutableString> AbsolutePath(Allocator& a, String path) {
    ASSERT(path.size);
    auto nspath = StringToNSString(path);
    if (StartsWith(path, '~')) nspath = nspath.stringByExpandingTildeInPath;
    auto url = [NSURL fileURLWithPath:nspath];
    if (url == nil) return FilesystemErrorFromNSError(nullptr);
    auto abs_string = [url path];
    if (abs_string == nil) return FilesystemErrorFromNSError(nullptr);

    auto result = NSStringToString(abs_string).Clone(a);
    ASSERT(path::IsAbsolute(result));
    return result;
}

ErrorCodeOr<MutableString> CanonicalizePath(Allocator& a, String path) {
    ASSERT(path.size);
    auto nspath = StringToNSString(path);
    if (StartsWith(path, '~')) nspath = nspath.stringByExpandingTildeInPath;

    // NOTE: on macOS there are some 'firmlinks' which are like symlinks but are resolved by the kernel.
    // These firmlinks are not resolved if we use the NSString resolving functions, so we use realpath.
    // e.g. /var -> /private/var only happens with realpath
    char resolved_path[PATH_MAX * 2];
    if (realpath([nspath fileSystemRepresentation], resolved_path) == nullptr)
        return FilesystemErrnoErrorCode(errno);
    return a.Clone(FromNullTerminated(resolved_path));
}

ErrorCodeOr<String> TrashFileOrDirectory(String path, Allocator& a) {
    NSURL* url = [NSURL fileURLWithPath:StringToNSString(path)];
    NSError* error = nil;
    NSURL* resulting_url = nil;
    auto const did_trash_item = [[NSFileManager defaultManager] trashItemAtURL:url
                                                              resultingItemURL:&resulting_url
                                                                         error:&error];
    (void)did_trash_item;
    if (error) return FilesystemErrorFromNSError(error);
    return a.Clone(NSStringToString([resulting_url path]));
}

ErrorCodeOr<void> Delete(String path, DeleteOptions options) {
    PathArena path_arena {Malloc::Instance()};
    switch (options.type) {
        case DeleteOptions::Type::Any:
        case DeleteOptions::Type::File:
        case DeleteOptions::Type::DirectoryRecursively: {
            NSError* error = nil;
            if (![[NSFileManager defaultManager] removeItemAtPath:StringToNSString(path) error:&error]) {
                auto const err = FilesystemErrorFromNSError(error);
                if (err == FilesystemError::PathDoesNotExist && !options.fail_if_not_exists) return k_success;
                return err;
            }
            return k_success;
        }
        case DeleteOptions::Type::DirectoryOnlyIfEmpty: {
            if (rmdir(NullTerminated(path, path_arena)) == 0)
                return k_success;
            else {
                if (errno == ENOENT && !options.fail_if_not_exists) return k_success;
                return FilesystemErrnoErrorCode(errno);
            }
            break;
        }
    }
    return k_success;
}

ErrorCodeOr<bool> DeleteDirectoryIfMacBundle(String dir) {
    auto bundle = [NSBundle bundleWithPath:StringToNSString(dir)];
    if (bundle != nil) {
        TRY(Delete(dir, {}));
        return true;
    }
    return false;
}

ErrorCodeOr<void> CreateDirectory(String path, CreateDirectoryOptions options) {
    if (options.create_intermediate_directories && options.fail_if_exists) {
        auto o = GetFileType(path);
        if (o.HasValue() && o.Value() == FileType::Directory)
            return ErrorCode {FilesystemError::PathAlreadyExists};
    }

    // returns YES if createIntermediates is set and the directory already exists
    NSError* error = nil;
    if (![[NSFileManager defaultManager]
                  createDirectoryAtPath:StringToNSString(path)
            withIntermediateDirectories:options.create_intermediate_directories ? YES : NO
                             attributes:nil
                                  error:&error]) {
        auto const ec = FilesystemErrorFromNSError(error);
        if (ec == FilesystemError::PathAlreadyExists && !options.fail_if_exists) return k_success;
    }
    return k_success;
}

ErrorCodeOr<MutableString> TemporaryDirectoryOnSameFilesystemAs(String existing_abs_path, Allocator& a) {
    NSError* error = nil;
    NSString* ns_path = StringToNSString(existing_abs_path);
    auto url = [[NSFileManager defaultManager] URLForDirectory:NSItemReplacementDirectory
                                                      inDomain:NSUserDomainMask
                                             appropriateForURL:[NSURL fileURLWithPath:ns_path]
                                                        create:YES
                                                         error:&error];
    if (url == nil && error == nil) {
        // try again but this time with a the local domain mask
        url = [[NSFileManager defaultManager] URLForDirectory:NSItemReplacementDirectory
                                                     inDomain:NSLocalDomainMask
                                            appropriateForURL:[NSURL fileURLWithPath:ns_path]
                                                       create:YES
                                                        error:&error];
    }
    if (url == nil) {
        if (error == nil) return ErrorCode {FilesystemError::PathDoesNotExist};
        return FilesystemErrorFromNSError(error);
    }
    return a.Clone(NSStringToString(url.path));
}

MutableString KnownDirectory(Allocator& a, KnownDirectoryType type, KnownDirectoryOptions options) {
    NSSearchPathDirectory dir_type {};
    NSSearchPathDomainMask domain {};
    Span<String const> extra_subdirs {};
    String fallback {};
    switch (type) {
        case KnownDirectoryType::Temporary: {
            auto temp_dir = NSStringToString(NSTemporaryDirectory());
            ASSERT(path::IsAbsolute(temp_dir));
            if (temp_dir[temp_dir.size - 1] == '/') temp_dir.size--;
            auto result = a.Clone(temp_dir);
            if (options.create) {
                if (auto const o =
                        CreateDirectory(result,
                                        {.create_intermediate_directories = true, .fail_if_exists = false});
                    o.HasError() && options.error_log) {
                    auto _ = fmt::FormatToWriter(*options.error_log,
                                                 "Failed creating directory {}: {}\n",
                                                 result,
                                                 o.Error());
                }
            }
            return result;
        }
        case KnownDirectoryType::Logs: {
            dir_type = NSLibraryDirectory;
            domain = NSUserDomainMask;
            static constexpr auto k_dirs = Array {"Logs"_s};
            extra_subdirs = k_dirs;
            fallback = "Library";
            break;
        }
        case KnownDirectoryType::Documents:
            dir_type = NSDocumentDirectory;
            domain = NSUserDomainMask;
            fallback = "Documents";
            break;
        case KnownDirectoryType::Downloads:
            dir_type = NSDownloadsDirectory;
            domain = NSUserDomainMask;
            fallback = "Downloads";
            break;
        case KnownDirectoryType::GlobalData: {
            // We ignore the 'create' option here because /Users/Shared is a folder that should always exist,
            // and if it doesn't, we need to handle it in a particular way here.
            String base_path {};

            NSError* error = nil;
            auto url = [[NSFileManager defaultManager] URLForDirectory:NSUserDirectory
                                                              inDomain:NSLocalDomainMask
                                                     appropriateForURL:nullptr
                                                                create:true
                                                                 error:&error];

            if (url == nil) {
                if (options.error_log) {
                    auto _ = fmt::FormatToWriter(*options.error_log,
                                                 "Failed getting /Users: {}\n",
                                                 FilesystemErrorFromNSError(error));
                }

                base_path = "/Users";
            } else {
                base_path = NSStringToString(url.path);
            }

            auto result = path::Join(a, Array {base_path, "Shared"_s});

            // /Users/Shared should be always be present on macOS systems. However, if it's not, we try to
            // create it here. We got the attributes by looking at the result of `stat /Users/Shared`. ignore
            // any error, we can't do anything about it
            [[NSFileManager defaultManager]
                      createDirectoryAtPath:StringToNSString(result)
                withIntermediateDirectories:NO
                                 attributes:@{
                                     NSFilePosixPermissions : @01775, // full permissions
                                     NSFileOwnerAccountID : @0, // root (UID 0)
                                     NSFileGroupOwnerAccountID : @0 // wheel (GID 0)
                                 }
                                      error:nil];

            return result;
        }
        case KnownDirectoryType::UserData:
            dir_type = NSApplicationSupportDirectory;
            domain = NSUserDomainMask;
            fallback = "Application Support";
            break;

        case KnownDirectoryType::GlobalVst3Plugins: {
            dir_type = NSLibraryDirectory;
            domain = NSLocalDomainMask;
            static constexpr auto k_dirs = Array {"Audio"_s, "Plug-Ins", "VST3"};
            extra_subdirs = k_dirs;
            fallback = "/Library";
            break;
        }
        case KnownDirectoryType::UserVst3Plugins: {
            dir_type = NSLibraryDirectory;
            domain = NSUserDomainMask;
            static constexpr auto k_dirs = Array {"Audio"_s, "Plug-Ins", "VST3"};
            extra_subdirs = k_dirs;
            fallback = "Library";
            break;
        }
        case KnownDirectoryType::GlobalClapPlugins: {
            dir_type = NSLibraryDirectory;
            domain = NSLocalDomainMask;
            static constexpr auto k_dirs = Array {"Audio"_s, "Plug-Ins", "CLAP"};
            extra_subdirs = k_dirs;
            fallback = "/Library";
            break;
        }
        case KnownDirectoryType::UserClapPlugins: {
            dir_type = NSLibraryDirectory;
            domain = NSUserDomainMask;
            static constexpr auto k_dirs = Array {"Audio"_s, "Plug-Ins", "CLAP"};
            extra_subdirs = k_dirs;
            fallback = "Library";
            break;
        }

        case KnownDirectoryType::MiragePreferences: {
            dir_type = NSMusicDirectory;
            domain = NSUserDomainMask;
            static constexpr auto k_dirs = Array {"Audio Music Apps"_s, "Plug-In Settings"};
            extra_subdirs = k_dirs;
            fallback = "Music";
            break;
        }
        case KnownDirectoryType::MiragePreferencesAlternate:
            dir_type = NSApplicationSupportDirectory;
            domain = NSUserDomainMask;
            fallback = "Application Support";
            break;
        case KnownDirectoryType::MirageGlobalPreferences:
        case KnownDirectoryType::MirageGlobalData:
            dir_type = NSApplicationSupportDirectory;
            domain = NSLocalDomainMask;
            fallback = "/Library/Application Support";
            break;

        case KnownDirectoryType::Count: PanicIfReached();
    }

    NSError* error = nil;
    auto url = [[NSFileManager defaultManager] URLForDirectory:dir_type
                                                      inDomain:domain
                                             appropriateForURL:nullptr
                                                        create:options.create
                                                         error:&error];

    MutableString path {};
    if (url == nil) {
        if (options.error_log) {
            auto _ = fmt::FormatToWriter(*options.error_log,
                                         "Failed getting known directory {}, domain {}: {}\n",
                                         dir_type,
                                         domain,
                                         FilesystemErrorFromNSError(error));
        }
        if (domain == NSUserDomainMask) {
            DynamicArrayBounded<String, 10> parts {};
            ASSERT(extra_subdirs.size <= (parts.Capacity() - 2));
            dyn::Append(parts, "/Users"_s);
            dyn::Append(parts, NSStringToString(NSUserName()));
            dyn::AppendSpan(parts, extra_subdirs);
            path = path::Join(a, parts);
        } else
            path = a.Clone(fallback);
    } else {
        path = a.Clone(NSStringToString(url.path));
    }

    if (extra_subdirs.size) {
        auto p = DynamicArray<char>::FromOwnedSpan(path, a);
        for (auto const subdir : extra_subdirs) {
            dyn::Append(p, '/');
            dyn::AppendSpan(p, subdir);
            if (options.create) {
                auto const o =
                    CreateDirectory(p, {.create_intermediate_directories = false, .fail_if_exists = false});
                if (o.HasError() && options.error_log) {
                    auto _ = fmt::FormatToWriter(*options.error_log,
                                                 "Failed creating directory {}: {}\n",
                                                 p,
                                                 o.Error());
                }
            }
        }
        return p.ToOwnedSpan();
    }

    return path;
}

Optional<Version> MacosBundleVersion(String path) {
    auto bundle = [NSBundle bundleWithPath:StringToNSString(path)];
    if (bundle == nil) return {};
    NSString* version_string = bundle.infoDictionary[@"CFBundleVersion"];
    NSString* beta_version = bundle.infoDictionary[@"Beta_Version"];
    FixedSizeAllocator<64> temp_mem {&Malloc::Instance()};
    DynamicArray<char> str {FromNullTerminated(version_string.UTF8String), temp_mem};
    if (beta_version != nil && beta_version.length) fmt::Append(str, "-Beta{}", beta_version.UTF8String);
    return ParseVersionString(str);
}

// NOTE: It seems you can receive filesystem events from before you start watching the directory even if you
// use the kFSEventStreamEventIdSinceNow flag. There could be some sort of buffering going on.

// NOTE: FSEvents is a callback based API. You receive events in a separate thread and there doesn't seem to
// be a way around that. FSEventStreamFlushSync might flush the events but you can still receive others at any
// time in the other thread. Therefore we use a queue to pass the buffer the events from the background thread
// to the PollDirectoryChanges thread.

// References:
// https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/FSEvents_ProgGuide/UsingtheFSEventsFramework/UsingtheFSEventsFramework.html
// CHOC library: https://github.com/Tracktion/choc/blob/main/platform/choc_FileWatcher.h
// EFSW library: https://github.com/SpartanJ/efsw/blob/master/src/efsw/WatcherFSEvents.cpp
// Atom file watcher: https://github.com/atom/watcher/blob/master/docs/macos.md

constexpr bool k_debug_fsevents = false && !PRODUCTION_BUILD;

// Thread Sanitizer does not understand about the synchronisation that happens in FSEvents callbacks and
// dispatch_sync. As a result, it thinks there's still possible race conditions with regard to the event
// callback being called while creating/destroying the watcher objects. We use this call to inform TSAN of
// this behaviour and therefore not get false positives.
struct TsanSyncBarrier {
#if __has_feature(thread_sanitizer)
    void StoreCallbackSynced(bool value) { callbacks_synced.Store(value, StoreMemoryOrder::Release); }
    void AcquireCallbackSynced() { callbacks_synced.Load(LoadMemoryOrder::Acquire); }
    Atomic<bool> callbacks_synced {true, StoreMemoryOrder::Release}; // For TSAN
#else
    void StoreCallbackSynced(bool) {}
    void AcquireCallbackSynced() {}
#endif
};

struct MacWatcher {
    FSEventStreamRef stream {};
    dispatch_queue_t queue {};

    [[no_unique_address]] TsanSyncBarrier tsan_sync_barrier;

    struct Event {
        String path;
        FSEventStreamEventFlags flags;
    };
    Mutex event_mutex;
    ArenaAllocator event_arena {PageAllocator::Instance()};
    ArenaStack<Event> events;
};

ErrorCodeOr<DirectoryWatcher> CreateDirectoryWatcher(Allocator& a) {
    ZoneScoped;
    DirectoryWatcher result {
        .allocator = a,
        .watched_dirs = {},
        .native_data = {.pointer = a.New<MacWatcher>()},
    };
    return result;
}

static void StopStream(MacWatcher& watcher) {
    // We dispatch_sync to the queue to ensure that we stop the stream only after all events have been
    // processed.
    dispatch_sync(watcher.queue, ^{
      FSEventStreamStop(watcher.stream);
      FSEventStreamInvalidate(watcher.stream);
      FSEventStreamRelease(watcher.stream);
      watcher.stream = nullptr;
    });

    watcher.tsan_sync_barrier.AcquireCallbackSynced();
}

void DestoryDirectoryWatcher(DirectoryWatcher& watcher) {
    ZoneScoped;

    auto mac_w = (MacWatcher*)watcher.native_data.pointer;
    if (mac_w->stream) StopStream(*mac_w);
    watcher.watched_dirs.Clear();
    watcher.allocator.Delete(mac_w);
    watcher.native_data.pointer = nullptr;
}

void EventCallback([[maybe_unused]] ConstFSEventStreamRef stream_ref,
                   [[maybe_unused]] void* user_data,
                   size_t num_events,
                   void* event_paths,
                   FSEventStreamEventFlags const event_flags[],
                   [[maybe_unused]] FSEventStreamEventId const event_ids[]) {
    auto& watcher = *(MacWatcher*)user_data;
    auto** paths = (char**)event_paths;

    // TSAN
    watcher.tsan_sync_barrier.StoreCallbackSynced(false);
    DEFER { watcher.tsan_sync_barrier.StoreCallbackSynced(true); };

    if constexpr (k_debug_fsevents) {
        DynamicArrayBounded<char, 4000> info;
        struct Flag {
            FSEventStreamEventFlags flag;
            String name;
        };
        constexpr Flag k_flags[] = {
            {kFSEventStreamEventFlagMustScanSubDirs, "MustScanSubDirs"},
            {kFSEventStreamEventFlagUserDropped, "UserDropped"},
            {kFSEventStreamEventFlagKernelDropped, "KernelDropped"},
            {kFSEventStreamEventFlagEventIdsWrapped, "EventIdsWrapped"},
            {kFSEventStreamEventFlagHistoryDone, "HistoryDone"},
            {kFSEventStreamEventFlagRootChanged, "RootChanged"},
            {kFSEventStreamEventFlagMount, "Mount"},
            {kFSEventStreamEventFlagUnmount, "Unmount"},
            {kFSEventStreamEventFlagItemChangeOwner, "ItemChangeOwner"},
            {kFSEventStreamEventFlagItemCreated, "ItemCreated"},
            {kFSEventStreamEventFlagItemFinderInfoMod, "ItemFinderInfoMod"},
            {kFSEventStreamEventFlagItemInodeMetaMod, "ItemInodeMetaMod"},
            {kFSEventStreamEventFlagItemIsDir, "ItemIsDir"},
            {kFSEventStreamEventFlagItemIsFile, "ItemIsFile"},
            {kFSEventStreamEventFlagItemIsHardlink, "ItemIsHardlink"},
            {kFSEventStreamEventFlagItemIsLastHardlink, "ItemIsLastHardlink"},
            {kFSEventStreamEventFlagItemIsSymlink, "ItemIsSymlink"},
            {kFSEventStreamEventFlagItemModified, "ItemModified"},
            {kFSEventStreamEventFlagItemRemoved, "ItemRemoved"},
            {kFSEventStreamEventFlagItemRenamed, "ItemRenamed"},
            {kFSEventStreamEventFlagItemXattrMod, "ItemXattrMod"},
            {kFSEventStreamEventFlagOwnEvent, "OwnEvent"},
            {kFSEventStreamEventFlagItemCloned, "ItemCloned"},
        };
        auto writer = dyn::WriterFor(info);

        auto _ = fmt::AppendLine(writer, "FSEvent received {} events:", num_events);

        for (size_t i = 0; i < num_events; i++) {
            MAYBE_UNUSED auto u1 = fmt::AppendLine(writer, "  {{");
            MAYBE_UNUSED auto u2 = fmt::AppendLine(writer, "    path: {}", paths[i]);

            DynamicArrayBounded<char, 1000> flags_str;
            for (auto const& flag : k_flags)
                if (event_flags[i] & flag.flag) {
                    dyn::AppendSpan(flags_str, flag.name);
                    dyn::AppendSpan(flags_str, ", ");
                }
            if (flags_str.size) flags_str.size -= 2;
            MAYBE_UNUSED auto u3 = fmt::AppendLine(writer, "    flags: {}", flags_str);
            MAYBE_UNUSED auto u4 = fmt::AppendLine(writer, "    id: {}", event_ids[i]);

            MAYBE_UNUSED auto u5 = fmt::AppendLine(writer, "  }}");
        }

        LogDebug(ModuleName::Filesystem, info);
    }

    {
        watcher.event_mutex.Lock();
        DEFER { watcher.event_mutex.Unlock(); };
        for (size_t i = 0; i < num_events; i++) {
            ASSERT(paths[i] != nullptr);
            auto const path = FromNullTerminated(paths[i]);
            ASSERT(path::IsAbsolute(path));
            ASSERT(IsValidUtf8(path));
            watcher.events.Append(
                {
                    .path = watcher.event_arena.Clone(path),
                    .flags = event_flags[i],
                },
                watcher.event_arena);
        }
    }
}

ErrorCodeOr<Span<DirectoryWatcher::DirectoryChanges const>>
PollDirectoryChanges(DirectoryWatcher& watcher, PollDirectoryChangesArgs args) {
    auto const any_states_changed =
        watcher.HandleWatchedDirChanges(args.dirs_to_watch, args.retry_failed_directories);

    for (auto& dir : watcher.watched_dirs)
        dir.directory_changes.Clear();

    ASSERT(watcher.native_data.pointer != nullptr);
    auto& mac_watcher = *(MacWatcher*)watcher.native_data.pointer;
    if (any_states_changed) {
        if (mac_watcher.stream) {
            if constexpr (k_debug_fsevents) LogDebug(ModuleName::Filesystem, "stopping FSEventStream");
            StopStream(mac_watcher);
        }

        // fsevents is batched, we don't need to individually stop watching each directory, so at this stage
        // all NeedsUnwatching directories are not watched.
        for (auto& dir : watcher.watched_dirs)
            if (dir.state == DirectoryWatcher::WatchedDirectory::State::NeedsUnwatching)
                dir.state = DirectoryWatcher::WatchedDirectory::State::NotWatching;

        CFMutableArrayRef paths = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
        DEFER { CFRelease(paths); };
        for (auto& dir : watcher.watched_dirs) {
            if (dir.state != DirectoryWatcher::WatchedDirectory::State::Watching &&
                dir.state != DirectoryWatcher::WatchedDirectory::State::NeedsWatching)
                continue;

            CFStringRef cf_path = CFStringCreateWithBytes(nullptr,
                                                          (u8 const*)dir.path.data,
                                                          (CFIndex)dir.path.size,
                                                          kCFStringEncodingUTF8,
                                                          false);
            DEFER { CFRelease(cf_path); };

            auto ns_string = (__bridge NSString*)cf_path;
            if ([[NSFileManager defaultManager] fileExistsAtPath:ns_string isDirectory:nil]) {
                CFArrayAppendValue(paths, cf_path);
            } else {
                dir.state = DirectoryWatcher::WatchedDirectory::State::WatchingFailed;
                dir.directory_changes.error = ErrorCode {FilesystemError::PathDoesNotExist};
            }
        }

        if (CFArrayGetCount(paths) != 0) {
            FSEventStreamContext context {};
            context.info = &mac_watcher;

            bool succeeded = false;
            DEFER {
                for (auto& dir : watcher.watched_dirs) {
                    if (dir.state == DirectoryWatcher::WatchedDirectory::State::Watching ||
                        dir.state == DirectoryWatcher::WatchedDirectory::State::NeedsWatching) {
                        if (!succeeded)
                            dir.state = DirectoryWatcher::WatchedDirectory::State::WatchingFailed;
                        else
                            dir.state = DirectoryWatcher::WatchedDirectory::State::Watching;
                    }
                }
            };

            if constexpr (k_debug_fsevents) LogDebug(ModuleName::Filesystem, "starting FSEventStream");

            mac_watcher.stream =
                FSEventStreamCreate(kCFAllocatorDefault,
                                    &EventCallback,
                                    &context,
                                    paths,
                                    kFSEventStreamEventIdSinceNow,
                                    args.coalesce_latency_ms / 1000.0,
                                    kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagWatchRoot |
                                        kFSEventStreamCreateFlagNoDefer);
            if (!mac_watcher.stream) return ErrorCode {FilesystemError::FileWatcherCreationFailed};

            // FSEvents is a callback based API where you always receive events in a separate thread.
            if (!mac_watcher.queue) {
                auto attr = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL,
                                                                    QOS_CLASS_USER_INITIATED,
                                                                    -10);
                mac_watcher.queue = dispatch_queue_create("com.floe.fseventsqueue", attr);
            }
            FSEventStreamSetDispatchQueue(mac_watcher.stream, mac_watcher.queue);

            if (!FSEventStreamStart(mac_watcher.stream)) {
                FSEventStreamInvalidate(mac_watcher.stream);
                FSEventStreamRelease(mac_watcher.stream);
                mac_watcher.stream = {};
                return ErrorCode {FilesystemError::FileWatcherCreationFailed};
            }

            mac_watcher.tsan_sync_barrier.StoreCallbackSynced(false);

            succeeded = true;
        }
    }

    if (mac_watcher.stream) FSEventStreamFlushAsync(mac_watcher.stream);

    {
        mac_watcher.event_mutex.Lock();
        DEFER { mac_watcher.event_mutex.Unlock(); };

        for (auto const& event : mac_watcher.events) {
            for (auto& dir : watcher.watched_dirs) {
                if (StartsWithSpan(event.path, dir.resolved_path)) {
                    String subpath {};
                    if (event.path.size != dir.resolved_path.size)
                        subpath = event.path.SubSpan(dir.resolved_path.size);
                    if (subpath.size && subpath[0] == '/') subpath = subpath.SubSpan(1);
                    subpath = args.result_arena.Clone(subpath);

                    // FSEvents ONLY supports recursive watching so we just have to ignore subdirectory
                    // events
                    if (!dir.recursive && Contains(subpath, '/')) {
                        if constexpr (k_debug_fsevents)
                            LogDebug(ModuleName::Filesystem,
                                     "ignoring subdirectory event: {} because {} is watched non-recursively",
                                     subpath,
                                     dir.path);
                        continue;
                    }

                    auto const file_type = (event.flags & kFSEventStreamEventFlagItemIsDir)
                                               ? FileType::Directory
                                               : FileType::File;

                    if (event.flags & kFSEventStreamEventFlagMustScanSubDirs) {
                        dir.directory_changes.Add(
                            {
                                .subpath = subpath,
                                .file_type = file_type,
                                .changes = DirectoryWatcher::ChangeType::ManualRescanNeeded,
                            },
                            args.result_arena);
                        continue;
                    }

                    // There is no way of knowing the exact ordering of events from FSEvents. We can lstat on
                    // a file to see if it exists at the time we receive the event which helps in some
                    // situations. But some situations are still ambiguous. Consider the case where we have 3
                    // flags set 'created', 'renamed' and 'removed' and we have checked that the file does not
                    // currently exist: we can't know if it was (1) removed -> created -> renamed (old-name)
                    // or (2) renamed (old-name) -> created -> removed.

                    // Additionally, checking the filesystem (with lstat() for example) is not at all reliable
                    // because what we are doing is tagging an event with the current state of the filesystem
                    // rather than the state at the time of the event.

                    DirectoryWatcher::ChangeTypeFlags changes {};
                    if (event.flags & kFSEventStreamEventFlagItemCreated)
                        changes |= DirectoryWatcher::ChangeType::Added;
                    if (event.flags & kFSEventStreamEventFlagItemRemoved)
                        changes |= DirectoryWatcher::ChangeType::Deleted;
                    if (event.flags & kFSEventStreamEventFlagItemRenamed)
                        changes |= DirectoryWatcher::ChangeType::RenamedOldOrNewName;
                    if (event.flags & kFSEventStreamEventFlagItemModified)
                        changes |= DirectoryWatcher::ChangeType::Modified;

                    if (changes)
                        dir.directory_changes.Add(
                            {
                                .subpath = subpath,
                                .file_type = file_type,
                                .changes = changes,
                            },
                            args.result_arena);
                }
            }
        }

        mac_watcher.events.Clear();
        mac_watcher.event_arena.ResetCursorAndConsolidateRegions();
    }

    watcher.RemoveAllNotWatching();

    return watcher.AllDirectoryChanges(args.result_arena);
}
