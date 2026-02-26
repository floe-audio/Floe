# Copyright 2025 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

{
  pkgs,
  zigpkgs,
  extraPackages ? [ ],
}:

pkgs.mkShell rec {
  nativeBuildInputs = [
    (pkgs.stdenv.mkDerivation rec {
      pname = "clang-build-analyzer";
      version = "1.6.0";

      src = pkgs.fetchFromGitHub {
        owner = "aras-p";
        repo = "ClangBuildAnalyzer";
        rev = "v${version}";
        hash = "sha256-GIMQZGPFKDrfMqCsA8nR3O8Hzp2jcaZ+yDrPeCxTsIg=";
      };

      nativeBuildInputs = [
        pkgs.cmake
      ];
    })
    pkgs.rcodesign
    pkgs.llvmPackages_19.bintools-unwrapped # llvm-lipo, llvm-addr2line, dsymutil
    pkgs.llvmPackages_19.clang-unwrapped # clangd, clang-tidy, clang-format
    pkgs.cppcheck
    pkgs.codespell
    pkgs.gnused
    pkgs.coreutils
    pkgs.jq
    pkgs.just
    pkgs.reuse
    pkgs.wget
    pkgs.hunspell
    pkgs.hunspellDicts.en_GB-ise
    pkgs.lychee # link checker
    zigpkgs."0.14.0"
    pkgs.zls_0_14
    pkgs.sentry-cli
    pkgs.nodejs_24 # For Docusaurus website development

    # dsymutil internally calls "lipo", so we have to make sure it's available under that name
    (pkgs.writeShellScriptBin "lipo" "llvm-lipo $@")

    # Wrapper around zig build that suppresses noise and only shows errors
    (pkgs.writeShellScriptBin "zb" ''
      echo "Building..."
      output=$(zig build --prominent-compile-errors "$@" 2>&1)
      exit_code=$?
      if [ $exit_code -eq 0 ]; then
        echo "Build succeeded"
      else
        echo "$output" | sed -n '/Build Summary/,$p'
      fi
      exit $exit_code
    '')
  ]
  ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
    pkgs.pkg-config
    pkgs.zenity
    pkgs.kcov
    pkgs.patchelf
    pkgs.valgrind
  ]
  ++ extraPackages;

  buildInputs =
    [ ]
    ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
      pkgs.alsa-lib
      pkgs.curl
      pkgs.glibc
      pkgs.libGL
      pkgs.libGLU
      pkgs.libxcb
      pkgs.vulkan-headers
      pkgs.vulkan-loader
      pkgs.xorg.libX11
      pkgs.xorg.libXcursor
      pkgs.xorg.libXext
    ];

  shellHook = ''
    # Use a relative path for reproducible builds
    export ZIG_GLOBAL_CACHE_DIR=".zig-cache-global"
  ''
  + pkgs.lib.optionalString pkgs.stdenv.isLinux ''
    # These are used in the Zig builds to ensure binaries work correctly on non-NixOS systems.
    export FLOE_RPATH="${pkgs.lib.makeLibraryPath buildInputs}"
    export FLOE_DYNAMIC_LINKER="${pkgs.glibc}/lib/ld-linux-x86-64.so.2"
  '';
}
