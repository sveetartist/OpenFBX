#include "FileDialogs.h"

#include <cstdio>
#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>

constexpr DWORD kOfnFileMustExist = OFN_FILEMUSTEXIST;
constexpr DWORD kOfnPathMustExist = OFN_PATHMUSTEXIST;
constexpr DWORD kOfnNoChangeDir = OFN_NOCHANGEDIR;
constexpr DWORD kOfnOverwritePrompt = OFN_OVERWRITEPROMPT;
#endif

namespace openfbx
{
std::string OpenFbxFileDialog()
{
#ifdef _WIN32
    char filePath[4096] = {};

    OPENFILENAMEA dialog{};
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

    OPENFILENAMEA dialog{};
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

std::string OpenTextureFolderDialog()
{
#ifdef _WIN32
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return {};

    std::string selectedPath;
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog))))
    {
        FILEOPENDIALOGOPTIONS options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options)) &&
            SUCCEEDED(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST)))
        {
            dialog->SetTitle(L"Select texture folder (includes subfolders)");
            if (SUCCEEDED(dialog->Show(nullptr)))
            {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dialog->GetResult(&item)))
                {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                    {
                        selectedPath = std::filesystem::path(path).string();
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return selectedPath;
#else
    return {};
#endif
}

std::string OpenTextureFileDialog()
{
#ifdef _WIN32
    char filePath[4096] = {};

    OPENFILENAMEA dialog{};
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
