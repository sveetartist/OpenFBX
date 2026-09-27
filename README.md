# openfbx

A small FBX viewer/editor built with CMake, raylib, and the Autodesk FBX SDK.

Current version: `0.1.0`.

## Build

For routine development, double-click **build-dev.cmd** or run
`./build-dev.ps1 -Run`. This updates `build/openfbx.exe` with the latest optimized build.
Add `-Test` to run the regression tests after building. Close the app before
rebuilding. CMake, Visual Studio 2022 C++ tools, and the FBX SDK are required.

From PowerShell, you can also build and launch the app:

```powershell
./build-dev.ps1 -Run -Test
# Create an optimized build:
./build-release.ps1 -Run
# For a custom SDK installation:
./build-release.ps1 -FbxSdkRoot "C:/path/to/FBX SDK/version"
```

To configure and build manually with CMake:

```powershell
cmake --preset release -DFBXSDK_ROOT="C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.10"
cmake --build --preset release
```

The latest app and runtime assets are in `build/`. CMake intermediates stay in
`build/.cmake/`. All build outputs are ignored by Git.

## Package a release

After building, double-click **pack-release.cmd**, or run `./pack-release.ps1`.
It packages the current `build/openfbx.exe` and runtime assets into
`releases/openfbx-0.1.0-windows-x64.zip`, using the version from `CMakeLists.txt`.
Only ZIPs go in `releases/`; it is ignored by Git and has no separate repository.
Upload these ZIPs to GitHub Releases. Packaging replaces the ZIP for the same
version; increase the project version to retain a separate release.

## Preferences and hotkeys

Use **Help > Check for updates** to check the latest public GitHub release.
The check runs in the background and offers a link to download a newer version.
Release tags should use `vMAJOR.MINOR.PATCH` (for example, `v0.2.0`).
No files are downloaded or installed automatically.

Preferences stays within the window; scroll inside it to reach controls in a short
window. Open **Preferences > Edit Hotkeys** to change command shortcuts. Click a
binding and press the new combination. Duplicate bindings are rejected; clear the
other binding first to reuse it. Escape cancels capture and Backspace clears a
binding. **Save** applies changes, **Cancel** discards them, and **Defaults** restores
the standard bindings for review before saving.

Shortcuts are stored in `%LOCALAPPDATA%/openfbx/hotkeys.txt`. Menu and toolbar labels
follow the saved bindings. Mouse navigation, selection modifiers, and text-entry
keys remain fixed. Shortcuts elsewhere in this README describe the defaults.

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
./build-dev.ps1 -Test
```

For all-format integration checks, generate the triangle fixtures using Blender:

```powershell
& "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup --disable-autoexec --python-exit-code 1 --python tests/GenerateConversionFixtures.py -- build/conversion-fixtures
& build/openfbx_conversion_tests.exe build/conversion-fixtures/triangle.obj build/conversion-fixtures/triangle.dae build/conversion-fixtures/triangle.glb build/conversion-fixtures/triangle.gltf build/conversion-fixtures/triangle.blend build/conversion-fixtures/triangle.stl build/conversion-fixtures/rigged_triangle.glb build/conversion-fixtures/rigged_triangle.gltf
```

The checks reopen each exported FBX, verify triangle geometry, and check materials,
embedded images, and animation for the GLB/glTF/Blender fixtures. Skinned GLB/glTF
fixtures also verify that bone display helpers are excluded and skinning survives.

## Project Layout

- `src/` contains application source files.
- `assets/` contains the icon and source artwork used by the app.
- `build/` contains the latest executable, runtime assets, and build intermediates.
- `releases/` contains zipped builds.

## Object visibility

Press **H** to toggle visibility of selected objects or bones. Hidden entries stay
selectable in the hierarchy and use darker text. **View > Show All** (**Alt+H**) restores all
hidden items, exits isolation, and enables geometry, bones, and empties. Visibility
is local to each open tab and resets on reload.

## Material workflows

In **Mats**, click the workflow button beneath the selected material to switch
between **Metallic - Roughness** and **Specular - Glossiness**. Each material keeps
its own workflow and both sets of maps; switching does not convert the textures.
Diffuse, normal, AO, emissive, and opacity maps are shared.

Specular maps use RGB color. Glossiness maps use the selected R/G/B/A channel;
higher values produce smoother highlights. For a combined specular/glossiness
texture, load it into both slots and select **A** for Glossiness. **Load Folder**
recognizes specular, glossiness, and smoothness names, and selects alpha for
combined maps with `spec` and `gloss` in their names. Channel previews follow the
selected workflow. Workflow changes support undo/redo; saving or packaging the
FBX preserves the workflow, glossiness channel, and assigned maps for reopening
in openfbx.

## Checker view

Press **V** to cycle to **Checker**, or choose **View > Checker**. The bundled
`assets/checker.png` displays a repeating UV checker without changing materials.
In **Preferences > Checker Texture**, adjust squares per UV tile from 2 to 128
(default 16). More squares make each checker smaller.

Enable **Preferences > Colored checker** to use the supplied labeled textures.
Click **Color** to cycle through all five variants. The texture-size controls select
512, 1024, 2048, or 4096 pixels, loading the matching supplied image (default 1024).

In the Validator, expand **Degenerate triangles**, right-click an issue, and choose
**Fix Degenerate Triangles** to remove the affected mesh's whole zero-area faces.
Surviving quads/ngons retain their original polygon edges in the viewport and
their topology, UVs, and material assignments when saved.

To check cleanup and topology preservation on a local model without modifying it:

```powershell
& build/openfbx_transform_tests.exe --repair test_models/hero_maya.fbx
```
Valid quads and n-gons are preserved without triangulation. Degenerate display
triangles within otherwise valid polygons remain flagged. The fix supports
Undo/Redo and is included when saving the FBX.

## Message log

The **Log** panel below the animation timeline starts collapsed and keeps the
latest 500 notices and errors. Scroll with the mouse wheel or drag its scrollbar; **Latest** resumes
following new messages. **Copy** copies the history and **Clear** empties it.
Collapse the panel with its arrow to leave only the latest message visible.
Long messages wrap in the expanded panel.
