# Copyright 2025 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

{ pkgs }:

pkgs.rustPlatform.buildRustPackage {
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
    lockFile = ../build_resources/clap-val-Cargo.lock;
    outputHashes = {
      "clap-sys-0.3.0" = "sha256-O+x9PCRxU/e4nvHa4Lu/MMWTzqCt6fLRZUe4/5HlvJM=";
    };
  };
}
