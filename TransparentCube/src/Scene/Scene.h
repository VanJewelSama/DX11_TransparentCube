#pragma once

#include "Actor.h"
#include "Camera.h"
#include "CameraManager.h"
#include <vector>
#include <memory>
#include <type_traits>

class Input;

// ============================================================================
// Scene（≈ UWorld 的单线程朴素版）：拥有并管理场景里的一切
// - Actor 由 SpawnActor 创建并纳入管理（Scene 持有所有权，外部拿裸指针）
// - 每帧逻辑刷新由 Scene::Tick 统一发起
// - 渲染侧只读访问（Render 不得修改逻辑状态）
// - 网格/纹理资产由 AssetManager 统一拥有（M12），Scene 不持有内容资产
// - 光源也是 Actor（M14），由 SpawnActor 纳入统一管理，Scene 不特殊持有
// ============================================================================
class Scene
{
public:
    // ≈ UWorld::SpawnActor：创建 T（默认 Actor）并纳入管理
    template<typename T = Actor>
    T* SpawnActor()
    {
        static_assert(std::is_base_of<Actor, T>::value, "T 必须派生自 Actor");
        m_actors.push_back(std::make_unique<T>());
        return static_cast<T*>(m_actors.back().get());
    }

    void Initialize();                     // 绑定默认相机
    void Tick(float dt, const Input& input); // 每帧逻辑刷新的统一入口

    // ---- 渲染侧只读访问 ----
    const std::vector<std::unique_ptr<Actor>>& GetActors() const { return m_actors; }

    // ---- 相机系统 ----
    CameraManager Cameras;
    FreeCamera    FreeCam;
    FixedCamera   FixedCam;

private:
    void SwitchCamera(ICamera* cam);

    std::vector<std::unique_ptr<Actor>> m_actors;
};
