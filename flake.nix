# Copyright 2018-2024 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-26.05";
    flake-utils = {
      url = "github:numtide/flake-utils";
    };
    zig.url = "github:mitchellh/zig-overlay";
    zon2nix = {
      url = "github:jcollie/zon2nix?ref=main";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      zig,
      zon2nix,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
        zigpkgs = zig.packages.${system};
      in
      {
        devShells.default = import ./nix/dev-shell.nix {
          inherit pkgs zigpkgs;
          extraPackages = [ zon2nix.packages.${system}.zon2nix ];
        };

        packages = pkgs.lib.optionalAttrs pkgs.stdenv.isLinux (
          let
            mkFloe =
              buildMode:
              pkgs.callPackage ./nix/package.nix {
                zig_0_14 = zigpkgs."0.14.0";
                inherit buildMode;
              };
          in
          {
            floe = mkFloe "production";
            floe-profiling = mkFloe "performance_profiling";
            floe-development = mkFloe "development";
            default = mkFloe "production";
          }
        );
      }
    );
}
