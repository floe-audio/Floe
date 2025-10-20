# Copyright 2018-2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

# NOTE: for the most part, we assume that `nix develop` has been run before using this justfile

set dotenv-load

native_arch_os_pair := arch() + "-" + os()
native_binary_dir := join("zig-out", native_arch_os_pair)
native_binary_dir_abs := join(justfile_directory(), native_binary_dir)
all_src_files := 'fd . -e .mm -e .cpp -e .hpp -e .h src' 
cache_dir := ".floe-cache"
release_files_dir := join(justfile_directory(), "zig-out", "release") # for final release files
run_windows_program := if os() == 'windows' {
  ''
} else {
  'wine'
}

# See the comment in .env.common.
# IMPROVE: we want to somehow get this value from .env.common to avoid duplication.
export ZIG_GLOBAL_CACHE_DIR := ".zig-cache-global"

build *ARGS:
  zig build {{ARGS}}
  just patch-rpath


# On Linux, if we have built our files using a Nix environment, the binaries will have references to Nix store paths.
# They won't work if the host system is not NixOS, so we need to run some commands to patch them.
patch-rpath:
  #!/usr/bin/env bash
  if [[ "{{os()}}" == "linux" && ! -f "/etc/NIXOS" ]]; then
    patch_file() {
      command=$1
      file=$2

      if [[ ! -f $file ]]; then
        echo "patch-rpath: file not found: $file"
        exit 1
      fi

      $command $file
    }

    patch_file patchrpath "{{native_binary_dir}}/Floe.clap"
    patch_file patchrpath "{{native_binary_dir}}/Floe.vst3/Contents/x86_64-linux/Floe.so"
    patch_file patchinterpreter "{{native_binary_dir}}/tests"
    patch_file patchinterpreter "{{native_binary_dir}}/docs_generator"
    patch_file patchinterpreter "{{native_binary_dir}}/VST3-Validator"
  fi

# This fetches logos too which may be not be GPL licenced.
build-release target_os='native':
  zig build compile -Dtargets={{target_os}} \
      -Dbuild-mode=production \
      -Dfetch-floe-logos=true 

# build and report compile-time statistics
build-timed *ARGS:
  #!/usr/bin/env bash
  artifactDir={{cache_dir}}/clang-build-analyzer-artifacts
  reportFile={{cache_dir}}/clang-build-analyzer-report
  mkdir -p ''${artifactDir}
  ClangBuildAnalyzer --start ${artifactDir}
  zig build {{ARGS}}
  returnCode=$?
  ClangBuildAnalyzer --stop ${artifactDir} ${reportFile}
  ClangBuildAnalyzer --analyze ${reportFile}
  exit ${returnCode}

check-reuse:
  reuse lint

check-format:
  {{all_src_files}} | xargs clang-format --dry-run --Werror

# hunspell doesn't do anything fancy at all, it just checks each word for spelling. It means we get lots of
# false positives, but I think it's still worth it. We can just add words to ignored-spellings.dic.
# In vim, use :sort u to remove duplicates.
[unix]
check-spelling:
  #!/usr/bin/env bash
  set -euo pipefail
  output=$(fd . -e .md -e .mdx --exclude third_party_libs/ | xargs hunspell -l -d en_GB -p ignored-spellings.dic | sort -u)
  echo "$output"
  test "$output" == ""

[unix]
check-links:
  #!/usr/bin/env bash
  set -euxo pipefail

  # If our website is being served locally (Docusaurus dev server), we can check links against the local version.
  docusaurus_localhost="http://localhost:3000"
  declare -a extra_args=()
  if curl -s --head --request GET "$docusaurus_localhost" | grep "200 OK" > /dev/null; then
    extra_args=(--remap "https://floe.audio $docusaurus_localhost" --base "$docusaurus_localhost")
  fi

  # For some reason creativecommons links return 403 via lychee, so we exclude them.
  lychee --exclude 'https://creativecommons.org/licenses/by/2.0' \
         --exclude 'https://creativecommons.org/licenses/by/4.0' \
         --exclude 'https://creativecommons.org/licenses/by-sa/4.0' \
         "${extra_args[@]}" \
         website readme.md

# install Compile DataBase (compile_commands.json)
install-cbd arch_os_pair=native_arch_os_pair:
  #!/usr/bin/env bash
  cdb_file="{{cache_dir}}/compile_commands_{{arch_os_pair}}.json"

  if [[ ! -f $cdb_file ]]; then
    echo "WARNING: compile_commands.json file not found for arch+OS: {{arch_os_pair}}"
    exit 0
  fi

  cp {{cache_dir}}/compile_commands_{{arch_os_pair}}.json {{cache_dir}}/compile_commands.json

clang-tidy arch_os_pair=native_arch_os_pair: (install-cbd arch_os_pair)
  #!/usr/bin/env bash
  cdb_file="{{cache_dir}}/compile_commands_{{arch_os_pair}}.json"

  # We return early with a warning if the compile commands file doesn't exist.
  if [[ ! -f $cdb_file ]]; then
    echo "WARNING: compile_commands.json file not found for arch+OS: {{arch_os_pair}}"
    exit 0
  fi

  # NOTE: we specify the config file because we don't want clang-tidy to go automatically looking for it and 
  # sometimes finding .clang-tidy files in third-party libraries that are incompatible with our version of clang-tidy
  jq -r '.[].file' "$cdb_file" | \
      grep -E -i "^{{justfile_directory()}}[/\\]src[/\\]" |
      xargs clang-tidy --config-file=.clang-tidy -p "{{cache_dir}}"

clang-tidy-all: (clang-tidy "x86_64-linux") (clang-tidy "x86_64-windows") (clang-tidy "aarch64-macos")

upload-errors:
  #!/usr/bin/env bash
  set -euxo pipefail
  
  case "$(uname -s)" in
    Linux*)   dir="$HOME/.local/state/Floe/Logs" ;;
    Darwin*)  dir="$HOME/Library/Logs/Floe" ;;
    MINGW*|CYGWIN*|MSYS*) dir="$LOCALAPPDATA/Floe/Logs" ;;
    *) echo "Unsupported OS" && exit 1 ;;
  esac

  if [ ! -d "$dir" ]; then
    exit 0
  fi
  
  cd "$dir" || exit 1
  for report in *.floe-report; do
    if [ -f "$report" ]; then
      sentry-cli send-envelope --raw "$report"
      rm "$report"
    fi
  done

project-items-json:
  gh project item-list 1 --owner floe-audio --limit 100 --format json

# Get project item ID for an issue number. All Floe Github issues are added to the project board automatically.
project-item-id issue_number:
  just project-items-json | jq -r ".items[] | select(.content.number == {{issue_number}}) | .id"

# Our project board has a 'status' field for tracking our workflow. This command gets the status for an issue number.
project-status issue_number:
  just project-items-json | jq -r ".items[] | select(.content.number == {{issue_number}}) | .status // \"null\""

# Set project board status for an issue number
# Status options: "Up Next", "In Progress", "Done"
project-set-status issue_number status:
  #!/usr/bin/env bash
  issue_id=$(just project-item-id {{issue_number}})
  if [ -z "$issue_id" ]; then
    echo "Error: Issue {{issue_number}} not found in project"
    exit 1
  fi
  
  # Map status names to option IDs
  case "{{status}}" in
    "Up Next") option_id="cabe1aa3" ;;
    "In Progress") option_id="47fc9ee4" ;;
    "Done") option_id="98236657" ;;
    *) echo "Error: Invalid status '{{status}}'. Valid options: Up Next, In Progress, Done"; exit 1 ;;
  esac
  
  gh project item-edit --id "$issue_id" --field-id "PVTSSF_lADOCkRkv84AkDXazgcUbLA" --single-select-option-id "$option_id" --project-id "PVT_kwDOCkRkv84AkDXa"

# Get issues by status - returns issue numbers and titles
project-issues-by-status status:
  just project-items-json | jq '.items[] | select(.status == "{{status}}") | {number: .content.number, title: .title}'

# Issues that have been solved, but not yet had a release are labelled "awaiting-release". This command
# removes that label from all closed issues. It should be run after a release is made.
release-cleanup:
  #!/usr/bin/env bash
  echo "Removing awaiting-release labels from released issues..."
  gh issue list --label "awaiting-release" --state closed --json number \
    --jq '.[].number' | while read issue_number; do
    echo "Cleaning issue #$issue_number"
    gh issue edit "$issue_number" --remove-label "awaiting-release"
  done

# Generated the static JSON that the website uses
website-generate:
  #!/usr/bin/env bash
  {{native_binary_dir}}/docs_generator > website/static/generated-data.json

# Website development and build commands for Docusaurus
website-dev:
  #!/usr/bin/env bash
  just website-generate
  cd website && npm run start

website-build:
  #!/usr/bin/env bash
  just website-generate
  cd website && npm run build

# IMPROVE: (June 2024) cppcheck v2.14.0 and v2.14.1 thinks there are syntax errors in valid code. It could be a cppcheck bug or it could be an incompatibility in how we are using it. Regardless, we should try again in the future and see if it's fixed. If it works it should run alongside clang-tidy in CI, etc.
# cppcheck arch_os_pair=native_arch_os_pair:
#   # IMPROVE: use --check-level=exhaustive?
#   # IMPROVE: investigate other flags such as --enable=constVariable
#   cppcheck --project={{justfile_directory()}}/{{cache_dir}}/compile_commands_{{arch_os_pair}}.json --cppcheck-build-dir={{justfile_directory()}}/.zig-cache --enable=unusedFunction --error-exitcode=2

format:
  {{all_src_files}} | xargs clang-format -i

# Clap Validator seems to have a bug that crashes the validator. 
# https://github.com/free-audio/clap-validator/issues/21
# We workaround this by skipping process and param tests.
# Additionally, we disable this test because we have a good reason to behave in a different way. Each instance of our plugin as an ID - we store that in the state so that loading a DAW project retains the instance IDs. But if a new instance is created and only its parameters are set, then our state will differ in terms of the instance ID - and that's okay. We don't want to fail because of this.
# state-reproducibility-flush: Randomizes a plugin's parameters, saves its state, recreates the plugin instance, sets the same parameters as before, saves the state again, and then asserts that the two states are identical. The parameter values are set updated using the process function to create the first state, and using the flush function to create the second state.
clap_val_args := "--test-filter '.*(process|param|state-reproducibility-flush).*' --invert-filter"

test-clap-val:
  clap-validator validate {{clap_val_args}} {{native_binary_dir}}/Floe.clap

test-units: 
  {{native_binary_dir}}/tests --log-level=debug --write-to-file

test-units-tsan:
  {{native_binary_dir}}-tsan/tests --log-level=debug --write-to-file

test-pluginval: 
  #!/usr/bin/env bash
  args=""
  if [[ "{{os()}}" == "linux" && ! -z "$CI" ]]; then
    args="--skip-gui-tests"
  fi
  pluginval $args --validate {{native_binary_dir}}/Floe.vst3

[macos]
check-au-installed:
  #!/usr/bin/env bash
  set -euxo pipefail
  if [[ ! -d /Library/Audio/Plug-Ins/Components/Floe.component && ! -d ~/Library/Audio/Plug-Ins/Components/Floe.component ]]; then
    echo "Floe.component not found in either /Library/Audio/Plug-Ins/Components or ~/Library/Audio/Plug-Ins/Components"
    exit 1
  fi

[macos]
install-au global='0':
  #!/usr/bin/env bash
  set -euxo pipefail
  if [[ {{global}} -eq 1 ]]; then
    sudo rm -rf /Library/Audio/Plug-Ins/Components/Floe.component
    sudo cp -r {{native_binary_dir}}/Floe.component /Library/Audio/Plug-Ins/Components/
  else
    rm -rf ~/Library/Audio/Plug-Ins/Components/Floe.component
    cp -r {{native_binary_dir}}/Floe.component ~/Library/Audio/Plug-Ins/Components/
  fi

[macos]
test-pluginval-au:
  #!/usr/bin/env bash
  set -euxo pipefail
  just check-au-installed
  pluginval {{native_binary_dir}}/Floe.component

[macos]
test-auval:
  #!/usr/bin/env bash
  set -euxo pipefail
  just check-au-installed
  auval -v aumu FLOE floA

test-vst3-val:
  timeout 20 {{native_binary_dir}}/VST3-Validator {{native_binary_dir}}/Floe.vst3

_download-and-unzip-to-cache-dir url:
  #!/usr/bin/env bash
  set -euxo pipefail
  mkdir -p {{cache_dir}}
  pushd {{cache_dir}}
  curl -O -L {{url}} 
  basename=$(basename {{url}})
  unzip $basename
  rm $basename
  popd

[linux, windows]
test-windows-installer:
  #!/usr/bin/env bash
  set -euxo pipefail
  cd zig-out/x86_64-windows
  installer_file=$(find . -type f -name "*Installer*.exe")
  if [[ -z "$installer_file" ]]; then
    echo "Installer not found"
    exit 1
  fi
  if [[ "{{os()}}" == "windows" ]]; then
    powershell.exe -Command "\$p = Start-Process '$installer_file' -Args '--autorun' -Verb RunAs -Wait -PassThru; exit \$p.ExitCode"
    # IMPROVE: test that the installer adds the registry key and then removes it
    # reg query "HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{1395024D-2B55-4B81-88CA-26DF09D175B1}" /v UninstallString
    powershell.exe -Command "\$p = Start-Process 'C:\Program Files\Floe\Floe-Uninstaller.exe' -Args '--autorun' -Verb RunAs -Wait -PassThru; exit \$p.ExitCode"

    # set +e  # Temporarily disable error propagation
    # reg query "HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{1395024D-2B55-4B81-88CA-26DF09D175B1}" /v UninstallString
    # if [ $? -eq 0 ]; then
    #     echo "ERROR: Registry key still exists after uninstall"
    #     exit 1
    # fi
    # set -e
  else
    {{run_windows_program}} $installer_file --autorun
  fi

[linux, windows]
test-windows-units:
  set -x
  {{run_windows_program}} zig-out/x86_64-windows/tests.exe --log-level=debug

[linux, windows]
test-windows-vst3-val:
  set -x
  {{run_windows_program}} zig-out/x86_64-windows/VST3-Validator.exe zig-out/x86_64-windows/Floe.vst3

[linux, windows]
test-windows-pluginval:
  #!/usr/bin/env bash
  set -x
  exe="{{cache_dir}}/pluginval.exe"
  if [[ ! -f "$exe" ]]; then
    just _download-and-unzip-to-cache-dir "https://github.com/Tracktion/pluginval/releases/download/v1.0.3/pluginval_Windows.zip"
    chmod +x "$exe"
  fi
  {{run_windows_program}} "$exe" --verbose --validate zig-out/x86_64-windows/Floe.vst3

[linux, windows]
test-windows-clap-val:
  #!/usr/bin/env bash
  set -x
  exe="{{cache_dir}}/clap-validator.exe"
  if [[ ! -f "$exe" ]]; then
    just _download-and-unzip-to-cache-dir  "https://github.com/free-audio/clap-validator/releases/download/0.3.2/clap-validator-0.3.2-windows.zip"
    chmod +x "$exe"
  fi
  timeout 5 {{run_windows_program}} "$exe" validate {{clap_val_args}} zig-out/x86_64-windows/Floe.clap

[linux]
coverage:
  set -x
  mkdir -p {{cache_dir}}
  # IMPROVE: run other tests with coverage and --merge the results
  kcov --include-pattern={{justfile_directory()}}/src {{cache_dir}}/coverage-out {{native_binary_dir}}/tests

# IMPROVE: also run validators through valgrind
[linux]
valgrind:
  valgrind \
    --leak-check=full \
    --fair-sched=yes \
    --num-callers=25 \
    --gen-suppressions=all \
    --suppressions=valgrind.supp \
    --error-exitcode=1 \
    --exit-on-first-error=no \
    {{native_binary_dir}}/tests \
    --log-level=debug \
    --write-to-file \
    --filter=*Hash*

checks_level_0 := replace( 
  "
  check-reuse
  check-format
  check-links
  check-spelling
  test-units
  test-clap-val
  test-vst3-val
  " + 
  if os() == "linux" {
    "
    test-windows-clap-val
    test-windows-units
    "
  } else {
    "
    test-pluginval
    "
  }, "\n", " ")

checks_level_1 := checks_level_0 + replace( 
  "
  clang-tidy
  ", "\n", " ")

checks_ci := replace(
  "
    test-units
    test-units-tsan
    test-clap-val
    test-vst3-val
  " +
  if os() == "linux" {
    "
    check-reuse 
    check-format
    check-spelling
    check-links
    coverage
    clang-tidy-all
    valgrind
    "
  } else if os() == "macos" {
    "
    test-pluginval
    test-pluginval-au
    test-auval
    "
  }, "\n", " ")

checks_ci_optimised := replace(
  "
    test-units
    test-clap-val
    test-vst3-val
  " +
  if os() == "macos" {
    "
    test-pluginval
    test-pluginval-au
    test-auval
    "
  }, "\n", " ")

[unix]
test level="0": (parallel if level == "0" { checks_level_0 } else { checks_level_1 })

[unix]
test-ci optimised="0": 
  #!/usr/bin/env bash
  set -x

  # Start our website so check-links fully works
  just website-generate
  pushd website
  npm run start &
  DOCUSAURUS_PID=$!
  popd

  # Start go-httpbin server for HTTP testing
  go-httpbin -host 127.0.0.1 -port 8081 &
  HTTPBIN_PID=$!

  sleep 2 # Wait a moment for both servers to fully start

  if [[ "{{optimised}}" -eq 1 ]]; then
      just parallel "{{checks_ci_optimised}}"
      return_code=$?
  else
      just parallel "{{checks_ci}}"
      return_code=$?
  fi

  kill $DOCUSAURUS_PID
  kill $HTTPBIN_PID

  exit $return_code

[unix]
install-pre-commit-hook:
  rm -f .git/hooks/pre-commit
  echo "#!/usr/bin/env bash" > .git/hooks/pre-commit
  echo "set -euo pipefail" >> .git/hooks/pre-commit
  echo "echo '[===] Running pre-commit checks...'" >> .git/hooks/pre-commit
  echo "just pre-commit-checks" >> .git/hooks/pre-commit
  echo "echo '[===] All pre-commit checks passed'" >> .git/hooks/pre-commit
  echo "echo ''" >> .git/hooks/pre-commit
  chmod +x .git/hooks/pre-commit

[unix]
pre-commit-checks: (parallel "check-format check-reuse check-spelling check-links") 

_print-ci-summary num_tasks num_failed:
  #!/usr/bin/env bash
  if [ "{{num_failed}}" -eq 0 ]; then
    echo -e "\033[0;32mAll {{num_tasks}} tasks passed\033[0m"
    [[ ! -z $GITHUB_ACTIONS ]] && echo "### :white_check_mark: All {{num_tasks}} tasks succeeded" >> $GITHUB_STEP_SUMMARY
  else
    echo -e "\033[0;31m{{num_failed}}/{{num_tasks}} tasks failed\033[0m"
    [[ ! -z $GITHUB_ACTIONS ]] && echo "### :x: {{num_failed}}/{{num_tasks}} tasks failed" >> $GITHUB_STEP_SUMMARY
  fi
  exit 0

[windows, linux]
test-ci-windows:
  #!/usr/bin/env bash
  set -x

  num_tasks=0
  num_failed=0

  if [[ ! -v GITHUB_ACTIONS ]]; then
    mkdir -p {{cache_dir}}
    rm -f {{cache_dir}}/test_ci_windows_summary.md
    export GITHUB_STEP_SUMMARY={{cache_dir}}/test_ci_windows_summary.md
  fi

  echo "# Summary (Windows)" >> $GITHUB_STEP_SUMMARY && echo "" >> $GITHUB_STEP_SUMMARY
  echo "| Command | Return-Code |" >> $GITHUB_STEP_SUMMARY
  echo "| --- | --- |" >> $GITHUB_STEP_SUMMARY

  # Start go-httpbin server for HTTP testing
  go run github.com/mccutchen/go-httpbin/v2/cmd/go-httpbin@latest -host 127.0.0.1 -port 8081 &
  HTTPBIN_PID=$!

  # Wait for server to be ready (poll with timeout)
  echo "Waiting for go-httpbin server to start..."
  for i in {1..90}; do
    if curl -s --connect-timeout 1 http://127.0.0.1:8081/status >/dev/null 2>&1; then
      echo "go-httpbin server is ready"
      break
    fi
    if [ $i -eq 90 ]; then
      echo "Timeout waiting for go-httpbin server to start"
      kill $HTTPBIN_PID 2>/dev/null || true
      exit 1
    fi
    sleep 1
  done

  test() {
    local name="$1"

    just "$name"
    local result=$?

    echo "| $name | $result |" >> $GITHUB_STEP_SUMMARY
    num_tasks=$((num_tasks + 1))
    [[ $result -ne 0 ]] && num_failed=$((num_failed + 1))
  }
  
  test test-windows-pluginval 
  test test-windows-units
  test test-windows-vst3-val 
  test test-windows-clap-val

  # Try to kill the go-httpbin server
  kill $HTTPBIN_PID 2>/dev/null || true
  # Fallback: kill any remaining go-httpbin processes
  pkill -f "go-httpbin" 2>/dev/null || true

  if [[ ! -v $GITHUB_ACTIONS ]]; then
    cat {{cache_dir}}/test_ci_windows_summary.md
  fi

  just _print-ci-summary $num_tasks $num_failed
  if [ $num_failed -ne 0 ]; then
    exit 1
  fi

[unix]
parallel tasks:
  #!/usr/bin/env bash
  set -x
  mkdir -p {{cache_dir}}
  results_json={{cache_dir}}/parallel_cmd_results.json

  # use the --bar argument only if we are not on GITHUB_ACTIONS
  progress_bar=""
  [[ -z $GITHUB_ACTIONS ]] && progress_bar="--bar"

  parallel $progress_bar --results $results_json just ::: {{tasks}}

  # parallel's '--results x.json' flag does not produce valid JSON, so we need to fix it
  sed 's/$/,/' $results_json | head -c -2 > parallel_cmd_results.json.tmp
  { echo "["; cat parallel_cmd_results.json.tmp; echo "]"; } > $results_json
  rm parallel_cmd_results.json.tmp

  # remove any items where `Command == ""` (for some reason just adds these)
  jq "[ .[] | select(.Command != \"\") ]" $results_json > parallel_cmd_results.json.tmp
  mv parallel_cmd_results.json.tmp $results_json

  # print stdout and stderr for failed 
  jq -r '.[] | select(.Exitval != 0) | "\n\u001b[34m[Stdout] \(.Command):\u001b[0m", .Stdout, "\n\u001b[34m[Stderr] \(.Command):\u001b[0m", .Stderr' $results_json

  # prepare a TSV summary of the results
  summary=$(jq -r '["Command", "Time(s)", "Return-Code"], (.[] | [.Command, .JobRuntime, .Exitval]) | @tsv' $results_json)
  num_failed=$(jq '. | map(select(.Exitval != 0)) | length' $results_json)
  num_tasks=$(jq '. | length' $results_json)

  # use Miller to pretty-print the summary, along with a markdown version for GitHub Actions
  echo -e "\033[0;34m[Summary]\033[0m"
  [[ ! -z $GITHUB_ACTIONS ]] && echo "# Summary ({{os()}})" >> $GITHUB_STEP_SUMMARY && echo "" >> $GITHUB_STEP_SUMMARY
  printf "%s\n" "$summary" | mlr --itsv --opprint sort -f "Return-Code"
  [[ ! -z $GITHUB_ACTIONS ]] && printf "%s\n" "$summary" | mlr --itsv --omd sort -f "Return-Code" >> $GITHUB_STEP_SUMMARY && echo "" >> $GITHUB_STEP_SUMMARY

  just _print-ci-summary $num_tasks $num_failed
  if [ $num_failed -ne 0 ]; then
    exit 1
  fi

[unix]
echo-latest-changes:
  #!/usr/bin/env bash
  # Extract text from the heading with the current version number until the next heading with exactly 2 hashes
  version=$(cat version.txt)
  changes=$(sed -n "/^## $version/,/^## [^#]/ { /^## [^#]/!p }" website/docs/changelog.md)
  printf "%s" "$changes" # trim trailing newline

[unix, no-cd]
_create-manual-install-readme os_name:
  #!/usr/bin/env bash
  echo "These are the manual-install {{os_name}} plugin files for Floe version $(cat {{justfile_directory()}}/version.txt)." > readme.txt
  echo "" >> readme.txt
  echo "It's normally easier to use the installer instead of these manual-install files." >> readme.txt
  echo "The installer is a separate download to this." >> readme.txt
  echo "" >> readme.txt
  echo "Installation instructions: https://floe.audio/" >> readme.txt

[unix]
windows-prepare-release:
  #!/usr/bin/env bash
  set -euxo pipefail

  version=$(cat version.txt)

  mkdir -p {{release_files_dir}}

  [[ ! -d zig-out/x86_64-windows ]] && echo "x86_64-windows folder not found" && exit 1
  cd zig-out/x86_64-windows

  installer_file=$(find . -type f -name "*Installer*.exe")

  # zip the installer
  final_installer_name=$(echo $installer_file | sed 's/.exe//')
  final_installer_zip_name="Floe-Installer-v$version-Windows.zip"
  zip -r $final_installer_zip_name $installer_file
  mv $final_installer_zip_name {{release_files_dir}}

  # zip the manual-install files
  just _create-manual-install-readme "Windows"
  final_manual_zip_name="Floe-Manual-Install-v$version-Windows.zip"
  zip -r $final_manual_zip_name Floe.clap Floe.vst3 readme.txt
  rm readme.txt
  mv $final_manual_zip_name {{release_files_dir}}

  # zip the packager
  final_packager_zip_name="Floe-Packager-v$version-Windows.zip"
  zip -r $final_packager_zip_name floe-packager.exe
  mv $final_packager_zip_name {{release_files_dir}}

[unix]
linux-prepare-release:
  #!/usr/bin/env bash
  set -euxo pipefail
  
  version=$(cat version.txt)
  mkdir -p {{release_files_dir}}
  [[ ! -d zig-out/x86_64-linux ]] && echo "x86_64-linux folder not found" && exit 1
  cd zig-out/x86_64-linux
  
  # CLAP
  final_clap_tar_name="Floe-CLAP-v$version-Linux.tar.gz"
  tar -czf $final_clap_tar_name Floe.clap
  mv $final_clap_tar_name {{release_files_dir}}
  
  # VST3
  final_vst3_tar_name="Floe-VST3-v$version-Linux.tar.gz"
  tar -czf $final_vst3_tar_name Floe.vst3
  mv $final_vst3_tar_name {{release_files_dir}}
  
  # Packager
  final_packager_tar_name="Floe-Packager-v$version-Linux.tar.gz"
  tar -czf $final_packager_tar_name floe-packager
  mv $final_packager_tar_name {{release_files_dir}}

[macos, no-cd]
macos-notarize file:
  #!/usr/bin/env bash
  set -euo pipefail # don't use 'set -x' because it might print sensitive information
  xcrun notarytool submit {{file}} --apple-id "$MACOS_NOTARIZATION_USERNAME" --password "$MACOS_NOTARIZATION_PASSWORD" --team-id $MACOS_TEAM_ID --wait

[macos]
macos-prepare-packager folder notarize="1":
  #!/usr/bin/env bash
  set -euxo pipefail
  [[ ! -f version.txt ]] && echo "version.txt file not found" && exit 1
  [[ ! -d zig-out/{{folder}} ]] && echo "{{folder}} folder not found" && exit 1

  version=$(cat version.txt)
  mkdir -p {{release_files_dir}}

  cd zig-out/{{folder}}

  just _workaround-invalid-macho floe-packager

  codesign --sign "$MACOS_DEV_ID_APP_NAME" --timestamp --options=runtime --force floe-packager
  
  arch_name=$(just _get-arch-name "floe-packager")

  final_packager_zip_name="Floe-Packager-v$version-macOS-$arch_name.zip"
  zip $final_packager_zip_name floe-packager

  if [[ "{{notarize}}" -eq 1 ]]; then
    just macos-notarize "$final_packager_zip_name"
    # NOTE: we can't staple the packager because it's a Unix binary 
  fi

  mv $final_packager_zip_name {{release_files_dir}}

[macos, no-cd]
_check-bundle bundle label:
  #!/usr/bin/env bash
  set -euxo pipefail
  echo "Checking {{bundle}} {{label}}"
  exe="{{bundle}}/Contents/MacOS/Floe"
  otool -l $exe | head -20 # check mach-o validity
  vtool -show-build $exe # another mach-o check
  dsymutil --dump-debug-map $exe | tail -20 # check debug info
  dsymutil --verify $exe # check debug info
  lipo -archs $exe # check mach-o validity and arch

[macos, no-cd]
_workaround-invalid-macho filepath:
  #!/usr/bin/env bash
  set -euxo pipefail
  # I believe there is a bug in the Zig mach-o toolchain that results in binaries that become invalid after codesigning. Perhaps 
  # it is an isuse with codesign. rcodesign and zsign will not even run on the binary. `codesign` succeeds, but the binary is invalid. See the various checks we do in _check-bundle.
  # 
  # otool: error: truncated or malformed object (offset field of section 0 in LC_SEGMENT_64 command 0 not past the headers of the
  # file)
  #
  # To workaround this, we can first run the binary through vtool which seems to happily accept the binary and convert it into     
  # something that codesign doesn't mess up. It was trial and error to find a command that works. Removing the source version 
  # seems to work for all binaries we are currently using. -set-source-version worked for some binaries but not all.
  file {{filepath}}
  vtool -remove-source-version -output {{filepath}}-fixed {{filepath}} # arbitrary change to trigger MachO fix
  rm {{filepath}}
  mv {{filepath}}-fixed {{filepath}}

[macos, no-cd]
_get-arch-name filepath:
  #!/usr/bin/env bash
  set -euxo pipefail
  # Helper function to determine architecture name from binary
  arch_info=$(lipo -archs {{filepath}} | xargs)  # xargs trims whitespace
  
  if [[ "$arch_info" == "arm64" ]]; then
    echo "Apple-Silicon"
  elif [[ "$arch_info" == "x86_64" ]]; then
    echo "Intel"
  elif [[ "$arch_info" == "x86_64 arm64" || "$arch_info" == "arm64 x86_64" ]]; then
    echo "Universal"
  else
    echo "ERROR: Unsupported architecture: $arch_info" >&2
    exit 1
  fi

[macos]
macos-prepare-release-plugins folder notarize="1":
  #!/usr/bin/env bash
  set -euxo pipefail
  [[ ! -f version.txt ]] && echo "version.txt file not found" && exit 1
  [[ ! -d zig-out/{{folder}} ]] && echo "{{folder}} folder not found" && exit 1

  version=$(cat version.txt)
  mkdir -p {{release_files_dir}}

  cd zig-out/{{folder}}

  # step 1: codesign
  cat >plugin.entitlements <<EOF
  <?xml version="1.0" encoding="UTF-8"?>
  <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
  <plist version="1.0">
  <dict>
      <key>com.apple.security.app-sandbox</key>
      <true/>
      <key>com.apple.security.files.user-selected.read-write</key>
      <true/>
      <key>com.apple.security.assets.music.read-write</key>
      <true/>
      <key>com.apple.security.files.bookmarks.app-scope</key>
      <true/>
  </dict>
  </plist>
  EOF

  codesign_plugin() {
    just _check-bundle $1 "before codesigning"

    just _workaround-invalid-macho $1/Contents/MacOS/Floe

    codesign --sign "$MACOS_DEV_ID_APP_NAME" --timestamp --options=runtime --deep --force --strict --entitlements plugin.entitlements $1
    just _check-bundle $1 "after codesigning"

    codesign --verify $1 --verbose
  }

  plugin_list="Floe.clap Floe.vst3 Floe.component"

  # we can do it in parallel for speed, but we need to be careful there's no conflicting use of the filesystem
  export -f codesign_plugin
  SHELL=$(type -p bash) parallel --bar codesign_plugin ::: $plugin_list

  rm plugin.entitlements

  # step 2: notarize
  if [[ "{{notarize}}" -eq 1 ]]; then
    notarize_plugin() {
        plugin=$1
        temp_subdir=notarizing_$plugin

        just _check-bundle $plugin "before notarize and stapling"

        rm -rf $temp_subdir
        mkdir -p $temp_subdir
        zip -r $temp_subdir/$plugin.zip $plugin

        just macos-notarize $temp_subdir/$plugin.zip

        unzip $temp_subdir/$plugin.zip -d $temp_subdir
        xcrun stapler staple $temp_subdir/$plugin
        # replace the original bundle with the stapled one
        rm -rf $plugin
        mv $temp_subdir/$plugin $plugin
        just _check-bundle $plugin "after notarize and stapling"
        rm -rf $temp_subdir
    }

    # we can do it in parallel for speed, but we need to be careful there's no conflicting use of the filesystem
    export -f notarize_plugin
    SHELL=$(type -p bash) parallel --bar notarize_plugin ::: $plugin_list
  fi

  # step 3: determine architecture and zip
  plugin_executable="Floe.clap/Contents/MacOS/Floe"
  arch_name=$(just _get-arch-name "$plugin_executable")
  
  just _create-manual-install-readme "macOS"
  final_manual_zip_name="Floe-Manual-Install-v$version-macOS-$arch_name.zip"
  rm -f $final_manual_zip_name
  zip -r $final_manual_zip_name $plugin_list readme.txt
  mv $final_manual_zip_name {{release_files_dir}}
  rm readme.txt

[macos]
macos-build-installer folder:
  #!/usr/bin/env bash
  set -euxo pipefail
  [[ ! -f version.txt ]] && echo "version.txt file not found" && exit 1
  [[ ! -d zig-out/{{folder}} ]] && echo "{{folder}} folder not found" && exit 1

  mkdir -p "{{release_files_dir}}"

  version=$(cat version.txt)
  zig_out_abs_path="{{justfile_directory()}}/zig-out/{{folder}}"
  final_installer_name="Floe-Installer-v$version"

  cd $zig_out_abs_path

  temp_working_subdir="temp_installer_working_subdir"
  rm -rf "$temp_working_subdir"
  mkdir -p "$temp_working_subdir"
  pushd "$temp_working_subdir"

  distribution_xml_choices=""
  distribution_xml_choice_outlines=""

  add_package_to_distribution_xml() {
    local identifier="$1"
    local title="$2"
    local description="$3"
    local pkg_name="$4"

    local choice=$(cat <<EOF
    <choice id="$identifier" title="$title" description="$description">
        <pkg-ref id="$identifier" version="$version">$pkg_name</pkg-ref>
    </choice>
  EOF)
    # add the choices with correct newlines
    distribution_xml_choices=$(printf "%s%s\n" "$distribution_xml_choices" "$choice")
    distribution_xml_choice_outlines=$(printf "%s%s\n" "$distribution_xml_choice_outlines" "<line choice=\"$identifier\" />")
  }

  # make packages for each plugin so they are selectable options in the final installer
  make_package() {
    local file_extension="$1"
    local destination_plugin_folder="$2"
    local title="$3"
    local description="$4"

    local identifier="com.Floe.$file_extension"
    local plugin_path="$zig_out_abs_path/Floe.$file_extension"
    local install_location="/Library/Audio/Plug-Ins/$destination_plugin_folder"

    codesign --verbose --verify "$plugin_path" || { echo "ERROR: the plugin file isn't codesigned, do that before this command"; exit 1; }

    pkgbuild --identifier "$identifier" --version "$version" --component "$plugin_path" --install-location "$install_location" "$identifier.pkg"

    add_package_to_distribution_xml "$identifier" "$title" "$description" "$identifier.pkg"
  }

  make_package vst3 VST3 "Floe VST3" "VST3 format of the Floe plugin"
  make_package component Components "Floe AudioUnit (AUv2)" "AudioUnit (v2) format of the Floe plugin"
  make_package clap CLAP "Floe CLAP" "CLAP format of the Floe plugin"

  # make the final installer combining all the packages
  mkdir -p productbuild_files
  echo "This application will install Floe on your computer. You will be able to select which types of audio plugin format you would like to install. Please note that sample libraries are separate: this installer just installs the Floe engine." > productbuild_files/welcome.txt

  # find the min macos version from one of the plugin's plists
  min_macos_version=$(grep -A 1 '<key>LSMinimumSystemVersion</key>' "$zig_out_abs_path/Floe.clap/Contents/Info.plist" | grep '<string>' | sed 's/.*<string>\(.*\)<\/string>.*/\1/')

  # Determine the architecture(s) from the executable
  plugin_executable="$zig_out_abs_path/Floe.clap/Contents/MacOS/Floe"
  arch_info=$(lipo -archs "$plugin_executable" | xargs)  # xargs trims whitespace
  arch_name=$(just _get-arch-name "$plugin_executable")

  # Set the architecture restriction for the installer based on the plugin architecture
  if [[ "$arch_info" == "arm64" ]]; then
    # Only arm64 architecture
    host_architectures='hostArchitectures="arm64"'
  elif [[ "$arch_info" == "x86_64" ]]; then
    # Only x86_64 architecture
    host_architectures='hostArchitectures="x86_64"'
  elif [[ "$arch_info" == "x86_64 arm64" || "$arch_info" == "arm64 x86_64" ]]; then
    # Universal binary with both architectures
    host_architectures='hostArchitectures="arm64,x86_64"'
  else
    # error - unsupported architecture
    echo "ERROR: Unsupported architecture: $arch_info"
    exit 1
  fi

  cat >distribution.xml <<EOF
  <installer-gui-script minSpecVersion="1">
      <title>Floe v$version</title>
      <welcome file="welcome.txt" mime-type="text/plain"/>
      <options customize="always" require-scripts="false" $host_architectures/>
      <os-version min="$min_macos_version" /> 
      $distribution_xml_choices
      <choices-outline>
          $distribution_xml_choice_outlines
      </choices-outline>
  </installer-gui-script>
  EOF

  cat distribution.xml

  productbuild --distribution distribution.xml --resources productbuild_files --package-path . unsigned.pkg
  productsign --timestamp --sign "$MACOS_DEV_ID_INSTALLER_NAME" unsigned.pkg "$zig_out_abs_path/$final_installer_name.pkg"

  popd
  rm -rf "$temp_working_subdir"

  # step 5: notarize the installer
  just macos-notarize $final_installer_name.pkg
  xcrun stapler staple $final_installer_name.pkg

  # step 6: zip the installer
  final_zip_name="$final_installer_name-macOS-$arch_name.zip"
  rm -f "$final_zip_name"
  zip -r "$final_zip_name" "$final_installer_name.pkg"
  mv "$final_zip_name" "{{release_files_dir}}"

[macos]
macos-prepare-release: 
  just macos-prepare-packager aarch64-macos
  just macos-prepare-packager x86_64-macos
  just macos-prepare-release-plugins aarch64-macos
  just macos-prepare-release-plugins x86_64-macos
  just macos-build-installer aarch64-macos
  just macos-build-installer x86_64-macos

