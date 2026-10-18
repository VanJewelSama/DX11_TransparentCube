#include "AssetManager.h"

#include <cstdio>
#include <utility>

bool AssetManager::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!device || !context)
        return false;

    m_device  = device;
    m_context = context;

    // ---- 引擎内置资产（≈ Engine Content 的默认纹理）----
    Texture white  = Texture::CreateSolidColor(device, context, 255, 255, 255, 255);
    Texture normal = Texture::CreateSolidColor(device, context, 128, 128, 255, 255);
    if (!white.IsValid() || !normal.IsValid())
        return false;

    m_textures.emplace(AssetName::DefaultWhite, std::move(white));
    m_textures.emplace(AssetName::DefaultNormal, std::move(normal));
    return true;
}

Mesh* AssetManager::RegisterMesh(const wchar_t* name, Mesh&& mesh)
{
    if (!name || mesh.IndexCount == 0)
        return nullptr;

    auto [it, inserted] = m_meshes.try_emplace(name, std::move(mesh));
    if (!inserted)
        printf("[AssetManager] 网格名重复注册: %ls\n", name);
    return &it->second;
}

Texture* AssetManager::LoadTexture(const wchar_t* name, const wchar_t* path,
                                   ETextureFallback fallback)
{
    if (!name || !path)
        return nullptr;

    Texture texture = Texture::LoadFromFile(m_device, m_context, path);
    if (!texture.IsValid())
    {
        // 兜底资产也以同名注册，保证"返回非空 → 名字必然可取到有效纹理"的契约
        if (fallback == ETextureFallback::FlatNormal)
        {
            printf("[AssetManager] %ls 加载失败，生成 1×1 平坦法线 fallback。\n", path);
            texture = Texture::CreateSolidColor(m_device, m_context, 128, 128, 255, 255);
        }
        else
        {
            printf("[AssetManager] %ls 加载失败，使用程序生成的棋盘格 fallback。\n", path);
            texture = Texture::CreateCheckerboard(m_device, m_context, 512);
        }

        if (!texture.IsValid())
            return nullptr;
    }

    auto [it, inserted] = m_textures.try_emplace(name, std::move(texture));
    if (!inserted)
        printf("[AssetManager] 纹理名重复注册: %ls\n", name);
    return &it->second;
}

Mesh* AssetManager::GetMesh(const wchar_t* name)
{
    const auto it = m_meshes.find(name);
    return it != m_meshes.end() ? &it->second : nullptr;
}

Texture* AssetManager::GetTexture(const wchar_t* name)
{
    const auto it = m_textures.find(name);
    return it != m_textures.end() ? &it->second : nullptr;
}
