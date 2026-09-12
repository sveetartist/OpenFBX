#include "platform/ModelConversion.h"
#include <fbxsdk.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

static void Require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

static void VerifyConverted(const std::string& source, int expectedMeshes = 1, int expectedPolygons = 1, bool checkFixtureData = true)
{
    std::string output, error;
    const bool converted = ConvertModelToFbx(source, output, error);
    Require(converted, error);
    Require(output != source && std::filesystem::is_regular_file(output), "No separate converted file");
    auto destroy = [](FbxManager* manager) { manager->Destroy(); };
    std::unique_ptr<FbxManager, decltype(destroy)> manager(FbxManager::Create(), destroy);
    manager->SetIOSettings(FbxIOSettings::Create(manager.get(), IOSROOT));
    auto* importer = FbxImporter::Create(manager.get(), "test");
    Require(importer->Initialize(output.c_str(), -1, manager->GetIOSettings()), "Cannot reopen converted FBX");
    Require(importer->IsFBX(), "Output is not FBX");
    auto* scene = FbxScene::Create(manager.get(), "test");
    Require(importer->Import(scene), "Cannot import converted FBX");
    Require(scene->GetSrcObjectCount<FbxMesh>() == expectedMeshes, "Conversion changed the expected mesh count");
    int polygons = 0;
    for (int i = 0; i < scene->GetSrcObjectCount<FbxMesh>(); ++i)
        polygons += scene->GetSrcObject<FbxMesh>(i)->GetPolygonCount();
    Require(polygons == expectedPolygons, "Conversion changed the expected polygon count");
    if (std::filesystem::path(source).stem().string() == "rigged_triangle")
        Require(scene->GetSrcObjectCount<FbxSkin>() > 0 && scene->GetSrcObjectCount<FbxCluster>() > 0, "Conversion lost skinning");
    const auto extension = std::filesystem::path(source).extension().string();
    if (checkFixtureData && (extension == ".glb" || extension == ".gltf" || extension == ".blend"))
    {
        Require(scene->GetSrcObjectCount<FbxSurfaceMaterial>() > 0, "Lost material");
        Require(scene->GetSrcObjectCount<FbxAnimStack>() > 0, "Lost animation");
        Require(scene->GetSrcObjectCount<FbxFileTexture>() > 0, "Lost texture");
        for (int i = 0; i < scene->GetSrcObjectCount<FbxFileTexture>(); ++i)
            Require(std::filesystem::is_regular_file(scene->GetSrcObject<FbxFileTexture>(i)->GetFileName()),
                std::string("Embedded texture is missing after conversion cleanup: ") + scene->GetSrcObject<FbxFileTexture>(i)->GetFileName());
    }
    std::cout << "PASS " << source << " -> " << output << " (" << polygons << " polygons, "
        << scene->GetSrcObjectCount<FbxSurfaceMaterial>() << " materials, "
        << scene->GetSrcObjectCount<FbxFileTexture>() << " textures, "
        << scene->GetSrcObjectCount<FbxAnimStack>() << " animations)\n";
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 5 && std::string(argv[1]) == "--geometry")
        {
            VerifyConverted(argv[2], std::stoi(argv[3]), std::stoi(argv[4]), false);
            return 0;
        }
        if (argc > 1)
        {
            for (int i = 1; i < argc; ++i) VerifyConverted(argv[i]);
            return 0;
        }
        const auto directory = std::filesystem::temp_directory_path() / "openfbx-conversion-regression";
        Require(std::filesystem::create_directory(directory), "Test directory already exists");
        const auto source = directory / "triangle.OBJ";
        { std::ofstream file(source); file << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"; }
        const auto existing = directory / "triangle_converted.fbx";
        { std::ofstream file(existing); file << "existing file"; }
        VerifyConverted(source.string());
        std::ifstream preserved(existing);
        std::string contents;
        std::getline(preserved, contents);
        Require(contents == "existing file", "Conversion overwrote an existing file");
        preserved.close();
        std::string output, error;
        Require(!ConvertModelToFbx((directory / "missing.obj").string(), output, error), "Missing input was accepted");
        Require(output.empty() && !error.empty(), "Missing input did not report an error");
        Require(!ConvertModelToFbx((directory / "unknown.xyz").string(), output, error), "Unsupported format accepted");
        Require(PrepareModelForOpening(existing.string(), output, error) && output == existing.string(), "FBX was not passed through");
        for (const auto& entry : std::filesystem::directory_iterator(directory)) std::filesystem::remove(entry.path());
        std::filesystem::remove(directory);
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
