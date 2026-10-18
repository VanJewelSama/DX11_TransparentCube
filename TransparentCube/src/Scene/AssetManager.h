#pragma once

#include "Mesh.h"
#include "Texture.h"

#include <d3d11.h>
#include <string>
#include <unordered_map>

// ============================================================================
// 资产管理层（≈ UAssetManager 的最小版）
//
// 职责：
//   - 统一拥有所有网格/纹理资产；Scene/Actor 只持有非拥有的裸指针，
//     UWorld 不拥有内容资产——本 demo 的 Scene 同样不持有。
//   - 按名字注册与查找，命名约定（对照 UE 的 /Game 与 /Engine）：
//       * 文件纹理：相对路径即名字（"assets/img_cube.JPG"）；
//       * 程序生成资产：虚拟路径（"Procedural/Cube"）；
//       * 引擎内置资产："Engine/..."（≈ Engine Content 的默认纹理）。
//
// 刻意不做（demo 规模）：异步加载、软引用、句柄/ID、资产卸载、热重载。
// unordered_map 节点存储保证元素地址在 rehash 后仍稳定，
// 外发裸指针在"不删元素"的前提下安全（本 demo 不删）。
// ============================================================================

// 引擎内置资产名（≈ /Engine 内容）：多处引用，用常量避免魔法字符串
namespace AssetName
{
    constexpr wchar_t DefaultWhite[]  = L"Engine/DefaultWhite";    // 1×1 白色
    constexpr wchar_t DefaultNormal[] = L"Engine/DefaultNormal";   // 1×1 平坦法线 (128,128,255)
}

// 文件纹理加载失败时的兜底策略
enum class ETextureFallback
{
    Checkerboard,   // 颜色贴图：生成棋盘格（贴图链路仍可验证）
    FlatNormal,     // 法线贴图：生成 1×1 平坦法线
};

class AssetManager
{
public:
    // 只创建引擎内置资产（DefaultWhite / DefaultNormal）
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    // 注册程序生成的网格（Mesh::CreateCube 等工厂的产物）。失败返回 nullptr。
    Mesh* RegisterMesh(const wchar_t* name, Mesh&& mesh);

    // 加载文件纹理并按 fallback 策略兜底。
    // 契约：返回非空 → 该名字必然已注册且有效，调用方无需再判空；
    //       返回 nullptr → 连 fallback 都创建失败（初始化应中止）。
    Texture* LoadTexture(const wchar_t* name, const wchar_t* path,
                         ETextureFallback fallback = ETextureFallback::Checkerboard);

    // 查找；未注册的名字返回 nullptr
    Mesh*    GetMesh(const wchar_t* name);
    Texture* GetTexture(const wchar_t* name);

private:
    std::unordered_map<std::wstring, Mesh>    m_meshes;
    std::unordered_map<std::wstring, Texture> m_textures;

    ID3D11Device*        m_device  = nullptr;   // 供 Initialize 之后再加载
    ID3D11DeviceContext* m_context = nullptr;
};
