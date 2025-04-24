# Copyright 2018-2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-24.11";
    flake-utils = {
      url = "github:numtide/flake-utils";
    };
    zig.url = "github:mitchellh/zig-overlay";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      zig,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
        zigpkgs = zig.packages.${system};

        nativeBinSubdir = "zig-out/${builtins.replaceStrings [ "darwin" ] [ "macos" ] system}";

        macosx-sdks = pkgs.stdenv.mkDerivation {
          pname = "macosx-sdks";
          version = "12.0";

          src = builtins.fetchurl {
            url = "https://github.com/joseluisq/macosx-sdks/releases/download/12.0/MacOSX12.0.sdk.tar.xz";
            sha256 = "sha256:1z8ga5m9624g3hc0kpdfpqbq1ghswxg57w813jdb18z6166g41xc";
          };

          buildPhase = ''
            mkdir -p $out
          '';

          installPhase = ''
            mkdir -p $out
            cp -R . $out
          '';
        };

        clang-build-analyzer = pkgs.stdenv.mkDerivation rec {
          pname = "clang-build-analyzer";
          version = "1.5.0";

          src = pkgs.fetchFromGitHub {
            owner = "aras-p";
            repo = "ClangBuildAnalyzer";
            rev = "v${version}";
            hash = "sha256-kmgdk634zM0W0OoRoP/RzepArSipa5bNqdVgdZO9gxo=";
          };

          nativeBuildInputs = [
            pkgs.cmake
          ];
        };

        # created using nix-init
        clap-val = pkgs.rustPlatform.buildRustPackage {
          pname = "clap-validator";
          version = "0.3.2";
          buildType = "debug";
          dontStrip = true;
          src = pkgs.fetchFromGitHub {
            owner = "free-audio";
            repo = "clap-validator";
            rev = "2f71690639a742ce805574ce42e31250e3147aa9";
            hash = "sha256-9urTQ5GJYd3Iy71vIOHj8sylZdo6RWa3XlT3AMYmIrE=";
          };
          cargoLock = {
            lockFile = ./build_resources/clap-val-Cargo.lock;
            outputHashes = {
              "clap-sys-0.3.0" = "sha256-O+x9PCRxU/e4nvHa4Lu/MMWTzqCt6fLRZUe4/5HlvJM=";
            };
          };
        };

        # created using nix-init
        pluginval = pkgs.stdenv.mkDerivation rec {
          pname = "pluginval";
          version = "1.0.3";

          src = pkgs.fetchFromGitHub {
            owner = "Tracktion";
            repo = "pluginval";
            rev = "v${version}";
            hash = "sha256-o253DBl3jHumaKzxHDZXK/MpFMrq06MmBia9HEYLtXs=";
            fetchSubmodules = true;
          };

          cmakeBuildType = "Debug";
          dontStrip = true;

          nativeBuildInputs = [
            pkgs.cmake
            pkgs.pkg-config
          ];

          buildInputs =
            pkgs.lib.optionals pkgs.stdenv.isLinux [
              pkgs.xorg.libX11
              pkgs.xorg.libXrandr
              pkgs.xorg.libXcursor
              pkgs.xorg.libXinerama
              pkgs.xorg.libXext
              pkgs.freetype
              pkgs.alsa-lib
              pkgs.curl
              pkgs.gtk3-x11
              pkgs.ladspa-sdk
            ]
            ++ pkgs.lib.optionals pkgs.stdenv.isDarwin [
              pkgs.darwin.apple_sdk.frameworks.CoreAudio
              pkgs.darwin.apple_sdk.frameworks.CoreMIDI
              pkgs.darwin.apple_sdk.frameworks.CoreFoundation
              pkgs.darwin.apple_sdk.frameworks.Cocoa
              pkgs.darwin.apple_sdk.frameworks.WebKit
              pkgs.darwin.apple_sdk.frameworks.MetalKit
              pkgs.darwin.apple_sdk.frameworks.Accelerate
              pkgs.darwin.apple_sdk.frameworks.CoreAudioKit
              pkgs.darwin.apple_sdk.frameworks.DiscRecording
            ];

          installPhase =
            ''
              mkdir -p $out/bin
            ''
            + pkgs.lib.optionalString pkgs.stdenv.isLinux ''
              cp $(find . -name "pluginval" -executable) $out/bin
            ''
            + pkgs.lib.optionalString pkgs.stdenv.isDarwin ''
              mkdir -p $out/Applications
              cp -R $(find . -type d -name "pluginval.app") $out/Applications
              ln -s $out/Applications/pluginval.app/Contents/MacOS/pluginval $out/bin
            '';
        };
      in
      {
        devShells.default = pkgs.mkShell rec {
          # nativeBuildInputs is for tools
          nativeBuildInputs =
            [
              clap-val
              pluginval
              clang-build-analyzer
              pkgs.zip
              pkgs.unzip
              pkgs.llvmPackages_18.bintools-unwrapped # llvm-lipo, llvm-addr2line, dsymutil
              pkgs.llvmPackages_18.clang-unwrapped # clangd, clang-tidy, clang-format
              pkgs.cppcheck
              pkgs.codespell
              pkgs.parallel
              pkgs.miller
              pkgs.gnused
              pkgs.coreutils
              pkgs.jq
              pkgs.fd
              pkgs.just
              pkgs.reuse
              pkgs.mdbook
              pkgs.osslsigncode
              pkgs.wget
              pkgs.hunspell
              pkgs.hunspellDicts.en_GB-ise
              pkgs.lychee # link checker
              zigpkgs."0.14.0" # NOTE(Sam): if you change this version you might also need to change your ZLS version
              pkgs.sentry-cli

              # dsymutil internally calls "lipo", so we have to make sure it's available under that name
              (pkgs.writeShellScriptBin "lipo" "llvm-lipo $@")
            ]
            ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
              pkgs.pkg-config
              pkgs.zenity
              pkgs.kcov
              pkgs.patchelf
              pkgs.valgrind

              # These following 2 'patch' utilities ensure that we can run the binaries that we build regardless of the system outside of
              # this nix devshell. For example on Ubuntu CI machines we don't have to manage what dependencies are
              # installed on the system via apt.

              # The dynamic linker can normally find the libraries inside the nix devshell except when we are running
              # an external program that hosts our audio plugin. For example clap-validator fails to load our clap with
              # the error 'libGL.so.1 cannot be found'. Presumably this is due to LD_LIBRARY_PATH not being available to
              # the external program.
              # As well as LD_LIBRARY_PATH, dynamic linkers also look at the rpath of the binary (which is embedded in
              # the binary itself) to find the libraries. So that's what we use patchelf for here.
              (pkgs.writeShellScriptBin "patchrpath" ''
                patchelf --set-rpath "${pkgs.lib.makeLibraryPath buildInputs}" $@
              '')

              # Executables (as opposed to shared libraries) will default to being interpreted by the system's dynamic
              # linker (often /lib64/ld-linux-x86-64.so.2). This can cause problems relating to using different versions
              # of glibc at the same time. So we use patchelf to force using the same ld.
              (pkgs.writeShellScriptBin "patchinterpreter" ''
                patchelf --set-interpreter "${pkgs.glibc}/lib/ld-linux-x86-64.so.2" $@
              '')
            ];

          # buildInputs is for libraries
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
          shellHook = ''
            export MACOSX_SDK_SYSROOT="${macosx-sdks}"
            export PATH="$PWD/${nativeBinSubdir}:$PATH"
          '';
        };
      }
    );
}
