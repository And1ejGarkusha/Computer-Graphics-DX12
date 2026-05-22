#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include "d3dUtil.h"

struct OBJVertexData
{
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexCoord;
    DirectX::XMFLOAT3 Tangent;
};

struct OBJMaterialData
{
    std::string Name;
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.05f, 0.05f, 0.05f };
    float Roughness = 0.5f;

    std::string DiffuseMapPath;
    std::string NormalMapPath;
    std::string DisplaceMapPath;
};

struct OBJMeshData
{
    std::vector<OBJVertexData> Vertices;
    std::vector<std::uint32_t> Indices;
    std::unordered_map<std::string, SubmeshGeometry> DrawArgs;
    std::unordered_map<std::string, std::string> SubmeshToMaterial;
};

class OBJLoader
{
public:
    static bool Load(const std::string& filename,
        OBJMeshData& outMesh,
        std::vector<OBJMaterialData>& outMaterials);

private:
    static bool LoadMTL(const std::string& filename,
        std::vector<OBJMaterialData>& outMaterials);

    static void ComputeTangents(OBJMeshData& mesh);
};