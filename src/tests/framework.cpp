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

struct TestResults {
    struct Case {
        enum class Result { Passed, Failed, Error };
        String name;
        String classname;
        usize num_assertions;
        f64 time_seconds;
        String log_content;
        Result result;
    };

    struct Suite {
        String name;
        usize num_tests;
        usize num_failures;
        usize num_errors;
        usize num_skipped;
        usize num_assertions;
        f64 time_seconds;
        fmt::TimestampRfc3339UtcArray timestamp;
        ArenaList<Case> test_cases;
    };

    ArenaList<Suite> suites;
};

static String EscapeXmlAttribute(ArenaAllocator& arena, String input) {
    DynamicArray<char> result {arena};
    for (auto c : input) {
        switch (c) {
            case '<': dyn::AppendSpan(result, "&lt;"); break;
            case '>': dyn::AppendSpan(result, "&gt;"); break;
            case '&': dyn::AppendSpan(result, "&amp;"); break;
            case '"': dyn::AppendSpan(result, "&quot;"); break;
            case '\'': dyn::AppendSpan(result, "&apos;"); break;
            default: dyn::Append(result, c); break;
        }
    }
    return result.ToOwnedSpan();
}

static String StripAnsiCodes(ArenaAllocator& arena, String input) {
    DynamicArray<char> result {arena};
    for (usize i = 0; i < input.size; ++i) {
        if (input[i] == '\x1b' && i + 1 < input.size && input[i + 1] == '[') {
            // Skip ANSI escape sequence
            i += 2;
            while (i < input.size && input[i] != 'm')
                ++i;
        } else {
            dyn::Append(result, input[i]);
        }
    }
    return result.ToOwnedSpan();
}

static ErrorCodeOr<void> WriteJUnitXmlTestResults(Tester& tester, Writer writer, TestResults const& results) {
    TRY(writer.WriteChars("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));

    // Calculate totals across all suites
    usize total_tests = 0;
    usize total_failures = 0;
    usize total_errors = 0;
    usize total_skipped = 0;
    usize total_assertions = 0;
    f64 total_time = 0;

    for (auto const& suite : results.suites) {
        total_tests += suite.num_tests;
        total_failures += suite.num_failures;
        total_errors += suite.num_errors;
        total_skipped += suite.num_skipped;
        total_assertions += suite.num_assertions;
        total_time += suite.time_seconds;
    }

    auto const now = TimestampRfc3339UtcNow();

    // Write testsuites root element
    TRY(writer.WriteChars(fmt::Format(tester.scratch_arena,
                                      "<testsuites name=\"Floe Tests\" tests=\"{}\" failures=\"{}\" "
                                      "errors=\"{}\" skipped=\"{}\" assertions=\"{}\" "
                                      "time=\"{.6}\" timestamp=\"{}\">\n",
                                      total_tests,
                                      total_failures,
                                      total_errors,
                                      total_skipped,
                                      total_assertions,
                                      total_time,
                                      now)));
    DEFER { auto _ = writer.WriteChars("</testsuites>\n"); };

    for (auto const& suite : results.suites) {
        // Write testsuite element
        // IMPROVE: Add file="" attribute when we track source file locations
        TRY(writer.WriteChars(fmt::Format(tester.scratch_arena,
                                          "  <testsuite name=\"{}\" tests=\"{}\" failures=\"{}\" "
                                          "errors=\"{}\" skipped=\"{}\" assertions=\"{}\" "
                                          "time=\"{.6}\" timestamp=\"{}\">\n",
                                          suite.name,
                                          suite.num_tests,
                                          suite.num_failures,
                                          suite.num_errors,
                                          suite.num_skipped,
                                          suite.num_assertions,
                                          suite.time_seconds,
                                          suite.timestamp)));
        DEFER { auto _ = writer.WriteChars("  </testsuite>\n"); };

        // Write properties
        TRY(writer.WriteChars("    <properties>\n"));
        TRY(writer.WriteChars(fmt::Format(tester.scratch_arena,
                                          "      <property name=\"version\" value=\"{}\" />\n",
                                          FLOE_VERSION_STRING)));

        // Add system information
        auto const os_info = GetOsInfo();
        auto const system_stats = CachedSystemStats();

        TRY(writer.WriteChars(fmt::Format(tester.scratch_arena,
                                          "      <property name=\"os_name\" value=\"{}\" />\n",
                                          EscapeXmlAttribute(tester.scratch_arena, os_info.name))));
        if (os_info.version.size) {
            TRY(writer.WriteChars(fmt::Format(tester.scratch_arena,
                                              "      <property name=\"os_version\" value=\"{}\" />\n",
                                              EscapeXmlAttribute(tester.scratch_arena, os_info.version))));
        }
        if (os_info.pretty_name.size) {
            TRY(writer.WriteChars(
                fmt::Format(tester.scratch_arena,
                            "      <property name=\"os_pretty_name\" value=\"{}\" />\n",
                            EscapeXmlAttribute(tester.scratch_arena, os_info.pretty_name))));
        }
        TRY(writer.WriteChars(fmt::Format(tester.scratch_arena,
                                          "      <property name=\"arch\" value=\"{}\" />\n",
                                          SystemStats::Arch())));
        TRY(writer.WriteChars(fmt::Format(tester.scratch_arena,
                                          "      <property name=\"cpu_count\" value=\"{}\" />\n",
                                          system_stats.num_logical_cpus)));
        if (system_stats.cpu_name.size) {
            TRY(writer.WriteChars(
                fmt::Format(tester.scratch_arena,
                            "      <property name=\"cpu_name\" value=\"{}\" />\n",
                            EscapeXmlAttribute(tester.scratch_arena, system_stats.cpu_name))));
        }

        // Add build and runtime configuration flags
        constexpr bool k_thread_sanitizer =
#if __has_feature(thread_sanitizer)
            true;
#else
            false;
#endif

        constexpr bool k_production_build =
#if PRODUCTION_BUILD
            true;
#else
            false;
#endif

        constexpr bool k_optimised_build =
#if OPTIMISED_BUILD
            true;
#else
            false;
#endif

        constexpr bool k_runtime_safety_checks =
#if RUNTIME_SAFETY_CHECKS_ON
            true;
#else
            false;
#endif

        TRY(writer.WriteChars(fmt::Format(tester.scratch_arena,
                                          "      <property name=\"thread_sanitizer\" value=\"{}\" />\n"
                                          "      <property name=\"valgrind\" value=\"{}\" />\n"
                                          "      <property name=\"production_build\" value=\"{}\" />\n"
                                          "      <property name=\"optimised_build\" value=\"{}\" />\n"
                                          "      <property name=\"runtime_safety_checks\" value=\"{}\" />\n",
                                          k_thread_sanitizer,
                                          (bool)RUNNING_ON_VALGRIND,
                                          k_production_build,
                                          k_optimised_build,
                                          k_runtime_safety_checks)));

        TRY(writer.WriteChars("    </properties>\n"));

        // Write test cases
        for (auto const& test_case : suite.test_cases) {
            // IMPROVE: Add file="" and line="" attributes when we track source locations
            auto escaped_name = EscapeXmlAttribute(tester.scratch_arena, test_case.name);
            auto escaped_classname = EscapeXmlAttribute(tester.scratch_arena, test_case.classname);

            TRY(writer.WriteChars(fmt::Format(tester.scratch_arena,
                                              "    <testcase name=\"{}\" classname=\"{}\" "
                                              "assertions=\"{}\" time=\"{.6}\"",
                                              escaped_name,
                                              escaped_classname,
                                              test_case.num_assertions,
                                              test_case.time_seconds)));

            bool has_content =
                (test_case.result != TestResults::Case::Result::Passed) || test_case.log_content.size;

            if (has_content) {
                TRY(writer.WriteChars(">\n"));

                // Handle failure/error results
                if (test_case.result == TestResults::Case::Result::Failed) {
                    // IMPROVE: Use more specific failure type instead of placeholder "Check"
                    TRY(writer.WriteChars("      <failure type=\"Check\" message=\"Test failed\" />\n"));
                } else if (test_case.result == TestResults::Case::Result::Error) {
                    // IMPROVE: Use more specific error type instead of placeholder "Check"
                    TRY(writer.WriteChars("      <error type=\"Check\" message=\"Test error\" />\n"));
                }

                // Add log content as system-err if present
                if (test_case.log_content.size) {
                    auto cleaned_log = StripAnsiCodes(tester.scratch_arena, test_case.log_content);
                    TRY(writer.WriteChars("      <system-err><![CDATA["));
                    TRY(writer.WriteChars(cleaned_log));
                    TRY(writer.WriteChars("]]></system-err>\n"));
                }

                TRY(writer.WriteChars("    </testcase>\n"));
            } else {
                TRY(writer.WriteChars(" />\n"));
            }
        }
    }

    return k_success;
}

int RunAllTests(Tester& tester, Span<String> filter_patterns, Optional<String> junit_xml_output_path) {
    DEFER {
        if (tester.temp_folder)
            auto _ = Delete(*tester.temp_folder, {.type = DeleteOptions::Type::DirectoryRecursively});
    };

    TestResults test_results {};

    tester.log.Info("Running tests ...");
    tester.log.Info("Valgrind: {}", RUNNING_ON_VALGRIND);
    tester.log.Info("Repeat tests: {}", tester.repeat_tests);

    Stopwatch const overall_stopwatch;

    for (auto const run_index : Range(tester.repeat_tests)) {
        Stopwatch const run_stopwatch;
        auto suite = test_results.suites.PrependUninitialised(tester.arena);
        PLACEMENT_NEW(suite)
        TestResults::Suite {
            .name = fmt::Format(tester.arena, "Floe Tests Run {}", run_index + 1),
            .num_tests = tester.test_cases.size,
            .timestamp = TimestampRfc3339UtcNow(),
        };
        DEFER { suite->time_seconds = run_stopwatch.SecondsElapsed(); };

        for (auto& test_case : tester.test_cases) {
            if (filter_patterns.size) {
                bool matches_any_pattern = false;
                for (auto const& pattern : filter_patterns) {
                    if (MatchWildcard(pattern, test_case.title)) {
                        matches_any_pattern = true;
                        break;
                    }
                }
                if (!matches_any_pattern) {
                    ++suite->num_skipped;
                    continue;
                }
            }

            tester.current_test_case = &test_case;
            tester.log.Debug("Running ...");

            tester.subcases_passed.Clear();
            tester.fixture_pointer = nullptr;
            tester.delete_fixture = nullptr;
            tester.current_test_num_assertions = 0;
            tester.fixture_arena.ResetCursorAndConsolidateRegions();

            Stopwatch const stopwatch;
            DynamicArray<char> output_buffer {tester.arena};
            tester.log.output_buffer = &output_buffer;

            auto case_entry = suite->test_cases.PrependUninitialised(tester.arena);
            PLACEMENT_NEW(case_entry)
            TestResults::Case {
                .name = test_case.title,
                .classname = "Tests"_s,
            };
            DEFER {
                case_entry->num_assertions = tester.current_test_num_assertions;
                case_entry->time_seconds = stopwatch.SecondsElapsed();
            };

            bool run_test = true;
            do {
                tester.scratch_arena.ResetCursorAndConsolidateRegions();
                tester.should_reenter = false;
                tester.subcases_current_max_level = 0;
                dyn::Clear(tester.subcases_stack);

                try {
                    auto const result = test_case.f(tester);
                    if (result.outcome.HasError()) {
                        ++suite->num_failures;
                        case_entry->result = TestResults::Case::Result::Failed;
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
                    ++suite->num_failures;
                    case_entry->result = TestResults::Case::Result::Failed;
                } catch (PanicException) {
                    ++suite->num_errors;
                    case_entry->result = TestResults::Case::Result::Error;
                    tester.should_reenter = false;
                    tester.current_test_case->failed = true;
                    tester.log.Error("Failed: test panicked");
                } catch (...) {
                    ++suite->num_errors;
                    case_entry->result = TestResults::Case::Result::Error;
                    tester.should_reenter = false;
                    tester.current_test_case->failed = true;
                    tester.log.Error("Failed: an exception was thrown");
                }

                if (!tester.should_reenter) run_test = false;
            } while (run_test);

            case_entry->log_content = output_buffer.ToOwnedSpan();
            tester.log.output_buffer = nullptr;

            if (tester.delete_fixture) tester.delete_fixture(tester.fixture_pointer, tester.fixture_arena);

            if (!test_case.failed)
                tester.log.Debug(ANSI_COLOUR_FOREGROUND_GREEN("Passed") " ({})\n", stopwatch);
            else
                tester.log.Error("Failed\n");

            suite->num_assertions += tester.current_test_num_assertions;
        }
    }
    tester.current_test_case = nullptr;

    tester.log.Info("Summary");
    tester.log.Info("--------");
    tester.log.Info("Assertions: {}", ({
                        usize total = 0;
                        for (auto const& s : test_results.suites)
                            total += s.num_assertions;
                        total;
                    }));
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

    if (junit_xml_output_path) {
        auto file = TRY_OR(OpenFile(*junit_xml_output_path, FileMode::Write()), {
            tester.log.Error("Failed to open JUnit XML output file {}: {}", *junit_xml_output_path, error);
            return 1;
        });

        BufferedWriter<Kb(4)> buffered_writer {file.Writer()};
        auto writer = buffered_writer.Writer();
        DEFER { buffered_writer.FlushReset(); };

        TRY_OR(WriteJUnitXmlTestResults(tester, writer, test_results), {
            tester.log.Error("Failed to write JUnit XML test results: {}", error);
            return 1;
        });
    }

    return num_failed ? 1 : 0;
}

__attribute__((noinline)) void
Check(Tester& tester, bool expression, String message, FailureAction failure_action, String file, int line) {
    ++tester.current_test_num_assertions;
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
