// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/package_format.hpp"
#include "common_infrastructure/paths.hpp"
#include "common_infrastructure/preferences.hpp"

#include "sample_lib_server/sample_library_server.hpp"

// This is a higher-level API on top of package_format.hpp.
//
// It provides an API for multi-threaded code to install packages. It brings together other parts of the
// codebase such as the sample library server in order to make the best decisions when installing.

namespace package {

struct ExistingInstalledComponent {
    enum class VersionDifference : u8 {
        Equal, // Installed version is the same as the package version.
        InstalledIsOlder, // Installed version is older than the package version.
        InstalledIsNewer, // Installed version is newer than the package version.
    };
    enum class ModifiedSinceInstalled : u8 {
        Unmodified, // Installed version is known to be unmodified since it was installed.
        MaybeModified, // We don't know if the installed version has been modified since it was installed.
        Modified, // Installed version has been modified since it was installed.
    };
    using enum VersionDifference;
    using enum ModifiedSinceInstalled;
    bool operator==(ExistingInstalledComponent const& o) const = default;
    bool installed;
    VersionDifference version_difference; // if installed
    ModifiedSinceInstalled modified_since_installed; // if installed
};

PUBLIC bool32 UserInputIsRequired(ExistingInstalledComponent status) {
    return status.installed && status.modified_since_installed != ExistingInstalledComponent::Unmodified;
}

PUBLIC bool32 NoInstallationRequired(ExistingInstalledComponent status) {
    return status.installed && status.modified_since_installed == ExistingInstalledComponent::Unmodified &&
           (status.version_difference == ExistingInstalledComponent::Equal ||
            status.version_difference == ExistingInstalledComponent::InstalledIsNewer);
}

struct InstallJob {
    enum class State {
        Installing, // worker owns all data
        AwaitingUserInput, // worker thread is not running, user input needed
        DoneSuccess, // worker thread is not running, packages install completed
        DoneError, // worker thread is not running, packages install failed
    };

    enum class UserDecision {
        Unknown,
        Overwrite,
        Skip,
    };

    enum class InstallDestinationType {
        FolderNonExistent,
        FolderOverwritable,
        FileOverwritable,
    };

    ArenaAllocator& arena;
    Atomic<State> state {State::Installing};
    Atomic<bool> abort {false};
    String const path;
    Array<String, ToInt(ComponentType::Count)> const install_folders;
    sample_lib_server::Server& sample_lib_server;
    Span<String> const preset_folders;

    Optional<Reader> file_reader {};
    Optional<PackageReader> reader {}; // NOTE: needs uninit
    DynamicArray<char> error_buffer {arena};

    struct Component {
        package::Component component;
        ExistingInstalledComponent existing_installation_status {};
        UserDecision user_decision {UserDecision::Unknown};
        String install_filename {};
        bool install_allow_overwrite {};
        String install_folder {};
    };
    ArenaList<Component> components;
};

// ==========================================================================================================
//      _      _        _ _
//     | |    | |      (_) |
//   __| | ___| |_ __ _ _| |___
//  / _` |/ _ \ __/ _` | | / __|
// | (_| |  __/ || (_| | | \__ \
//  \__,_|\___|\__\__,_|_|_|___/
//
// ==========================================================================================================

namespace detail {

static ErrorCodeOr<ExistingInstalledComponent>
LibraryCheckExistingInstallation(Component const& component,
                                 sample_lib::Library const* existing_matching_library,
                                 ArenaAllocator& scratch_arena) {
    ASSERT_EQ(component.type, ComponentType::Library);
    ASSERT(component.library);

    if (!existing_matching_library) return ExistingInstalledComponent {.installed = false};

    if (existing_matching_library->file_format_specifics.tag == sample_lib::FileFormat::Mdata) {
        if (component.library->file_format_specifics.tag == sample_lib::FileFormat::Lua) {
            // MDATAs are a legacy format so a Lua library with the same ID must be newer.
            return ExistingInstalledComponent {
                .installed = true,
                .version_difference = ExistingInstalledComponent::InstalledIsOlder,
                .modified_since_installed = ExistingInstalledComponent::Unmodified,
            };
        } else {
            // We just assume that if the package MDATA is different from the installed MDATA, then it should
            // overwrite the existing. While MDATAs had versions, they were never used.
            return ExistingInstalledComponent {
                .installed = true,
                .version_difference = TRY(ChecksumForFile(existing_matching_library->path, scratch_arena)) ==
                                              *component.mdata_checksum
                                          ? ExistingInstalledComponent::Equal
                                          : ExistingInstalledComponent::InstalledIsOlder,
                .modified_since_installed = ExistingInstalledComponent::Unmodified,
            };
        }
    }

    auto const existing_folder = *path::Directory(existing_matching_library->path);
    ASSERT_EQ(existing_matching_library->id, component.library->id);

    auto const actual_checksums = TRY(ChecksumsForFolder(existing_folder, scratch_arena, scratch_arena));

    if (!ChecksumsDiffer(component.checksum_values, actual_checksums, k_nullopt))
        return ExistingInstalledComponent {
            .installed = true,
            .version_difference = ExistingInstalledComponent::Equal,
            .modified_since_installed = ExistingInstalledComponent::Unmodified,
        };

    ExistingInstalledComponent result = {.installed = true};

    auto const checksum_file_path = path::Join(scratch_arena, Array {existing_folder, k_checksums_file});
    if (auto const o = ReadEntireFile(checksum_file_path, scratch_arena); !o.HasError()) {
        auto const stored_checksums = ParseChecksumFile(o.Value(), scratch_arena);
        if (stored_checksums.HasValue() &&
            !ChecksumsDiffer(stored_checksums.Value(), actual_checksums, k_nullopt)) {
            result.modified_since_installed = ExistingInstalledComponent::Unmodified;
        } else {
            // The library has been modified since it was installed. OR the checksum file is badly formatted,
            // which presumably means it was modified.
            result.modified_since_installed = ExistingInstalledComponent::Modified;
        }
    } else {
        result.modified_since_installed = ExistingInstalledComponent::MaybeModified;
    }

    if (existing_matching_library->minor_version < component.library->minor_version)
        result.version_difference = ExistingInstalledComponent::InstalledIsOlder;
    else if (existing_matching_library->minor_version > component.library->minor_version)
        result.version_difference = ExistingInstalledComponent::InstalledIsNewer;
    else
        result.version_difference = ExistingInstalledComponent::Equal;

    return result;
}

// We don't actually check the checksums file of a presets folder. All we do is check if the exact files from
// the package are already installed. If there's any discrepancy, we just install the package again to a new
// folder. It means there could be duplicate files, but it's not a problem; preset files are tiny, and our
// preset system will ignore duplicate files by checking their checksums.
//
// We take this approach because there is no reason to overwrite preset files. Preset files are tiny. If
// there's a 'version 2' of a preset bank, then it might as well be installed alongside version 1.
static ErrorCodeOr<ExistingInstalledComponent>
PresetsCheckExistingInstallation(Component const& component,
                                 Span<String const> presets_folders,
                                 ArenaAllocator& scratch_arena) {
    for (auto const folder : presets_folders) {
        auto const entries = TRY(FindEntriesInFolder(scratch_arena,
                                                     folder,
                                                     {
                                                         .options {
                                                             .wildcard = "*",
                                                             .get_file_size = true,
                                                             .skip_dot_files = true,
                                                         },
                                                         .recursive = true,
                                                     }));

        if constexpr (IS_WINDOWS)
            for (auto& entry : entries)
                Replace(entry.subpath, '\\', '/');

        for (auto const dir_entry : entries) {
            if (dir_entry.type != FileType::Directory) continue;

            bool dir_contains_all_expected_files = true;
            for (auto const [expected_path, checksum, _] : component.checksum_values) {
                bool found_expected = false;
                for (auto const file_entry : entries) {
                    if (file_entry.type != FileType::File) continue;
                    auto const relative =
                        detail::RelativePathIfInFolder(file_entry.subpath, dir_entry.subpath);
                    if (!relative) continue;
                    if (path::Equal(*relative, expected_path)) {
                        found_expected = true;
                        break;
                    }
                }
                if (!found_expected) {
                    dir_contains_all_expected_files = false;
                    break;
                }
            }

            if (dir_contains_all_expected_files) {
                bool matches_exactly = true;

                // Check the checksums of all files.
                for (auto const [expected_path, checksum, _] : component.checksum_values) {
                    auto const cursor = scratch_arena.TotalUsed();
                    DEFER {
                        auto const new_used = scratch_arena.TryShrinkTotalUsed(cursor);
                        ASSERT(new_used >= cursor);
                    };

                    auto const full_path =
                        path::Join(scratch_arena, Array {folder, dir_entry.subpath, expected_path});

                    auto const matches_file = TRY(FileMatchesChecksum(full_path, checksum, scratch_arena));

                    if (!matches_file) {
                        matches_exactly = false;
                        break;
                    }
                }

                if (matches_exactly)
                    return ExistingInstalledComponent {
                        .installed = true,
                        .version_difference = ExistingInstalledComponent::Equal,
                        .modified_since_installed = ExistingInstalledComponent::Unmodified,
                    };
            }
        }
    }

    // It may actually be installed, but for presets we take the approach of just installing the package again
    // unless it is already exactly installed.
    return ExistingInstalledComponent {.installed = false};
}

// Returns the filename that doesn't conflict.
static ErrorCodeOr<String>
FindNextNonExistentFilename(String folder, String filename, ArenaAllocator& arena) {
    auto const does_not_exist = [&](String path) -> ErrorCodeOr<bool> {
        auto o = GetFileType(path);
        if (o.HasError()) {
            if (o.Error() == FilesystemError::PathDoesNotExist) return true;
            return o.Error();
        }
        return false;
    };

    folder = path::TrimDirectorySeparatorsEnd(folder);
    filename = path::TrimDirectorySeparatorsStart(filename);

    constexpr usize k_max_suffix_number = 999;
    constexpr usize k_max_suffix_str_size = " (999)"_s.size;

    auto buffer =
        arena.AllocateExactSizeUninitialised<char>(folder.size + 1 + filename.size + k_max_suffix_str_size);
    usize pos = 0;
    WriteAndIncrement(pos, buffer, folder);
    WriteAndIncrement(pos, buffer, path::k_dir_separator);
    auto const filename_start_pos = pos;
    WriteAndIncrement(pos, buffer, filename);

    if (TRY(does_not_exist({buffer.data, pos}))) return filename;

    pos = filename_start_pos;
    auto const ext = path::Extension(filename);

    auto filename_no_ext = TrimEndIfMatches(filename.SubSpan(0, filename.size - ext.size), ' ');
    usize suffix_num = 1;
    if (filename_no_ext.size && Last(filename_no_ext) == ')') {
        if (auto const open_paren = FindLast(filename_no_ext, '(')) {
            auto const num_str =
                filename_no_ext.SubSpan(*open_paren + 1, (filename_no_ext.size - 1) - (*open_paren + 1));
            if (num_str.size) {
                auto const num = ParseInt(num_str, ParseIntBase::Decimal, nullptr);
                if (num && *num >= 0) {
                    suffix_num = (usize)*num + 1;

                    // We have found a valid suffix, so remove the whole () part.
                    filename_no_ext.size = *open_paren;
                    filename_no_ext = TrimEndIfMatches(filename_no_ext, ' ');
                }
            }
        }
    }

    WriteAndIncrement(pos, buffer, filename_no_ext);
    if (filename_no_ext.size) WriteAndIncrement(pos, buffer, ' ');
    WriteAndIncrement(pos, buffer, '(');

    Optional<ErrorCode> error {};

    for (; suffix_num <= k_max_suffix_number; ++suffix_num) {
        usize initial_pos = pos;
        DEFER { pos = initial_pos; };

        pos +=
            fmt::IntToString(suffix_num, buffer.data + pos, {.base = fmt::IntToStringOptions::Base::Decimal});
        WriteAndIncrement(pos, buffer, ')');
        WriteAndIncrement(pos, buffer, ext);

        if (TRY(does_not_exist({buffer.data, pos})))
            return String {buffer.data + filename_start_pos, pos - filename_start_pos};
    }

    return error ? *error : ErrorCode {FilesystemError::FolderContainsTooManyFiles};
}

static ErrorCodeOr<void> ExtractFile(PackageReader& package, String file_path, String destination_path) {
    auto const find_file = [&](String file_path) -> ErrorCodeOr<mz_zip_archive_file_stat> {
        for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
            auto const file_stat = TRY(FileStat(package, file_index));
            if (FromNullTerminated(file_stat.m_filename) == file_path) return file_stat;
        }
        PanicIfReached();
        return ErrorCode {CommonError::NotFound};
    };

    auto const file_stat = find_file(file_path).Value();
    LogDebug(ModuleName::Package, "Extracting file: {} to {}", file_path, destination_path);
    auto out_file = TRY(OpenFile(destination_path, FileMode::WriteNoOverwrite()));
    return detail::ExtractFileToFile(package, file_stat, out_file);
}

static ErrorCodeOr<void> ExtractFolder(PackageReader& package,
                                       String dir_in_zip,
                                       String destination_folder,
                                       ArenaAllocator& scratch_arena,
                                       HashTable<String, ChecksumValues> destination_checksums) {
    LogInfo(ModuleName::Package, "extracting folder");
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
        auto const file_stat = TRY(detail::FileStat(package, file_index));
        if (file_stat.m_is_directory) continue;
        auto const path = PathWithoutTrailingSlash(file_stat.m_filename);
        auto const relative_path = detail::RelativePathIfInFolder(path, dir_in_zip);
        if (!relative_path) continue;

        auto const out_path = path::Join(scratch_arena, Array {destination_folder, *relative_path});
        DEFER { scratch_arena.Free(out_path.ToByteSpan()); };
        TRY(CreateDirectory(*path::Directory(out_path),
                            {
                                .create_intermediate_directories = true,
                                .fail_if_exists = false,
                            }));
        auto out_file = TRY(OpenFile(out_path, FileMode::WriteNoOverwrite()));
        TRY(detail::ExtractFileToFile(package, file_stat, out_file));
    }

    {
        auto const checksum_file_path =
            path::Join(scratch_arena, Array {destination_folder, k_checksums_file});
        TRY(CreateDirectory(*path::Directory(checksum_file_path),
                            {
                                .create_intermediate_directories = true,
                                .fail_if_exists = false,
                            }));
        TRY(WriteChecksumsValuesToFile(checksum_file_path,
                                       destination_checksums,
                                       scratch_arena,
                                       "Generated by Floe"));
    }

    return k_success;
}

static ErrorCodeOr<void> ReaderInstallComponent(PackageReader& package,
                                                InstallJob::Component const& component,
                                                ArenaAllocator& scratch_arena) {
    TRY(CreateDirectory(component.install_folder, {.create_intermediate_directories = true}));

    // Try to get a folder on the same filesystem so that we can atomic-rename.
    auto const temp_folder = ({
        String s {};
        if (auto const o = TemporaryDirectoryOnSameFilesystemAs(component.install_folder, scratch_arena);
            o.HasValue()) {
            s = o.Value();
        } else {
            // If we can't get a temporary directory on the same filesystem, we shall try to use a
            // standard directory - it might work. If not, then we will fail later.
            s = KnownDirectoryWithSubdirectories(scratch_arena,
                                                 KnownDirectoryType::Temporary,
                                                 Array {"Floe-Package-Install"_s},
                                                 k_nullopt,
                                                 {.create = true});
        }
        s;
    });
    DEFER {
        auto _ = Delete(temp_folder,
                        {
                            .type = DeleteOptions::Type::DirectoryRecursively,
                            .fail_if_not_exists = false,
                        });
    };

    auto const install_type = component.component.InstallType();

    // We extract to a temp folder than then rename to the final location. This ensures we either fail or
    // succeed, without any in-between cases where the folder is partially extracted. Additionally, it doesn't
    // generate lots of filesystem-change notifications which Floe might try to process and fail on.

    auto const temp_path = ({
        auto s = temp_folder;
        // Files need a filename, whereas folders can just use the temp folder directly, there's no need to
        // create a subfolder for them.
        if (install_type == FileType::File)
            s = path::Join(scratch_arena, Array {s, component.install_filename});
        s;
    });

    if (install_type == FileType::File) {
        TRY(detail::ExtractFile(package, component.component.path, temp_path));
    } else {
        TRY(detail::ExtractFolder(package,
                                  component.component.path,
                                  temp_path,
                                  scratch_arena,
                                  component.component.checksum_values));
    }

    auto installed_name = component.install_filename;
    auto allow_overwrite = component.install_allow_overwrite;

    // With files, Rename() will overwrite existing files, we need to check for that if overwrite is not
    // allowed. This is not ideal as it introduces a tiny window where another process could create the file
    // after we check and before we rename.
    if (!allow_overwrite && install_type == FileType::File)
        installed_name =
            TRY(FindNextNonExistentFilename(component.install_folder, installed_name, scratch_arena));

    DynamicArray<char> full_dest {component.install_folder, scratch_arena};
    for (auto const _ : Range(50)) {
        dyn::Resize(full_dest, component.install_folder.size);
        path::JoinAppend(full_dest, installed_name);
        if (auto const rename_o = Rename(temp_path, full_dest); rename_o.Succeeded()) {
            break;
        } else if (rename_o.Error() == FilesystemError::NotEmpty) {
            // The destination is a non-empty folder.
            if (allow_overwrite) {
                // Rather than overwrite files one-by-one (which will cause lots of filesystem events, and
                // potentially leave things in an incomplete state), we put the existing folder to one side
                // for a moment, install the new folder, and finally if that succeeds, move the old folder to
                // the Trash.

                // For moving aside the existing folder, we generate a unique, recognizable filename that will
                // be easy to spot in the Trash.
                String const new_name = ({
                    auto n = scratch_arena.AllocateExactSizeUninitialised<char>(full_dest.size +
                                                                                " (old-)"_s.size + 13);
                    usize pos = 0;
                    WriteAndIncrement(pos, n, full_dest);
                    WriteAndIncrement(pos, n, " (old-"_s);
                    auto const chars_written =
                        fmt::IntToString(RandomU64(package.seed),
                                         n.data + pos,
                                         {.base = fmt::IntToStringOptions::Base::Base32});
                    ASSERT(chars_written <= 13);
                    pos += chars_written;
                    WriteAndIncrement(pos, n, ')');
                    n.size = pos;

                    n;
                });

                // Move the existing folder out of the way.
                TRY(Rename(full_dest, new_name));

                // The old folder is out of the way so we can now install the new component.
                if (auto const rename2_o = Rename(temp_path, full_dest); rename2_o.HasError()) {
                    // We failed to install the new files, try to restore the old files.
                    auto const _ = Rename(new_name, full_dest);

                    return rename2_o.Error();
                }

                // The new component is installed, let's try to trash the old folder.
                if (auto const o = TrashFileOrDirectory(new_name, scratch_arena); o.HasError()) {
                    ErrorCodeOr<void> error = o.Error();

                    if (o.Error() == FilesystemError::NotSupported) {
                        // Trash is not supported, so just delete the old folder.
                        if (auto const delete_outcome =
                                Delete(new_name,
                                       {
                                           .type = DeleteOptions::Type::DirectoryRecursively,
                                           .fail_if_not_exists = false,
                                       });
                            delete_outcome.HasError())
                            error = delete_outcome.Error();
                        else
                            error = k_success;
                    }

                    if (error.HasError()) {
                        // Try to undo the rename
                        auto const _ = Rename(new_name, full_dest);

                        return error.Error();
                    }
                }

                break;
            } else {
                // Try a new name.
                installed_name =
                    TRY(FindNextNonExistentFilename(component.install_folder, installed_name, scratch_arena));
                continue;
            }
        } else if (rename_o.Error() == FilesystemError::PathIsAFile && allow_overwrite) {
            // The destination exists as a file. This can only happen with folder-to-file installs since
            // Rename() overwrites files automatically. We trash the existing file and try again.
            if (auto const trash_o = TrashFileOrDirectory(full_dest, scratch_arena);
                trash_o.HasError() && trash_o.Error() == FilesystemError::NotSupported) {
                TRY(Delete(full_dest,
                           {
                               .type = DeleteOptions::Type::File,
                               .fail_if_not_exists = false,
                           }));
            }

            // Additionally with this folder-to-file case, we rename the destination since we don't want to
            // end up with a folder that has a file's name with an extension.
            installed_name = path::Filename(component.component.path);
            allow_overwrite = false;

            continue;
        } else {
            // Other error.
            return rename_o.Error();
        }
    }

    // remove hidden
    TRY(WindowsSetFileAttributes(full_dest, k_nullopt));

    return k_success;
}

struct TryHelpersToState {
    static auto IsError(auto const& o) { return o.HasError(); }
    static auto ExtractError(auto const&) { return InstallJob::State::DoneError; }
    static auto ExtractValue(auto& o) { return o.ReleaseValue(); }
};

static InstallJob::State DoJobPhase1(InstallJob& job) {
    using H = package::detail::TryHelpersToState;

    job.file_reader = TRY_OR(Reader::FromFile(job.path), {
        fmt::Append(job.error_buffer, "Couldn't read file {}: {}\n", path::Filename(job.path), error);
        return InstallJob::State::DoneError;
    });

    job.reader = PackageReader {.zip_file_reader = *job.file_reader};

    TRY_H(ReaderInit(*job.reader));

    PackageComponentIndex it {};
    bool user_input_needed = false;
    u32 num_components = 0;
    constexpr u32 k_max_components = 4000;
    for (; num_components < k_max_components; ++num_components) {
        if (job.abort.Load(LoadMemoryOrder::Acquire)) {
            dyn::AppendSpan(job.error_buffer, "aborted\n");
            return InstallJob::State::DoneError;
        }

        auto const component = TRY_H(IteratePackageComponents(*job.reader, it, job.arena));
        if (!component) {
            // end of folders
            break;
        }

        String install_filename {};
        String install_folder {};
        bool install_allow_overwrite = false;

        auto const existing_check = ({
            ExistingInstalledComponent r;
            switch (component->type) {
                case package::ComponentType::Library: {
                    ASSERT(component->library);
                    sample_lib_server::RequestScanningOfUnscannedFolders(job.sample_lib_server);

                    auto const succeed =
                        sample_lib_server::WaitIfLibrariesAreLoading(job.sample_lib_server, 120u * 1000);
                    if (!succeed) {
                        ReportError(ErrorLevel::Error,
                                    SourceLocationHash(),
                                    "timed out waiting for sample libraries to be scanned");
                        return InstallJob::State::DoneError;
                    }

                    auto existing_lib =
                        sample_lib_server::FindLibraryRetained(job.sample_lib_server, component->library->id);
                    DEFER { existing_lib.Release(); };
                    LogDebug(ModuleName::Package,
                             "Checking existing installation of library {}, server returned {}",
                             component->library->id,
                             existing_lib ? "true" : "false");

                    r = TRY_H(
                        detail::LibraryCheckExistingInstallation(*component,
                                                                 existing_lib ? &*existing_lib : nullptr,
                                                                 job.arena));
                    LogDebug(ModuleName::Package,
                             "Existing installation status: installed={}, version_difference={}, "
                             "modified_since_installed={}",
                             r.installed,
                             r.version_difference,
                             r.modified_since_installed);

                    if (existing_lib) {
                        auto const path =
                            existing_lib->file_format_specifics.tag == sample_lib::FileFormat::Mdata
                                ? existing_lib->path
                                : *path::Directory(existing_lib->path);
                        install_filename = job.arena.Clone(path::Filename(path));
                        install_folder = job.arena.Clone(*path::Directory(path));
                        install_allow_overwrite = true; // Should we need to update, we allow overwriting.
                    } else {
                        install_filename = path::Filename(component->path);
                        install_folder = job.install_folders[ToInt(ComponentType::Library)];
                        install_allow_overwrite = false;
                    }

                    break;
                }
                case package::ComponentType::Presets: {
                    r = TRY_H(
                        detail::PresetsCheckExistingInstallation(*component, job.preset_folders, job.arena));
                    install_filename = path::Filename(component->path);
                    install_folder = job.install_folders[ToInt(ComponentType::Presets)];
                    install_allow_overwrite = false;
                    break;
                }
                case package::ComponentType::Count: PanicIfReached();
            }
            r;
        });

        if (UserInputIsRequired(existing_check)) user_input_needed = true;

        PLACEMENT_NEW(job.components.PrependUninitialised(job.arena))
        InstallJob::Component {
            .component = *component,
            .existing_installation_status = existing_check,
            .user_decision = InstallJob::UserDecision::Unknown,
            .install_filename = install_filename,
            .install_allow_overwrite = install_allow_overwrite,
            .install_folder = install_folder,
        };
    }

    if (num_components == k_max_components) {
        dyn::AppendSpan(job.error_buffer, "too many components in package\n");
        return InstallJob::State::DoneError;
    }

    if (user_input_needed) return InstallJob::State::AwaitingUserInput;

    return InstallJob::State::Installing;
}

static InstallJob::State DoJobPhase2(InstallJob& job) {
    using H = package::detail::TryHelpersToState;

    for (auto& component : job.components) {
        if (job.abort.Load(LoadMemoryOrder::Acquire)) {
            dyn::AppendSpan(job.error_buffer, "aborted\n");
            return InstallJob::State::DoneError;
        }

        if (NoInstallationRequired(component.existing_installation_status)) continue;

        if (UserInputIsRequired(component.existing_installation_status)) {
            ASSERT(component.user_decision != InstallJob::UserDecision::Unknown);
            if (component.user_decision == InstallJob::UserDecision::Skip) continue;
        }

        TRY_H(ReaderInstallComponent(*job.reader, component, job.arena));

        if (component.component.type == ComponentType::Library) {
            // The sample library server should receive filesystem-events about the move and rescan
            // automatically. But the timing of filesystem events is not reliable. As we already know that the
            // folder has changed, we can issue a rescan immediately. This way, the changes will be reflected
            // sooner.
            sample_lib_server::RescanFolder(job.sample_lib_server, component.install_folder);
        }
    }

    return InstallJob::State::DoneSuccess;
}

} // namespace detail

// ==========================================================================================================
//
//       _       _                _____ _____
//      | |     | |         /\   |  __ \_   _|
//      | | ___ | |__      /  \  | |__) || |
//  _   | |/ _ \| '_ \    / /\ \ |  ___/ | |
// | |__| | (_) | |_) |  / ____ \| |    _| |_
//  \____/ \___/|_.__/  /_/    \_\_|   |_____|
//
//
// ==========================================================================================================

struct CreateJobOptions {
    String zip_path;
    Array<String, ToInt(ComponentType::Count)> install_folders;
    sample_lib_server::Server& server;
    Span<String> preset_folders;
};

// [main thread]
PUBLIC InstallJob* CreateInstallJob(ArenaAllocator& arena, CreateJobOptions opts) {
    ASSERT(path::IsAbsolute(opts.zip_path));
    for (auto const& f : opts.install_folders)
        ASSERT(path::IsAbsolute(f));
    auto j = arena.NewUninitialised<InstallJob>();
    PLACEMENT_NEW(j)
    InstallJob {
        .arena = arena,
        .path = arena.Clone(opts.zip_path),
        .install_folders = ({
            Array<String, ToInt(ComponentType::Count)> f;
            for (auto const i : Range(ToInt(ComponentType::Count)))
                f[i] = arena.Clone(opts.install_folders[i]);
            f;
        }),
        .sample_lib_server = opts.server,
        .preset_folders = arena.Clone(opts.preset_folders, CloneType::Deep),
        .error_buffer = {arena},
        .components = {},
    };
    return j;
}

// [main thread]
PUBLIC void DestroyInstallJob(InstallJob* job) {
    ASSERT(job);
    ASSERT(job->state.Load(LoadMemoryOrder::Acquire) != InstallJob::State::Installing);
    if (job->reader) package::ReaderDeinit(*job->reader);
    job->~InstallJob();
}

PUBLIC void DoJobPhase2(InstallJob& job);

// Run this and then check the 'state' variable. You might need to ask the user a question on the main thread
// and then call OnAllUserInputReceived.
// [worker thread (probably)]
PUBLIC void DoJobPhase1(InstallJob& job) {
    ASSERT_EQ(job.state.Load(LoadMemoryOrder::Acquire), InstallJob::State::Installing);
    auto const result = detail::DoJobPhase1(job);
    LogDebug(ModuleName::Package, "DoJobPhase1 finished with state: {}", result);
    if (result != InstallJob::State::Installing) {
        job.state.Store(result, StoreMemoryOrder::Release);
        return;
    }

    DoJobPhase2(job);
}

// [worker thread (probably)]
PUBLIC void DoJobPhase2(InstallJob& job) {
    ASSERT_EQ(job.state.Load(LoadMemoryOrder::Acquire), InstallJob::State::Installing);
    auto const result = detail::DoJobPhase2(job);
    job.state.Store(result, StoreMemoryOrder::Release);
}

// Complete a job that was started but needed user input.
// [main thread]
PUBLIC void OnAllUserInputReceived(InstallJob& job, ThreadPool& thread_pool) {
    ASSERT_EQ(job.state.Load(LoadMemoryOrder::Acquire), InstallJob::State::AwaitingUserInput);
    for (auto& component : job.components)
        if (UserInputIsRequired(component.existing_installation_status))
            ASSERT(component.user_decision != InstallJob::UserDecision::Unknown);

    job.state.Store(InstallJob::State::Installing, StoreMemoryOrder::Release);
    thread_pool.AddJob([&job]() {
        try {
            package::DoJobPhase2(job);
        } catch (PanicException) {
            dyn::AppendSpan(job.error_buffer, "fatal error\n");
            job.state.Store(InstallJob::State::DoneError, StoreMemoryOrder::Release);
        }
    });
}

// [threadsafe]
PUBLIC String TypeOfActionTaken(ExistingInstalledComponent existing_installation_status,
                                InstallJob::UserDecision user_decision) {
    if (!existing_installation_status.installed) return "installed";

    if (UserInputIsRequired(existing_installation_status)) {
        switch (user_decision) {
            case InstallJob::UserDecision::Unknown: PanicIfReached();
            case InstallJob::UserDecision::Overwrite: {
                if (existing_installation_status.version_difference ==
                    ExistingInstalledComponent::InstalledIsOlder)
                    return "updated";
                else
                    return "overwritten";
            }
            case InstallJob::UserDecision::Skip: return "skipped";
        }
    }

    if (NoInstallationRequired(existing_installation_status)) {
        if (existing_installation_status.version_difference == ExistingInstalledComponent::InstalledIsNewer) {
            return "newer version already installed";
        } else {
            ASSERT(existing_installation_status.installed);
            return "already installed";
        }
    }

    if (existing_installation_status.installed &&
        existing_installation_status.version_difference == ExistingInstalledComponent::InstalledIsOlder &&
        existing_installation_status.modified_since_installed == ExistingInstalledComponent::Unmodified) {
        return "updated";
    }

    PanicIfReached();
}

// [main-thread]
PUBLIC String TypeOfActionTaken(InstallJob::Component const& component) {
    return TypeOfActionTaken(component.existing_installation_status, component.user_decision);
}

// ==========================================================================================================
//
//       _       _       _      _     _              _____ _____
//      | |     | |     | |    (_)   | |       /\   |  __ \_   _|
//      | | ___ | |__   | |     _ ___| |_     /  \  | |__) || |
//  _   | |/ _ \| '_ \  | |    | / __| __|   / /\ \ |  ___/ | |
// | |__| | (_) | |_) | | |____| \__ \ |_   / ____ \| |    _| |_
//  \____/ \___/|_.__/  |______|_|___/\__| /_/    \_\_|   |_____|
//
// ==========================================================================================================

struct ManagedInstallJob {
    ~ManagedInstallJob() {
        if (job) DestroyInstallJob(job);
    }
    ArenaAllocator arena {PageAllocator::Instance()};
    InstallJob* job {};
};

// The 'state' variable dictates who is allowed access to a job's data at any particular time: whether that's
// the main thread or a worker thread. We use a data structure that does not reallocate memory, so that we can
// safely push more jobs onto the list from the main thread, and give the worker thread a reference to the
// job.
using InstallJobs = BoundedList<ManagedInstallJob, 16>;

// [main thread]
PUBLIC void AddJob(InstallJobs& jobs,
                   String zip_path,
                   prefs::Preferences& prefs,
                   FloePaths const& paths,
                   ThreadPool& thread_pool,
                   ArenaAllocator& scratch_arena,
                   sample_lib_server::Server& sample_library_server) {
    ASSERT(!jobs.Full());
    ASSERT(path::IsAbsolute(zip_path));
    ASSERT(g_is_logical_main_thread);

    auto job = jobs.AppendUninitialised();
    PLACEMENT_NEW(job) ManagedInstallJob();
    job->job = CreateInstallJob(
        job->arena,
        CreateJobOptions {
            .zip_path = zip_path,
            .install_folders = ({
                Array<String, ToInt(ComponentType::Count)> fs;
                fs[ToInt(ComponentType::Library)] =
                    prefs::GetString(prefs,
                                     InstallLocationDescriptor(paths, prefs, ScanFolderType::Libraries));
                fs[ToInt(ComponentType::Presets)] =
                    prefs::GetString(prefs, InstallLocationDescriptor(paths, prefs, ScanFolderType::Presets));
                fs;
            }),
            .server = sample_library_server,
            .preset_folders =
                CombineStringArrays(scratch_arena,
                                    ExtraScanFolders(paths, prefs, ScanFolderType::Presets),
                                    Array {paths.always_scanned_folder[ToInt(ScanFolderType::Presets)]}),
        });
    thread_pool.AddJob([job]() {
        try {
            package::DoJobPhase1(*job->job);
        } catch (PanicException) {
            dyn::AppendSpan(job->job->error_buffer, "fatal error\n");
            job->job->state.Store(InstallJob::State::DoneError, StoreMemoryOrder::Release);
        }
    });
}

// [main thread]
PUBLIC InstallJobs::Iterator RemoveJob(InstallJobs& jobs, InstallJobs::Iterator it) {
    ASSERT(g_is_logical_main_thread);
    auto const state = it->job->state.Load(LoadMemoryOrder::Acquire);
    ASSERT(state == InstallJob::State::DoneError || state == InstallJob::State::DoneSuccess);

    return jobs.Remove(it);
}

// Stalls until all jobs are done.
// [main thread]
PUBLIC void ShutdownJobs(InstallJobs& jobs) {
    ASSERT(g_is_logical_main_thread);
    if (jobs.Empty()) return;

    for (auto& j : jobs)
        j.job->abort.Store(true, StoreMemoryOrder::Release);

    u32 wait_ms = 0;
    constexpr u32 k_sleep_ms = 100;
    constexpr u32 k_timeout_ms = 120 * 1000;

    for (; wait_ms < k_timeout_ms; wait_ms += k_sleep_ms) {
        bool jobs_are_installing = false;
        for (auto& j : jobs) {
            if (j.job->state.Load(LoadMemoryOrder::Acquire) == InstallJob::State::Installing) {
                jobs_are_installing = true;
                break;
            }
        }

        if (!jobs_are_installing) break;

        SleepThisThread(k_sleep_ms);
    }

    ASSERT(wait_ms < k_timeout_ms);

    jobs.RemoveAll();
}

} // namespace package
