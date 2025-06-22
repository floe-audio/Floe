// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "framework.hpp"

#include <valgrind/valgrind.h>

#include "foundation/memory/allocators.hpp"
#include "foundation/utils/format.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "utils/debug/debug.hpp"

namespace tests {

struct TestFailed {};

void RegisterTest(Tester& tester, TestFunction f, String title) {
    dyn::Append(tester.test_cases, TestCase {f, title});
}

String TempFolder(Tester& tester) {
    if (!tester.temp_folder) {
        auto error_log = StdWriter(StdStream::Out);
        tester.temp_folder =
            KnownDirectoryWithSubdirectories(tester.arena,
                                             KnownDirectoryType::Temporary,
                                             Array {(String)UniqueFilename("Floe-", "", tester.random_seed)},
                                             k_nullopt,
                                             {
                                                 .create = true,
                                                 .error_log = &error_log,
                                             });
        auto _ = StdPrint(StdStream::Err,
                          fmt::Format(tester.scratch_arena, "Test output folder: {}\n", *tester.temp_folder));
    }
    return *tester.temp_folder;
}

String TempFilename(Tester& tester) {
    auto folder = TempFolder(tester);
    auto filename = UniqueFilename("tmp-", "", tester.random_seed);
    return path::Join(tester.scratch_arena, Array {folder, filename});
}

static Optional<String> SearchUpwardsFromExeForFolder(Tester& tester, String folder_name) {
    auto const path_outcome = CurrentBinaryPath(tester.scratch_arena);
    if (path_outcome.HasError()) {
        tester.log.Error("failed to get the current exe path: {}", path_outcome.Error());
        return k_nullopt;
    }

    auto result = SearchForExistingFolderUpwards(path_outcome.Value(), folder_name, tester.arena);
    if (!result) {
        tester.log.Error("failed to find {} folder", folder_name);
        return k_nullopt;
    }
    return result;
}

String TestFilesFolder(Tester& tester) {
    if (!tester.test_files_folder) {
        auto const opt_folder = SearchUpwardsFromExeForFolder(tester, "test_files");
        if (!opt_folder) {
            Check(tester, false, "failed to find test_files folder", FailureAction::FailAndExitTest);
            tester.test_files_folder = "ERROR";
        } else {
            tester.test_files_folder = *opt_folder;
        }
    }
    return *tester.test_files_folder;
}

String HumanCheckableOutputFilesFolder(Tester& tester) {
    if (!tester.human_checkable_output_files_folder) {
        auto const output_dir = String(path::Join(
            tester.arena,
            Array {String(KnownDirectory(tester.arena, KnownDirectoryType::UserData, {.create = true})),
                   "Floe",
                   "Test-Output-Files"}));
        auto const outcome = CreateDirectory(output_dir, {.create_intermediate_directories = true});
        if (outcome.HasError()) {
            Check(tester, false, "failed to create output directory", FailureAction::FailAndExitTest);
            tester.human_checkable_output_files_folder = "ERROR";
        } else {
            tester.human_checkable_output_files_folder = output_dir;
        }
        tester.log.Info("Human checkable output files folder: {}",
                        *tester.human_checkable_output_files_folder);
    }
    return *tester.human_checkable_output_files_folder;
}

Optional<String> BuildResourcesFolder(Tester& tester) {
    if (!tester.build_resources_folder)
        tester.build_resources_folder.Emplace(
            SearchUpwardsFromExeForFolder(tester, k_build_resources_subdir));
    return *tester.build_resources_folder;
}

void* CreateOrFetchFixturePointer(Tester& tester,
                                  CreateFixturePointer create,
                                  DeleteFixturePointer delete_fixture) {
    if (!tester.fixture_pointer) {
        tester.fixture_pointer = create(tester.fixture_arena, tester);
        ASSERT(tester.fixture_pointer, "create function should return a fixture");
    }
    if (!tester.delete_fixture) tester.delete_fixture = delete_fixture;
    return tester.fixture_pointer;
}

int RunAllTests(Tester& tester, Span<String> filter_patterns) {
    DEFER {
        if (tester.temp_folder)
            auto _ = Delete(*tester.temp_folder, {.type = DeleteOptions::Type::DirectoryRecursively});
    };

    tester.log.Info("Running tests ...");
    tester.log.Info("Valgrind: {}", RUNNING_ON_VALGRIND);
    tester.log.Info("Repeat tests: {}", tester.repeat_tests);

    Stopwatch const overall_stopwatch;

    for (auto _ : Range(tester.repeat_tests)) {
        for (auto& test_case : tester.test_cases) {
            if (filter_patterns.size) {
                bool matches_any_pattern = false;
                for (auto const& pattern : filter_patterns) {
                    if (MatchWildcard(pattern, test_case.title)) {
                        matches_any_pattern = true;
                        break;
                    }
                }
                if (!matches_any_pattern) continue;
            }

            tester.current_test_case = &test_case;
            tester.log.Debug("Running ...");

            tester.subcases_passed.Clear();
            tester.fixture_pointer = nullptr;
            tester.delete_fixture = nullptr;
            tester.fixture_arena.ResetCursorAndConsolidateRegions();

            Stopwatch const stopwatch;

            bool run_test = true;
            do {
                tester.scratch_arena.ResetCursorAndConsolidateRegions();
                tester.should_reenter = false;
                tester.subcases_current_max_level = 0;
                dyn::Clear(tester.subcases_stack);

                try {
                    auto const result = test_case.f(tester);
                    if (result.outcome.HasError()) {
                        tester.should_reenter = false;
                        tester.current_test_case->failed = true;
                        tester.log.Error("Failed: test returned an error:\n{}", result.outcome.Error());
                        if (result.stacktrace.HasValue()) {
                            ASSERT(result.stacktrace.Value().size);
                            auto const str =
                                StacktraceString(result.stacktrace.Value(), tester.scratch_arena);
                            tester.log.Info("Stacktrace:\n{}", str);
                        }
                    }
                } catch (TestFailed const& _) {
                } catch (PanicException) {
                    tester.should_reenter = false;
                    tester.current_test_case->failed = true;
                    tester.log.Error("Failed: test panicked");
                } catch (...) {
                    tester.should_reenter = false;
                    tester.current_test_case->failed = true;
                    tester.log.Error("Failed: an exception was thrown");
                }

                if (!tester.should_reenter) run_test = false;
            } while (run_test);

            if (tester.delete_fixture) tester.delete_fixture(tester.fixture_pointer, tester.fixture_arena);

            if (!test_case.failed)
                tester.log.Debug(ANSI_COLOUR_FOREGROUND_GREEN("Passed") " ({})\n", stopwatch);
            else
                tester.log.Error("Failed\n");
        }
    }
    tester.current_test_case = nullptr;

    tester.log.Info("Summary");
    tester.log.Info("--------");
    tester.log.Info("Assertions: {}", tester.num_assertions);
    tester.log.Info("Tests: {}", tester.test_cases.size);
    tester.log.Info("Time taken: {.2}s", overall_stopwatch.SecondsElapsed());

    if (tester.num_warnings == 0)
        tester.log.Info("Warnings: " ANSI_COLOUR_FOREGROUND_GREEN("0"));
    else
        tester.log.Info("Warnings: " ANSI_COLOUR_SET_FOREGROUND_RED "{}" ANSI_COLOUR_RESET,
                        tester.num_warnings);

    auto const num_failed = CountIf(tester.test_cases, [](TestCase const& t) { return t.failed; });
    if (num_failed == 0) {
        tester.log.Info("Failed: " ANSI_COLOUR_FOREGROUND_GREEN("0"));
        tester.log.Info("Result: " ANSI_COLOUR_FOREGROUND_GREEN("Success"));
    } else {
        DynamicArray<char> failed_test_names {tester.scratch_arena};
        for (auto& test_case : tester.test_cases) {
            if (test_case.failed) {
                fmt::Append(failed_test_names,
                            " ({}{})",
                            test_case.title,
                            num_failed == 1 ? "" : " and others");
                break;
            }
        }

        tester.log.Info("Failed: " ANSI_COLOUR_SET_FOREGROUND_RED "{}" ANSI_COLOUR_RESET "{}",
                        num_failed,
                        failed_test_names);
        tester.log.Info("Result: " ANSI_COLOUR_SET_FOREGROUND_RED "Failure");
    }

    return num_failed ? 1 : 0;
}

__attribute__((noinline)) void
Check(Tester& tester, bool expression, String message, FailureAction failure_action, String file, int line) {
    ++tester.num_assertions;
    if (!expression) {
        String pretext = "REQUIRE failed";
        if (failure_action == FailureAction::FailAndContinue)
            pretext = "CHECK failed";
        else if (failure_action == FailureAction::LogWarningAndContinue)
            pretext = "WARNING issued";

        tester.log.Error("{}: {}", pretext, message);
        tester.log.Error("  File      {}:{}", file, line);
        for (auto const& s : tester.subcases_stack)
            tester.log.Error("  SUBCASE   {}", s.name);

        auto capture = tester.capture_buffer.UsedStackData();
        if (capture.size) {
            auto capture_str = String {(char const*)capture.data, capture.size};

            while (auto pos = Find(capture_str, '\n')) {
                String const part {capture_str.data, *pos};
                tester.log.Error(part);

                capture_str.RemovePrefix(*pos + 1);
            }
            if (capture_str.size) tester.log.Error(capture_str);
        }

        auto _ = PrintCurrentStacktrace(StdStream::Err, {}, ProgramCounter {CALL_SITE_PROGRAM_COUNTER});

        if (failure_action != FailureAction::LogWarningAndContinue) {
            tester.should_reenter = false;
            tester.current_test_case->failed = true;
        } else {
            ++tester.num_warnings;
        }
        if (failure_action == FailureAction::FailAndExitTest) throw TestFailed();
    }
}

Subcase::Subcase(Tester& tester, String name, String file, int line) : tester(tester), entered(false) {
    // if a Subcase on the same level has already been entered
    if (tester.subcases_stack.size < tester.subcases_current_max_level) {
        tester.should_reenter = true;
        return;
    }

    // push the current signature to the stack so we can check if the
    // current stack + the current new subcase have been traversed
    ASSERT(name.size <= decltype(SubcaseSignature::name)::Capacity());
    dyn::Append(tester.subcases_stack, SubcaseSignature {name, file, line});
    if (tester.subcases_passed.Contains(tester.subcases_stack)) {
        // pop - revert to previous stack since we've already passed this
        dyn::Pop(tester.subcases_stack);
        return;
    }

    tester.subcases_current_max_level = tester.subcases_stack.size;
    entered = true;

    DynamicArray<char> buf {tester.scratch_arena};
    for (auto const& subcase : tester.subcases_stack) {
        fmt::Append(buf, "\"{}\"", subcase.name);
        if (&subcase != &Last(tester.subcases_stack)) dyn::AppendSpan(buf, " -> ");
    }
    tester.log.Debug(buf);
}

Subcase::~Subcase() {
    if (entered) {
        // only mark the subcase stack as passed if no subcases have been skipped
        if (tester.should_reenter == false) tester.subcases_passed.Add(tester.subcases_stack);
        dyn::Pop(tester.subcases_stack);
    }
}

} // namespace tests
