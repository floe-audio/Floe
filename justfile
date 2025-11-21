# Copyright 2018-2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

# NOTE: for the most part, we assume that `nix develop` has been run before using this justfile

set dotenv-load

native_arch_os_pair := arch() + "-" + os()
native_binary_dir := join("zig-out", native_arch_os_pair)
native_binary_dir_abs := join(justfile_directory(), native_binary_dir)
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

# This fetches logos too which may be not be GPL licenced.
build-release target_os='native':
  zig build compile -Dtargets={{target_os}} \
      -Dbuild-mode=production \
      -Dfetch-floe-logos=true \
      --global-cache-dir {{ZIG_GLOBAL_CACHE_DIR}}

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

[unix, no-cd]
_create-manual-install-readme os_name:
  #!/usr/bin/env bash
  echo "These are the manual-install {{os_name}} plugin files for Floe version $(cat {{justfile_directory()}}/version.txt)." > readme.txt
  echo "" >> readme.txt
  echo "It's normally easier to use the installer instead of these manual-install files." >> readme.txt
  echo "The installer is a separate download to this." >> readme.txt
  echo "" >> readme.txt
  echo "Installation instructions: https://floe.audio/" >> readme.txt

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

