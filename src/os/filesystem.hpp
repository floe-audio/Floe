// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "misc.hpp"

enum class FilesystemError : u32 {
    PathDoesNotExist,
    PathAlreadyExists,
    TooManyFilesOpen,
    FolderContainsTooManyFiles,
    AccessDenied,
    UsedByAnotherProcess,
    PathIsAFile,
    PathIsAsDirectory,
    FileWatcherCreationFailed,
    NotSupported,
    FilesystemBusy,
    DiskFull,
    DifferentFilesystems,
    NotEmpty,
    Count,
};

ErrorCodeCategory const& ErrorCategoryForEnum(FilesystemError e);

// attempts to translate errno to a FilesystemError
ErrorCode FilesystemErrnoErrorCode(s64 error_code,
                                   char const* extra_debug_info = nullptr,
                                   SourceLocation loc = SourceLocation::Current());

// File API
// =======================================================================================================
struct FileMode {
    // Open for reading if it exists.
    static constexpr FileMode Read() {
        return {
            .capability = Capability::Read,
            .win32_share = Share::Read,
            .creation = Creation::OpenExisting,
            .everyone_read_write = false,
        };
    }

    // Open for writing, overwriting if it already exists.
    static constexpr FileMode Write() {
        return {
            .capability = Capability::Write,
            .win32_share = Share::None,
            .creation = Creation::CreateAlways,
            .everyone_read_write = false,
        };
    }

    // Open for writing, fail if it already exists.
    static constexpr FileMode WriteNoOverwrite() {
        return {
            .capability = Capability::Write,
            .win32_share = Share::None,
            .creation = Creation::CreateNew,
            .everyone_read_write = false,
        };
    }

    // Open for reading and writing, create if it doesn't exist.
    static constexpr FileMode ReadWrite() {
        return {
            .capability = Capability::ReadWrite,
            .win32_share = Share::ReadWrite,
            .creation = Creation::OpenAlways,
            .everyone_read_write = false,
        };
    }

    // Overwrites if it already exists (but doesn't change file permissions). If it doesn't exist, it will
    // be created with read/write permissions for everyone.
    static constexpr FileMode WriteEveryoneReadWrite() {
        return {
            .capability = Capability::Write,
            .win32_share = Share::None,
            .creation = Creation::CreateAlways,
            .everyone_read_write = true,
        };
    }

    // Open for appending.
    static constexpr FileMode Append() {
        return {
            .capability = Capability::Write | Capability::Append,
            .win32_share = Share::None,
            .creation = Creation::OpenAlways,
            .everyone_read_write = false,
        };
    }

    enum class Capability : u8 {
        Read = 1 << 0, // open the file for reading
        Write = 1 << 1, // open the file for writing
        ReadWrite = Read | Write,
        Append = 1 << 2, // open the file for appending
    };
    friend constexpr Capability operator|(Capability a, Capability b) {
        return (Capability)(ToInt(a) | ToInt(b));
    }

    // Windows only. On Unix, you're always allowed to open a file, but on Windows you must specify what
    // sharing you accept.
    enum class Share : u8 {
        None = 0,
        Read = 1 << 0, // allow others to read the file while you have it open
        Write = 1 << 1, // allow others to write to the file while you have it open
        DeleteRename = 1 << 2, // allows other to delete or rename the file while you have it open
        ReadWrite = Read | Write,
    };
    friend constexpr Share operator|(Share a, Share b) { return (Share)(ToInt(a) | ToInt(b)); }

    enum class Creation : u8 {
        OpenExisting, // fail if it doesn't exist
        OpenAlways, // open if it exists, create if it doesn't
        CreateNew, // create new, fail if it already exists
        CreateAlways, // create new, overwrite if it already exists
        TruncateExisting, // open if it exists, truncate it to 0 bytes
    };

    Capability capability = Capability::Read;
    Share win32_share = Share::Read;
    Creation creation = Creation::OpenExisting;

    // Add extra permissions to the file so that any user on the system can read and write to it.
    bool everyone_read_write = false;
    int default_permissions = 0644; // Unix only
};

struct FileLockOptions {
    enum class Type { Exclusive, Shared };
    Type type = Type::Exclusive;
    bool non_blocking = false;
};

// File is created with OpenFile()
struct File {
#if IS_WINDOWS
    using NativeFileHandle = void*;
    static constexpr NativeFileHandle k_invalid_file_handle = nullptr;
    static constexpr bool k_is_buffered = true;
#else
    using NativeFileHandle = int;
    static constexpr NativeFileHandle k_invalid_file_handle = -1;
    static constexpr bool k_is_buffered = false;
#endif

    File(File&& other) {
        handle = other.handle;
        other.handle = k_invalid_file_handle;
    }
    File& operator=(File&& other) {
        CloseFile();
        handle = other.handle;
        other.handle = k_invalid_file_handle;
        return *this;
    }
    File(File const& other) = delete;
    File& operator=(File const& other) = delete;

    ~File();

    ErrorCodeOr<u64> CurrentPosition();
    enum class SeekOrigin { Start, End, Current };
    ErrorCodeOr<void> Seek(s64 const offset, SeekOrigin origin);
    ErrorCodeOr<u64> FileSize();

    ErrorCodeOr<void> Flush();

    // returns true if the lock was acquired (always true for non-blocking locks)
    ErrorCodeOr<bool> Lock(FileLockOptions options);
    ErrorCodeOr<void> Unlock();

    ErrorCodeOr<s128> LastModifiedTimeNsSinceEpoch();
    ErrorCodeOr<void> SetLastModifiedTimeNsSinceEpoch(s128 time);

    ErrorCodeOr<MutableString>
    ReadSectionOfFile(usize const bytes_offset_from_file_start, usize const size_in_bytes, Allocator& a);
    ErrorCodeOr<MutableString> ReadWholeFile(Allocator& a);

    ErrorCodeOr<usize> Read(void* data, usize num_bytes);

    ::Writer Writer() {
        ::Writer result;
        result.Set<File>(*this, [](File& f, Span<u8 const> bytes) -> ErrorCodeOr<void> {
            TRY(f.Write(bytes));
            return k_success;
        });
        return result;
    }

    ErrorCodeOr<void> Truncate(u64 new_size);

    ErrorCodeOr<usize> Write(Span<u8 const> data);
    ErrorCodeOr<usize> Write(Span<char const> data) { return Write(data.ToByteSpan()); }

    ErrorCodeOr<usize> WriteBinaryNumber(Integral auto number) {
        return Write(Span<u8 const>((u8*)&number, sizeof(number)));
    }

    ErrorCodeOr<usize> WriteAt(s64 position, Span<u8 const> data) {
        TRY(Seek(position, SeekOrigin::Start));
        return Write(data);
    }

    friend ErrorCodeOr<File> OpenFile(String filename, FileMode mode);

    NativeFileHandle handle {};

  private:
    File(NativeFileHandle file) : handle(file) {}
    void CloseFile();
    static constexpr int k_fseek_success = 0;
    static constexpr s64 k_ftell_error = -1;
};

ErrorCodeOr<File> OpenFile(String filename, FileMode mode);
ErrorCodeOr<MutableString> ReadEntireFile(String filename, Allocator& a);
ErrorCodeOr<MutableString> ReadSectionOfFile(String filename,
                                             usize const bytes_offset_from_file_start,
                                             usize const size_in_bytes,
                                             Allocator& a);

ErrorCodeOr<u64> FileSize(String filename);
ErrorCodeOr<s128> LastModifiedTimeNsSinceEpoch(String filename);
ErrorCodeOr<void> SetLastModifiedTimeNsSinceEpoch(String filename, s128 time);

ErrorCodeOr<usize> WriteFile(String filename, Span<u8 const> data);
inline ErrorCodeOr<usize> WriteFile(String filename, Span<char const> data) {
    return WriteFile(filename, data.ToByteSpan());
}

ErrorCodeOr<usize> AppendFile(String filename, Span<u8 const> data);
inline ErrorCodeOr<usize> AppendFile(String filename, Span<char const> data) {
    return AppendFile(filename, data.ToByteSpan());
}

ErrorCodeOr<void> ReadSectionOfFileAndWriteToOtherFile(File& file_to_read_from,
                                                       usize section_start,
                                                       usize size,
                                                       String filename_to_write_to);

// Checking the filesystem
// Returned paths will use whatever the OS's path separator. And they never have a trailing path seporator.
// =======================================================================================================

using PathArena = ArenaAllocatorWithInlineStorage<2000>;

// Generic directories, they won't have a 'Floe' subdirectory
enum class KnownDirectoryType : u8 {
    Documents,
    Downloads,
    Logs,
    Temporary, // Don't delete this directory, delete the file in it

    UserData,

    // Any user can read and write here. It's carefully picked to also work when we're running as an audio
    // plugin and even sandboxed.
    //
    // We still need to be mindful of permissions. If one user creates a file, it should be readable by
    // everyone, but it might not be writable by everyone. If we wan't to share write-access then we can use
    // things like open()'s mode argument, chmod() or umask() on Unix, or CreateFile()'s security attributes
    // or SetFileSecurity() on Windows.
    //
    // We tend to prefer global locations because as an audio plugin, we're almost always going to be
    // installed globally anyways. Things like sample libraries are extensions of the application, it makes
    // no sense to install them per-user.
    //
    // NOTE: on Linux it's not global, it's in the user's home directory.
    GlobalData,

    GlobalVst3Plugins,
    GlobalClapPlugins,

    // NOTE: per-user plugin locations are not typically used.
    UserVst3Plugins,
    UserClapPlugins,

    MirageGlobalPreferences,
    MiragePreferences,
    MiragePreferencesAlternate,
    MirageGlobalData,

    Count,
};

struct KnownDirectoryOptions {
    bool create;
    Writer* error_log;
};

// You'll probably want to use KnownDirectoryWithSubdirectories instead.
MutableString KnownDirectory(Allocator& a, KnownDirectoryType type, KnownDirectoryOptions options);

// Gets a known directory and adds subdirectories and (optionally) a filename. It will create the
// subdirectories if options.create is true.
MutableString KnownDirectoryWithSubdirectories(Allocator& a,
                                               KnownDirectoryType type,
                                               Span<String const> subdirectories,
                                               Optional<String> filename,
                                               KnownDirectoryOptions options);

// Returns a Floe-specific path. Might be a KnownDirectory with a 'Floe' subdirectory. Just a wrapper around
// KnownDirectoryWithSubdirectories.
enum class FloeKnownDirectoryType : u8 {
    Logs,
    Preferences,
    Libraries,
    Presets,
    Autosaves,
    MirageDefaultLibraries,
    MirageDefaultPresets,
};
MutableString FloeKnownDirectory(Allocator& a,
                                 FloeKnownDirectoryType type,
                                 Optional<String> filename,
                                 KnownDirectoryOptions options);

// Path of Floe's preferences file. This is static and doesn't change during the lifetime of the program.
// thread-safe
String PreferencesFilepath(String* error_log = nullptr);

// The path where logs and error reports are written to. This is static and doesn't change during the lifetime
// of the program.
void InitLogFolderIfNeeded(); // thread-safe
Optional<String> LogFolder(); // thread-safe, signal-safe, guaranteed valid after InitLogFolder()

inline DynamicArrayBounded<char, 48> UniqueFilename(String prefix, String suffix, u64& seed) {
    ASSERT(prefix.size <= 16);
    ASSERT(suffix.size <= 16);
    DynamicArrayBounded<char, 48> name {prefix};
    auto const chars_added = fmt::IntToString(RandomU64(seed),
                                              name.data + name.size,
                                              {.base = fmt::IntToStringOptions::Base::Base32});
    ASSERT(chars_added <= 16);
    name.size += chars_added;
    dyn::AppendSpan(name, suffix);
    return name;
}

constexpr String k_temporary_directory_prefix = ".floe-temp-";

// Creates a unique directory on the same filesystem as an already existing path. Delete the directory when
// you're done with it.
ErrorCodeOr<MutableString> TemporaryDirectoryOnSameFilesystemAs(String existing_abs_path, Allocator& a);

// Creates a directory with the prefix k_temporary_directory_prefix in the given folder. Delete the directory
// when you're done with it.
ErrorCodeOr<MutableString>
TemporaryDirectoryWithinFolder(String existing_abs_folder, Allocator& a, u64& seed);

enum class FileType { File, Directory };

ErrorCodeOr<FileType> GetFileType(String path);

// Turns a relative path into an absolute path.
// Unix:
// - Replaces tilde ~ with the user's home directory.
ErrorCodeOr<MutableString> AbsolutePath(Allocator& a, String path);

// Makes it an AbsolutePath, and:
// - Resolves ../ and ./ components.
// - Resolves symlinks.
// Windows:
// - Add the drive specifier if it's missing.
// - Replaces / with \.
ErrorCodeOr<MutableString> CanonicalizePath(Allocator& a, String path);

Optional<Version> MacosBundleVersion(String path);

// Path to the currently running executable or shared library
ErrorCodeOr<MutableString> CurrentBinaryPath(Allocator& a);

Optional<String> SearchForExistingFolderUpwards(String dir, String folder_name_to_find, Allocator& allocator);

// Manipulating the filesystem
// =======================================================================================================

struct CreateDirectoryOptions {
    bool create_intermediate_directories = false;
    bool fail_if_exists = false; // returns FilesystemError::PathAlreadyExists
    bool win32_hide_dirs_starting_with_dot = true;
};
ErrorCodeOr<void> CreateDirectory(String path, CreateDirectoryOptions options = {});

#if IS_WINDOWS
#define TRASH_NAME "Recycle Bin"
#else
#define TRASH_NAME "Trash"
#endif

// Returns the path to the trashed file or directory which you can use to restore it.
ErrorCodeOr<String> TrashFileOrDirectory(String path, Allocator& a);

struct DeleteOptions {
    enum class Type { Any, File, DirectoryRecursively, DirectoryOnlyIfEmpty };
    Type type = Type::Any;
    bool fail_if_not_exists = true; // returns FilesystemError::PathDoesNotExist
};
ErrorCodeOr<void> Delete(String path, DeleteOptions options);

// Returns true if there was a bundle and it was successfully deleted
ErrorCodeOr<bool> DeleteDirectoryIfMacBundle(String dir);

enum class ExistingDestinationHandling {
    Skip, // Keep the existing file without reporting an error
    Overwrite, // Overwrite it if it exists
    Fail, // Fail if it exists
};

// rename() on Unix, MoveFile() on Windows
// - old_name and new_name must be the same type: both files or both directories
// - old_name and new_name must be on the same filesystem
// - The new_name can be in a different directory
// - If they're files, new_name will be overwritten if it exists
// - If they're directories, new_name must not exist OR it must be empty
ErrorCodeOr<void> Rename(String old_name, String new_name);

// Same as Rename except the destination is a folder that will contain the moved file or directory.
ErrorCodeOr<void> MoveIntoFolder(String from, String destination_folder);

ErrorCodeOr<void> CopyFile(String from, String to, ExistingDestinationHandling existing);

struct WindowsFileAttributes {
    bool hidden {};
};
// no-op on non-Windows. If attributes is not given, it will remove all attributes.
ErrorCodeOr<void> WindowsSetFileAttributes(String path, Optional<WindowsFileAttributes> attributes);

// DirectoryIterator
// =======================================================================================================

namespace dir_iterator {

struct Options {
    Options Clone(Allocator& a, CloneType) const {
        return {
            .wildcard = a.Clone(wildcard),
            .get_file_size = get_file_size,
            .skip_dot_files = skip_dot_files,
        };
    }
    String wildcard = "*";
    bool get_file_size = false;
    bool skip_dot_files = true;
};

struct Entry {
    MutableString subpath; // path relative to the base iterator path
    FileType type;
    u64 file_size; // ONLY valid if options.get_file_size == true
};

struct Iterator {
    // private
    static ErrorCodeOr<Iterator> InternalCreate(ArenaAllocator& arena, String path, Options options) {
        ASSERT(IsValidUtf8(path));
        ASSERT(path::IsAbsolute(path));
        ASSERT(options.wildcard.size);
        ASSERT(IsValidUtf8(options.wildcard));
        Iterator result {
            .options = options.Clone(arena, CloneType::Deep),
            .base_path = arena.Clone(path),
        };
        return result;
    }

    Options options;
    void* handle;
    String base_path;
    bool reached_end;
};

struct RecursiveIterator {
    ArenaAllocator& arena;
    ArenaList<Iterator> stack;
    DynamicArray<char> dir_path_to_iterate;
    String base_path;
    Options options;
};

// NOTE: these may succeed even if the folder doesn't exist. In which case, Next() will return an error.
ErrorCodeOr<Iterator> Create(ArenaAllocator& a, String path, Options options);
ErrorCodeOr<RecursiveIterator> RecursiveCreate(ArenaAllocator& a, String path, Options options);

void Destroy(Iterator& it);
void Destroy(RecursiveIterator& it);

ErrorCodeOr<Optional<Entry>> Next(Iterator& it, ArenaAllocator& result_arena);
ErrorCodeOr<Optional<Entry>> Next(RecursiveIterator& it, ArenaAllocator& result_arena);

inline MutableString
FullPath(auto& iterator, Entry const& entry, ArenaAllocator& arena, bool null_terminate = false) {
    auto result = arena.AllocateExactSizeUninitialised<char>(iterator.base_path.size + 1 +
                                                             entry.subpath.size + (null_terminate ? 1 : 0));
    usize write_pos = 0;
    WriteAndIncrement(write_pos, result, iterator.base_path);
    WriteAndIncrement(write_pos, result, path::k_dir_separator);
    WriteAndIncrement(write_pos, result, entry.subpath);
    if (null_terminate) WriteAndIncrement(write_pos, result, '\0');
    return result;
}

} // namespace dir_iterator

struct FindEntriesInFolderOptions {
    dir_iterator::Options options;
    bool recursive = false;
    Optional<FileType> only_file_type;
};

ErrorCodeOr<Span<dir_iterator::Entry>>
FindEntriesInFolder(ArenaAllocator& a, String directory, FindEntriesInFolderOptions options);

// =======================================================================================================

// Directory watcher
// =======================================================================================================
// - inotify on Linux, ReadDirectoryChangesW on Windows, FSEvents on macOS
// - Super simple poll-like API, just create, poll, destroy - all from one thread
// - Recursive or non-recursive
// - Events are grouped to each directory you request watching for
// - Full error handling
// - Failed actions are only retried if you explicitly ask for it, to reduce spam
//
// NOTE(Sam): The use-case that I designed this for was for an event/worker thread. The thread is already
// regularly polling for events from other systems. So for file changes it's convenient to have the same
// poll-like API. The alternative API that file watchers often have is a callback-based API where you receive
// events in a separate thread. For my use-case that would just mean having to do lots of extra thread-safety
// work.
//
// There's no fallback if the file system watcher fails to initialize or produces an error. But if needed, we
// could add a system that tracks changes by regularly scanning the directories.
//
// This directory watcher gives you a coalesced bitset of changes that happend to each sub-path. We don't give
// the order of events. We do this for 2 reasons:
// 1. On macOS (FSEvents), this kind of coalescing already happens to a certain extent, so it's impossible to
//    get the exact order of events.
// 2. Having the exact order isn't normally the important bit. For example knowing that something was modified
//    before being deleted doesn't really help. It's not like we even know what the modification was. As
//    always with the filesystem, you can't trust the state of anything until you've run a filesystem
//    operation. The same goes for receiving filesystem events. You might have been given a 'created' event
//    but the file might have been deleted in the time between the event being generated and you acting on it.
//    Therefore the changes that you receive are prompts to take further actions, not a guarantee of the
//    current state of the filesystem.
//
// This directory watcher API uses a single call for multiple directories rather than allowing for separate
// calls - one for each directory that you want to watch. This is because in some of the backends (Linux and
// macOS), a single 'watching' object is created to watch multiple directories at once. We follow that pattern
// rather than fighting it.
//
// IMPORTANT: you should check if you receive a 'Delete' change for the watched directory itself. If you poll
// for a directory that doesn't exist then you will get a 'file or folder doesn't exist' error.
//
// On macOS:
// - You may receive changes that occurred very shortly BEFORE you created the watcher.
// - You do not get the distinction between 'renamed to' and 'renamed from'. You only get a 'renamed' event,
//   you must work out yourself if it was a rename to or from.
//
// On Windows:
// - The root directory itself is NOT watched. You will not receive events if the root directory is deleted
//   for example.
// - Windows is very sketchy about giving you events for directories. You might not get the events you'd
//   expect for creating a subdirectory for example.

struct DirectoryToWatch {
    String path;
    bool recursive;
    void* user_data;
};

struct DirectoryWatcher {
    union NativeData {
        void* pointer;
        int int_id;
    };

    using ChangeTypeFlags = u32;
    struct ChangeType {
        enum : ChangeTypeFlags {
            Added = 1 << 0,
            Deleted = 1 << 1,
            Modified = 1 << 2,
            RenamedOldName = 1 << 3,
            RenamedNewName = 1 << 4,
            RenamedOldOrNewName = 1 << 5, // (macOS only) we don't know if it was renamed to or from this name

            // if true, ignore all other changes and recursively rescan this directory
            ManualRescanNeeded = 1 << 6,
        };
        static constexpr DynamicArrayBounded<char, 200> ToString(ChangeTypeFlags c) {
            DynamicArrayBounded<char, 200> result;
            if (c & Added) dyn::AppendSpan(result, "Added, ");
            if (c & Deleted) dyn::AppendSpan(result, "Deleted, ");
            if (c & Modified) dyn::AppendSpan(result, "Modified, ");
            if (c & RenamedOldName) dyn::AppendSpan(result, "RenamedOldName, ");
            if (c & RenamedNewName) dyn::AppendSpan(result, "RenamedNewName, ");
            if (c & RenamedOldOrNewName) dyn::AppendSpan(result, "RenamedOldOrNewName, ");
            if (c & ManualRescanNeeded) dyn::AppendSpan(result, "ManualRescanNeeded, ");
            if (result.size) result.size -= 2;
            return result;
        }
    };

    struct SubpathChangeSet {
        bool IsSingleChange() const { return Popcount(changes) == 1; }

        // bitset
        ChangeTypeFlags changes;

        // relative to the watched directory, empty if the watched directory itself changed
        String subpath;

        // Might not be available. We get it for free on Linux and macOS but not on Windows.
        Optional<FileType> file_type;
    };

    struct DirectoryChanges {
        // private
        void Clear() {
            error = k_nullopt;
            subpath_changesets.Clear();
        }

        // private
        bool HasContent() const { return error || subpath_changesets.size; }

        // private
        struct Change {
            String subpath;
            Optional<FileType> file_type;
            ChangeTypeFlags changes;
        };

        // private
        void Add(Change change, ArenaAllocator& a) {
            ASSERT(IsValidUtf8(change.subpath));
            // try finding the subpath+file_type and add the change to it
            for (auto& subpath_changeset : subpath_changesets)
                // We check both subpath and file_type because a file can be deleted and then created as a
                // different type. We shouldn't coalesce in this case.
                if (path::Equal(subpath_changeset.subpath, change.subpath) &&
                    subpath_changeset.file_type == change.file_type) {
                    subpath_changeset.changes |= change.changes;
                    return;
                }

            // else, we create a new one
            subpath_changesets.Append(
                {
                    .changes = change.changes,
                    .subpath = change.subpath,
                    .file_type = change.file_type,
                },
                a);
        }

        // A pointer to the directory that you requested watching for. Allows you to more easily associate the
        // changes with a directory.
        DirectoryToWatch const* linked_dir_to_watch {};

        // An error occurred, events could be incomplete. What to do is probably dependent on the type of
        // error.
        Optional<ErrorCode> error;

        // Unordered list of changesets: one for each subpath that had changes. You will also get one of these
        // with an empty 'subpath' if the watched directory itself changed.
        ArenaStack<SubpathChangeSet> subpath_changesets {};
    };

    struct WatchedDirectory {
        enum class State {
            NeedsWatching,
            NeedsUnwatching,
            Watching,
            WatchingFailed,
            NotWatching,
        };

        ArenaAllocator arena;
        State state;
        String path;
        String resolved_path;
        bool recursive;

        DirectoryChanges directory_changes {}; // ephemeral
        bool is_desired {}; // ephemeral

        NativeData native_data;
    };

    // private
    void RemoveAllNotWatching() {
        watched_dirs.RemoveIf(
            [](WatchedDirectory const& dir) { return dir.state == WatchedDirectory::State::NotWatching; });
    }

    // private
    Span<DirectoryChanges const> AllDirectoryChanges(ArenaAllocator& result_arena) const {
        DynamicArray<DirectoryChanges> result(result_arena);
        for (auto const& dir : watched_dirs)
            if (dir.directory_changes.HasContent()) dyn::Append(result, dir.directory_changes);
        return result.ToOwnedSpan();
    }

    // private
    bool HandleWatchedDirChanges(Span<DirectoryToWatch const> dirs_to_watch, bool retry_failed_directories) {
        for (auto& dir : watched_dirs)
            dir.is_desired = false;

        bool any_states_changed = false;

        for (auto& dir_to_watch : dirs_to_watch) {
            if (auto dir_ptr = ({
                    DirectoryWatcher::WatchedDirectory* d = nullptr;
                    for (auto& dir : watched_dirs) {
                        if (path::Equal(dir.path, dir_to_watch.path) &&
                            dir.recursive == dir_to_watch.recursive) {
                            d = &dir;
                            break;
                        }
                    }
                    d;
                })) {
                dir_ptr->is_desired = true;
                dir_ptr->directory_changes.linked_dir_to_watch = &dir_to_watch;
                if (retry_failed_directories && dir_ptr->state == WatchedDirectory::State::WatchingFailed) {
                    dir_ptr->state = WatchedDirectory::State::NeedsWatching;
                    any_states_changed = true;
                }
                continue;
            }

            any_states_changed = true;

            auto new_dir = watched_dirs.PrependUninitialised(arena);
            PLACEMENT_NEW(new_dir)
            DirectoryWatcher::WatchedDirectory {
                .arena = {Malloc::Instance(), 0, 256},
                .state = DirectoryWatcher::WatchedDirectory::State::NeedsWatching,
                .recursive = dir_to_watch.recursive,
                .is_desired = true,
            };
            auto const path = new_dir->arena.Clone(dir_to_watch.path);
            new_dir->path = path;
            // Some backends (FSEvents) give us events containing paths with resolved symlinks, so we need
            // to resolve it ourselves to be able to correctly compare paths.
            new_dir->resolved_path = CanonicalizePath(new_dir->arena, dir_to_watch.path).ValueOr(path);
            new_dir->directory_changes.linked_dir_to_watch = &dir_to_watch;
        }

        for (auto& dir : watched_dirs)
            if (!dir.is_desired) {
                dir.state = DirectoryWatcher::WatchedDirectory::State::NeedsUnwatching;
                any_states_changed = true;
            }

        return any_states_changed;
    }

    Allocator& allocator;
    ArenaAllocator arena {allocator};
    ArenaList<WatchedDirectory> watched_dirs;
    NativeData native_data;
};

ErrorCodeOr<DirectoryWatcher> CreateDirectoryWatcher(Allocator& a);
void DestoryDirectoryWatcher(DirectoryWatcher& w);

struct PollDirectoryChangesArgs {
    Span<DirectoryToWatch const> dirs_to_watch;
    bool retry_failed_directories = false;
    double coalesce_latency_ms = 10; // macOS only
    ArenaAllocator& result_arena;
    ArenaAllocator& scratch_arena;
};

ErrorCodeOr<Span<DirectoryWatcher::DirectoryChanges const>>
PollDirectoryChanges(DirectoryWatcher& w, PollDirectoryChangesArgs args);
