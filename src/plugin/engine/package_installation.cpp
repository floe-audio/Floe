// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "package_installation.hpp"

#include "tests/framework.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/error_reporting.hpp"

namespace fmt {

static ErrorCodeOr<void>
CustomValueToString(Writer writer, package::ExistingInstalledComponent value, FormatOptions) {
    return FormatToWriter(writer, "{}", DumpStruct(value));
}

static ErrorCodeOr<void>
CustomValueToString(Writer writer, package::InstallJob::State state, FormatOptions o) {
    String s = {"Unknown"};
    switch (state) {
        case package::InstallJob::State::Installing: s = "Installing"; break;
        case package::InstallJob::State::AwaitingUserInput: s = "AwaitingUserInput"; break;
        case package::InstallJob::State::DoneSuccess: s = "DoneSuccess"; break;
        case package::InstallJob::State::DoneError: s = "DoneError"; break;
    }
    return ValueToString(writer, s, o);
}

} // namespace fmt

namespace package {

bool32 UserInputIsRequired(ExistingInstalledComponent status) {
    return status.installed && status.modified_since_installed != ModifiedSinceInstalled::Unmodified;
}

bool32 NoInstallationRequired(ExistingInstalledComponent status) {
    return status.installed &&
           (status.modified_since_installed == ModifiedSinceInstalled::Unmodified ||
            status.modified_since_installed == ModifiedSinceInstalled::UnmodifiedButFilesAdded) &&
           (status.version_difference == VersionDifference::Equal ||
            status.version_difference == VersionDifference::InstalledIsNewer);
}

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
                .version_difference = VersionDifference::InstalledIsOlder,
                .modified_since_installed = ModifiedSinceInstalled::Unmodified,
            };
        } else {
            // We just assume that if the package MDATA is different from the installed MDATA, then it should
            // overwrite the existing. While MDATAs had versions, they were never used.
            return ExistingInstalledComponent {
                .installed = true,
                .version_difference = TRY(ChecksumForFile(existing_matching_library->path, scratch_arena)) ==
                                              *component.mdata_checksum
                                          ? VersionDifference::Equal
                                          : VersionDifference::InstalledIsOlder,
                .modified_since_installed = ModifiedSinceInstalled::Unmodified,
            };
        }
    }

    auto const existing_folder = *path::Directory(existing_matching_library->path);
    ASSERT_EQ(existing_matching_library->id, component.library->id);

    auto actual_checksums = TRY(ChecksumsForFolder(existing_folder, scratch_arena, scratch_arena));
    actual_checksums.RemoveIf([](auto const& key, auto const&) { return key == k_checksums_file; });

    if (CompareChecksums(component.checksum_values,
                         actual_checksums,
                         {.test_table_allowed_extra_files = true}) != CompareChecksumsResult::Differ)
        return ExistingInstalledComponent {
            .installed = true,
            .version_difference = VersionDifference::Equal,
            .modified_since_installed = ModifiedSinceInstalled::Unmodified,
        };

    return ExistingInstalledComponent {
        .installed = true,
        .version_difference = ({
            VersionDifference v {};
            if (existing_matching_library->minor_version < component.library->minor_version)
                v = VersionDifference::InstalledIsOlder;
            else if (existing_matching_library->minor_version > component.library->minor_version)
                v = VersionDifference::InstalledIsNewer;
            else
                v = VersionDifference::Equal;
            v;
        }),
        .modified_since_installed = ({
            ModifiedSinceInstalled m {};

            if (auto const o =
                    ReadEntireFile(path::Join(scratch_arena, Array {existing_folder, k_checksums_file}),
                                   scratch_arena);
                !o.HasError()) {
                if (auto const stored_checksums = ParseChecksumFile(o.Value(), scratch_arena);
                    stored_checksums.HasValue()) {
                    switch (CompareChecksums(stored_checksums.Value(),
                                             actual_checksums,
                                             {
                                                 .test_table_allowed_extra_files = true,
                                             })) {
                        case CompareChecksumsResult::Same: {
                            m = ModifiedSinceInstalled::Unmodified;
                            break;
                        }
                        case CompareChecksumsResult::SameButHasExtraFiles: {
                            m = ModifiedSinceInstalled::UnmodifiedButFilesAdded;
                            break;
                        }
                        case CompareChecksumsResult::Differ: {
                            m = ModifiedSinceInstalled::Modified;
                            break;
                        }
                    }
                } else {
                    // The checksum file is badly formatted, which presumably means it was modified.
                    m = ModifiedSinceInstalled::Modified;
                }
            } else {
                // We couldn't read the existing checksum (maybe it doesn't exist).
                m = ModifiedSinceInstalled::MaybeModified;
            }
            m;
        }),
    };
}

struct ParsedFilename {
    String filename_no_ext; // Filename without extension or suffix
    String ext; // File extension including the dot
    Optional<usize> suffix_num; // The numeric suffix found in " (N)" format, or nullopt if none
};

// Parses a filename to extract the base name (without extension), extension, and any existing numeric
// suffix in the form " (N)". For example:
// - "file.txt" -> {filename_no_ext: "file", ext: ".txt", suffix_num: nullopt}
// - "file (3).txt" -> {filename_no_ext: "file", ext: ".txt", suffix_num: 3}
// - "file (invalid).txt" -> {filename_no_ext: "file (invalid)", ext: ".txt", suffix_num: nullopt}
static ParsedFilename ParseFilenameWithSuffix(String filename) {
    auto const ext = path::Extension(filename);
    auto filename_no_ext = WhitespaceStrippedEnd(filename.SubSpan(0, filename.size - ext.size));
    Optional<usize> suffix_num {};

    if (filename_no_ext.size && Last(filename_no_ext) == ')') {
        if (auto const open_paren = FindLast(filename_no_ext, '(')) {
            auto const num_str =
                filename_no_ext.SubSpan(*open_paren + 1, (filename_no_ext.size - 1) - (*open_paren + 1));
            if (num_str.size) {
                auto const num = ParseInt(num_str, ParseIntBase::Decimal, nullptr);
                if (num && *num >= 0) {
                    suffix_num = (usize)*num;

                    // We have found a valid suffix, so remove the whole () part.
                    filename_no_ext.size = *open_paren;
                    filename_no_ext = WhitespaceStrippedEnd(filename_no_ext);
                }
            }
        }
    }

    return ParsedFilename {
        .filename_no_ext = filename_no_ext,
        .ext = ext,
        .suffix_num = suffix_num,
    };
}

// Writes a filename with a numeric suffix into a buffer. The buffer must have enough space for the
// filename, suffix, and extension. Returns the number of bytes written.
// For example: WriteFilenameWithSuffix("file", ".txt", 3, buffer) writes "file (3).txt" to buffer.
static usize
WriteFilenameWithSuffix(String filename_no_ext, String ext, usize suffix_num, Span<char> buffer) {
    usize pos = 0;
    WriteAndIncrement(pos, buffer, filename_no_ext);
    if (filename_no_ext.size) WriteAndIncrement(pos, buffer, ' ');
    WriteAndIncrement(pos, buffer, '(');
    pos += fmt::IntToString(suffix_num, buffer.data + pos, {.base = fmt::IntToStringOptions::Base::Decimal});
    WriteAndIncrement(pos, buffer, ')');
    WriteAndIncrement(pos, buffer, ext);
    return pos;
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

    // First try the filename as-is.
    {
        WriteAndIncrement(pos, buffer, filename);
        if (TRY(does_not_exist({buffer.data, pos}))) return filename;
    }

    // Next, try with suffixes.
    auto const parsed = ParseFilenameWithSuffix(filename);
    usize suffix_num = parsed.suffix_num.ValueOr(1) + 1;

    Optional<ErrorCode> error {};

    for (; suffix_num <= k_max_suffix_number; ++suffix_num) {
        auto const filename_size = WriteFilenameWithSuffix(parsed.filename_no_ext,
                                                           parsed.ext,
                                                           suffix_num,
                                                           buffer.SubSpan(filename_start_pos));
        auto const full_path_size = filename_start_pos + filename_size;

        if (TRY(does_not_exist({buffer.data, full_path_size})))
            return String {buffer.data + filename_start_pos, filename_size};
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
    return ExtractFileToFile(package, file_stat, out_file);
}

static ErrorCodeOr<void> ExtractFolder(PackageReader& package,
                                       String dir_in_zip,
                                       String destination_folder,
                                       ArenaAllocator& scratch_arena,
                                       HashTable<String, ChecksumValues> destination_checksums) {
    LogInfo(ModuleName::Package, "extracting folder");
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
        auto const file_stat = TRY(FileStat(package, file_index));
        if (file_stat.m_is_directory) continue;
        auto const path = PathWithoutTrailingSlash(file_stat.m_filename);
        auto const relative_path = RelativePathIfInFolder(path, dir_in_zip);
        if (!relative_path) continue;

        auto const out_path = path::Join(scratch_arena, Array {destination_folder, *relative_path});
        DEFER { scratch_arena.Free(out_path.ToByteSpan()); };
        TRY(CreateDirectory(*path::Directory(out_path),
                            {
                                .create_intermediate_directories = true,
                                .fail_if_exists = false,
                            }));
        auto out_file = TRY(OpenFile(out_path, FileMode::WriteNoOverwrite()));
        TRY(ExtractFileToFile(package, file_stat, out_file));
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
                                                package::Component const& component,
                                                ComponentInstallConfig const& config,
                                                ArenaAllocator& scratch_arena) {
    TRY(CreateDirectory(config.folder, {.create_intermediate_directories = true}));

    // Try to get a folder on the same filesystem so that we can atomic-rename.
    auto const temp_folder = ({
        String s {};
        if (auto const o = TemporaryDirectoryOnSameFilesystemAs(config.folder, scratch_arena); o.HasValue()) {
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

    auto const install_type = component.InstallFileType();

    // We extract to a temp folder than then rename to the final location. This ensures we either fail or
    // succeed, without any in-between cases where the folder is partially extracted. Additionally, it doesn't
    // generate lots of filesystem-change notifications which Floe might try to process and fail on.

    auto const temp_path = ({
        auto s = temp_folder;
        // Files need a filename, whereas folders can just use the temp folder directly, there's no need to
        // create a subfolder for them.
        if (install_type == FileType::File) s = path::Join(scratch_arena, Array {s, config.filename});
        s;
    });

    if (install_type == FileType::File)
        TRY(ExtractFile(package, component.path, temp_path));
    else
        TRY(ExtractFolder(package, component.path, temp_path, scratch_arena, component.checksum_values));

    auto installed_name = config.filename;
    auto allow_overwrite = config.allow_overwrite;

    // If we've been requested to not overwrite _files_, we need to handle that before because Rename() always
    // overwrites files automatically. Doing this before is not ideal as it introduces a tiny window where
    // another process could create the file after we check and before we rename.
    if (!allow_overwrite && install_type == FileType::File)
        installed_name = TRY(FindNextNonExistentFilename(config.folder, installed_name, scratch_arena));

    DynamicArray<char> full_dest {config.folder, scratch_arena};
    for (auto const _ : Range(50)) {
        dyn::Resize(full_dest, config.folder.size);
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
                String const existing_folder_path = ({
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
                TRY(Rename(full_dest, existing_folder_path));

                // The old folder is out of the way so we can now install the new component.
                if (auto const rename2_o = Rename(temp_path, full_dest); rename2_o.HasError()) {
                    // We failed to install the new files, try to restore the old files.
                    auto const _ = Rename(existing_folder_path, full_dest);

                    return rename2_o.Error();
                }

                // The new component is installed, let's try to trash the existing folder.
                if (auto const o = TrashFileOrDirectory(existing_folder_path, scratch_arena); o.HasError()) {
                    ErrorCodeOr<void> error = o.Error();

                    if (o.Error() == FilesystemError::NotSupported) {
                        // Trash is not supported, so just delete the existing folder.
                        if (auto const delete_outcome =
                                Delete(existing_folder_path,
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
                        // Try to restore the existing folder.
                        auto const _ = Rename(existing_folder_path, full_dest);

                        return error.Error();
                    }
                }

                break;
            } else {
                // Try a new name.
                installed_name =
                    TRY(FindNextNonExistentFilename(config.folder, installed_name, scratch_arena));
                continue;
            }
        } else if (rename_o.Error() == FilesystemError::PathIsAFile) {
            // The destination exists as a file. This can only happen with folder-to-file installs since
            // Rename() handles file-to-file overwrites automatically.

            if (allow_overwrite) {
                if (auto const trash_o = TrashFileOrDirectory(full_dest, scratch_arena);
                    trash_o.HasError() && trash_o.Error() == FilesystemError::NotSupported) {
                    TRY(Delete(full_dest,
                               {
                                   .type = DeleteOptions::Type::File,
                                   .fail_if_not_exists = false,
                               }));
                }

                // We have handled the overwrite case now, we don't want the next step to overwrite something
                // unrelated.
                allow_overwrite = false;
            }

            // We know this is a folder-to-file case, and we therefore don't want the final folder to have the
            // same name as original file (it would be strange to have a file extension for a folder).
            installed_name = path::Filename(component.path);

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

static bool MirageIsInstalled() {
    if constexpr (IS_LINUX) return false; // Mirage wasn't available for Linux.

    Span<String const> possible_paths {};

    if constexpr (IS_MACOS) {
        static constexpr auto k_paths = Array {
            "/Library/Audio/Plug-Ins/VST/Mirage.vst"_s,
            "/Library/Audio/Plug-Ins/Components/FrozenPlain Mirage.component"_s,
        };
        possible_paths = k_paths;
    }

    if constexpr (IS_WINDOWS) {
        static constexpr auto k_paths = Array {
            "C:\\Program Files\\VSTPlugins\\mirage64.dll"_s,
            "C:\\Program Files\\Steinberg\\VSTPlugins\\mirage64.dll",
            "C:\\Program Files\\Common Files\\VST2\\mirage64.dll",
            "C:\\Program Files\\Common Files\\Steinberg\\VST2\\mirage64.dll",
        };
        possible_paths = k_paths;
    }

    for (auto const p : possible_paths)
        if (GetFileType(p).HasValue()) return true;

    return false;
}

static InstallJob::State DoJobPhase1Impl(InstallJob& job) {
    using H = package::TryHelpersToState;

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
        if (!component) break; // No more folders.

        ComponentInstallConfig install_config {};
        ExistingInstalledComponent existing_check {};

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

                existing_check =
                    TRY_H(LibraryCheckExistingInstallation(*component,
                                                           existing_lib ? &*existing_lib : nullptr,
                                                           job.arena));
                LogDebug(ModuleName::Package,
                         "Existing installation status: installed={}, version_difference={}, "
                         "modified_since_installed={}",
                         existing_check.installed,
                         existing_check.version_difference,
                         existing_check.modified_since_installed);

                if (existing_lib) {
                    auto const path = existing_lib->file_format_specifics.tag == sample_lib::FileFormat::Mdata
                                          ? existing_lib->path
                                          : *path::Directory(existing_lib->path);
                    install_config = {
                        .filename = job.arena.Clone(path::Filename(path)),
                        .folder = job.arena.Clone(*path::Directory(path)),
                        .allow_overwrite = ({
                            bool allow = true; // Should we need to update, we allow overwriting.

                            if (existing_lib->file_format_specifics.tag == sample_lib::FileFormat::Mdata &&
                                component->library->file_format_specifics.tag ==
                                    sample_lib::FileFormat::Lua) {
                                // When upgrading from a Mirage library to a Lua library, we don't want to
                                // overwrite it if Mirage is still installed because it would break Mirage's
                                // usage of the library.
                                if (MirageIsInstalled()) allow = false;
                            }

                            allow;
                        }),
                    };
                } else {
                    install_config = {
                        .filename = path::Filename(component->path),
                        .folder = job.install_folders[ToInt(ComponentType::Library)],
                        .allow_overwrite = false,
                    };
                }

                break;
            }
            case package::ComponentType::Presets: {
                StartScanningIfNeeded(job.preset_server);
                auto const succeed = WaitIfFoldersAreScanning(job.preset_server, 120u * 1000);
                if (!succeed) {
                    ReportError(ErrorLevel::Error,
                                SourceLocationHash(),
                                "timed out waiting for presets folders to be scanned");
                    return InstallJob::State::DoneError;
                }

                ArenaAllocatorWithInlineStorage<4000> scratch_arena {PageAllocator::Instance()};

                auto const [snapshot, handle] = BeginReadFolders(job.preset_server, scratch_arena);
                DEFER { EndReadFolders(job.preset_server, handle); };

                bool matched = false;

                if (component->preset_bank) {
                    ASSERT(component->preset_bank->id != k_misc_bank_id);

                    for (auto const [index, listing] : Enumerate(snapshot.banks)) {
                        auto const existing_bank = PresetBankAtNode(listing->node);
                        ASSERT(existing_bank);
                        auto const path = *FolderPath(&listing->node, scratch_arena);

                        if (existing_bank->id == component->preset_bank->id) {
                            existing_check = {
                                .installed = true,
                                .version_difference = ({
                                    VersionDifference d {};
                                    if (existing_bank->minor_version == component->preset_bank->minor_version)
                                        d = VersionDifference::Equal;
                                    else if (existing_bank->minor_version <
                                             component->preset_bank->minor_version)
                                        d = VersionDifference::InstalledIsOlder;
                                    else
                                        d = VersionDifference::InstalledIsNewer;
                                    d;
                                }),
                                .modified_since_installed = ({
                                    ModifiedSinceInstalled m {};

                                    if (auto const o = ReadEntireFile(
                                            path::Join(scratch_arena, Array {path, k_checksums_file}),
                                            scratch_arena);
                                        !o.HasError()) {
                                        if (auto const stored_checksums =
                                                ParseChecksumFile(o.Value(), scratch_arena);
                                            stored_checksums.HasValue()) {

                                            auto actual_checksums =
                                                TRY_H(ChecksumsForFolder(path, scratch_arena, scratch_arena));
                                            actual_checksums.RemoveIf([](auto const& key, auto const&) {
                                                return key == k_checksums_file;
                                            });

                                            switch (
                                                CompareChecksums(stored_checksums.Value(),
                                                                 actual_checksums,
                                                                 {
                                                                     .test_table_allowed_extra_files = true,
                                                                 })) {
                                                case CompareChecksumsResult::Same: {
                                                    m = ModifiedSinceInstalled::Unmodified;
                                                    break;
                                                }
                                                case CompareChecksumsResult::SameButHasExtraFiles: {
                                                    m = ModifiedSinceInstalled::UnmodifiedButFilesAdded;
                                                    break;
                                                }
                                                case CompareChecksumsResult::Differ: {
                                                    m = ModifiedSinceInstalled::Modified;
                                                    break;
                                                }
                                            }
                                        } else {
                                            // The checksum file is badly formatted, which presumably means it
                                            // was modified.
                                            m = ModifiedSinceInstalled::Modified;
                                        }
                                    } else {
                                        // We couldn't read the existing checksum (maybe it doesn't exist).
                                        m = ModifiedSinceInstalled::MaybeModified;
                                    }
                                    m;
                                }),
                            };
                            install_config = {
                                .filename = job.arena.Clone(path::Filename(path)),
                                .folder = job.arena.Clone(*path::Directory(path)),
                                .allow_overwrite = ({
                                    bool allow = true;

                                    if (HasNestedBank(listing->node))
                                        allow = false;
                                    else if (MirageIsInstalled())
                                        // Disallow overwriting Mirage presets if Mirage is in use.
                                        ForEachNode(
                                            const_cast<FolderNode*>(&listing->node),
                                            [&allow](FolderNode* n) {
                                                if (!allow) return;
                                                if (auto l = n->user_data.As<PresetFolderListing const>()) {
                                                    if (l->folder) {
                                                        for (auto const& p : l->folder->presets) {
                                                            if (p.file_format == PresetFormat::Mirage) {
                                                                allow = false;
                                                                break;
                                                            }
                                                        }
                                                    }
                                                }
                                            });

                                    allow;
                                }),
                            };
                            matched = true;
                            break;
                        }
                    }
                } else {
                    // The incoming presets are not a bank. Let's just scan the currently installed banks to
                    // find an exact match of the files (ignoring folder structure), in which case we can say
                    // it's already installed.
                    for (auto const [index, listing] : Enumerate(snapshot.banks)) {
                        auto const existing_bank = PresetBankAtNode(listing->node);
                        ASSERT(existing_bank);
                        auto const path = *FolderPath(&listing->node, scratch_arena);

                        if (CompareChecksums(component->checksum_values,
                                             TRY_H(ChecksumsForFolder(path, scratch_arena, scratch_arena)),
                                             {
                                                 .ignore_path_nesting = true,
                                                 .test_table_allowed_extra_files = true,
                                             }) != CompareChecksumsResult::Differ) {
                            // We have found all the component's files exactly. They might have a different
                            // folder nesting structure, and it might contain more folders but we still say
                            // the component is fully installed.
                            existing_check = {
                                .installed = true,
                                .version_difference = VersionDifference::Equal,
                                .modified_since_installed = ModifiedSinceInstalled::Unmodified,
                            };
                            install_config = {}; // Irrelevant.
                            matched = true;
                            break;
                        }
                    }
                }

                if (!matched) {
                    // We cannot find an installed bank matching our existing one.
                    existing_check = {
                        .installed = false,
                    };
                    install_config = {
                        .filename = path::Filename(component->path),
                        .folder = job.install_folders[ToInt(ComponentType::Presets)],
                        .allow_overwrite = false,
                    };
                }

                break;
            }
            case package::ComponentType::Count: PanicIfReached();
        }

        if (UserInputIsRequired(existing_check)) user_input_needed = true;

        PLACEMENT_NEW(job.components.PrependUninitialised(job.arena))
        InstallJob::Component {
            .component = *component,
            .existing_installation_status = existing_check,
            .user_decision = InstallJob::UserDecision::Unknown,
            .install_config = install_config,
        };
    }

    if (num_components == k_max_components) {
        dyn::AppendSpan(job.error_buffer, "too many components in package\n");
        return InstallJob::State::DoneError;
    }

    if (user_input_needed) return InstallJob::State::AwaitingUserInput;

    return InstallJob::State::Installing;
}

static InstallJob::State DoJobPhase2Impl(InstallJob& job) {
    using H = package::TryHelpersToState;

    for (auto& component : job.components) {
        if (job.abort.Load(LoadMemoryOrder::Acquire)) {
            dyn::AppendSpan(job.error_buffer, "aborted\n");
            return InstallJob::State::DoneError;
        }

        if (NoInstallationRequired(component.existing_installation_status)) continue;

        if (UserInputIsRequired(component.existing_installation_status)) {
            switch (component.user_decision) {
                case InstallJob::UserDecision::Skip: continue;
                case InstallJob::UserDecision::InstallCopy:
                    component.install_config.allow_overwrite = false;
                    component.install_config.folder = job.install_folders[ToInt(component.component.type)];
                    break;
                case InstallJob::UserDecision::Overwrite:
                    component.install_config.allow_overwrite = true;
                    break;
                case InstallJob::UserDecision::Unknown: PanicIfReached();
            }
        }

        TRY_H(ReaderInstallComponent(*job.reader, component.component, component.install_config, job.arena));

        switch (component.component.type) {
            case ComponentType::Library: {
                // The sample library server should receive filesystem-events about the move and rescan
                // automatically. But the timing of filesystem events is not reliable. As we already know that
                // the folder has changed, we can issue a rescan immediately. This way, the changes will be
                // reflected sooner.
                sample_lib_server::RescanFolder(job.sample_lib_server, component.install_config.folder);
                break;
            }
            case ComponentType::Presets: {
                RescanFolder(job.preset_server, component.install_config.folder);
                break;
            }
            case ComponentType::Count: PanicIfReached();
        }
    }

    return InstallJob::State::DoneSuccess;
}

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

InstallJob* CreateInstallJob(ArenaAllocator& arena, CreateJobOptions opts) {
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
        .sample_lib_server = opts.sample_lib_server,
        .preset_server = opts.preset_server,
        .error_buffer = {arena},
        .components = {},
    };
    return j;
}

void DestroyInstallJob(InstallJob* job) {
    ASSERT(job);
    ASSERT(job->state.Load(LoadMemoryOrder::Acquire) != InstallJob::State::Installing);
    if (job->reader) package::ReaderDeinit(*job->reader);
    job->~InstallJob();
}

void DoJobPhase2(InstallJob& job);

// Run this and then check the 'state' variable. You might need to ask the user a question on the main thread
// and then call OnAllUserInputReceived.
// [worker thread (probably)]
void DoJobPhase1(InstallJob& job) {
    ASSERT_EQ(job.state.Load(LoadMemoryOrder::Acquire), InstallJob::State::Installing);
    auto const result = DoJobPhase1Impl(job);
    LogDebug(ModuleName::Package, "DoJobPhase1 finished with state: {}", result);
    if (result != InstallJob::State::Installing) {
        job.state.Store(result, StoreMemoryOrder::Release);
        return;
    }

    DoJobPhase2(job);
}

// [worker thread (probably)]
void DoJobPhase2(InstallJob& job) {
    ASSERT_EQ(job.state.Load(LoadMemoryOrder::Acquire), InstallJob::State::Installing);
    auto const result = DoJobPhase2Impl(job);
    job.state.Store(result, StoreMemoryOrder::Release);
}

// Complete a job that was started but needed user input.
// [main thread]
void OnAllUserInputReceived(InstallJob& job, ThreadPool& thread_pool) {
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
String TypeOfActionTaken(ExistingInstalledComponent existing_installation_status,
                         InstallJob::UserDecision user_decision) {
    if (!existing_installation_status.installed) return "installed";

    if (UserInputIsRequired(existing_installation_status)) {
        switch (user_decision) {
            case InstallJob::UserDecision::Unknown: PanicIfReached();
            case InstallJob::UserDecision::Overwrite: {
                if (existing_installation_status.version_difference == VersionDifference::InstalledIsOlder)
                    return "updated";
                else
                    return "overwritten";
            }
            case InstallJob::UserDecision::InstallCopy: return "installed as copy";
            case InstallJob::UserDecision::Skip: return "skipped";
        }
    }

    if (NoInstallationRequired(existing_installation_status)) {
        if (existing_installation_status.version_difference == VersionDifference::InstalledIsNewer) {
            return "newer version already installed";
        } else {
            ASSERT(existing_installation_status.installed);
            return "already installed";
        }
    }

    if (existing_installation_status.installed &&
        existing_installation_status.version_difference == VersionDifference::InstalledIsOlder &&
        existing_installation_status.modified_since_installed == ModifiedSinceInstalled::Unmodified) {
        return "updated";
    }

    PanicIfReached();
}

// [main-thread]
String TypeOfActionTaken(InstallJob::Component const& component) {
    return TypeOfActionTaken(component.existing_installation_status, component.user_decision);
}

// ==========================================================================================================
//
//       _       _       _      _     _              _____ _____
//      | |     | |     | |    (_)   | |       /\   |  __ \_   _|
//      | | ___ | |__   | |     _ ___| |_     /  \  | |__) || |
//  _   | |/ _ \| '_ \  | |    | / __| __|   / /\ \ |  ___/ | |
// | |__| | (_) | |_) | | |____| \__ \ |_   / ____ \| |    _| |_
//  \____/ \___/|_.__/  |______|_|___\__| /_/    \_\_|   |_____|
//
// ==========================================================================================================

// [main thread]
void AddJob(InstallJobs& jobs,
            String zip_path,
            prefs::Preferences& prefs,
            FloePaths const& paths,
            sample_lib_server::Server& sample_library_server,
            PresetServer& preset_server) {
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
            .sample_lib_server = sample_library_server,
            .preset_server = preset_server,
        });

    Thread thread;
    thread.Start(
        [job]() {
            try {
                package::DoJobPhase1(*job->job);
            } catch (PanicException) {
                dyn::AppendSpan(job->job->error_buffer, "fatal error\n");
                job->job->state.Store(InstallJob::State::DoneError, StoreMemoryOrder::Release);
            }
        },
        "pkg-instll-job");
    thread.Detach();
}

// [main thread]
InstallJobs::Iterator RemoveJob(InstallJobs& jobs, InstallJobs::Iterator it) {
    ASSERT(g_is_logical_main_thread);
    auto const state = it->job->state.Load(LoadMemoryOrder::Acquire);
    ASSERT(state == InstallJob::State::DoneError || state == InstallJob::State::DoneSuccess);

    return jobs.Remove(it);
}

// Stalls until all jobs are done.
// [main thread]
void ShutdownJobs(InstallJobs& jobs) {
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

// ==========================================================================================================
// Tests
// ==========================================================================================================

enum class LibFolder { Regular, Extra };

static MutableString FullTestLibraryPath(tests::Tester& tester, LibFolder folder, String lib_folder_name) {
    return path::Join(tester.scratch_arena,
                      Array {tests::TestFilesFolder(tester),
                             ({
                                 String f;
                                 switch (folder) {
                                     case LibFolder::Regular: f = tests::k_libraries_test_files_subdir; break;
                                     case LibFolder::Extra:
                                         f = tests::k_extra_libraries_test_files_subdir;
                                         break;
                                 }
                                 f;
                             }),
                             lib_folder_name});
}

static String TestPresetsFolder(tests::Tester& tester) {
    return path::Join(tester.scratch_arena,
                      Array {tests::TestFilesFolder(tester), tests::k_preset_test_files_subdir});
}

static ErrorCodeOr<sample_lib::Library*>
LoadTestLibrary(tests::Tester& tester, LibFolder folder, String lib_subpath) {
    auto const format = sample_lib::DetermineFileFormat(lib_subpath);
    if (!format.HasValue()) {
        tester.log.Error("Unknown file format for '{}'", lib_subpath);
        return ErrorCode {PackageError::InvalidLibrary};
    }

    auto const path = FullTestLibraryPath(tester, folder, lib_subpath);
    auto reader = TRY(Reader::FromFile(path));
    auto lib_outcome =
        sample_lib::Read(reader, format.Value(), path, tester.scratch_arena, tester.scratch_arena);

    if (lib_outcome.HasError()) {
        tester.log.Error("Failed to read library from test lua file: {}", lib_outcome.Error().message);
        return lib_outcome.Error().code;
    }
    auto lib = lib_outcome.ReleaseValue();
    return lib;
}

static ErrorCodeOr<Span<u8 const>>
CreateValidTestPackage(tests::Tester& tester, LibFolder folder, String lib_subpath, bool include_presets) {
    DynamicArray<u8> zip_data {tester.scratch_arena};
    auto writer = dyn::WriterFor(zip_data);
    auto package = WriterCreate(writer);
    DEFER { WriterDestroy(package); };

    auto lib = TRY(LoadTestLibrary(tester, folder, lib_subpath));
    TRY(WriterAddLibrary(package, *lib, tester.scratch_arena, "tester"));

    if (include_presets)
        TRY(WriterAddPresetsFolder(package, TestPresetsFolder(tester), tester.scratch_arena, "tester"));

    WriterFinalise(package);
    return zip_data.ToOwnedSpan();
}

static ErrorCodeOr<void> PrintDirectory(tests::Tester& tester, String dir, String heading) {
    auto it = TRY(dir_iterator::RecursiveCreate(tester.scratch_arena, dir, {}));
    DEFER { dir_iterator::Destroy(it); };

    tester.log.Debug("{} Contents of '{}':", heading, dir);
    while (auto const entry = TRY(dir_iterator::Next(it, tester.scratch_arena)))
        tester.log.Debug("  {}", entry->subpath);

    return k_success;
}

struct TestOptions {
    String test_name;
    String destination_folder;
    String zip_path;
    sample_lib_server::Server& sample_lib_server;
    PresetServer& preset_server;

    InstallJob::State expected_state;

    ExistingInstalledComponent expected_library_status;
    String expected_library_action;
    Optional<InstallJob::UserDecision> library_user_decision;

    ExistingInstalledComponent expected_presets_status;
    String expected_presets_action;
};

static ErrorCodeOr<void> Test(tests::Tester& tester, TestOptions options) {
    CAPTURE(options.test_name);

    auto job =
        CreateInstallJob(tester.scratch_arena,
                         {
                             .zip_path = options.zip_path,
                             .install_folders = {options.destination_folder, options.destination_folder},
                             .sample_lib_server = options.sample_lib_server,
                             .preset_server = options.preset_server,
                         });
    DEFER { DestroyInstallJob(job); };

    DoJobPhase1(*job);

    CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), options.expected_state);

    for (auto& comp : job->components) {
        switch (comp.component.type) {
            case ComponentType::Library:
                CHECK_EQ(comp.existing_installation_status, options.expected_library_status);

                if (options.library_user_decision) {
                    CHECK(UserInputIsRequired(comp.existing_installation_status));
                    comp.user_decision = *options.library_user_decision;
                }

                break;

            case ComponentType::Presets:
                CHECK_EQ(comp.existing_installation_status, options.expected_presets_status);
                break;

            case ComponentType::Count: PanicIfReached();
        }
    }

    if (options.expected_state == InstallJob::State::AwaitingUserInput) {
        job->state.Store(InstallJob::State::Installing, StoreMemoryOrder::Release);
        DoJobPhase2(*job);

        for (auto& comp : job->components)
            if (comp.component.type == ComponentType::Library)
                CHECK_EQ(TypeOfActionTaken(comp), options.expected_library_action);
            else
                CHECK_EQ(TypeOfActionTaken(comp), options.expected_presets_action);
    }

    if (options.expected_state != InstallJob::State::DoneError) {
        CHECK(job->error_buffer.size == 0);
        if (job->error_buffer.size > 0) tester.log.Error("Unexpected errors: {}", job->error_buffer);
    }

    TRY(PrintDirectory(tester,
                       options.destination_folder,
                       fmt::Format(tester.scratch_arena, "Post {}", options.test_name)));

    return k_success;
}

String
CreatePackageZipFile(tests::Tester& tester, LibFolder folder, String lib_subpath, bool include_presets) {
    auto const zip_data = ({
        auto o = package::CreateValidTestPackage(tester, folder, lib_subpath, include_presets);
        REQUIRE(!o.HasError());
        o.ReleaseValue();
    });
    CHECK_NEQ(zip_data.size, 0uz);

    auto const zip_path = tests::TempFilename(tester);
    REQUIRE(!WriteFile(zip_path, zip_data).HasError());

    return zip_path;
}

TEST_CASE(TestPackageInstallationUpdatePresets) {
    auto const destination_folder = tests::TempFolderUnique(tester);

    ThreadPool thread_pool;
    thread_pool.Init("pkg-install", {});
    ThreadsafeErrorNotifications error_notif;
    sample_lib_server::Server server {thread_pool, destination_folder, error_notif};
    PresetServer preset_server {
        .error_notifications = error_notif,
    };

    InitPresetServer(preset_server, destination_folder);
    DEFER { ShutdownPresetServer(preset_server); };

    constexpr String k_presets_folder_name = "my-presets";

    auto const create_zip_file = [&](String filename, u32 version) -> ErrorCodeOr<String> {
        DynamicArray<u8> data {tester.scratch_arena};
        auto writer = dyn::WriterFor(data);
        auto package = WriterCreate(writer);
        DEFER { WriterDestroy(package); };

        auto const folder =
            (String)path::Join(tester.scratch_arena,
                               Array {tests::TempFolderUnique(tester), k_presets_folder_name});
        TRY(CreateDirectory(folder, {.create_intermediate_directories = false}));
        TRY(CopyFile(
            path::Join(tester.scratch_arena,
                       Array {tests::TestFilesFolder(tester), tests::k_preset_test_files_subdir, filename}),
            path::Join(tester.scratch_arena, Array {folder, filename}),
            ExistingDestinationHandling::Fail));
        TRY(WriteFile(path::Join(tester.scratch_arena, Array {folder, k_preset_bank_filename}),
                      fmt::Format(tester.scratch_arena,
                                  "minor_version = {}\n"_s
                                  "id = org.floe-audio.test\n",
                                  version)));
        TRY(WriterAddPresetsFolder(package, folder, tester.scratch_arena, "tester"));

        WriterFinalise(package);

        auto const zip = tests::TempFilename(tester);
        TRY(WriteFile(zip, data));
        return zip;
    };

    constexpr String k_preset_filename_v1 = "sine.floe-preset";
    constexpr String k_preset_filename_v2 = "generic-test-1.mirage-phoenix";
    auto const zip_path_v1 = TRY(create_zip_file(k_preset_filename_v1, 1));
    auto const zip_path_v2 = TRY(create_zip_file(k_preset_filename_v2, 2));

    CreateJobOptions job_opts {
        .zip_path = zip_path_v1,
        .install_folders = {destination_folder, destination_folder},
        .sample_lib_server = server,
        .preset_server = preset_server,
    };

    auto const installed_dir =
        (String)path::Join(tester.scratch_arena, Array {destination_folder, k_presets_folder_name});
    auto const installed_file_v1 =
        path::Join(tester.scratch_arena, Array {installed_dir, k_preset_filename_v1});
    auto const installed_file_v2 =
        path::Join(tester.scratch_arena, Array {installed_dir, k_preset_filename_v2});

    // Install version 1.
    {
        auto const job = CreateInstallJob(tester.scratch_arena, job_opts);
        DEFER { DestroyInstallJob(job); };
        DoJobPhase1(*job); // Should do both phases.
        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::DoneSuccess);
        auto const& comp = job->components.first->data;
        CHECK(!comp.existing_installation_status.installed);

        CHECK_EQ(TRY(GetFileType(installed_dir)), FileType::Directory);
        CHECK_EQ(TRY(GetFileType(installed_file_v1)), FileType::File);
    }

    SUBCASE("same bank does nothing") {
        auto const job = CreateInstallJob(tester.scratch_arena, job_opts);
        DEFER { DestroyInstallJob(job); };
        DoJobPhase1(*job); // Should do both phases.
        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::DoneSuccess);
        auto const& comp = job->components.first->data;
        CHECK(comp.existing_installation_status.installed);
        CHECK_EQ(comp.existing_installation_status.modified_since_installed,
                 ModifiedSinceInstalled::Unmodified);
        CHECK_EQ(comp.existing_installation_status.version_difference, VersionDifference::Equal);

        CHECK_EQ(TRY(GetFileType(installed_file_v1)), FileType::File);
    }

    // Now we test the various cases of installing verison 2.
    job_opts.zip_path = zip_path_v2;

    SUBCASE("updates automatically when unmodified") {
        auto const job = CreateInstallJob(tester.scratch_arena, job_opts);
        DEFER { DestroyInstallJob(job); };
        DoJobPhase1(*job); // Should do both phases.
        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::DoneSuccess);
        auto const& comp = job->components.first->data;
        CHECK(comp.existing_installation_status.installed);

        CHECK_EQ(TRY(GetFileType(installed_dir)), FileType::Directory);
        CHECK_EQ(TRY(GetFileType(installed_file_v2)), FileType::File);
        CHECK(GetFileType(installed_file_v1).HasError());
    }

    SUBCASE("modified file requires user input") {
        auto state = TRY(LoadPresetFile(installed_file_v1, tester.scratch_arena, false));
        state.inst_ids[0] = sample_lib::InstrumentId {.library = "foo"_s, .inst_id = "bar"_s};
        TRY(SavePresetFile(installed_file_v1, state));

        auto const job = CreateInstallJob(tester.scratch_arena, job_opts);
        DEFER { DestroyInstallJob(job); };
        DoJobPhase1(*job);

        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::AwaitingUserInput);
        auto& comp = job->components.first->data;
        CHECK(comp.existing_installation_status.installed);
        CHECK_EQ(comp.existing_installation_status.modified_since_installed,
                 ModifiedSinceInstalled::Modified);
        CHECK_EQ(comp.existing_installation_status.version_difference, VersionDifference::InstalledIsOlder);

        comp.user_decision = InstallJob::UserDecision::Skip;
        job->state.Store(InstallJob::State::Installing, StoreMemoryOrder::Release);
        DoJobPhase2(*job);
    }

    SUBCASE("extra file added requires user input") {
        auto const extra_file = path::Join(tester.scratch_arena, Array {installed_dir, "file.txt"});
        TRY(WriteFile(extra_file, ""_s));
        RescanFolder(preset_server, installed_dir);

        auto const job = CreateInstallJob(tester.scratch_arena, job_opts);
        DEFER { DestroyInstallJob(job); };
        DoJobPhase1(*job);
        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::AwaitingUserInput);
        auto& comp = job->components.first->data;
        CHECK(comp.existing_installation_status.installed);
        CHECK_EQ(comp.existing_installation_status.modified_since_installed,
                 ModifiedSinceInstalled::UnmodifiedButFilesAdded);
        CHECK_EQ(comp.existing_installation_status.version_difference, VersionDifference::InstalledIsOlder);

        SUBCASE("overwrite") { comp.user_decision = InstallJob::UserDecision::Overwrite; }
        SUBCASE("skip") { comp.user_decision = InstallJob::UserDecision::Skip; }
        SUBCASE("install copy") { comp.user_decision = InstallJob::UserDecision::InstallCopy; }

        job->state.Store(InstallJob::State::Installing, StoreMemoryOrder::Release);
        DoJobPhase2(*job);
        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::DoneSuccess);
        CHECK_EQ(TRY(GetFileType(installed_dir)), FileType::Directory);

        TRY(PrintDirectory(tester, destination_folder, "installed"));

        switch (comp.user_decision) {
            case InstallJob::UserDecision::Unknown: PanicIfReached();
            case InstallJob::UserDecision::Overwrite: {
                CHECK_EQ(TRY(GetFileType(installed_file_v2)), FileType::File);
                CHECK(GetFileType(installed_file_v1).HasError());
                CHECK(GetFileType(extra_file).HasError());
                break;
            }
            case InstallJob::UserDecision::Skip: {
                CHECK_EQ(TRY(GetFileType(installed_file_v1)), FileType::File);
                CHECK_EQ(TRY(GetFileType(extra_file)), FileType::File);
                break;
            }
            case InstallJob::UserDecision::InstallCopy: {
                CHECK_EQ(TRY(GetFileType(installed_file_v1)), FileType::File);
                CHECK_EQ(TRY(GetFileType(extra_file)), FileType::File);

                auto const separate_dir = (String)fmt::Format(tester.scratch_arena, "{} (2)", installed_dir);
                auto const separate_file =
                    path::Join(tester.scratch_arena, Array {separate_dir, k_preset_filename_v2});
                CHECK_EQ(TRY(GetFileType(separate_dir)), FileType::Directory);
                CHECK_EQ(TRY(GetFileType(separate_file)), FileType::File);
                break;
            }
        }
    }

    return k_success;
}

TEST_CASE(TestPackageInstallationMdataToLua) {
    auto const destination_folder = tests::TempFolderUnique(tester);

    ThreadPool thread_pool;
    thread_pool.Init("pkg-install", {});
    ThreadsafeErrorNotifications error_notif;
    sample_lib_server::Server server {thread_pool, destination_folder, error_notif};
    PresetServer preset_server {
        .error_notifications = error_notif,
    };

    InitPresetServer(preset_server, destination_folder);
    DEFER { ShutdownPresetServer(preset_server); };

    auto const lua_package = CreatePackageZipFile(tester, LibFolder::Extra, "Mdata-To-Lua/floe.lua", false);
    auto const mdata_package =
        CreatePackageZipFile(tester, LibFolder::Regular, "shared_files_test_lib.mdata", false);

    CreateJobOptions job_opts {
        .zip_path = mdata_package,
        .install_folders = {destination_folder, destination_folder},
        .sample_lib_server = server,
        .preset_server = preset_server,
    };

    auto const expected_final_mdata_path =
        path::Join(tester.scratch_arena, Array {destination_folder, "FrozenPlain - SharedFilesMdata.mdata"});

    // Install the MDATA.
    {
        auto const job = CreateInstallJob(tester.scratch_arena, job_opts);
        DEFER { DestroyInstallJob(job); };
        DoJobPhase1(*job); // Should do both phases.
        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::DoneSuccess);
        auto const& comp = job->components.first->data;
        CHECK(!comp.existing_installation_status.installed);

        CHECK(TRY(GetFileType(expected_final_mdata_path)) == FileType::File);
    }

    // Installing the Lua should automatically replace the existing since the Lua is the same ID but newer.
    job_opts.zip_path = lua_package;
    {
        CHECK(!MirageIsInstalled());
        auto const job = CreateInstallJob(tester.scratch_arena, job_opts);
        DEFER { DestroyInstallJob(job); };
        DoJobPhase1(*job); // Should do both phases.
        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::DoneSuccess);
        auto const& comp = job->components.first->data;
        CHECK(comp.existing_installation_status.installed);
        CHECK_EQ(comp.existing_installation_status.version_difference, VersionDifference::InstalledIsOlder);
        CHECK_EQ(comp.existing_installation_status.modified_since_installed,
                 ModifiedSinceInstalled::Unmodified);

        {
            auto const o = GetFileType(expected_final_mdata_path);
            REQUIRE(o.HasError());
            CHECK_EQ(o.Error(), FilesystemError::PathDoesNotExist);
        }

        {
            auto const o =
                GetFileType(path::Join(tester.scratch_arena, Array {destination_folder, "Tester - Foo"}));
            REQUIRE(o.HasValue());
            CHECK_EQ(o.Value(), FileType::Directory);
        }
    }

    return k_success;
}

TEST_CASE(TestPackageInstallationExtraFiles) {
    auto const destination_folder = tests::TempFolderUnique(tester);

    ThreadPool thread_pool;
    thread_pool.Init("pkg-install", {});
    ThreadsafeErrorNotifications error_notif;
    sample_lib_server::Server server {thread_pool, destination_folder, error_notif};
    PresetServer preset_server {
        .error_notifications = error_notif,
    };

    InitPresetServer(preset_server, destination_folder);
    DEFER { ShutdownPresetServer(preset_server); };

    auto const zip_path_v1 = CreatePackageZipFile(tester, LibFolder::Regular, "Test-Lib-1/floe.lua", false);
    auto const zip_path_v2 =
        CreatePackageZipFile(tester, LibFolder::Regular, "Test-Lib-1-v2/floe.lua", false);

    CreateJobOptions job_opts {
        .zip_path = zip_path_v1,
        .install_folders = {destination_folder, destination_folder},
        .sample_lib_server = server,
        .preset_server = preset_server,
    };

    // Install the library.
    {
        auto const job = CreateInstallJob(tester.scratch_arena, job_opts);
        DEFER { DestroyInstallJob(job); };
        DoJobPhase1(*job); // Should do both phases.
        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::DoneSuccess);
        auto const& comp = job->components.first->data;
        CHECK(!comp.existing_installation_status.installed);
    }

    auto const extra_file_path =
        path::Join(tester.scratch_arena,
                   Array {destination_folder, "Tester - Test Lua", "my-extra-file.txt"});
    // Add an additional, unrelated file to the folder.
    {
        TRY(WriteFile(extra_file_path, "Extra file content"_s));
        sample_lib_server::RescanFolder(server, destination_folder);
    }

    // Trying to install again should do nothing; it's already installed exactly, it just has an extra
    // file.
    {
        auto const job = CreateInstallJob(tester.scratch_arena, job_opts);
        DEFER { DestroyInstallJob(job); };
        DoJobPhase1(*job);
        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::DoneSuccess);
        auto const& comp = job->components.first->data;
        CHECK(comp.existing_installation_status.installed);
        CHECK_EQ(comp.existing_installation_status.version_difference, VersionDifference::Equal);
        CHECK_EQ(comp.existing_installation_status.modified_since_installed,
                 ModifiedSinceInstalled::Unmodified);

        // The file should still exist.
        auto const type = REQUIRE_UNWRAP(GetFileType(extra_file_path));
        CHECK_EQ(type, FileType::File);
    }

    // Update to a new version - this should prompt user input because overwriting the existing folder
    // would delete the extra file that was added - we should be asking permission before doing that.
    job_opts.zip_path = zip_path_v2;
    {
        auto const job = CreateInstallJob(tester.scratch_arena, job_opts);
        DEFER { DestroyInstallJob(job); };
        DoJobPhase1(*job);
        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::AwaitingUserInput);
        auto& comp = job->components.first->data;
        CHECK(comp.existing_installation_status.installed);
        CHECK_EQ(comp.existing_installation_status.version_difference, VersionDifference::InstalledIsOlder);
        CHECK_EQ(comp.existing_installation_status.modified_since_installed,
                 ModifiedSinceInstalled::UnmodifiedButFilesAdded);

        // Let's say to overwrite.
        comp.user_decision = InstallJob::UserDecision::Overwrite;
        job->state.Store(InstallJob::State::Installing, StoreMemoryOrder::Release);

        DoJobPhase2(*job);
        CHECK_EQ(job->state.Load(LoadMemoryOrder::Acquire), InstallJob::State::DoneSuccess);

        // The extra file should no longer exist because we chose to overwrite.
        auto const o = GetFileType(extra_file_path);
        REQUIRE(o.HasError());
        CHECK(o.Error() == FilesystemError::PathDoesNotExist);
    }

    return k_success;
}

TEST_CASE(TestPackageInstallation) {
    auto const destination_folder = tests::TempFolderUnique(tester);

    ThreadPool thread_pool;
    thread_pool.Init("pkg-install", {});

    ThreadsafeErrorNotifications error_notif;
    sample_lib_server::Server sample_lib_server {thread_pool, destination_folder, error_notif};
    PresetServer preset_server {
        .error_notifications = error_notif,
    };
    InitPresetServer(preset_server, destination_folder);
    DEFER { ShutdownPresetServer(preset_server); };

    auto const zip_path = CreatePackageZipFile(tester, LibFolder::Regular, "Test-Lib-1/floe.lua", true);

    // Initially we're expecting success without any user input because the package is valid, it's not
    // installed anywhere else, and the destination folder is empty.
    TRY(Test(tester,
             {
                 .test_name = "Initial installation succeeds",
                 .destination_folder = destination_folder,
                 .zip_path = zip_path,
                 .sample_lib_server = sample_lib_server,
                 .preset_server = preset_server,
                 .expected_state = InstallJob::State::DoneSuccess,
                 .expected_library_status = {.installed = false},
                 .expected_library_action = "installed"_s,
                 .expected_presets_status = {.installed = false},
                 .expected_presets_action = "installed"_s,
             }));

    // If we try to install the exact same package again, it should notice that and do nothing.
    TRY(Test(tester,
             {
                 .test_name = "Reinstalling the same package does nothing",
                 .destination_folder = destination_folder,
                 .zip_path = zip_path,
                 .sample_lib_server = sample_lib_server,
                 .preset_server = preset_server,
                 .expected_state = InstallJob::State::DoneSuccess,
                 .expected_library_status {
                     .installed = true,
                     .version_difference = VersionDifference::Equal,
                     .modified_since_installed = ModifiedSinceInstalled::Unmodified,
                 },
                 .expected_library_action = "already installed"_s,
                 .expected_presets_status {
                     .installed = true,
                     .version_difference = VersionDifference::Equal,
                     .modified_since_installed = ModifiedSinceInstalled::Unmodified,
                 },
                 .expected_presets_action = "already installed"_s,
             }));

    // Setup for the next tests.
    // Rename the installed components to prompt checksum failure. If this fails then it might mean the
    // test files have moved.
    auto const floe_lua_path =
        path::Join(tester.scratch_arena, Array {destination_folder, "Tester - Test Lua", "floe.lua"});
    auto const preset_path =
        path::Join(tester.scratch_arena, Array {destination_folder, "presets", "sine.floe-preset"});
    {
        TRY(Rename(
            floe_lua_path,
            path::Join(tester.scratch_arena, Array {*path::Directory(floe_lua_path), "renamed.floe.lua"})));
        TRY(Rename(preset_path,
                   path::Join(tester.scratch_arena,
                              Array {*path::Directory(preset_path), "renamed-sine.floe-preset"})));

        TRY(PrintDirectory(tester, destination_folder, "Files renamed"));

        // Tell the server to rename so it notices the changes. It probably does this automatically via
        // file watchers but it's not guaranteed.
        sample_lib_server::RescanFolder(sample_lib_server, destination_folder);
        RescanFolder(preset_server, destination_folder);
    }

    // If the components are modified and we set to Skip, it should skip them.
    TRY(Test(tester,
             {
                 .test_name = "Skipping modified-by-rename components",
                 .destination_folder = destination_folder,
                 .zip_path = zip_path,
                 .sample_lib_server = sample_lib_server,
                 .preset_server = preset_server,
                 .expected_state = InstallJob::State::AwaitingUserInput,

                 .expected_library_status {
                     .installed = true,
                     .version_difference = VersionDifference::Equal,
                     .modified_since_installed = ModifiedSinceInstalled::Modified,
                 },
                 .expected_library_action = "skipped"_s,
                 .library_user_decision = InstallJob::UserDecision::Skip,

                 .expected_presets_status {.installed = false},
                 .expected_presets_action = "installed"_s,
             }));

    // If the components are modified and we set to Overwrite, it should overwrite them.
    TRY(Test(tester,
             {
                 .test_name = "Overwriting modified-by-rename components",
                 .destination_folder = destination_folder,
                 .zip_path = zip_path,
                 .sample_lib_server = sample_lib_server,
                 .preset_server = preset_server,
                 .expected_state = InstallJob::State::AwaitingUserInput,

                 .expected_library_status {
                     .installed = true,
                     .version_difference = VersionDifference::Equal,
                     .modified_since_installed = ModifiedSinceInstalled::Modified,
                 },
                 .expected_library_action = "overwritten"_s,
                 .library_user_decision = InstallJob::UserDecision::Overwrite,

                 // In our previous 'skip' case, the presets we reinstalled. They would be put in a
                 // separate folder, name appended with a number. So we expect the system to have found
                 // this installation.
                 .expected_presets_status {
                     .installed = true,
                     .version_difference = VersionDifference::Equal,
                     .modified_since_installed = ModifiedSinceInstalled::Unmodified,
                 },
                 .expected_presets_action = "already installed"_s,
             }));

    // Setup for the next tests.
    // Modify files this time rather than just rename.
    TRY(AppendFile(floe_lua_path, "\n"));

    // If the components are modified and we set to Overwrite, it should overwrite them.
    TRY(Test(tester,
             {
                 .test_name = "Overwriting modified-by-edit components",
                 .destination_folder = destination_folder,
                 .zip_path = zip_path,
                 .sample_lib_server = sample_lib_server,
                 .preset_server = preset_server,
                 .expected_state = InstallJob::State::AwaitingUserInput,

                 .expected_library_status {
                     .installed = true,
                     .version_difference = VersionDifference::Equal,
                     .modified_since_installed = ModifiedSinceInstalled::Modified,
                 },
                 .expected_library_action = "overwritten"_s,
                 .library_user_decision = InstallJob::UserDecision::Overwrite,

                 // In our previous 'skip' case, the presets we reinstalled. They would be put in a
                 // separate folder, name appended with a number. So we expect the system to have found
                 // this installation.
                 .expected_presets_status {
                     .installed = true,
                     .version_difference = VersionDifference::Equal,
                     .modified_since_installed = ModifiedSinceInstalled::Unmodified,
                 },
                 .expected_presets_action = "already installed"_s,
             }));

    // Try updating a library to a newer version.
    TRY(Test(tester,
             {
                 .test_name = "Updating library to newer version",
                 .destination_folder = destination_folder,
                 .zip_path = CreatePackageZipFile(tester, LibFolder::Regular, "Test-Lib-1-v2/floe.lua", true),
                 .sample_lib_server = sample_lib_server,
                 .preset_server = preset_server,
                 .expected_state = InstallJob::State::DoneSuccess,

                 .expected_library_status {
                     .installed = true,
                     .version_difference = VersionDifference::InstalledIsOlder,
                     .modified_since_installed = ModifiedSinceInstalled::Unmodified,
                 },
                 .expected_library_action = "updated"_s,

                 .expected_presets_status {
                     .installed = true,
                     .version_difference = VersionDifference::Equal,
                     .modified_since_installed = ModifiedSinceInstalled::Unmodified,
                 },
                 .expected_presets_action = "already installed"_s,
             }));

    // Do nothing if we now try to downgrade a library
    TRY(Test(tester,
             {
                 .test_name = "Downgrading library does nothing",
                 .destination_folder = destination_folder,
                 .zip_path = zip_path,
                 .sample_lib_server = sample_lib_server,
                 .preset_server = preset_server,
                 .expected_state = InstallJob::State::DoneSuccess,

                 .expected_library_status {
                     .installed = true,
                     .version_difference = VersionDifference::InstalledIsNewer,
                     .modified_since_installed = ModifiedSinceInstalled::Unmodified,
                 },
                 .expected_library_action = "newer version already installed"_s,

                 .expected_presets_status {
                     .installed = true,
                     .version_difference = VersionDifference::Equal,
                     .modified_since_installed = ModifiedSinceInstalled::Unmodified,
                 },
                 .expected_presets_action = "already installed"_s,
             }));

    // Try installing a MDATA library
    auto const mdata_package =
        CreatePackageZipFile(tester, LibFolder::Regular, "shared_files_test_lib.mdata", false);
    TRY(Test(tester,
             {
                 .test_name = "Installing MDATA library",
                 .destination_folder = destination_folder,
                 .zip_path = mdata_package,
                 .sample_lib_server = sample_lib_server,
                 .preset_server = preset_server,
                 .expected_state = InstallJob::State::DoneSuccess,

                 .expected_library_status {
                     .installed = false,
                 },
                 .expected_library_action = "installed"_s,
             }));

    // Try installing a MDATA library again to see if it skips
    TRY(Test(tester,
             {
                 .test_name = "Installing MDATA library again does nothing",
                 .destination_folder = destination_folder,
                 .zip_path = mdata_package,
                 .sample_lib_server = sample_lib_server,
                 .preset_server = preset_server,
                 .expected_state = InstallJob::State::DoneSuccess,

                 .expected_library_status {
                     .installed = true,
                     .version_difference = VersionDifference::Equal,
                     .modified_since_installed = ModifiedSinceInstalled::Unmodified,
                 },
                 .expected_library_action = "already installed"_s,
             }));

    return k_success;
}

TEST_CASE(TestTypeOfActionTaken) {
    for (auto const installed : Array {true, false}) {
        for (auto const version_difference : EnumIterator<VersionDifference>()) {
            for (auto const modified_since_installed : EnumIterator<ModifiedSinceInstalled>()) {
                for (auto const user_decision : Array {InstallJob::UserDecision::Overwrite,
                                                       InstallJob::UserDecision::Skip,
                                                       InstallJob::UserDecision::InstallCopy}) {
                    auto const status = ExistingInstalledComponent {
                        .installed = installed,
                        .version_difference = version_difference,
                        .modified_since_installed = modified_since_installed,
                    };

                    auto const action_taken = TypeOfActionTaken(status, user_decision);

                    CAPTURE(status);
                    CAPTURE(user_decision);
                    CHECK(action_taken != "error"_s);
                }
            }
        }
    }

    return k_success;
}

TEST_CASE(TestParseFilenameWithSuffix) {
    SUBCASE("no suffix") {
        auto const result = ParseFilenameWithSuffix("file.txt"_s);
        CHECK_EQ(result.filename_no_ext, "file"_s);
        CHECK_EQ(result.ext, ".txt"_s);
        CHECK(!result.suffix_num.HasValue());
    }

    SUBCASE("with valid suffix") {
        auto const result = ParseFilenameWithSuffix("file (3).txt"_s);
        CHECK_EQ(result.filename_no_ext, "file"_s);
        CHECK_EQ(result.ext, ".txt"_s);
        REQUIRE(result.suffix_num.HasValue());
        CHECK_EQ(*result.suffix_num, 3u);
    }

    SUBCASE("with zero suffix") {
        auto const result = ParseFilenameWithSuffix("file (0).txt"_s);
        CHECK_EQ(result.filename_no_ext, "file"_s);
        CHECK_EQ(result.ext, ".txt"_s);
        REQUIRE(result.suffix_num.HasValue());
        CHECK_EQ(*result.suffix_num, 0u);
    }

    SUBCASE("with large suffix") {
        auto const result = ParseFilenameWithSuffix("file (999).txt"_s);
        CHECK_EQ(result.filename_no_ext, "file"_s);
        CHECK_EQ(result.ext, ".txt"_s);
        REQUIRE(result.suffix_num.HasValue());
        CHECK_EQ(*result.suffix_num, 999u);
    }

    SUBCASE("with invalid suffix text") {
        auto const result = ParseFilenameWithSuffix("file (abc).txt"_s);
        CHECK_EQ(result.filename_no_ext, "file (abc)"_s);
        CHECK_EQ(result.ext, ".txt"_s);
        CHECK(!result.suffix_num.HasValue());
    }

    SUBCASE("with negative number") {
        auto const result = ParseFilenameWithSuffix("file (-5).txt"_s);
        CHECK_EQ(result.filename_no_ext, "file (-5)"_s);
        CHECK_EQ(result.ext, ".txt"_s);
        CHECK(!result.suffix_num.HasValue());
    }

    SUBCASE("with empty parentheses") {
        auto const result = ParseFilenameWithSuffix("file ().txt"_s);
        CHECK_EQ(result.filename_no_ext, "file ()"_s);
        CHECK_EQ(result.ext, ".txt"_s);
        CHECK(!result.suffix_num.HasValue());
    }

    SUBCASE("with space before suffix") {
        auto const result = ParseFilenameWithSuffix("file (5).txt"_s);
        CHECK_EQ(result.filename_no_ext, "file"_s);
        CHECK_EQ(result.ext, ".txt"_s);
        REQUIRE(result.suffix_num.HasValue());
        CHECK_EQ(*result.suffix_num, 5u);
    }

    SUBCASE("with trailing spaces") {
        auto const result = ParseFilenameWithSuffix("file   (5).txt"_s);
        CHECK_EQ(result.filename_no_ext, "file"_s);
        CHECK_EQ(result.ext, ".txt"_s);
        REQUIRE(result.suffix_num.HasValue());
        CHECK_EQ(*result.suffix_num, 5u);
    }

    SUBCASE("without extension") {
        auto const result = ParseFilenameWithSuffix("file"_s);
        CHECK_EQ(result.filename_no_ext, "file"_s);
        CHECK_EQ(result.ext, ""_s);
        CHECK(!result.suffix_num.HasValue());
    }

    SUBCASE("without extension but with suffix") {
        auto const result = ParseFilenameWithSuffix("file (7)"_s);
        CHECK_EQ(result.filename_no_ext, "file"_s);
        CHECK_EQ(result.ext, ""_s);
        REQUIRE(result.suffix_num.HasValue());
        CHECK_EQ(*result.suffix_num, 7u);
    }

    SUBCASE("complex filename") {
        auto const result = ParseFilenameWithSuffix("my-file_v2.final.txt"_s);
        CHECK_EQ(result.filename_no_ext, "my-file_v2"_s);
        CHECK_EQ(result.ext, ".final.txt"_s);
        CHECK(!result.suffix_num.HasValue());
    }

    SUBCASE("parentheses in middle") {
        auto const result = ParseFilenameWithSuffix("file (note) (5).txt"_s);
        CHECK_EQ(result.filename_no_ext, "file (note)"_s);
        CHECK_EQ(result.ext, ".txt"_s);
        REQUIRE(result.suffix_num.HasValue());
        CHECK_EQ(*result.suffix_num, 5u);
    }

    return k_success;
}

TEST_CASE(TestWriteFilenameWithSuffix) {
    Array<char, 128> buffer;

    SUBCASE("basic filename") {
        auto const size = WriteFilenameWithSuffix("file"_s, ".txt"_s, 1, buffer);
        CHECK_EQ((String {buffer.data, size}), "file (1).txt"_s);
    }

    SUBCASE("with larger suffix") {
        auto const size = WriteFilenameWithSuffix("file"_s, ".txt"_s, 999, buffer);
        CHECK_EQ((String {buffer.data, size}), "file (999).txt"_s);
    }

    SUBCASE("with zero suffix") {
        auto const size = WriteFilenameWithSuffix("file"_s, ".txt"_s, 0, buffer);
        CHECK_EQ((String {buffer.data, size}), "file (0).txt"_s);
    }

    SUBCASE("without extension") {
        auto const size = WriteFilenameWithSuffix("file"_s, ""_s, 5, buffer);
        CHECK_EQ((String {buffer.data, size}), "file (5)"_s);
    }

    SUBCASE("empty filename") {
        auto const size = WriteFilenameWithSuffix(""_s, ".txt"_s, 3, buffer);
        CHECK_EQ((String {buffer.data, size}), "(3).txt"_s);
    }

    SUBCASE("complex filename") {
        auto const size = WriteFilenameWithSuffix("my-file_v2.final"_s, ".txt"_s, 42, buffer);
        CHECK_EQ((String {buffer.data, size}), "my-file_v2.final (42).txt"_s);
    }

    SUBCASE("long extension") {
        auto const size = WriteFilenameWithSuffix("file"_s, ".tar.gz"_s, 10, buffer);
        CHECK_EQ((String {buffer.data, size}), "file (10).tar.gz"_s);
    }

    return k_success;
}

TEST_CASE(TestFindNextNonExistentFilename) {
    auto const folder = tests::TempFolderUnique(tester);

    SUBCASE("file doesn't exist") {
        auto const result = TRY(FindNextNonExistentFilename(folder, "test.txt"_s, tester.scratch_arena));
        CHECK_EQ(result, "test.txt"_s);
    }

    SUBCASE("file exists, returns (2)") {
        auto const path = path::Join(tester.scratch_arena, Array {folder, "file.txt"_s});
        TRY(WriteFile(path, ""_s));

        auto const result = TRY(FindNextNonExistentFilename(folder, "file.txt"_s, tester.scratch_arena));
        CHECK_EQ(result, "file (2).txt"_s);
    }

    SUBCASE("file and (2) exist, returns (3)") {
        auto const path1 = path::Join(tester.scratch_arena, Array {folder, "foo.txt"_s});
        auto const path2 = path::Join(tester.scratch_arena, Array {folder, "foo (2).txt"_s});
        TRY(WriteFile(path1, ""_s));
        TRY(WriteFile(path2, ""_s));

        auto const result = TRY(FindNextNonExistentFilename(folder, "foo.txt"_s, tester.scratch_arena));
        CHECK_EQ(result, "foo (3).txt"_s);
    }

    SUBCASE("filename with existing suffix") {
        auto const path = path::Join(tester.scratch_arena, Array {folder, "bar (5).txt"_s});
        TRY(WriteFile(path, ""_s));

        auto const result = TRY(FindNextNonExistentFilename(folder, "bar (5).txt"_s, tester.scratch_arena));
        CHECK_EQ(result, "bar (6).txt"_s);
    }

    return k_success;
}

} // namespace package

TEST_REGISTRATION(RegisterPackageInstallationTests) {
    REGISTER_TEST(package::TestPackageInstallationExtraFiles);
    REGISTER_TEST(package::TestPackageInstallation);
    REGISTER_TEST(package::TestTypeOfActionTaken);
    REGISTER_TEST(package::TestParseFilenameWithSuffix);
    REGISTER_TEST(package::TestWriteFilenameWithSuffix);
    REGISTER_TEST(package::TestFindNextNonExistentFilename);
    REGISTER_TEST(package::TestPackageInstallationUpdatePresets);
    REGISTER_TEST(package::TestPackageInstallationMdataToLua);
}
