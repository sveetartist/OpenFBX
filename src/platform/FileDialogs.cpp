#include "FileDialogs.h"

#include <cstdio>
#include <filesystem>

#ifdef _WIN32
using WindowsBool = int;
using WindowsDword = unsigned long;
using WindowsHinstance = void*;
using WindowsHwnd = void*;
using WindowsLparam = long long;
using WindowsLpstr = char*;
using WindowsLpcstr = const char*;
using WindowsWord = unsigned short;

struct WindowsOpenFileNameA
{
    WindowsDword lStructSize;
    WindowsHwnd hwndOwner;
    WindowsHinstance hInstance;
    WindowsLpcstr lpstrFilter;
    WindowsLpstr lpstrCustomFilter;
    WindowsDword nMaxCustFilter;
    WindowsDword nFilterIndex;
    WindowsLpstr lpstrFile;
    WindowsDword nMaxFile;
    WindowsLpstr lpstrFileTitle;
    WindowsDword nMaxFileTitle;
    WindowsLpcstr lpstrInitialDir;
    WindowsLpcstr lpstrTitle;
    WindowsDword Flags;
    WindowsWord nFileOffset;
    WindowsWord nFileExtension;
    WindowsLpcstr lpstrDefExt;
    WindowsLparam lCustData;
    void* lpfnHook;
    WindowsLpcstr lpTemplateName;
    void* pvReserved;
    WindowsDword dwReserved;
    WindowsDword FlagsEx;
};

extern "C" __declspec(dllimport) WindowsBool __stdcall GetOpenFileNameA(WindowsOpenFileNameA* dialog);
extern "C" __declspec(dllimport) WindowsBool __stdcall GetSaveFileNameA(WindowsOpenFileNameA* dialog);

constexpr WindowsDword kOfnFileMustExist = 0x00001000;
constexpr WindowsDword kOfnPathMustExist = 0x00000800;
constexpr WindowsDword kOfnNoChangeDir = 0x00000008;
constexpr WindowsDword kOfnOverwritePrompt = 0x00000002;
#endif

namespace openfbx
{
std::string OpenFbxFileDialog()
{
#ifdef _WIN32
    char filePath[4096] = {};

    WindowsOpenFileNameA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = "FBX files (*.fbx)\0*.fbx\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = filePath;
    dialog.nMaxFile = sizeof(filePath);
    dialog.Flags = kOfnFileMustExist | kOfnPathMustExist | kOfnNoChangeDir;
    dialog.lpstrDefExt = "fbx";

    if (GetOpenFileNameA(&dialog))
    {
        return filePath;
    }
#endif

    return {};
}

std::string SaveAsFbxFileDialog(const std::string& sourcePath)
{
#ifdef _WIN32
    char filePath[4096] = {};
    std::filesystem::path defaultPath(sourcePath);
    if (!defaultPath.empty())
    {
        defaultPath.replace_extension("");
        const std::string suggested = defaultPath.string() + "_edited.fbx";
        std::snprintf(filePath, sizeof(filePath), "%s", suggested.c_str());
    }

    WindowsOpenFileNameA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = "FBX files (*.fbx)\0*.fbx\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = filePath;
    dialog.nMaxFile = sizeof(filePath);
    dialog.Flags = kOfnPathMustExist | kOfnNoChangeDir | kOfnOverwritePrompt;
    dialog.lpstrDefExt = "fbx";

    if (GetSaveFileNameA(&dialog))
    {
        return filePath;
    }
#endif

    return {};
}

std::string OpenTextureFileDialog()
{
#ifdef _WIN32
    char filePath[4096] = {};

    WindowsOpenFileNameA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = "Image files (*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.psd;*.gif;*.hdr)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.psd;*.gif;*.hdr\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = filePath;
    dialog.nMaxFile = sizeof(filePath);
    dialog.Flags = kOfnFileMustExist | kOfnPathMustExist | kOfnNoChangeDir;

    if (GetOpenFileNameA(&dialog))
    {
        return filePath;
    }
#endif

    return {};
}
}
