#pragma once
#include <string>
#include <vector>

std::vector<std::string> GetSupportedModelExtensions();
bool ConvertModelToFbx(const std::string& sourcePath, std::string& outputPath, std::string& error);
bool PrepareModelForOpening(const std::string& sourcePath, std::string& outputPath, std::string& error);
bool RunBlenderConversion(const std::string& sourcePath, const std::string& outputPath, std::string& error);
