#include "platform/UpdateChecker.h"
#include <iostream>

int main(int argc, char**)
{
    using openfbx::CompareReleaseVersion;
    if (!CompareReleaseVersion("v0.10.0", "0.9.0").available ||
        !CompareReleaseVersion("1.0.0", "0.99.99").available ||
        CompareReleaseVersion("v0.1.0", "0.1.0").available ||
        CompareReleaseVersion("0.1.0", "0.2.0").available ||
        CompareReleaseVersion("v1.0.0-beta", "0.1.0").available ||
        CompareReleaseVersion("invalid", "0.1.0").available ||
        CompareReleaseVersion("999999999999999999999999.0.0", "0.1.0").available)
        return 1;
    if (argc > 1) std::cout << openfbx::CheckForUpdates("0.1.0").message << '\n';
    std::cout << "Version checks passed\n";
}
