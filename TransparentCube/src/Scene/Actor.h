#pragma once

#include "Mesh.h"
#include "Material.h"

// ============================================================================
// Actor（≈ AActor 的最小版）："可摆放进场景的基础单位"
// 基类只持有"存在性"属性：变换 + Tick 框架，不含任何渲染/外观数据
// ============================================================================
class Actor
{
public:
    DirectX::XMFLOAT3 Location = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 Rotation = { 0.0f, 0.0f, 0.0f };   // 弧度 (Pitch, Yaw, Roll)
    DirectX::XMFLOAT3 Scale    = { 1.0f, 1.0f, 1.0f };

    DirectX::XMMATRIX GetWorldMatrix() const;   // S·R·T

    virtual void Tick(float dt) {}   // 基类默认无行为
    virtual ~Actor() = default;
};

// ============================================================================
// StaticMeshActor（≈ AStaticMeshActor 的最小版）：最基础的可视单位
// 外观 = 网格引用 + 材质参数；行为 = 轻量字段 + 虚 Tick
// ============================================================================
class StaticMeshActor : public Actor
{
public:
    Mesh*    MeshPtr = nullptr;   // 引用的模型资源（不拥有）
    Material MaterialData;        // 基础色/粗糙度/高光强度/半透明标记

    // 是否进入 Shadow Pass 投射阴影（≈ UE primitive 的 CastShadow）。
    // 地面这类只负责接收的物体可以设为 false。
    bool bCastShadow = true;

    // 是否把可见像素写入 stencil（用于后处理的屏幕区域蒙版）。
    // M8.5 里只有 CubeB 打开：它的轮廓区域会被后处理“保护”起来。
    bool bWriteStencil = false;

    // ---- 平面镜（M10）----
    // bIsMirror：主 Pass 用专门的镜面 Shader 绘制，并且不进入自身的反射 Pass。
    // MirrorTint：镜面默认的淡白色，a 表示与反射贴图的混合权重。
    bool bIsMirror = false;
    DirectX::XMFLOAT4 MirrorTint = { 0.92f, 0.94f, 0.97f, 0.15f };

    // 自转角速度（弧度/秒）；全零 = 不转。轻量行为字段，
    // 代价是以后每加一种行为每个对象都多占几个字节内存（本 demo 可接受）
    DirectX::XMFLOAT3 RotationRate = { 0.0f, 0.0f, 0.0f };

    void Tick(float dt) override;

private:
    void ApplyRotationRate(float dt);   // RotationRate 非零时执行自转
};
