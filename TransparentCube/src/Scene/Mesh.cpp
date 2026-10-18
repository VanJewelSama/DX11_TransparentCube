#include "Mesh.h"
#include <cstdint>
#include <cstdio>

using namespace DirectX;

Mesh Mesh::CreateCube(ID3D11Device* device)
{
    // 8 个角点位置（-0.5 ~ +0.5）。这里只存位置，法线由每个面单独提供。
    const XMFLOAT3 corners[8] =
    {
        XMFLOAT3(-0.5f, -0.5f, -0.5f),   // 0
        XMFLOAT3(-0.5f, +0.5f, -0.5f),   // 1
        XMFLOAT3(+0.5f, +0.5f, -0.5f),   // 2
        XMFLOAT3(+0.5f, -0.5f, -0.5f),   // 3
        XMFLOAT3(-0.5f, -0.5f, +0.5f),   // 4
        XMFLOAT3(-0.5f, +0.5f, +0.5f),   // 5
        XMFLOAT3(+0.5f, +0.5f, +0.5f),   // 6
        XMFLOAT3(+0.5f, -0.5f, +0.5f),   // 7
    };

    // 每个面 4 个角点，顶点顺序沿用 M2 验证过的绕序：
    // 从外侧看为顺时针（D3D 默认 FrontCounterClockwise = FALSE）。
    // 四边形按 [0,1,2,3] 排列，索引固定为 0,1,2 / 0,2,3。
    //
    // 为什么一个立方体要 24 个顶点而不是 8 个？
    // 因为同一个角点同时属于三个面，而三个面的朝外法线不同。
    // 顶点缓冲里一个顶点只能带一个法线，所以每个面复制一份角点。
    //
    // UV 不手填，而是给每个面一个"纹理上方"参考方向 Up，
    // 再用面内切线基算出 UV：
    //   Right = cross(Normal, Up)
    //   u = dot(P, Right) + 0.5
    //   v = 0.5 - dot(P, Up)
    // 单位立方体坐标范围是 [-0.5, 0.5]，所以 u/v 正好落在 [0,1]。
    // v 取反是因为 D3D11 的纹理坐标 (0,0) 在左上角。
    struct Face
    {
        int      Corner[4];
        XMFLOAT3 Normal;
        XMFLOAT3 Up;
    };
    const Face faces[6] =
    {
        { { 3, 0, 1, 2 }, {  0.0f,  0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },   // -Z
        { { 0, 4, 5, 1 }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f, 0.0f } },   // -X
        { { 3, 7, 4, 0 }, {  0.0f, -1.0f,  0.0f }, { 0.0f, 0.0f, 1.0f } },   // -Y
        { { 1, 5, 6, 2 }, {  0.0f,  1.0f,  0.0f }, { 0.0f, 0.0f, 1.0f } },   // +Y
        { { 2, 6, 7, 3 }, {  1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f, 0.0f } },   // +X
        { { 7, 6, 5, 4 }, {  0.0f,  0.0f,  1.0f }, { 0.0f, 1.0f, 0.0f } },   // +Z
    };

    Vertex        vertices[24];
    std::uint16_t indices[36];

    for (int f = 0; f < 6; ++f)
    {
        const XMVECTOR normal = XMLoadFloat3(&faces[f].Normal);
        const XMVECTOR up     = XMLoadFloat3(&faces[f].Up);
        const XMVECTOR right  = XMVector3Normalize(XMVector3Cross(normal, up));
        // UV 的 v = 0.5 - dot(P, Up)，所以 +V 方向对应 -Up
        const XMVECTOR bitangent = XMVectorNegate(up);

        for (int v = 0; v < 4; ++v)
        {
            Vertex& vertex   = vertices[f * 4 + v];
            vertex.Position  = corners[faces[f].Corner[v]];
            vertex.Normal    = faces[f].Normal;
            vertex.Tangent   = XMFLOAT3(XMVectorGetX(right),
                                        XMVectorGetY(right),
                                        XMVectorGetZ(right));
            vertex.Bitangent = XMFLOAT3(XMVectorGetX(bitangent),
                                        XMVectorGetY(bitangent),
                                        XMVectorGetZ(bitangent));

            const XMVECTOR p = XMLoadFloat3(&vertex.Position);
            vertex.UV.x = XMVectorGetX(XMVector3Dot(p, right)) + 0.5f;
            vertex.UV.y = 0.5f - XMVectorGetX(XMVector3Dot(p, up));
        }

        const std::uint16_t base = static_cast<std::uint16_t>(f * 4);
        indices[f * 6 + 0] = base + 0;
        indices[f * 6 + 1] = base + 1;
        indices[f * 6 + 2] = base + 2;
        indices[f * 6 + 3] = base + 0;
        indices[f * 6 + 4] = base + 2;
        indices[f * 6 + 5] = base + 3;
    }

    Mesh mesh;

    // ---- 顶点缓冲：IMMUTABLE = 创建时填好数据，之后 CPU/GPU 都不再改 ----
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.Usage     = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbInit = {};
    vbInit.pSysMem = vertices;
    if (FAILED(device->CreateBuffer(&vbDesc, &vbInit, &mesh.VertexBuffer)))
    {
        printf("[Mesh] 创建顶点缓冲失败\n");
        return {};
    }

    // ---- 索引缓冲 ----
    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = sizeof(indices);
    ibDesc.Usage     = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibInit = {};
    ibInit.pSysMem = indices;
    if (FAILED(device->CreateBuffer(&ibDesc, &ibInit, &mesh.IndexBuffer)))
    {
        printf("[Mesh] 创建索引缓冲失败\n");
        return {};
    }

    mesh.IndexCount = static_cast<UINT>(sizeof(indices) / sizeof(indices[0]));
    return mesh;
}

Mesh Mesh::CreatePlane(ID3D11Device* device, float size)
{
    const float h = size * 0.5f;
    const XMFLOAT3 up        = XMFLOAT3(0.0f, 1.0f, 0.0f);
    const XMFLOAT3 tangent   = XMFLOAT3(1.0f, 0.0f, 0.0f);   // +U = +X
    const XMFLOAT3 bitangent = XMFLOAT3(0.0f, 0.0f, -1.0f);  // +V = -Z
    const Vertex vertices[] =
    {
        // 位置                       法线 切线    副切线     UV
        { XMFLOAT3(-h, 0.0f, -h), up, tangent, bitangent, XMFLOAT2(0.0f, 1.0f) },   // 0 左下
        { XMFLOAT3(-h, 0.0f, +h), up, tangent, bitangent, XMFLOAT2(0.0f, 0.0f) },   // 1 左上
        { XMFLOAT3(+h, 0.0f, +h), up, tangent, bitangent, XMFLOAT2(1.0f, 0.0f) },   // 2 右上
        { XMFLOAT3(+h, 0.0f, -h), up, tangent, bitangent, XMFLOAT2(1.0f, 1.0f) },   // 3 右下
    };

    // 绕序使右手定则法线朝 +Y（与立方体同一约定：外侧法线朝外）
    const std::uint16_t indices[] = { 0, 1, 2,   0, 2, 3 };

    Mesh mesh;

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.Usage     = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbInit = {};
    vbInit.pSysMem = vertices;
    if (FAILED(device->CreateBuffer(&vbDesc, &vbInit, &mesh.VertexBuffer)))
    {
        printf("[Mesh] 创建平面顶点缓冲失败\n");
        return {};
    }

    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = sizeof(indices);
    ibDesc.Usage     = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibInit = {};
    ibInit.pSysMem = indices;
    if (FAILED(device->CreateBuffer(&ibDesc, &ibInit, &mesh.IndexBuffer)))
    {
        printf("[Mesh] 创建平面索引缓冲失败\n");
        return {};
    }

    mesh.IndexCount = static_cast<UINT>(sizeof(indices) / sizeof(indices[0]));
    return mesh;
}

Mesh Mesh::CreateVerticalPlane(ID3D11Device* device, float width, float height)
{
    const float hw = width * 0.5f;
    const float hh = height * 0.5f;
    const XMFLOAT3 normal    = XMFLOAT3(0.0f, 0.0f, 1.0f);
    const XMFLOAT3 tangent   = XMFLOAT3(1.0f, 0.0f, 0.0f);
    const XMFLOAT3 bitangent = XMFLOAT3(0.0f, -1.0f, 0.0f);   // +V 向下

    const Vertex vertices[] =
    {
        { XMFLOAT3(-hw, -hh, 0.0f), normal, tangent, bitangent, XMFLOAT2(0.0f, 1.0f) },   // 0 左下
        { XMFLOAT3(-hw, +hh, 0.0f), normal, tangent, bitangent, XMFLOAT2(0.0f, 0.0f) },   // 1 左上
        { XMFLOAT3(+hw, +hh, 0.0f), normal, tangent, bitangent, XMFLOAT2(1.0f, 0.0f) },   // 2 右上
        { XMFLOAT3(+hw, -hh, 0.0f), normal, tangent, bitangent, XMFLOAT2(1.0f, 1.0f) },   // 3 右下
    };

    // 从 +Z 外侧看为顺时针（CULL_BACK 的默认正面）。
    const std::uint16_t indices[] = { 0, 2, 1,   0, 3, 2 };

    Mesh mesh;

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.Usage     = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbInit = {};
    vbInit.pSysMem = vertices;
    if (FAILED(device->CreateBuffer(&vbDesc, &vbInit, &mesh.VertexBuffer)))
    {
        printf("[Mesh] 创建垂直平面顶点缓冲失败\n");
        return {};
    }

    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = sizeof(indices);
    ibDesc.Usage     = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibInit = {};
    ibInit.pSysMem = indices;
    if (FAILED(device->CreateBuffer(&ibDesc, &ibInit, &mesh.IndexBuffer)))
    {
        printf("[Mesh] 创建垂直平面索引缓冲失败\n");
        return {};
    }

    mesh.IndexCount = static_cast<UINT>(sizeof(indices) / sizeof(indices[0]));
    return mesh;
}
