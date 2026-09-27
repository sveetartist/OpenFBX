#pragma once
#include <string>

namespace openfbx {
struct UpdateResult { std::string message; bool available = false; };
UpdateResult CompareReleaseVersion(const std::string& tag, const std::string& current);
UpdateResult CheckForUpdates(const std::string& current);
}
