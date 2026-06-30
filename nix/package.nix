# Copyright 2026 Sam Windell
# SPDX-License-Identifier: GPL-3.0-or-later

{
  lib,
  stdenv,
  callPackage,
  autoPatchelfHook,
  pkg-config,
  zig_0_14,
  alsa-lib,
  curl,
  libGL,
  libGLU,
  libxcb,
  vulkan-loader,
  libX11,
  libXcursor,
  libXext,
  buildMode ? "production",
  fetchFloeLogos ? true,
}:

let
  deps = callPackage ../build.zig.zon.nix {
    name = "floe-zig-deps";
    # build.zig.zon.nix is generated for zig_0_15, but Floe builds with 0.14. Use 0.14 to repack git
    # dependencies so their artifact hashes match what the build expects.
    zig_0_15 = zig_0_14;
  };
in
stdenv.mkDerivation {
  pname = "floe" + lib.optionalString (buildMode != "production") "-${buildMode}";
  version = lib.removeSuffix "\n" (builtins.readFile ../version.txt);

  src = lib.cleanSourceWith {
    src = ../.;
    filter =
      path: type:
      !(builtins.elem (baseNameOf path) [
        "website"
        "zig-out"
        ".zig-cache"
        ".zig-cache-global"
        ".direnv"
        ".floe-cache"
        ".workshop"
      ]);
  };

  nativeBuildInputs = [
    pkg-config
    zig_0_14
    autoPatchelfHook
  ];

  buildInputs = [
    alsa-lib
    curl
    libGL
    libGLU
    libxcb
    vulkan-loader
    libX11
    libXcursor
    libXext
  ];

  dontConfigure = true;

  buildPhase = ''
    runHook preBuild

    export HOME="$TMPDIR"
    export ZIG_GLOBAL_CACHE_DIR="$TMPDIR/zig-global"
    mkdir -p "$ZIG_GLOBAL_CACHE_DIR"

    zig build install:all \
      --prefix "$out" \
      --system "${deps}" \
      --color off \
      -Dbuild-mode=${buildMode} \
      -Dinclude-git-hash=false \
      ${lib.optionalString fetchFloeLogos "-Dfetch-floe-logos"}

    # Zig installs plugins under $out/.clap and $out/.vst3 (the layout used for a $HOME prefix). Move
    # them to the $out/lib/{clap,vst3} convention that Nix audio-plugin consumers expect.
    for type in clap vst3; do
      if [ -d "$out/.$type" ]; then
        mkdir -p "$out/lib"
        mv "$out/.$type" "$out/lib/$type"
      fi
    done

    runHook postBuild
  '';

  dontInstall = true;

  meta = {
    description = "Sample library platform plugin (CLAP/VST3)";
    homepage = "https://floe.audio";
    license = lib.licenses.gpl3Plus;
    platforms = lib.platforms.linux;
  };
}
