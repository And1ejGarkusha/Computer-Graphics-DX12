#pragma once
#include <Windows.h>
#include <DirectXCollision.h>
#include <vector>
#include <memory>
#include <array>
#include <algorithm>
#include <cfloat>

class Octree
{
public:
    void Build(const std::vector<DirectX::BoundingBox>& itemBounds,
        int maxDepth = 6, int maxItemsPerLeaf = 16);

    void Query(const DirectX::BoundingFrustum& frustum,
        std::vector<UINT>& outIndices) const;

private:
    struct Node
    {
        DirectX::BoundingBox                 Bounds;
        std::vector<UINT>                    Indices;
        std::array<std::unique_ptr<Node>, 8> Children;
        bool                                 IsLeaf = true;
    };

    std::unique_ptr<Node> mRoot;

    static DirectX::BoundingBox ComputeUnion(
        const std::vector<DirectX::BoundingBox>& all,
        const std::vector<UINT>& indices);

    static void BuildNode(Node* node,
        const std::vector<DirectX::BoundingBox>& all,
        int depth, int maxDepth, int maxItems);

    static void CollectAll(const Node* node, std::vector<UINT>& out);

    static void QueryNode(const Node* node,
        const DirectX::BoundingFrustum& frustum,
        std::vector<UINT>& out);
};

inline DirectX::BoundingBox Octree::ComputeUnion(
    const std::vector<DirectX::BoundingBox>& all,
    const std::vector<UINT>& indices)
{
    using namespace DirectX;
    XMVECTOR mn = XMVectorReplicate(+FLT_MAX);
    XMVECTOR mx = XMVectorReplicate(-FLT_MAX);
    for (UINT i : indices)
    {
        XMVECTOR c = XMLoadFloat3(&all[i].Center);
        XMVECTOR e = XMLoadFloat3(&all[i].Extents);
        mn = XMVectorMin(mn, XMVectorSubtract(c, e));
        mx = XMVectorMax(mx, XMVectorAdd(c, e));
    }
    BoundingBox result;
    XMStoreFloat3(&result.Center, XMVectorScale(XMVectorAdd(mn, mx), 0.5f));
    XMStoreFloat3(&result.Extents, XMVectorScale(XMVectorSubtract(mx, mn), 0.5f));
    return result;
}

inline void Octree::BuildNode(Node* node,
    const std::vector<DirectX::BoundingBox>& all,
    int depth, int maxDepth, int maxItems)
{
    if (depth >= maxDepth || (int)node->Indices.size() <= maxItems)
    {
        node->IsLeaf = true;
        return;
    }
    node->IsLeaf = false;

    const DirectX::XMFLOAT3 c = node->Bounds.Center;
    const DirectX::XMFLOAT3 e = node->Bounds.Extents;
    const float hx = e.x * 0.5f, hy = e.y * 0.5f, hz = e.z * 0.5f;

    for (int i = 0; i < 8; ++i)
    {
        const float ox = (i & 1) ? hx : -hx;
        const float oy = (i & 2) ? hy : -hy;
        const float oz = (i & 4) ? hz : -hz;

        node->Children[i] = std::make_unique<Node>();
        node->Children[i]->Bounds.Center = { c.x + ox, c.y + oy, c.z + oz };
        node->Children[i]->Bounds.Extents = { hx, hy, hz };
        node->Children[i]->IsLeaf = true;
    }

    for (UINT idx : node->Indices)
    {
        for (int i = 0; i < 8; ++i)
        {
            if (node->Children[i]->Bounds.Intersects(all[idx]))
                node->Children[i]->Indices.push_back(idx);
        }
    }
    node->Indices.clear();

    for (int i = 0; i < 8; ++i)
    {
        if (!node->Children[i]->Indices.empty())
            BuildNode(node->Children[i].get(), all, depth + 1, maxDepth, maxItems);
    }
}

inline void Octree::Build(const std::vector<DirectX::BoundingBox>& itemBounds,
    int maxDepth, int maxItemsPerLeaf)
{
    mRoot.reset();
    if (itemBounds.empty()) return;

    std::vector<UINT> allIdx(itemBounds.size());
    for (UINT i = 0; i < (UINT)itemBounds.size(); ++i) allIdx[i] = i;

    mRoot = std::make_unique<Node>();
    mRoot->Bounds = ComputeUnion(itemBounds, allIdx);
    mRoot->Indices = std::move(allIdx);

    BuildNode(mRoot.get(), itemBounds, 0, maxDepth, maxItemsPerLeaf);
}

inline void Octree::CollectAll(const Node* node, std::vector<UINT>& out)
{
    if (!node) return;
    if (node->IsLeaf)
        out.insert(out.end(), node->Indices.begin(), node->Indices.end());
    else
        for (int i = 0; i < 8; ++i)
            CollectAll(node->Children[i].get(), out);
}

inline void Octree::QueryNode(const Node* node,
    const DirectX::BoundingFrustum& frustum,
    std::vector<UINT>& out)
{
    using namespace DirectX;
    if (!node) return;

    const ContainmentType ct = frustum.Contains(node->Bounds);

    if (ct == DISJOINT)  return;
    if (ct == CONTAINS) { CollectAll(node, out); return; }

    if (node->IsLeaf)
        out.insert(out.end(), node->Indices.begin(), node->Indices.end());
    else
        for (int i = 0; i < 8; ++i)
            QueryNode(node->Children[i].get(), frustum, out);
}

inline void Octree::Query(const DirectX::BoundingFrustum& frustum,
    std::vector<UINT>& outIndices) const
{
    outIndices.clear();
    QueryNode(mRoot.get(), frustum, outIndices);

    std::sort(outIndices.begin(), outIndices.end());
    outIndices.erase(std::unique(outIndices.begin(), outIndices.end()),
        outIndices.end());
}