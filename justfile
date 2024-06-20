# Copyright 2018-2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

# This file assumes 'nix develop' has already been run

native_arch_os_pair := arch() + "-" + os()
native_binary_dir := "zig-out/" + native_arch_os_pair
all_src_files := 'fd . -e .mm -e .cpp -e .hpp -e .h src' 
gen_files_dir := "build_gen"

build target_os='native':
  zig build compile -Dtargets={{target_os}} -Dbuild-mode=development

build-tracy:
  zig build compile -Dtargets=native -Dbuild-mode=development -Dtracy

build-release target_os='native':
  zig build compile -Dtargets={{target_os}} -Dbuild-mode=production

# build and report compile-time statistics
build-timed target_os='native':
  #!/usr/bin/env bash
  artifactDir={{gen_files_dir}}/clang-build-analyzer-artifacts
  reportFile={{gen_files_dir}}/clang-build-analyzer-report
  mkdir -p ''${artifactDir}
  ClangBuildAnalyzer --start ${artifactDir}
  time just build {{target_os}}
  returnCode=$?
  ClangBuildAnalyzer --stop ${artifactDir} ${reportFile}
  ClangBuildAnalyzer --analyze ${reportFile}
  exit ${returnCode}

check-reuse:
  reuse lint

check-format:
  {{all_src_files}} | xargs clang-format --dry-run --Werror

# install compile database (compile_commands.json)
install-cbd arch_os_pair=native_arch_os_pair:
  cp {{gen_files_dir}}/compile_commands_{{arch_os_pair}}.json {{gen_files_dir}}/compile_commands.json

clang-tidy arch_os_pair=native_arch_os_pair: (install-cbd arch_os_pair)
  #!/usr/bin/env bash
  jq -r '.[].file' {{gen_files_dir}}/compile_commands_{{arch_os_pair}}.json | xargs clang-tidy -p {{gen_files_dir}} 

clang-tidy-all: (clang-tidy "x86_64-linux") (clang-tidy "x86_64-windows") (clang-tidy "aarch64-macos")

# IMPROVE: (June 2024) cppcheck v2.14.0 and v2.14.1 thinks there are syntax errors in valid code. It could be a cppcheck bug or it could be an incompatibility in how we are using it. Regardless, we should try again in the future and see if it's fixed. If it works it should run alongside clang-tidy in CI, etc.
# cppcheck arch_os_pair=native_arch_os_pair:
#   # IMPROVE: use --check-level=exhaustive?
#   # IMPROVE: investigate other flags such as --enable=constVariable
#   cppcheck --project={{justfile_directory()}}/{{gen_files_dir}}/compile_commands_{{arch_os_pair}}.json --cppcheck-build-dir={{justfile_directory()}}/.zig-cache --enable=unusedFunction --error-exitcode=2

_build_if_requested condition build-type:
  if [[ -n "{{condition}}" ]]; then just build {{build-type}}; fi

format:
  {{all_src_files}} | xargs clang-format -i

test-clap-val build="": (_build_if_requested build "native")
  clap-validator validate --in-process {{native_binary_dir}}/Floe.clap

test-units build="" +args="": (_build_if_requested build "native")
  {{native_binary_dir}}/tests {{args}}

test-pluginval build="": (_build_if_requested build "native")
  pluginval {{native_binary_dir}}/Floe.vst3

test-pluginval-au build="": (_build_if_requested build "native")
  pluginval {{native_binary_dir}}/Floe.component

test-vst3-val build="": (_build_if_requested build "native")
  timeout 2 {{native_binary_dir}}/VST3-Validator {{native_binary_dir}}/Floe.vst3

[linux]
test-wine-vst3-val build="": (_build_if_requested build "windows")
  wine zig-out/x86_64-windows/VST3-Validator.exe zig-out/x86_64-windows/Floe.vst3

[linux]
test-wine-pluginval build="": (_build_if_requested build "windows")
  wine $PLUGINVAL_WINDOWS_PATH zig-out/x86_64-windows/Floe.vst3

[linux]
test-wine-units build="": (_build_if_requested build "windows")
  wine zig-out/x86_64-windows/tests.exe

[linux]
test-wine-clap-val build="": (_build_if_requested build "windows")
  wine $CLAPVAL_WINDOWS_PATH validate zig-out/x86_64-windows/Floe.clap

[linux]
coverage build="": (_build_if_requested build "native")
  mkdir -p {{gen_files_dir}}
  # IMPROVE: run other tests with coverage and --merge the results
  kcov --include-pattern={{justfile_directory()}}/src {{gen_files_dir}}/coverage-out {{native_binary_dir}}/tests

[linux]
valgrind build="": (_build_if_requested build "native")
  valgrind --fair-sched=yes {{native_binary_dir}}/tests

# IMPROVE: add auval tests on macos
checks_level_0 := replace( 
  "
  check-reuse
  check-format
  test-units
  test-clap-val
  test-pluginval
  test-vst3-val
  " + 
  if os() == "linux" {
    "
    test-wine-vst3-val
    test-wine-pluginval
    test-wine-clap-val
    test-wine-units
    "
  } else {
    "test-pluginval-au"
  }, "\n", " ")

checks_level_1 := checks_level_0 + replace( 
  "
  clang-tidy
  ", "\n", " ")

# IMPROVE: Linux CI: enable plugin tests when we have a solution to the crashes
# IMPROVE: Linux CI: enable wine tests when we have a way to install wine on CI
checks_ci := replace(
  if os() == "linux" {
    "
    check-reuse 
    check-format
    test-units
    coverage
    clang-tidy-all
    test-clap-val
    "
  } else {
    "
    test-units
    test-clap-val
    test-pluginval
    test-vst3-val
    test-pluginval-au
    "
  }, "\n", " ")

test level="0" build="": (_build_if_requested build "dev") (parallel if level == "0" { checks_level_0 } else { checks_level_1 })

test-ci: (parallel checks_ci)

parallel tasks:
  #!/usr/bin/env bash
  mkdir -p {{gen_files_dir}}
  results_json={{gen_files_dir}}/results.json

  # use the --bar argument only if we are not on GITHUB_ACTIONS
  progress_bar=""
  [[ -z $GITHUB_ACTIONS ]] && progress_bar="--bar"

  parallel $progress_bar --results $results_json just ::: {{tasks}}

  # parallel's '--results x.json' flag does not produce valid JSON, so we need to fix it
  sed 's/$/,/' $results_json | head -c -2 > results.json.tmp
  { echo "["; cat results.json.tmp; echo "]"; } > $results_json
  rm results.json.tmp

  # remove any items where `Command == ""` (for some reason just adds these)
  jq "[ .[] | select(.Command != \"\") ]" $results_json > results.json.tmp
  mv results.json.tmp $results_json

  # print stdout and stderr for failed 
  jq -r '.[] | select(.Exitval != 0) | "\n\u001b[34m[Stdout] \(.Command):\u001b[0m", .Stdout, "\n\u001b[34m[Stderr] \(.Command):\u001b[0m", .Stderr' $results_json

  # prepare a TSV summary of the results
  summary=$(jq -r '["Command", "Time(s)", "Return-Code"], (.[] | [.Command, .JobRuntime, .Exitval]) | @tsv' $results_json)
  failed=$(jq '. | map(select(.Exitval != 0)) | length' $results_json)
  num_tasks=$(jq '. | length' $results_json)

  # use Miller to pretty-print the summary, along with a markdown version for GitHub Actions
  echo -e "\033[0;34m[Summary]\033[0m"
  [[ ! -z $GITHUB_ACTIONS ]] && echo "# Summary ({{os()}})" >> $GITHUB_STEP_SUMMARY && echo "" >> $GITHUB_STEP_SUMMARY
  printf "%s\n" "$summary" | mlr --itsv --opprint sort -f "Return-Code"
  [[ ! -z $GITHUB_ACTIONS ]] && printf "%s\n" "$summary" | mlr --itsv --omd sort -f "Return-Code" >> $GITHUB_STEP_SUMMARY && echo "" >> $GITHUB_STEP_SUMMARY

  if [ $failed -eq 0 ]; then
    echo -e "\033[0;32mAll $num_tasks tasks passed\033[0m"
    [[ ! -z $GITHUB_ACTIONS ]] && echo "### :white_check_mark: All $num_tasks tasks succeeded" >> $GITHUB_STEP_SUMMARY
    exit 0
  else
    echo -e "\033[0;31m$failed/$num_tasks tasks failed\033[0m"
    [[ ! -z $GITHUB_ACTIONS ]] && echo "### :x: $failed/$num_tasks tasks failed" >> $GITHUB_STEP_SUMMARY
    exit 1
  fi
