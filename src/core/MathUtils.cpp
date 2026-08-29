#include "MathUtils.h"

#include <algorithm>
#include <cmath>

#include "raymath.h"

namespace openfbx
{
float ClampFloat(float value, float minimum, float maximum)
{
    return std::max(minimum, std::min(maximum, value));
}

int ClampInt(int value, int minimum, int maximum)
{
    return std::max(minimum, std::min(maximum, value));
}

Vector3 NormalizeOrFallback(Vector3 value, Vector3 fallback)
{
    const float length = Vector3Length(value);
    if (length > 0.000001f) return Vector3Scale(value, 1.0f / length);
    return fallback;
}

Vector3 ChoosePerpendicular(Vector3 normal)
{
    const Vector3 axis = std::fabs(normal.y) < 0.9f ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
    return NormalizeOrFallback(Vector3CrossProduct(axis, normal), Vector3{ 1.0f, 0.0f, 0.0f });
}

Vector3 LerpVector3(Vector3 a, Vector3 b, float alpha)
{
    return Vector3{
        a.x + (b.x - a.x) * alpha,
        a.y + (b.y - a.y) * alpha,
        a.z + (b.z - a.z) * alpha
    };
}
}
