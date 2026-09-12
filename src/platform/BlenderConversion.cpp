#include "ModelConversion.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace
{
std::filesystem::path FindBlender()
{
    wchar_t buffer[32768] = {};
    const DWORD length = GetEnvironmentVariableW(L"OPENFBX_BLENDER_PATH", buffer, 32768);
    if (length > 0 && length < 32768)
        return std::filesystem::path(buffer);
    const DWORD found = SearchPathW(nullptr, L"blender.exe", nullptr, 32768, buffer, nullptr);
    if (found > 0 && found < 32768) return std::filesystem::path(buffer);
    const DWORD programFilesLength = GetEnvironmentVariableW(L"ProgramFiles", buffer, 32768);
    if (!programFilesLength || programFilesLength >= 32768) return {};
    const auto root = std::filesystem::path(buffer) / "Blender Foundation";
    std::error_code ec;
    std::vector<std::filesystem::path> candidates;
    std::filesystem::directory_iterator entries(root, ec), end;
    for (; !ec && entries != end; entries.increment(ec))
    {
        const auto executable = entries->path() / "blender.exe";
        std::error_code fileError;
        if (std::filesystem::is_regular_file(executable, fileError)) candidates.push_back(executable);
    }
    std::sort(candidates.rbegin(), candidates.rend());
    return candidates.empty() ? std::filesystem::path{} : candidates.front();
}

// Quote one Windows process argument; do not invoke a shell.
std::wstring QuoteArgument(const std::wstring& argument)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : argument)
    {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += c;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}

struct ConversionFiles
{
    std::filesystem::path directory;
    ~ConversionFiles()
    {
        std::error_code ec;
        std::filesystem::directory_iterator entries(directory, ec), end;
        for (; !ec && entries != end; entries.increment(ec))
        {
            const auto filename = entries->path().filename().string();
            if (filename.rfind("packed_", 0) == 0 && entries->path().extension() == ".png")
            {
                std::error_code removeError;
                std::filesystem::remove(entries->path(), removeError);
            }
        }
        for (const char* name : { "convert.py", "blender.log", "converted.fbx" })
            std::filesystem::remove(directory / name, ec);
        std::filesystem::remove(directory, ec);
    }
};
}
#endif

bool RunBlenderConversion(const std::string& sourcePath, const std::string& outputPath, std::string& error)
{
#ifdef _WIN32
    const auto blender = FindBlender();
    if (blender.empty() || !std::filesystem::is_regular_file(blender))
    {
        error = "This format requires Blender. Install Blender or set OPENFBX_BLENDER_PATH to blender.exe.";
        return false;
    }
    const auto tempRoot = std::filesystem::temp_directory_path();
    std::filesystem::path directory;
    for (unsigned int attempt = 0; ; ++attempt)
    {
        directory = tempRoot / ("openfbx-convert-" + std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(attempt));
        if (std::filesystem::create_directory(directory)) break;
    }
    ConversionFiles files{directory};
    const auto script = directory / "convert.py";
    const auto converted = directory / "converted.fbx";
    const auto log = directory / "blender.log";
    std::ofstream scriptFile(script);
    scriptFile << R"PY(import bpy
import os
import sys

source, destination = sys.argv[sys.argv.index("--") + 1:]
extension = os.path.splitext(source)[1].lower()
bpy.ops.wm.read_factory_settings(use_empty=True)
if extension == ".blend":
    bpy.ops.wm.open_mainfile(filepath=source, load_ui=False, use_scripts=False)
elif extension in {".glb", ".gltf"}:
    # Bone display shapes are Blender helpers, not geometry from the source.
    bpy.ops.import_scene.gltf(filepath=source, disable_bone_shape=True)
elif extension == ".stl":
    bpy.ops.wm.stl_import(filepath=source)
else:
    raise RuntimeError("Unsupported Blender conversion format: " + extension)
if not bpy.context.scene.objects:
    raise RuntimeError("The source contains no scene objects")
# Materialize packed and generated images so the FBX exporter can embed them.
for index, image in enumerate(bpy.data.images):
    if image.packed_file or image.source == 'GENERATED':
        image.filepath_raw = os.path.join(os.path.dirname(destination), "packed_" + str(index) + ".png")
        image.file_format = 'PNG'
        image.save()
result = bpy.ops.export_scene.fbx(filepath=destination, check_existing=False,
    use_selection=False, add_leaf_bones=False, bake_anim=True, use_triangles=False,
    path_mode='COPY', embed_textures=True)
if 'FINISHED' not in result:
    raise RuntimeError("FBX export did not finish")
)PY";
    scriptFile.close();
    if (!scriptFile)
    {
        error = "Failed to write the Blender conversion script.";
        return false;
    }
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE logHandle = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logHandle == INVALID_HANDLE_VALUE)
    {
        error = "Failed to create the Blender conversion log.";
        return false;
    }
    std::wstring command = QuoteArgument(blender.wstring()) +
        L" --background --factory-startup --disable-autoexec --python-exit-code 1 --python " +
        QuoteArgument(script.wstring()) + L" -- " + QuoteArgument(std::filesystem::path(sourcePath).wstring()) +
        L" " + QuoteArgument(converted.wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = logHandle;
    startup.hStdError = logHandle;
    startup.hStdInput = nullptr;
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(blender.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process);
    CloseHandle(logHandle);
    if (!started)
    {
        error = "Failed to start Blender (Windows error " + std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(process.hThread);
    const DWORD waitResult = WaitForSingleObject(process.hProcess, 300000);
    DWORD exitCode = 1;
    if (waitResult == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &exitCode);
    else
    {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    }
    CloseHandle(process.hProcess);
    if (waitResult != WAIT_OBJECT_0 || exitCode != 0 || !std::filesystem::is_regular_file(converted))
    {
        std::ifstream stream(log);
        std::string detail((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        if (detail.size() > 1600) detail = detail.substr(detail.size() - 1600);
        error = waitResult == WAIT_TIMEOUT ? "Blender conversion timed out." : "Blender could not convert this model.";
        if (!detail.empty()) error += " " + detail;
        return false;
    }
    std::error_code copyError;
    if (!std::filesystem::copy_file(converted, outputPath, std::filesystem::copy_options::none, copyError))
    {
        error = "Failed to save converted FBX: " + copyError.message();
        return false;
    }
    return true;
#else
    error = "Blender conversion is currently supported on Windows only.";
    return false;
#endif
}
