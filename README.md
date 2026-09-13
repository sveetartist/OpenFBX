# openfbx

A small FBX viewer/editor built with CMake, raylib, and the Autodesk FBX SDK.

Current version: `0.1.0`.

## Build

For routine edits, double-click **build-release.cmd** in the project folder.
It configures and builds the Release app and keeps the window open to show errors
or the output path. Close the app before rebuilding. It reuses the existing CMake
configuration, or detects the FBX SDK in its standard installation folder on a
first build. CMake, Visual Studio 2022 C++ tools, and the FBX SDK are required.

From PowerShell, you can also build and launch the app:

```powershell
./build-release.ps1 -Run
# For a custom SDK installation:
./build-release.ps1 -FbxSdkRoot "C:/path/to/FBX SDK/version"
```

To configure and build manually with CMake:

```powershell
cmake --preset vs2022 -DFBXSDK_ROOT="C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.10"
cmake --build --preset release
```

The executable is written to `build/vs2022/Release/openfbx.exe`. Debug builds go to
`build/vs2022/Debug/openfbx.exe`.

## Model conversion

Use **File > Open Model**, Ctrl+O, drag and drop, or a command-line file argument
to open FBX, OBJ, DAE, GLB, glTF, Blender `.blend`, or STL files. Non-FBX models
are converted automatically to `name_converted.fbx` beside the source and opened
for editing. Existing names receive a numeric suffix. The source is preserved;
the source folder must be writable. Save and Reload use the converted FBX.

OBJ and DAE use the Autodesk FBX SDK. GLB, glTF, `.blend`, and STL require a local
Blender installation (4.2 or newer). The app finds `blender.exe` on PATH or under
`Program Files/Blender Foundation`; set `OPENFBX_BLENDER_PATH` to the full executable
path for a portable/custom installation. Blender runs in the background.

Conversion exports scene geometry, materials, texture images, and animation where
supported by FBX. Blender procedural shaders and application-specific features
may not transfer exactly. External textures and glTF `.bin` files must remain
available beside their source files. Packed images are embedded in the FBX.

### Conversion checks

Configure with `-DOPENFBX_BUILD_TESTS=ON`, build Release, and run:

```powershell
ctest --test-dir build/vs2022 -C Release --output-on-failure
```

For all-format integration checks, generate the triangle fixtures using Blender:

```powershell
& "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup --disable-autoexec --python-exit-code 1 --python tests/GenerateConversionFixtures.py -- build/conversion-fixtures
& build/vs2022/Release/openfbx_conversion_tests.exe build/conversion-fixtures/triangle.obj build/conversion-fixtures/triangle.dae build/conversion-fixtures/triangle.glb build/conversion-fixtures/triangle.gltf build/conversion-fixtures/triangle.blend build/conversion-fixtures/triangle.stl build/conversion-fixtures/rigged_triangle.glb build/conversion-fixtures/rigged_triangle.gltf
```

The checks reopen each exported FBX, verify triangle geometry, and check materials,
embedded images, and animation for the GLB/glTF/Blender fixtures. Skinned GLB/glTF
fixtures also verify that bone display helpers are excluded and skinning survives.

## Project Layout

- `src/` contains application source files.
- `assets/` contains the icon and source artwork used by the app.
- `build/` and `build-*` are generated CMake output directories and are ignored.

## Object visibility

Press **H** to toggle visibility of selected objects or bones. Hidden entries stay
selectable in the hierarchy and use darker text. **View > Show All** (**Alt+H**) restores all
hidden items, exits isolation, and enables geometry, bones, and empties. Visibility
is local to each open tab and resets on reload.

## Checker view

Press **V** to cycle to **Checker**, or choose **View > Checker**. The bundled
`assets/checker.png` displays a repeating UV checker without changing materials.
In **Preferences > Checker Texture**, adjust squares per UV tile from 2 to 128
(default 16). More squares make each checker smaller.
