#pragma once

#include <DirectXMath.h>

struct Texture;

// ============================================================================
// 着色模型（M16）：Renderer 按它选择 shader 路径
// （≈ UE 材质的 shading model 属性 / Unity 材质持有 shader 引用的最小版——
//   用枚举而不是 ShaderSet* 指针，保持 Scene 不依赖 Renderer 的模块方向）
// ============================================================================
enum class EShadingModel
{
    Lit,    // 默认：Lambert + Blinn-Phong + 法线贴图（Cube.hlsl）
    Toon,   // 风格化：阶梯量化漫反射 + 背面外扩描边（Toon.hlsl，双 Pass）
};

// ============================================================================
// 材质参数（≈ UE 材质实例暴露给着色器的参数集合，超简化版）
// 只保存"表面看起来是什么样"的数据，不保存 shader/状态对象——
// 那些属于渲染侧资源，M6 需要混合状态时再决定放在哪一层。
// ============================================================================
struct Material
{
    EShadingModel ShadingModel = EShadingModel::Lit;

    DirectX::XMFLOAT4 BaseColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    // 贴图颜色与 BaseColor 的混合强度：
    //   0 = 只使用贴图颜色；
    //   1 = 贴图颜色 × BaseColor（完全染色，默认行为）。
    // Alpha 始终由 BaseColor.a 控制，不受这个参数影响。
    float TintStrength = 1.0f;

    // 非拥有指针：纹理由 AssetManager 持有（和 MeshPtr 一样）。
    // 不贴图的材质应指向内置默认白纹理（AssetName::DefaultWhite，1×1 白色），
    // 这样 shader 可以无条件采样，不必写“有没有贴图”的分支。
    const Texture* TexturePtr = nullptr;

    // 切线空间法线贴图，Renderer 绑定到 t3/s0。
    // 没有法线细节时指向内置默认法线（AssetName::DefaultNormal，
    // 1×1 的 (128,128,255)，解析为平坦法线 (0,0,1)）。
    const Texture* NormalTexturePtr = nullptr;

    // 0 = 光滑（高光集中），1 = 粗糙（高光发散）。
    // 当前 Blinn-Phong 模型里它只被映射成高光指数，并不是真正的 PBR 粗糙度。
    float Roughness = 0.5f;

    // Blinn-Phong 高光强度，0 = 完全没有高光。
    float SpecularIntensity = 0.5f;

    // 半透明 Pass 的筛选依据：true 走 Translucency Pass（不写深度）。
    bool bIsTransparent = false;

    // ---- 风格化描边参数（M16，仅 ShadingModel = Toon 时使用）----
    // 描边用 inverted hull：背面沿法线外扩 OutlineWidth（世界空间，米），
    // 只画背面并输出 OutlineColor。Lit 材质忽略这两个参数。
    DirectX::XMFLOAT3 OutlineColor = { 0.0f, 0.0f, 0.0f };
    float           OutlineWidth = 0.02f;
};
