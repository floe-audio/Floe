# Copyright 2025 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

{ pkgs }:

pkgs.stdenv.mkDerivation rec {
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
}
