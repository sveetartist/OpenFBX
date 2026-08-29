#pragma once

#include <vector>

namespace openfbx
{
void BuildMeshTangents(const std::vector<float>& vertices, const std::vector<float>& normals, const float* texcoords, std::vector<float>& tangents);
}
