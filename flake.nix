# Copyright 2018-2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-25.05";
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

        # Optional validation tools
        clap-val = import ./nix/clap-validator.nix { inherit pkgs; };
        pluginval = import ./nix/pluginval.nix { inherit pkgs; };
      in
      {
        devShells.default = import ./nix/dev-shell.nix {
          inherit pkgs zigpkgs nativeBinSubdir;
        };

        # Dev shell with validation tools included
        devShells.with-validators = import ./nix/dev-shell.nix {
          inherit pkgs zigpkgs nativeBinSubdir;
          extraPackages = [
            clap-val
            pluginval
          ];
        };
      }
    );
}
