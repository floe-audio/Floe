# Copyright 2025 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

{
  pkgs,
  zigpkgs,
  extraPackages ? [ ],
}:

pkgs.mkShell rec {
  nativeBuildInputs =
    [
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
      pkgs.zip
      pkgs.unzip
      pkgs.llvmPackages_19.bintools-unwrapped # llvm-lipo, llvm-addr2line, dsymutil
      pkgs.llvmPackages_19.clang-unwrapped # clangd, clang-tidy, clang-format
      pkgs.cppcheck
      pkgs.codespell
      pkgs.parallel
      pkgs.gnused
      pkgs.coreutils
      pkgs.jq
      pkgs.just
      pkgs.reuse
      pkgs.osslsigncode
      pkgs.wget
      pkgs.hunspell
      pkgs.hunspellDicts.en_GB-ise
      pkgs.lychee # link checker
      zigpkgs."0.14.0"
      pkgs.zls
      pkgs.sentry-cli
      pkgs.nodejs_24 # For Docusaurus website development

      # dsymutil internally calls "lipo", so we have to make sure it's available under that name
      (pkgs.writeShellScriptBin "lipo" "llvm-lipo $@")
    ]
    ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
      pkgs.pkg-config
      pkgs.zenity
      pkgs.kcov
      pkgs.patchelf
      pkgs.valgrind
      pkgs.wineWowPackages.minimal
    ]
    ++ extraPackages;

  buildInputs =
    [ ]
    ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
      pkgs.alsa-lib
      pkgs.xorg.libX11
      pkgs.xorg.libXext
      pkgs.xorg.libXcursor
      pkgs.libGL
      pkgs.curl
      pkgs.libGLU
      pkgs.glibc
    ];

  shellHook =
    ''
      # Use a relative path for reproducible builds
      export ZIG_GLOBAL_CACHE_DIR=".zig-cache-global"
    ''
    + pkgs.lib.optionalString pkgs.stdenv.isLinux ''
      # These are used in the Zig builds to ensure binaries work correctly on non-NixOS systems.
      export FLOE_RPATH="${pkgs.lib.makeLibraryPath buildInputs}"
      export FLOE_DYNAMIC_LINKER="${pkgs.glibc}/lib/ld-linux-x86-64.so.2"
    '';
}
