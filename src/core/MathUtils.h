#pragma once

#include "raylib.h"

namespace openfbx
{
float ClampFloat(float value, float minimum, float maximum);
int ClampInt(int value, int minimum, int maximum);
Vector3 NormalizeOrFallback(Vector3 value, Vector3 fallback);
Vector3 ChoosePerpendicular(Vector3 normal);
Vector3 LerpVector3(Vector3 a, Vector3 b, float alpha);
}
