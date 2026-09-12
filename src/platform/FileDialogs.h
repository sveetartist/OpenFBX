#pragma once

#include <string>

namespace openfbx
{
std::string OpenFbxFileDialog();
std::string SaveAsFbxFileDialog(const std::string& sourcePath);
std::string OpenTextureFileDialog();
std::string OpenTextureFolderDialog();
}
