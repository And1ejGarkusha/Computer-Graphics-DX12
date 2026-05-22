#include "OBJLoader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <map>
#include <tuple>

using namespace DirectX;

struct VertexKeyHash
{
    std::size_t operator()(const std::tuple<int, int, int>& key) const
    {
        const auto& [p, t, n] = key;
        return std::hash<int>()(p) ^ (std::hash<int>()(t) << 1) ^ (std::hash<int>()(n) << 2);
    }
};

bool OBJLoader::LoadMTL(const std::string& filename,
    std::vector<OBJMaterialData>& outMaterials)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        OutputDebugStringA(("Failed to open MTL file: " + filename + "\n").c_str());
        return false;
    }

    OBJMaterialData currentMaterial;
    std::string line;

    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "newmtl")
        {
            if (!currentMaterial.Name.empty())
            {
                outMaterials.push_back(currentMaterial);
            }
            currentMaterial = OBJMaterialData();
            iss >> currentMaterial.Name;
            OutputDebugStringA(("Found material: " + currentMaterial.Name + "\n").c_str());
        }
        else if (prefix == "Kd")
        {
            iss >> currentMaterial.DiffuseAlbedo.x
                >> currentMaterial.DiffuseAlbedo.y
                >> currentMaterial.DiffuseAlbedo.z;
            currentMaterial.DiffuseAlbedo.w = 1.0f;
        }
        else if (prefix == "Ks")
        {
            float r, g, b;
            iss >> r >> g >> b;
            currentMaterial.FresnelR0 = XMFLOAT3(r, g, b);
        }
        else if (prefix == "Ns")
        {
            float shininess;
            iss >> shininess;
            currentMaterial.Roughness = 1.0f - (shininess / 1000.0f);
            currentMaterial.Roughness = std::clamp(currentMaterial.Roughness, 0.05f, 0.95f);
        }
        else if (prefix == "map_Kd")
        {
            iss >> currentMaterial.DiffuseMapPath;
            OutputDebugStringA(("Diffuse tex: " + currentMaterial.DiffuseMapPath + "\n").c_str());
        }
        else if (prefix == "map_Bump" || prefix == "bump")
        {
            std::string token;
            iss >> token;
            if (token == "-bm")
            {
                float bumpScale;
                iss >> bumpScale;
                iss >> token;
            }
            currentMaterial.NormalMapPath = token;
            OutputDebugStringA(("Normal map: " + currentMaterial.NormalMapPath + "\n").c_str());
        }
        else if (prefix == "map_Disp")
        {
            iss >> currentMaterial.DisplaceMapPath;
            OutputDebugStringA(("Displacement map: " + currentMaterial.DisplaceMapPath + "\n").c_str());
        }
        else if (prefix == "d")
        {
            float alpha;
            iss >> alpha;
            currentMaterial.DiffuseAlbedo.w = alpha;
        }
        else if (prefix == "Tr")
        {
            float alpha;
            iss >> alpha;
            currentMaterial.DiffuseAlbedo.w = 1.0f - alpha;
        }
    }

    if (!currentMaterial.Name.empty())
    {
        outMaterials.push_back(currentMaterial);
    }

    file.close();
    return !outMaterials.empty();
}

bool OBJLoader::Load(const std::string& filename,
    OBJMeshData& outMesh,
    std::vector<OBJMaterialData>& outMaterials)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        OutputDebugStringA(("Failed to open OBJ file: " + filename + "\n").c_str());
        return false;
    }

    std::filesystem::path objPath(filename);
    std::string objDir = objPath.parent_path().string();
    if (!objDir.empty() && objDir.back() != '\\' && objDir.back() != '/')
        objDir += "/";

    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> texCoords;

    std::unordered_map<std::tuple<int, int, int>, UINT, VertexKeyHash> vertexCache;

    std::string currentMaterial = "default";
    std::string currentSubmesh = "default";
    UINT currentStartIndex = 0;
    UINT currentBaseVertex = 0;

    auto normalizeIndex = [](int idx, int total) -> int
        {
            if (idx < 0) return total + idx;
            return idx - 1;
        };

    auto getVertexIndex = [&](int posIdx, int texIdx, int normIdx) -> UINT
        {
            int p = normalizeIndex(posIdx, (int)positions.size());
            int t = (texIdx == 0) ? -1 : normalizeIndex(texIdx, (int)texCoords.size());
            int n = (normIdx == 0) ? -1 : normalizeIndex(normIdx, (int)normals.size());

            auto key = std::make_tuple(p, t, n);

            auto it = vertexCache.find(key);
            if (it != vertexCache.end())
                return it->second;

            OBJVertexData vertex = {};

            vertex.Position = (p >= 0 && p < (int)positions.size()) ? positions[p] : XMFLOAT3(0, 0, 0);
            vertex.Normal = (n >= 0 && n < (int)normals.size()) ? normals[n] : XMFLOAT3(0, 1, 0);
            vertex.TexCoord = { 0.5f, 0.5f };

            if (t >= 0 && t < (int)texCoords.size())
            {
                vertex.TexCoord = texCoords[t];
            }

            UINT newIndex = (UINT)outMesh.Vertices.size();
            outMesh.Vertices.push_back(vertex);
            vertexCache[key] = newIndex;
            return newIndex;
        };

    auto finalizeSubmesh = [&]()
        {
            if (!currentSubmesh.empty() && outMesh.Indices.size() > currentStartIndex)
            {
                SubmeshGeometry submesh;
                submesh.IndexCount = (UINT)outMesh.Indices.size() - currentStartIndex;
                submesh.StartIndexLocation = currentStartIndex;
                submesh.BaseVertexLocation = currentBaseVertex;
                outMesh.DrawArgs[currentSubmesh] = submesh;
                outMesh.SubmeshToMaterial[currentSubmesh] = currentMaterial;

                OutputDebugStringA(("Saved submesh: " + currentSubmesh +
                    " | Mat: " + currentMaterial +
                    " | Indices: " + std::to_string(submesh.IndexCount) + "\n").c_str());
            }
        };

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v")
        {
            XMFLOAT3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        }
        else if (prefix == "vn")
        {
            XMFLOAT3 norm;
            iss >> norm.x >> norm.y >> norm.z;
            normals.push_back(norm);
        }
        else if (prefix == "vt")
        {
            XMFLOAT2 tex;
            iss >> tex.x >> tex.y;
            texCoords.push_back(tex);
        }
        else if (prefix == "f")
        {
            std::string v1, v2, v3;
            iss >> v1 >> v2 >> v3;

            auto parseFaceVertex = [](const std::string& v, int& posIdx, int& texIdx, int& normIdx)
                {
                    posIdx = texIdx = normIdx = 0;

                    size_t pos1 = v.find('/');
                    if (pos1 == std::string::npos) return;

                    posIdx = std::stoi(v.substr(0, pos1));

                    size_t pos2 = v.find('/', pos1 + 1);
                    if (pos2 == std::string::npos)
                    {
                        std::string rest = v.substr(pos1 + 1);
                        if (!rest.empty())
                            texIdx = std::stoi(rest);
                    }
                    else
                    {
                        std::string texStr = v.substr(pos1 + 1, pos2 - pos1 - 1);
                        if (!texStr.empty())
                            texIdx = std::stoi(texStr);

                        std::string normStr = v.substr(pos2 + 1);
                        if (!normStr.empty())
                            normIdx = std::stoi(normStr);
                    }
                };

            int p1, t1, n1, p2, t2, n2, p3, t3, n3;
            parseFaceVertex(v1, p1, t1, n1);
            parseFaceVertex(v2, p2, t2, n2);
            parseFaceVertex(v3, p3, t3, n3);

            outMesh.Indices.push_back(getVertexIndex(p1, t1, n1));
            outMesh.Indices.push_back(getVertexIndex(p2, t2, n2));
            outMesh.Indices.push_back(getVertexIndex(p3, t3, n3));
        }
        else if (prefix == "usemtl")
        {
            finalizeSubmesh();

            iss >> currentMaterial;
            currentSubmesh = "submesh_" + std::to_string(outMesh.DrawArgs.size());
            currentStartIndex = (UINT)outMesh.Indices.size();
            currentBaseVertex = 0;
        }
        else if (prefix == "mtllib")
        {
            std::string mtlFile;
            iss >> mtlFile;
            std::string fullMtlPath = objDir + mtlFile;
            LoadMTL(fullMtlPath, outMaterials);
        }
    }

    finalizeSubmesh();

    file.close();

    OutputDebugStringA(("Source: " + filename + "\n").c_str());
    OutputDebugStringA(("Unique positions: " + std::to_string(positions.size()) + "\n").c_str());
    OutputDebugStringA(("Unique texcoords: " + std::to_string(texCoords.size()) + "\n").c_str());
    OutputDebugStringA(("Unique normals: " + std::to_string(normals.size()) + "\n").c_str());
    OutputDebugStringA(("Final vertices: " + std::to_string(outMesh.Vertices.size()) + "\n").c_str());
    OutputDebugStringA(("Final indices: " + std::to_string(outMesh.Indices.size()) + "\n").c_str());
    OutputDebugStringA(("Triangles: " + std::to_string(outMesh.Indices.size() / 3) + "\n").c_str());
    OutputDebugStringA(("Submeshes: " + std::to_string(outMesh.DrawArgs.size()) + "\n").c_str());
    OutputDebugStringA(("Materials: " + std::to_string(outMaterials.size()) + "\n").c_str());

    ComputeTangents(outMesh);
    OutputDebugStringA("Tangents computed.\n");

    return !outMesh.Vertices.empty();
}
void OBJLoader::ComputeTangents(OBJMeshData& mesh)
{
    using namespace DirectX;

    const size_t vertCount = mesh.Vertices.size();
    std::vector<XMFLOAT3> tan1(vertCount, XMFLOAT3(0, 0, 0));

    for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
    {
        uint32_t i0 = mesh.Indices[i];
        uint32_t i1 = mesh.Indices[i + 1];
        uint32_t i2 = mesh.Indices[i + 2];

        const XMFLOAT3& p0 = mesh.Vertices[i0].Position;
        const XMFLOAT3& p1 = mesh.Vertices[i1].Position;
        const XMFLOAT3& p2 = mesh.Vertices[i2].Position;

        const XMFLOAT2& uv0 = mesh.Vertices[i0].TexCoord;
        const XMFLOAT2& uv1 = mesh.Vertices[i1].TexCoord;
        const XMFLOAT2& uv2 = mesh.Vertices[i2].TexCoord;

        float dx1 = p1.x - p0.x, dy1 = p1.y - p0.y, dz1 = p1.z - p0.z;
        float dx2 = p2.x - p0.x, dy2 = p2.y - p0.y, dz2 = p2.z - p0.z;

        float du1 = uv1.x - uv0.x, dv1 = uv1.y - uv0.y;
        float du2 = uv2.x - uv0.x, dv2 = uv2.y - uv0.y;

        float det = du1 * dv2 - du2 * dv1;
        if (fabsf(det) < 1e-8f) continue;

        float r = 1.0f / det;
        XMFLOAT3 tangent(
            r * (dv2 * dx1 - dv1 * dx2),
            r * (dv2 * dy1 - dv1 * dy2),
            r * (dv2 * dz1 - dv1 * dz2));

        for (uint32_t idx : { i0, i1, i2 })
        {
            tan1[idx].x += tangent.x;
            tan1[idx].y += tangent.y;
            tan1[idx].z += tangent.z;
        }
    }

    for (size_t i = 0; i < vertCount; ++i)
    {
        XMVECTOR N = XMLoadFloat3(&mesh.Vertices[i].Normal);
        XMVECTOR T = XMLoadFloat3(&tan1[i]);

        if (XMVector3Equal(T, XMVectorZero()))
        {
            XMVECTOR up = XMVectorSet(0, 1, 0, 0);
            if (fabsf(XMVectorGetX(XMVector3Dot(N, up))) > 0.99f)
                up = XMVectorSet(1, 0, 0, 0);
            T = XMVector3Normalize(XMVector3Cross(N, up));
        }
        else
        {
            T = XMVector3Normalize(T - N * XMVector3Dot(N, T));
        }

        XMStoreFloat3(&mesh.Vertices[i].Tangent, T);
    }
}