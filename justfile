# Copyright 2018-2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

set dotenv-load

# build and report compile-time statistics
# build-timed *ARGS:
#   #!/usr/bin/env bash
#   artifactDir={{cache_dir}}/clang-build-analyzer-artifacts
#   reportFile={{cache_dir}}/clang-build-analyzer-report
#   mkdir -p ''${artifactDir}
#   ClangBuildAnalyzer --start ${artifactDir}
#   zig build {{ARGS}}
#   returnCode=$?
#   ClangBuildAnalyzer --stop ${artifactDir} ${reportFile}
#   ClangBuildAnalyzer --analyze ${reportFile}
#   exit ${returnCode}

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

# Issues that have been solved, but not yet had a release are labelled "awaiting-release". This command closes
# issues with that label, adds a comment about the release, and removes the label. It should be run after a 
# release is made.
close-released-issues version:
  #!/usr/bin/env bash
  set -uo pipefail
  
  echo "Processing issues fixed in release {{version}}..."
  
  issues=$(gh issue list --label "awaiting-release" --state open --json number --jq '.[].number')
  list_result=$?
  
  if [ $list_result -ne 0 ]; then
    echo "⚠️  Warning: Failed to fetch issue list" >&2
    exit 0
  fi
  
  if [ -z "$issues" ]; then
    echo "No issues awaiting release"
    exit 0
  fi
  
  count=$(echo "$issues" | wc -l)
  echo "Found $count issue(s) to process"
  
  success=0
  failed=0
  
  while read -r issue_number; do
    if [ -n "$issue_number" ]; then
      echo "Processing issue #$issue_number"
      gh issue comment "$issue_number" --body "This issue has been resolved and is now available in release {{version}}: https://floe.audio/download"
      if [ $? -ne 0 ]; then
        echo "  ⚠️  Warning: Failed to comment on #$issue_number" >&2
      fi
      
      gh issue edit "$issue_number" --remove-label "awaiting-release"
      if [ $? -ne 0 ]; then
        echo "  ⚠️  Warning: Failed to remove label from #$issue_number" >&2
        ((failed++)) || true
        continue
      fi
      
      gh issue close "$issue_number" --reason completed
      if [ $? -ne 0 ]; then
        echo "  ⚠️  Warning: Failed to close issue #$issue_number" >&2
        ((failed++)) || true
      else
        ((success++)) || true
      fi
    fi
  done < <(echo "$issues")
  
  echo "Completed: $success succeeded, $failed encountered warnings"


# IMPROVE: (June 2024) cppcheck v2.14.0 and v2.14.1 thinks there are syntax errors in valid code. It could be a cppcheck bug or it could be an incompatibility in how we are using it. Regardless, we should try again in the future and see if it's fixed. If it works it should run alongside clang-tidy in CI, etc.
# cppcheck arch_os_pair=native_arch_os_pair:
#   # IMPROVE: use --check-level=exhaustive?
#   # IMPROVE: investigate other flags such as --enable=constVariable
#   cppcheck --project={{justfile_directory()}}/{{cache_dir}}/compile_commands_{{arch_os_pair}}.json --cppcheck-build-dir={{justfile_directory()}}/.zig-cache --enable=unusedFunction --error-exitcode=2

