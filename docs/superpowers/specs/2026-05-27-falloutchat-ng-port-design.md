# FalloutChat NG Port — Design Spec

**Date:** 2026-05-27

## Summary

Create a self-contained copy of FalloutChat at `E:\F4SE OG\FalloutChat-NG\` that targets the Fallout 4 Next-Gen update (v1.10.984+). The NG CommonLibF4 (Ryan-rsm-McKenzie fork) is embedded as a git submodule inside the project so the folder is fully isolated from the original.

## Folder Structure

```
E:\F4SE OG\FalloutChat-NG\
├── .git\
├── CommonLibF4\            ← git submodule: https://github.com/Ryan-rsm-McKenzie/CommonLibF4
├── cmake\                  ← copied from FalloutChat (Version.h.in, sourcelist.cmake, version.rc.in)
├── docs\                   ← copied from FalloutChat (specs, plans)
├── fonts\                  ← copied from FalloutChat (fa-brands-400.ttf, fa-solid-900.ttf)
├── include\                ← copied from FalloutChat (Renderer.h, ChatClient.h, SimpleIni.h, icons/)
├── src\                    ← copied from FalloutChat (main.cpp, Renderer.cpp, ChatClient.cpp, PCH.h)
├── CMakeLists.txt          ← modified (CommonLibF4 path)
├── vcpkg.json              ← copied unchanged
├── FalloutChat.ini         ← copied unchanged
└── build_dll.bat           ← modified (CommonLibF4 path + NG deploy dir)
```

## Changes from Original

### 1. `CMakeLists.txt`
One line change — point to local submodule instead of sibling directory:
```cmake
# Before:
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../CommonLibF4" CommonLibF4)

# After:
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/CommonLibF4" CommonLibF4)
```

### 2. `src/main.cpp`
Update runtime version check to accept NG runtime:
```cpp
// Before:
if (ver < F4SE::RUNTIME_1_10_162) {

// After:
if (ver < F4SE::RUNTIME_1_10_984) {
```

### 3. `build_dll.bat`
- `VCPKG_ROOT` and `MSBUILD` paths stay the same (same machine)
- `DEPLOY_DIR` updated to NG modlist placeholder:
  ```bat
  set DEPLOY_DIR=E:\Modlists\Fallen World Alpha 2 NG\mods\FalloutChat\F4SE\Plugins
  ```
  (Update this path before first use)
- CMake configure line: remove `../CommonLibF4` reference (cmake uses CMakeLists.txt directly)

## Git Setup

New repo initialized at `E:\F4SE OG\FalloutChat-NG\`. The Ryan-rsm-McKenzie CommonLibF4 is added as a submodule:
```bash
git submodule add https://github.com/Ryan-rsm-McKenzie/CommonLibF4 CommonLibF4
git submodule update --init --recursive
```

## Source Files

All source files (`src/`, `include/`, `cmake/`, `fonts/`) are **copied** from the original, not symlinked. The two repos evolve independently from this point.

## No Changes To

- `vcpkg.json` — same dependencies
- `src/Renderer.cpp` — no hardcoded addresses, NG CommonLibF4 remaps transparently
- `src/ChatClient.cpp` — WebSocket code, no game API
- `include/` headers — no version-specific code
