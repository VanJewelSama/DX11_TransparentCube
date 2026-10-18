#include "Actor.h"

using namespace DirectX;

XMMATRIX Actor::GetWorldMatrix() const
{
    // 行向量约定 v·S·R·T：先缩放，再旋转，最后平移
    return XMMatrixScaling(Scale.x, Scale.y, Scale.z) *
           XMMatrixRotationRollPitchYaw(Rotation.x, Rotation.y, Rotation.z) *
           XMMatrixTranslation(Location.x, Location.y, Location.z);
}

void StaticMeshActor::Tick(float dt)
{
    ApplyRotationRate(dt);
}

void StaticMeshActor::ApplyRotationRate(float dt)
{
    if (RotationRate.x == 0.0f && RotationRate.y == 0.0f && RotationRate.z == 0.0f)
        return;
    Rotation.x += RotationRate.x * dt;
    Rotation.y += RotationRate.y * dt;
    Rotation.z += RotationRate.z * dt;
}
