#pragma once

#include <cstdint>
#include <DirectXMath.h>
#include <vector>

class GeometryGenerator
{
public:

    using uint16 = std::uint16_t;
    using uint32 = std::uint32_t;

    struct Vertex
    {
        Vertex() {}
        Vertex(
            const DirectX::XMFLOAT3& p,
            const DirectX::XMFLOAT3& n,
            const DirectX::XMFLOAT3& t,
            const DirectX::XMFLOAT2& uv,
            const DirectX::XMFLOAT2& c = DirectX::XMFLOAT2(0, 0)) :
            Position(p),
            Normal(n),
            TangentU(t),
            TexC(uv),
            Color(c) {
        }
        Vertex(
            float px, float py, float pz,
            float nx, float ny, float nz,
            float tx, float ty, float tz,
            float u, float v,
            float cx = 0, float cy = 0) :
            Position(px, py, pz),
            Normal(nx, ny, nz),
            TangentU(tx, ty, tz),
            TexC(u, v),
            Color(cx, cy) {
        }

        DirectX::XMFLOAT3 Position;
        DirectX::XMFLOAT3 Normal;
        DirectX::XMFLOAT3 TangentU;
        DirectX::XMFLOAT2 TexC;
        DirectX::XMFLOAT2 Color;
    };

	struct MeshData
	{
		std::vector<Vertex> Vertices;
        std::vector<uint32> Indices32;

        std::vector<uint16>& GetIndices16()
        {
			if(mIndices16.empty())
			{
				mIndices16.resize(Indices32.size());
				for(size_t i = 0; i < Indices32.size(); ++i)
					mIndices16[i] = static_cast<uint16>(Indices32[i]);
			}

			return mIndices16;
        }

	private:
		std::vector<uint16> mIndices16;
	};

    MeshData CreateBox(float width, float height, float depth, uint32 numSubdivisions);

    MeshData CreateSphere(float radius, uint32 sliceCount, uint32 stackCount);

    MeshData CreateGeosphere(float radius, uint32 numSubdivisions);

    MeshData CreateCylinder(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount);

    MeshData CreateGrid(float width, float depth, uint32 m, uint32 n);

    MeshData CreateQuad(float x, float y, float w, float h, float depth);

    MeshData CreateChessboard(float width, float depth, uint32 squaresPerSide, float yOffset = 0.0f);

private:
	void Subdivide(MeshData& meshData);
    Vertex MidPoint(const Vertex& v0, const Vertex& v1);
    void BuildCylinderTopCap(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount, MeshData& meshData);
    void BuildCylinderBottomCap(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount, MeshData& meshData);
};

