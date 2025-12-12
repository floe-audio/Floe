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

enum class VersionDifference : u8 {
    Equal, // Installed version is the same as the package version.
    InstalledIsOlder, // Installed version is older than the package version.
    InstalledIsNewer, // Installed version is newer than the package version.
    Count,
};

enum class ModifiedSinceInstalled : u8 {
    Unmodified, // Installed version is known to be unmodified since it was installed.
    MaybeModified, // We don't know if the installed version has been modified since it was installed.
    Modified, // Installed version has been modified since it was installed.
    UnmodifiedButFilesAdded, // Unchanged, but extra files were added.
    Count,
};

struct ExistingInstalledComponent {
    bool operator==(ExistingInstalledComponent const& o) const = default;
    bool installed;
    VersionDifference version_difference; // if installed
    ModifiedSinceInstalled modified_since_installed; // if installed
};

bool32 UserInputIsRequired(ExistingInstalledComponent status);
bool32 NoInstallationRequired(ExistingInstalledComponent status);

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
InstallJob* CreateInstallJob(ArenaAllocator& arena, CreateJobOptions opts);

// [main thread]
void DestroyInstallJob(InstallJob* job);

// Run this and then check the 'state' variable. You might need to ask the user a question on the main thread
// and then call OnAllUserInputReceived.
// [worker thread (probably)]
void DoJobPhase1(InstallJob& job);

// [worker thread (probably)]
void DoJobPhase2(InstallJob& job);

// Complete a job that was started but needed user input.
// [main thread]
void OnAllUserInputReceived(InstallJob& job, ThreadPool& thread_pool);

// [threadsafe]
String TypeOfActionTaken(ExistingInstalledComponent existing_installation_status,
                         InstallJob::UserDecision user_decision);

// [main-thread]
String TypeOfActionTaken(InstallJob::Component const& component);

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
void AddJob(InstallJobs& jobs,
            String zip_path,
            prefs::Preferences& prefs,
            FloePaths const& paths,
            ArenaAllocator& scratch_arena,
            sample_lib_server::Server& sample_library_server);

// [main thread]
InstallJobs::Iterator RemoveJob(InstallJobs& jobs, InstallJobs::Iterator it);

// Stalls until all jobs are done.
// [main thread]
void ShutdownJobs(InstallJobs& jobs);

} // namespace package
