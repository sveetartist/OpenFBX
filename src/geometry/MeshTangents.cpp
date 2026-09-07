#include "MeshTangents.h"

#include <cmath>
#include <algorithm>
#include <execution>
#include <numeric>

#include "../core/MathUtils.h"
#include "raylib.h"
#include "raymath.h"

namespace openfbx
{
void BuildMeshTangents(const std::vector<float>& vertices, const std::vector<float>& normals, const float* texcoords, std::vector<float>& tangents)
{
    const size_t vertexCount = vertices.size() / 3;
    tangents.assign(vertexCount * 4, 0.0f);
    if (normals.size() < vertexCount * 3 || !texcoords) return;

    auto buildTriangle = [&](size_t vertex)
    {
        const size_t p0 = vertex * 3;
        const size_t p1 = (vertex + 1) * 3;
        const size_t p2 = (vertex + 2) * 3;
        const size_t uv0 = vertex * 2;
        const size_t uv1 = (vertex + 1) * 2;
        const size_t uv2 = (vertex + 2) * 2;

        const Vector3 v0{ vertices[p0], vertices[p0 + 1], vertices[p0 + 2] };
        const Vector3 v1{ vertices[p1], vertices[p1 + 1], vertices[p1 + 2] };
        const Vector3 v2{ vertices[p2], vertices[p2 + 1], vertices[p2 + 2] };
        const Vector2 t0{ texcoords[uv0], texcoords[uv0 + 1] };
        const Vector2 t1{ texcoords[uv1], texcoords[uv1 + 1] };
        const Vector2 t2{ texcoords[uv2], texcoords[uv2 + 1] };

        const Vector3 edge1 = Vector3Subtract(v1, v0);
        const Vector3 edge2 = Vector3Subtract(v2, v0);
        const Vector2 delta1{ t1.x - t0.x, t1.y - t0.y };
        const Vector2 delta2{ t2.x - t0.x, t2.y - t0.y };
        const float determinant = delta1.x * delta2.y - delta2.x * delta1.y;

        Vector3 tangent{};
        Vector3 bitangent{};
        if (std::fabs(determinant) > 0.00000001f)
        {
            const float inverseDeterminant = 1.0f / determinant;
            tangent = Vector3Scale(Vector3Subtract(Vector3Scale(edge1, delta2.y), Vector3Scale(edge2, delta1.y)), inverseDeterminant);
            bitangent = Vector3Scale(Vector3Subtract(Vector3Scale(edge2, delta1.x), Vector3Scale(edge1, delta2.x)), inverseDeterminant);
        }

        for (size_t local = 0; local < 3; ++local)
        {
            const size_t global = vertex + local;
            const size_t normalBase = global * 3;
            const Vector3 normal = NormalizeOrFallback(Vector3{ normals[normalBase], normals[normalBase + 1], normals[normalBase + 2] }, Vector3{ 0.0f, 1.0f, 0.0f });
            Vector3 orthogonalTangent = Vector3Subtract(tangent, Vector3Scale(normal, Vector3DotProduct(normal, tangent)));
            orthogonalTangent = NormalizeOrFallback(orthogonalTangent, ChoosePerpendicular(normal));
            const float handedness = Vector3DotProduct(Vector3CrossProduct(normal, orthogonalTangent), bitangent) < 0.0f ? -1.0f : 1.0f;
            const size_t tangentBase = global * 4;
            tangents[tangentBase] = orthogonalTangent.x;
            tangents[tangentBase + 1] = orthogonalTangent.y;
            tangents[tangentBase + 2] = orthogonalTangent.z;
            tangents[tangentBase + 3] = handedness;
        }
    };

    // Triangle-expanded vertices have no shared output, so chunks are independent.
    constexpr size_t chunkVertices = 3072;
    if (vertexCount < chunkVertices * 2)
    {
        for (size_t vertex = 0; vertex + 2 < vertexCount; vertex += 3) buildTriangle(vertex);
    }
    else
    {
        std::vector<size_t> chunks((vertexCount + chunkVertices - 1) / chunkVertices);
        std::iota(chunks.begin(), chunks.end(), size_t{0});
        std::for_each(std::execution::par, chunks.begin(), chunks.end(), [&](size_t chunk) {
            const size_t end = std::min((chunk + 1) * chunkVertices, vertexCount);
            for (size_t vertex = chunk * chunkVertices; vertex + 2 < end; vertex += 3) buildTriangle(vertex);
        });
    }
}
}
