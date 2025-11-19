# Copyright 2025 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

{
  pkgs,
  zigpkgs,
  nativeBinSubdir,
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

      # TODO: maybe remove these if our FLOE_RPATH and FLOE_DYNAMIC_LINKER plan works out

      # These following 2 'patch' utilities ensure that we can run the binaries that we build regardless of the system outside of
      # this nix devshell. For example on Ubuntu CI machines we don't have to manage what dependencies are
      # installed on the system via apt.

      # The dynamic linker can normally find the libraries inside the nix devshell except when we are running
      # an external program that hosts our audio plugin. For example clap-validator fails to load our clap with
      # the error 'libGL.so.1 cannot be found'. Presumably this is due to LD_LIBRARY_PATH not being available to
      # the external program.
      # As well as LD_LIBRARY_PATH, dynamic linkers also look at the rpath of the binary (which is embedded in
      # the binary itself) to find the libraries. So that's what we use patchelf for here.

      # Executables (as opposed to shared libraries) will default to being interpreted by the system's dynamic
      # linker (often /lib64/ld-linux-x86-64.so.2). This can cause problems relating to using different versions
      # of glibc at the same time. So we use patchelf to force using the same ld.
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
      export PATH="$PWD/${nativeBinSubdir}:$PATH"
      export ZIG_GLOBAL_CACHE_DIR=".zig-cache-global"
    ''
    + pkgs.lib.optionalString pkgs.stdenv.isLinux ''
      export FLOE_RPATH="${pkgs.lib.makeLibraryPath buildInputs}"
      export FLOE_DYNAMIC_LINKER="${pkgs.glibc}/lib/ld-linux-x86-64.so.2"
    '';
}
