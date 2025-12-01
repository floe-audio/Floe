// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "package_installation.hpp"

#include "tests/framework.hpp"

namespace fmt {

PUBLIC ErrorCodeOr<void>
CustomValueToString(Writer writer, package::ExistingInstalledComponent value, FormatOptions) {
    return FormatToWriter(writer, "{}", DumpStruct(value));
}

PUBLIC ErrorCodeOr<void>
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

static MutableString FullTestLibraryPath(tests::Tester& tester, String lib_folder_name) {
    return path::Join(
        tester.scratch_arena,
        Array {tests::TestFilesFolder(tester), tests::k_libraries_test_files_subdir, lib_folder_name});
}

static String TestPresetsFolder(tests::Tester& tester) {
    return path::Join(tester.scratch_arena,
                      Array {tests::TestFilesFolder(tester), tests::k_preset_test_files_subdir});
}

static ErrorCodeOr<sample_lib::Library*> LoadTestLibrary(tests::Tester& tester, String lib_subpath) {
    auto const format = sample_lib::DetermineFileFormat(lib_subpath);
    if (!format.HasValue()) {
        tester.log.Error("Unknown file format for '{}'", lib_subpath);
        return ErrorCode {PackageError::InvalidLibrary};
    }

    auto const path = FullTestLibraryPath(tester, lib_subpath);
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
CreateValidTestPackage(tests::Tester& tester, String lib_subpath, bool include_presets) {
    DynamicArray<u8> zip_data {tester.scratch_arena};
    auto writer = dyn::WriterFor(zip_data);
    auto package = WriterCreate(writer);
    DEFER { WriterDestroy(package); };

    auto lib = TRY(LoadTestLibrary(tester, lib_subpath));
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
    sample_lib_server::Server& server;

    InstallJob::State expected_state;

    ExistingInstalledComponent expected_library_status;
    String expected_library_action;
    Optional<InstallJob::UserDecision> library_user_decision;

    ExistingInstalledComponent expected_presets_status;
    String expected_presets_action;
};

static ErrorCodeOr<void> Test(tests::Tester& tester, TestOptions options) {
    CAPTURE(options.test_name);

    auto job = CreateInstallJob(tester.scratch_arena,
                                {
                                    .zip_path = options.zip_path,
                                    .libraries_install_folder = options.destination_folder,
                                    .presets_install_folder = options.destination_folder,
                                    .server = options.server,
                                    .preset_folders = Array {String(options.destination_folder)},
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

String CreatePackageZipFile(tests::Tester& tester, String lib_subpath, bool include_presets) {
    auto const zip_data = ({
        auto o = package::CreateValidTestPackage(tester, lib_subpath, include_presets);
        REQUIRE(!o.HasError());
        o.ReleaseValue();
    });
    CHECK_NEQ(zip_data.size, 0uz);

    auto const zip_path = tests::TempFilename(tester);
    REQUIRE(!WriteFile(zip_path, zip_data).HasError());

    return zip_path;
}

TEST_CASE(TestPackageInstallation) {
    auto const destination_folder {tests::TempFilename(tester)};
    REQUIRE(!CreateDirectory(destination_folder,
                             {.create_intermediate_directories = false, .fail_if_exists = false})
                 .HasError());

    ThreadPool thread_pool;
    thread_pool.Init("pkg-install", {});

    ThreadsafeErrorNotifications error_notif;
    sample_lib_server::Server server {thread_pool, destination_folder, error_notif};

    auto const zip_path = CreatePackageZipFile(tester, "Test-Lib-1/floe.lua", true);

    // Initially we're expecting success without any user input because the package is valid, it's not
    // installed anywhere else, and the destination folder is empty.
    TRY(Test(tester,
             {
                 .test_name = "Initial installation succeeds",
                 .destination_folder = destination_folder,
                 .zip_path = zip_path,
                 .server = server,
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
                 .server = server,
                 .expected_state = InstallJob::State::DoneSuccess,
                 .expected_library_status {
                     .installed = true,
                     .version_difference = ExistingInstalledComponent::Equal,
                     .modified_since_installed = ExistingInstalledComponent::Unmodified,
                 },
                 .expected_library_action = "already installed"_s,
                 .expected_presets_status {
                     .installed = true,
                     .version_difference = ExistingInstalledComponent::Equal,
                     .modified_since_installed = ExistingInstalledComponent::Unmodified,
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

        // Tell the server to rename so it notices the changes. It probably does this automatically via file
        // watchers but it's not guaranteed.
        sample_lib_server::RescanFolder(server, destination_folder);
    }

    // If the components are modified and we set to Skip, it should skip them.
    TRY(Test(tester,
             {
                 .test_name = "Skipping modified-by-rename components",
                 .destination_folder = destination_folder,
                 .zip_path = zip_path,
                 .server = server,
                 .expected_state = InstallJob::State::AwaitingUserInput,

                 .expected_library_status {
                     .installed = true,
                     .version_difference = ExistingInstalledComponent::Equal,
                     .modified_since_installed = ExistingInstalledComponent::Modified,
                 },
                 .expected_library_action = "skipped"_s,
                 .library_user_decision = InstallJob::UserDecision::Skip,

                 // Presets never require user input, they're always installed or skipped automatically.
                 .expected_presets_status {.installed = false},
                 .expected_presets_action = "installed"_s,
             }));

    // If the components are modified and we set to Overwrite, it should overwrite them.
    TRY(Test(tester,
             {
                 .test_name = "Overwriting modified-by-rename components",
                 .destination_folder = destination_folder,
                 .zip_path = zip_path,
                 .server = server,
                 .expected_state = InstallJob::State::AwaitingUserInput,

                 .expected_library_status {
                     .installed = true,
                     .version_difference = ExistingInstalledComponent::Equal,
                     .modified_since_installed = ExistingInstalledComponent::Modified,
                 },
                 .expected_library_action = "overwritten"_s,
                 .library_user_decision = InstallJob::UserDecision::Overwrite,

                 // In our previous 'skip' case, the presets we reinstalled. They would be put in a
                 // separate folder, name appended with a number. So we expect the system to have found
                 // this installation.
                 .expected_presets_status {
                     .installed = true,
                     .version_difference = ExistingInstalledComponent::Equal,
                     .modified_since_installed = ExistingInstalledComponent::Unmodified,
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
                 .server = server,
                 .expected_state = InstallJob::State::AwaitingUserInput,

                 .expected_library_status {
                     .installed = true,
                     .version_difference = ExistingInstalledComponent::Equal,
                     .modified_since_installed = ExistingInstalledComponent::Modified,
                 },
                 .expected_library_action = "overwritten"_s,
                 .library_user_decision = InstallJob::UserDecision::Overwrite,

                 // In our previous 'skip' case, the presets we reinstalled. They would be put in a
                 // separate folder, name appended with a number. So we expect the system to have found
                 // this installation.
                 .expected_presets_status {
                     .installed = true,
                     .version_difference = ExistingInstalledComponent::Equal,
                     .modified_since_installed = ExistingInstalledComponent::Unmodified,
                 },
                 .expected_presets_action = "already installed"_s,
             }));

    // Try updating a library to a newer version.
    TRY(Test(tester,
             {
                 .test_name = "Updating library to newer version",
                 .destination_folder = destination_folder,
                 .zip_path = CreatePackageZipFile(tester, "Test-Lib-1-v2/floe.lua", true),
                 .server = server,
                 .expected_state = InstallJob::State::DoneSuccess,

                 .expected_library_status {
                     .installed = true,
                     .version_difference = ExistingInstalledComponent::InstalledIsOlder,
                     .modified_since_installed = ExistingInstalledComponent::Unmodified,
                 },
                 .expected_library_action = "updated"_s,

                 .expected_presets_status {
                     .installed = true,
                     .version_difference = ExistingInstalledComponent::Equal,
                     .modified_since_installed = ExistingInstalledComponent::Unmodified,
                 },
                 .expected_presets_action = "already installed"_s,
             }));

    // Do nothing if we now try to downgrade a library
    TRY(Test(tester,
             {
                 .test_name = "Downgrading library does nothing",
                 .destination_folder = destination_folder,
                 .zip_path = zip_path,
                 .server = server,
                 .expected_state = InstallJob::State::DoneSuccess,

                 .expected_library_status {
                     .installed = true,
                     .version_difference = ExistingInstalledComponent::InstalledIsNewer,
                     .modified_since_installed = ExistingInstalledComponent::Unmodified,
                 },
                 .expected_library_action = "newer version already installed"_s,

                 .expected_presets_status {
                     .installed = true,
                     .version_difference = ExistingInstalledComponent::Equal,
                     .modified_since_installed = ExistingInstalledComponent::Unmodified,
                 },
                 .expected_presets_action = "already installed"_s,
             }));

    // Try installing a MDATA library
    auto const mdata_package = CreatePackageZipFile(tester, "shared_files_test_lib.mdata", false);
    TRY(Test(tester,
             {
                 .test_name = "Installing MDATA library",
                 .destination_folder = destination_folder,
                 .zip_path = mdata_package,
                 .server = server,
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
                 .server = server,
                 .expected_state = InstallJob::State::DoneSuccess,

                 .expected_library_status {
                     .installed = true,
                     .version_difference = ExistingInstalledComponent::Equal,
                     .modified_since_installed = ExistingInstalledComponent::Unmodified,
                 },
                 .expected_library_action = "already installed"_s,
             }));

    return k_success;
}

TEST_CASE(TestTypeOfActionTaken) {
    for (auto const installed : Array {true, false}) {
        for (auto const version_difference :
             Array {ExistingInstalledComponent::VersionDifference::Equal,
                    ExistingInstalledComponent::VersionDifference::InstalledIsOlder,
                    ExistingInstalledComponent::VersionDifference::InstalledIsNewer}) {
            for (auto const modified_since_installed :
                 Array {ExistingInstalledComponent::ModifiedSinceInstalled::Modified,
                        ExistingInstalledComponent::ModifiedSinceInstalled::MaybeModified,
                        ExistingInstalledComponent::ModifiedSinceInstalled::Unmodified}) {
                for (auto const user_decision :
                     Array {InstallJob::UserDecision::Overwrite, InstallJob::UserDecision::Skip}) {
                    auto const status = ExistingInstalledComponent {
                        .installed = installed,
                        .version_difference = version_difference,
                        .modified_since_installed = modified_since_installed,
                    };

                    auto const action_taken = TypeOfActionTaken(status, user_decision);

                    (void)action_taken;

                    CAPTURE(status);
                    CAPTURE(user_decision);
                    CHECK(action_taken != "error"_s);
                }
            }
        }
    }

    return k_success;
}

} // namespace package

TEST_REGISTRATION(RegisterPackageInstallationTests) {
    REGISTER_TEST(package::TestPackageInstallation);
    REGISTER_TEST(package::TestTypeOfActionTaken);
}
