<p align="center">
  <img src="assets/ui/menu/tvorin_logo.png" alt="Tvorin" width="460">
</p>

<p align="center">
  A deterministic 2D strategy prototype about industry, logistics, and territorial expansion.
</p>

<p align="center">
  <a href="https://github.com/telmaris/rts-with-codex/actions/workflows/windows-release.yml"><img src="https://github.com/telmaris/rts-with-codex/actions/workflows/windows-release.yml/badge.svg" alt="Windows build"></a>
  <a href="https://github.com/telmaris/rts-with-codex/releases/latest"><img src="https://img.shields.io/github/v/release/telmaris/rts-with-codex?label=release&color=blue" alt="Latest release"></a>
  <a href="https://github.com/telmaris/rts-with-codex/releases"><img src="https://img.shields.io/github/downloads/telmaris/rts-with-codex/total?color=blue" alt="Downloads"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white" alt="C++20">
  <img src="https://img.shields.io/badge/raylib-5.0-black" alt="raylib 5.0">
</p>

## About

Tvorin combines the production chains and physical logistics of a factory game with the territorial and strategic layer of a grand-strategy game. Players establish an economy on a procedurally generated local map, move real resource packages through a capacity-limited road network, raise armies, and expand into foreign provinces.

The project is a playable hobby prototype. Single-player is the primary mode; two-player LAN multiplayer is experimental.

## Gameplay

- Build production chains for food, construction materials, tools, and military equipment.
- Connect producers, warehouses, and consumers with roads whose speed and capacity create real bottlenecks.
- Supply settlements to grow population and convert manpower into workers or soldiers.
- Recruit typed units, organize forces, defend provinces, and attack hostile targets.
- Research technologies, complete strategic focuses, and progress through state-development tiers.
- Scout, trade, colonize, and fight across a procedurally generated province network.

A typical early chain looks like this:

```text
forest -> woodcutter -> wood -> lumber mill -> planks
wheat field -> farm -> wheat -> windmill -> flour -> bakery -> bread
ore + coal -> mine -> foundry -> metal -> workshops -> equipment
```

## Engineering highlights

- C++20 and raylib 5.0 with a CMake build.
- Deterministic 100 Hz simulation; gameplay state changes only through serializable commands.
- Authoritative host model with checksums, snapshot synchronization, and desync recovery.
- Data-driven buildings, recipes, units, technologies, focuses, events, routes, and balance modifiers.
- Fixed-size resource pool and deterministic containers in simulation-critical paths.
- Automated Windows build, data validation, unit/integration tests, packaging, and tagged releases.

## Build

Requirements:

- Windows 10 or newer
- CMake 3.16+
- Visual Studio 2022 with the Desktop development with C++ workload
- Git, used by CMake to fetch raylib on the first configure

Build and optionally launch the game:

```powershell
.\scripts\build.ps1
.\scripts\build.ps1 -Run
```

The script uses a prebuilt raylib installation from `RAYLIB_ROOT` or `deps/raylib` when available. Otherwise, CMake fetches raylib 5.0 automatically.

The equivalent direct CMake workflow is:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel --target tvorin
.\build\Release\tvorin.exe
```

Prebuilt Windows packages are available on the [Releases page](https://github.com/telmaris/rts-with-codex/releases).

## Tests and coverage

Run the complete test suite and the shipped-data validator:

```powershell
.\scripts\test.ps1
```

List CTest entries without running them:

```powershell
.\scripts\test.ps1 -List
```

Generate HTML and Cobertura coverage reports with [OpenCppCoverage](https://github.com/OpenCppCoverage/OpenCppCoverage):

```powershell
.\scripts\coverage.ps1 -OpenReport
```

## Controls

The in-game Controls screen contains the complete list, including debug bindings available on debug maps.

