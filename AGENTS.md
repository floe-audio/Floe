This is the Floe repo, a open source sample library platform (audio plugin) written in C++, using Zig as the build system.
- Linux, Windows, macOS
- CLAP plugin at its core - using clap-wrapper third-party lib for VST3/AU versions
- We manage a nix flake devshell as our build/dev environment

Commands - run via `nix develop --command`:
- Quick compile check: `zb`. (friendly wrapper around zig build)
- Build + run tests: `zig build test`. (add `--filter=` with a `*` wildcard or full test name. mulitple `--filter` is allowed)
- Compile for hot-reload: `zig build install:clap`. (when developing, Floe runs as a standalone that hot-reloads the CLAP plugin for instant user testing)
- Format: `zig build script:format`
- Add `-Dtargets=linux` for cross-compilation. (or `windows`, `mac_arm`, `mac_x86` - omit for native OS)

Repo knowledge base:
- Refer to relevant markdown primers in `knowledge/` before doing complex tasks.

C++ style:
- No C++ STL/standard library.
- Minimal comments. Use them for section markers or notes not-evident from reading code. Prefer renaming variables/functions to be clearer/longer over comments.
- Write in a Zig-like style: closer to modern C than C++.
- Liberally use ASSERT macros (or ASSERT_HOT if the codepath is known to be hot).
- Naming convention: `.clang-tidy` readability-identifier-naming.
- Enums: use switch statements rather than ifs for compile-time exhaustiveness checking. Avoid 'default' case unless really needed. Specify enum size types (typically ` : u8`).
- Where needed, use Clang/GCC 'statement expressions' to initialise a variable to a const to avoid function-wide mutability and unclear encapsulation.
- Always use `auto` type if possible.
- Prefer `Range` over C-style for loops: `for (auto index : Range(10))`.
- Prefer names such as `step_index` over `i` or `j`. `mix_01` over `r`.
- Don't use anonymous namespaces, prefer static functions
- Utilise pure functions. Reduce the number of state-mutating functions.
- Consider using the 'options/args/context struct' pattern along with designated initialiser syntax instead of lots of function arguments.

Repo overview:
- `src/`: complete Floe code with subfolders for the plugin, tools, standalone, libs
- `website/`: Docusaurus website. 2 channels: stable (`versioned_docs` subdir) and beta (regular docs subdir). We run `zig build script:website-promote-beta-to-stable` to transfer everything from beta to stable.

Tests:
Write tests. We use our own test framework (similar to Catch2): src/tests/framework.hpp. Tests live in the same cpp file as the implementation. Write a test case with TEST_CASE(TestName). Then use TEST_REGISTRATION(RegisterMyTests), inside that use REGISTER_TEST(TestName), finally add this registration function to src/tests/tests_main.cpp.

Benchmarks:
`zb install:all -Dbuild-mode=performance_profiling`, then use `hyperfine './zig-out/bin/floe-benchmarks --filter=Name'`. Use `./zig-out/bin/floe-benchmarks --list` to see available benchmarks. Define benchmarks with `BENCHMARK_REGISTRATION`/`REGISTER_BENCHMARK` in the same cpp file as the code, then add the registration function to `src/benchmarks/benchmarks_main.cpp`. API: `src/benchmarks/framework.hpp`.
