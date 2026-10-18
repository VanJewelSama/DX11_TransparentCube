#pragma once

#include "RHI/D3D11Device.h"
#include "Shader.h"
#include "ShadowMap.h"
#include "PostProcess.h"
#include "PlanarMirror.h"
#include <DirectXMath.h>
#include <vector>

class Window;
class Scene;
class StaticMeshActor;
class DirectionalLightActor;
class PointLightActor;
class LightActor;
struct Mesh;

// 渲染器（≈ UE FSceneRenderer 的雏形）
// 一帧 = 清屏 → 从场景收集可渲染物 → 逐个 DrawIndexed → Present
// 渲染刷新只读场景（const Scene&），不修改任何逻辑状态（≈ 渲染线程契约）
class Renderer
{
public:
    bool Initialize(Window& window);
    void OnResize(int width, int height);

    void Render(const Scene& scene);

    // 实体/线框切换（调试用，观察三角形剖分与剔除行为）
    void SetWireframe(bool enable) { m_wireframe = enable; }
    bool IsWireframe() const { return m_wireframe; }

    // M8 / M8.5：后处理模式循环（数字键 3）：关 → 灰度 → 暗化蒙版 → 关
    void CyclePostProcessMode();
    const char* GetPostProcessModeName() const;

    // M10.5：FXAA 开关（数字键 0）
    void ToggleFXAA() { m_fxaaEnabled = !m_fxaaEnabled; }
    bool IsFXAAEnabled() const { return m_fxaaEnabled; }

    // M15：设置光源位置指示器共用的网格（非拥有；通常为 Procedural/Cube）
    void SetLightIndicatorMesh(Mesh* mesh) { m_indicatorMesh = mesh; }

    ID3D11Device* GetDevice() const { return m_rhi.GetDevice(); }
    ID3D11DeviceContext* GetContext() const { return m_rhi.GetContext(); }

private:
    // 光源数据来自渲染收集到的 Light Actor（M14）
    void UpdatePerFrameConstants(ID3D11DeviceContext* context,
                                 const DirectX::XMMATRIX& viewProj,
                                 const DirectX::XMFLOAT3& cameraPos,
                                 const DirectionalLightActor& dirLight,
                                 const std::vector<const PointLightActor*>& pointLights);

    // reflectionPass = true：反射 Pass，使用翻转绕序的光栅化状态
    void DrawObject(const StaticMeshActor& obj, bool reflectionPass = false);

    // M16：Toon 物体双 Pass（先描边后本体）；CB/IA 已由 DrawObject 备好
    void DrawToonObject(const StaticMeshActor& obj, bool reflectionPass);

    // M15：光源位置指示器（unlit 小方块；不进 Shadow Pass、不写 stencil）
    void DrawLightIndicators(const std::vector<const LightActor*>& lights, bool reflectionPass);

    D3D11Device            m_rhi;
    ShaderSet              m_cubeShader;
    ShaderSet              m_toonShader;         // M16：Toon 本体（量化光照）
    ShaderSet              m_toonOutlineShader;  // M16：Toon 描边（背面外扩）
    ShaderSet              m_unlitShader;    // M15：光源指示器（unlit 纯色）
    Mesh*                  m_indicatorMesh = nullptr;   // 指示器共用网格（非拥有）
    ShadowMap              m_shadowMap;     // M7：平行光阴影
    PostProcess            m_postProcess;   // M8：SceneColor + 全屏后处理
    PlanarMirror           m_planarMirror;  // M10：平面镜反射
    ComPtr<ID3D11Buffer>   m_perFrameCB;    // 每帧常量缓冲：ViewProj / 相机 / 光源
    ComPtr<ID3D11Buffer>   m_perObjectCB;   // 每物体常量缓冲：World / 法线矩阵 / 材质
    ComPtr<ID3D11SamplerState> m_linearSampler;   // 纹理采样：双线性过滤 + Wrap 寻址

    // 混合/深度状态：M6 用它们区分 Opaque Pass 与 Translucency Pass
    ComPtr<ID3D11BlendState>        m_blendOpaque;       // 关闭混合，直接写颜色
    ComPtr<ID3D11BlendState>        m_blendAlpha;        // SrcAlpha / InvSrcAlpha
    ComPtr<ID3D11DepthStencilState> m_depthOpaque;       // 深度测试 + 写深度
    ComPtr<ID3D11DepthStencilState> m_depthTransparent;  // 深度测试，但不写深度
    ComPtr<ID3D11DepthStencilState> m_depthTransparentStencil; // 透明 + 写 stencil=1（M8.5 蒙版）

    // 两套光栅化状态：实体填充（剔除背面）/ 线框（不剔除，画出全部边线）
    ComPtr<ID3D11RasterizerState> m_rsSolid;
    ComPtr<ID3D11RasterizerState> m_rsWireframe;
    ComPtr<ID3D11RasterizerState> m_rsOutline;   // M16 描边：实体 + CULL_FRONT
    bool m_wireframe = false;
    EPostProcessMode m_postProcessMode = EPostProcessMode::Off;   // 默认关闭，按 3 循环
    bool m_fxaaEnabled = false;                                    // 默认关闭，按 0 开启

    float m_aspect = 16.0f / 9.0f;   // 视口宽高比（OnResize 时更新；投影矩阵每帧由 PCM 缓存现算）

    float m_clearColor[4] = { 0.15f, 0.25f, 0.45f, 1.0f };
};
