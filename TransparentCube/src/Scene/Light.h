#pragma once

#include "Actor.h"

// ============================================================================
// 光源 Actor（M14，≈ ALight 层级的最小版；无组件系统 → 浅类层级）
//
// 光源也是场景里的 Actor：由 SpawnActor 纳入 Scene 统一管理，
// Renderer 每帧像收集 StaticMeshActor 一样收集光源（≈ FScene 收集 lights）。
// Scene 不再特殊持有任何光源成员。
//
// 层级：
//   LightActor            基类：所有光源共享的参数（光色 + 强度）
//   ├─ DirectionalLightActor  平行光：方向由 Actor Rotation 决定（≈ UE 平行光用朝向）
//   └─ PointLightActor        点光源：位置直接用 Actor::Location，额外只有影响半径
//
// 光源默认无行为（基类 Tick 为空实现）；以后想让光源动起来（环绕、闪烁），
// 在派生类里 override Tick 即可，机制与 StaticMeshActor 一致。
// ============================================================================

class LightActor : public Actor
{
public:
    // rgb = 光色, a = 强度（≈ LightColor + Intensity 合并存储）
    DirectX::XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };

    // ---- 位置指示器（M15）----
    // 在光源位置画一个不透明小方块（unlit、不投影、不写 stencil），
    // 方便在场景里定位光源。网格由 Renderer 统一持有
    // （SetLightIndicatorMesh），光源 Actor 不持有渲染资源——
    // 指示器是渲染侧的可视化关切（≈ UE 编辑器里的光源图标）。
    bool  bShowIndicator = false;
    float IndicatorSize  = 0.15f;   // 指示方块边长（米）
};

// 平行光：只取 Rotation，Location / Scale 对它无意义（不要缩放光源 Actor）
class DirectionalLightActor : public LightActor
{
public:
    DirectionalLightActor()
    {
        // 沿用 M4 以来的主光参数（兜底路径也用它，保持视觉一致）
        Color = { 1.00f, 0.96f, 0.88f, 1.0f };
    }

    // 环境光颜色：模拟多次反弹的间接光（场景级参数，挂在这盏主光上）
    DirectX::XMFLOAT4 Ambient = { 0.15f, 0.17f, 0.22f, 1.0f };

    // shader / ShadowMap 需要的 L（表面 → 光源，世界空间，已归一化）。
    // 光线传播方向 = Actor 本地 +Z 经旋转后的方向；L 是它的反方向。
    //
    // 注意一个历史约定差异：Actor 的 XMMatrixRotationRollPitchYaw 里
    // Rotation.x（pitch）为正值时 +Z 朝 -y 低头，与 CameraViewInfo::GetForward
    // 的“pitch 正值 = 抬头”符号相反。配置角度时以本函数的输出为准。
    DirectX::XMFLOAT3 GetLightDirection() const;
};

// 点光源：位置用 Actor::Location；无阴影、仅直接光照
class PointLightActor : public LightActor
{
public:
    PointLightActor()
    {
        bShowIndicator = true;   // 点光源默认显示位置指示器
    }

    // 影响半径：超出后完全不贡献（shader 里做平滑截断）
    float Range = 5.0f;
};

// 点光源数量上限：必须与 Renderer 的 PerFrameCB、Cube.hlsl 的数组大小保持一致
constexpr int kMaxPointLights = 4;
