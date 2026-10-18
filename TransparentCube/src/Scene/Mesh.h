#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;

// 顶点：位置 + 法线 + 切线 + 副切线 + UV
// - 法线：表面朝外的方向，光照计算用；
// - 切线/副切线：切线空间基，法线贴图 TBN 用；
// - UV：纹理坐标，(0,0) 对应纹理左上角，(1,1) 对应右下角（D3D11 约定）。
struct Vertex
{
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT3 Tangent;     // 对应 +U 方向
    DirectX::XMFLOAT3 Bitangent;   // 对应 +V 方向
    DirectX::XMFLOAT2 UV;
};

// 一份几何数据 = 顶点缓冲 + 索引缓冲 + 索引数
// 创建好后不再修改（IMMUTABLE），纯粹是 GPU 侧资产
struct Mesh
{
    ComPtr<ID3D11Buffer> VertexBuffer;
    ComPtr<ID3D11Buffer> IndexBuffer;
    UINT                 IndexCount = 0;

    // 单位立方体：24 顶点（每面 4 个，边长 1，中心在原点）+ 36 索引。
    // 同一个角点在相邻面上的法线不同，所以不能共用 8 个顶点。
    // 失败时返回 IndexCount == 0 的空 Mesh
    static Mesh CreateCube(ID3D11Device* device);

    // XZ 地面平面：4 顶点（边长 size，中心在原点，法线朝 +Y）+ 6 索引
    // 颜色/材质不属于几何——摆放时由 Actor 持有
    static Mesh CreatePlane(ID3D11Device* device, float size = 10.0f);

    // 垂直平面（局部 XY 平面，中心在原点，法线朝 +Z）：
    // 用于镜面等竖直放置的薄片。
    static Mesh CreateVerticalPlane(ID3D11Device* device, float width, float height);
};
