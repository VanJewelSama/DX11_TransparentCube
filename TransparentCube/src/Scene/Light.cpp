#include "Light.h"

using namespace DirectX;

XMFLOAT3 DirectionalLightActor::GetLightDirection() const
{
    // 光线传播方向 = 本地 +Z 经旋转后的方向（与 Actor::GetWorldMatrix 同一套
    // 旋转函数，但只取旋转部分，忽略 Location/Scale）。
    const XMMATRIX rotation =
        XMMatrixRotationRollPitchYaw(Rotation.x, Rotation.y, Rotation.z);
    const XMVECTOR forward =
        XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotation);

    // Lambert 公式要的是 L（表面 → 光源）= 传播方向的反方向
    const XMVECTOR L = XMVector3Normalize(XMVectorNegate(forward));

    XMFLOAT3 result;
    XMStoreFloat3(&result, L);
    return result;
}
