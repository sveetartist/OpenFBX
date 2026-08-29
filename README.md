# openfbx

A small FBX viewer/editor built with CMake, raylib, and the Autodesk FBX SDK.

## Build

Install the Autodesk FBX SDK, then configure and build with CMake:

```powershell
cmake --preset vs2022 -DFBXSDK_ROOT="C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.10"
cmake --build --preset release
```

The executable is written to `build/vs2022/Release/openfbx.exe`. Debug builds go to
`build/vs2022/Debug/openfbx.exe`.

## Project Layout

- `src/` contains application source files.
- `assets/` contains the icon and source artwork used by the app.
- `build/` and `build-*` are generated CMake output directories and are ignored.
