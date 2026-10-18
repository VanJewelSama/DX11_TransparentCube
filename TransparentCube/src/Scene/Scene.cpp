#include "Scene.h"
#include "Core/Input.h"
#include <cstdio>

void Scene::Initialize()
{
    // 默认激活自由相机：相机自己与 PCM 绑定
    FreeCam.BindTo(Cameras);
}

void Scene::Tick(float dt, const Input& input)
{
    // ---- 相机切换与行为 ----
    if (input.WasKeyPressed('1')) SwitchCamera(&FreeCam);
    if (input.WasKeyPressed('2')) SwitchCamera(&FixedCam);
    if (ICamera* active = Cameras.GetViewTarget())
        active->Tick(dt, input);

    // ---- 所有 Actor 的逻辑刷新（由 Scene 统一发起）----
    for (auto& actor : m_actors)
        actor->Tick(dt);

    // ---- PCM：上一帧缓存滚动 + 从 ViewTarget 拉取本帧参数 ----
    Cameras.UpdateCache();
}

void Scene::SwitchCamera(ICamera* cam)
{
    if (Cameras.GetViewTarget() == cam)
        return;
    cam->BindTo(Cameras);   // 新激活的相机自己与 PCM 绑定
    printf("[Camera] ViewTarget -> %s\n", cam->GetName());
}
