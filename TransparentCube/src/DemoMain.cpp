// ============================================================================
// TransparentCube Demo 入口（≈ UE 的 FEngineLoop：Init → 主循环 Tick → Shutdown）
// 里程碑 M16：风格化渲染（Toon 阶梯量化着色 + inverted hull 黑色描边）
//
// 本文件是"引擎循环 + Demo 场景装配"模块：
// - 全局对象只在本 .cpp 内可见（匿名命名空间）
// - main 只负责初始化和主循环；内容加载拆到 LoadContent()，场景摆放拆到 BuildScene()
// ============================================================================
#include "Core/Window.h"
#include "Core/Timer.h"
#include "Core/Input.h"
#include "Renderer/Renderer.h"
#include "Scene/Scene.h"
#include "Scene/Light.h"
#include "Scene/AssetManager.h"

#include <cstdio>
#include <Windows.h>

namespace
{
    // ---- 窗口参数 ----
    constexpr int     kWindowWidth  = 1280;
    constexpr int     kWindowHeight = 720;
    constexpr wchar_t kWindowTitle[] = L"TransparentCube - M16 Toon Shading";

    // WIC（Windows Imaging Component）是 COM 组件，使用前必须初始化 COM。
    // 用 RAII 包装：无论 main 从哪条 return 路径退出，都能正确 CoUninitialize。
    class ComScope
    {
    public:
        ComScope() : m_hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}

        // COM 初始化是"每次调用配对一次反初始化"的计数语义，
        // 复制这个对象会导致析构时多调用一次 CoUninitialize。
        ComScope(const ComScope&) = delete;
        ComScope& operator=(const ComScope&) = delete;

        ~ComScope()
        {
            // S_OK / S_FALSE 都表示本次调用需要配对 CoUninitialize；
            // RPC_E_CHANGED_MODE 表示线程已经用别的套间模型初始化过 COM，
            // 此时 COM 可用，但本次调用不负责反初始化。
            if (SUCCEEDED(m_hr))
                CoUninitialize();
        }

        bool IsUsable() const
        {
            return SUCCEEDED(m_hr) || m_hr == RPC_E_CHANGED_MODE;
        }

    private:
        HRESULT m_hr = E_FAIL;
    };

    // ============================================================================
    // 模块级全局对象
    //
    // 声明顺序 = 构造顺序；全局对象析构顺序与声明顺序相反。
    // g_com 必须最先声明：它最后析构，保证 Scene 里的 WIC/D3D 纹理对象
    // 先释放，再执行 CoUninitialize。
    // ============================================================================
    ComScope g_com;
    Window   g_window;
    Input    g_input;
    Renderer g_renderer;
    // g_assets 声明在 g_scene 之前：逆序析构时 Scene 的 Actor 先销毁，
    // 它们持有的资产裸指针随后才随 AssetManager 一起失效（与 g_com 同理）。
    AssetManager g_assets;
    Scene    g_scene;

    // ============================================================================
    // 内容加载：注册/加载所有网格与纹理（≈ 引擎启动期的 content loading）
    // 只负责"把资产放进 AssetManager"，不涉及场景摆放
    // ============================================================================
    bool LoadContent()
    {
        ID3D11Device* device = g_renderer.GetDevice();

        // ---- 程序生成网格：虚拟路径命名 ----
        if (!g_assets.RegisterMesh(L"Procedural/Cube", Mesh::CreateCube(device)))
            return false;
        if (!g_assets.RegisterMesh(L"Procedural/Plane", Mesh::CreatePlane(device, 10.0f)))
            return false;
        if (!g_assets.RegisterMesh(L"Procedural/VerticalPlane",
                                   Mesh::CreateVerticalPlane(device, 3.6f, 2.5f)))
            return false;

        // ---- 文件纹理：相对路径即资产名 ----
        // 想换贴图：把新图片放进 assets/，改下面的路径即可（建议 512/1024 正方形 PNG）。
        // 颜色贴图失败 → 棋盘格 fallback；法线贴图失败 → 平坦法线 fallback（不致命）。
        if (!g_assets.LoadTexture(L"assets/img_cube.JPG", L"assets/img_cube.JPG"))
            return false;
        if (!g_assets.LoadTexture(L"assets/img_cube_2.JPG", L"assets/img_cube_2.JPG"))
            return false;
        g_assets.LoadTexture(L"assets/normal_tian.png", L"assets/normal_tian.png",
                             ETextureFallback::FlatNormal);

        return true;
    }

    // ============================================================================
    // 场景装配：只负责"往场景里放东西"，不涉及窗口/设备初始化
    // ============================================================================
    void BuildScene()
    {
        // 自转立方体：贴 img_cube.JPG；表面偏光滑 → 高光集中、可见度高
        auto* cube = g_scene.SpawnActor<StaticMeshActor>();
        cube->MeshPtr = g_assets.GetMesh(L"Procedural/Cube");
        cube->MaterialData.TexturePtr        = g_assets.GetTexture(L"assets/img_cube.JPG");
        cube->MaterialData.NormalTexturePtr  = g_assets.GetTexture(L"assets/normal_tian.png");
        cube->MaterialData.BaseColor         = { 1.0f, 1.0f, 1.0f, 1.0f };  // 贴图已经带颜色，基础色用白
        cube->MaterialData.Roughness         = 0.35f;
        cube->MaterialData.SpecularIntensity = 0.8f;
        cube->RotationRate = { 0.0f, 0.8f, 0.0f };

        // 地面平面：绿色，很粗糙 → 高光发散、整体偏暗
        // （立方体底面在 y=-0.5，地面贴上去）
        // 不贴图时指向内置 1×1 白色纹理，shader 就可以统一走采样路径。
        auto* ground = g_scene.SpawnActor<StaticMeshActor>();
        ground->MeshPtr = g_assets.GetMesh(L"Procedural/Plane");
        ground->MaterialData.TexturePtr        = g_assets.GetTexture(AssetName::DefaultWhite);
        ground->MaterialData.NormalTexturePtr  = g_assets.GetTexture(AssetName::DefaultNormal);
        ground->MaterialData.BaseColor         = { 0.35f, 0.85f, 0.38f, 1.0f };
        ground->MaterialData.Roughness         = 0.9f;
        ground->MaterialData.SpecularIntensity = 0.15f;
        ground->Location = { 0.0f, -0.5f, 0.0f };
        // 地面只接收阴影，不进入 Shadow Pass；否则只会增加地面自阴影 acne。
        ground->bCastShadow = false;

        // 半透明 CubeB：使用 img_cube_2.JPG，并用红色 BaseColor 做轻染色；
        // TintStrength=0.5 → 贴图颜色为主；alpha = 0.7 控制整体透明度。
        // 位置略微抬高，让它在屏幕上覆盖 CubeA 的中心，便于观察“透过 B 看到 A”；
        // 这是为了让效果明显，不代表它必须悬空摆放。
        auto* transparentCube = g_scene.SpawnActor<StaticMeshActor>();
        transparentCube->MeshPtr = g_assets.GetMesh(L"Procedural/Cube");
        transparentCube->MaterialData.TexturePtr        = g_assets.GetTexture(L"assets/img_cube_2.JPG");
        transparentCube->MaterialData.NormalTexturePtr  = g_assets.GetTexture(AssetName::DefaultNormal);
        transparentCube->MaterialData.BaseColor         = { 1.0f, 0.12f, 0.12f, 0.7f };
        // 0 = 只保留贴图颜色，1 = 完全乘以红色；0.5 让贴图为主、红色中等染色。
        transparentCube->MaterialData.TintStrength      = 0.8f;
        transparentCube->MaterialData.Roughness         = 0.2f;
        transparentCube->MaterialData.SpecularIntensity = 0.6f;
        transparentCube->MaterialData.bIsTransparent    = true;
        transparentCube->bCastShadow                    = false;   // 透明物体不投实心阴影
        transparentCube->bWriteStencil                  = true;    // 后处理蒙版：保护它的屏幕区域
        transparentCube->Location      = { -0.3f, 0.15f, -2.f };
        transparentCube->Scale         = { 0.65f, 0.65f, 0.65f };
        transparentCube->RotationRate  = { 0.0f, 0.4f, 0.0f };

        // 平面镜：竖直立在右侧，面朝原点/主相机。
        // 镜面自己不投影、不写 stencil；主 Pass 用专门的 Mirror shader 绘制。
        auto* mirror = g_scene.SpawnActor<StaticMeshActor>();
        mirror->MeshPtr      = g_assets.GetMesh(L"Procedural/VerticalPlane");
        mirror->bIsMirror    = true;
        mirror->bCastShadow  = true;
        mirror->MirrorTint   = { 0.92f, 0.94f, 0.97f, 0.27f };
        // 镜面在 CubeA 右后方，朝向“前左方”，这样能反射到 CubeA 的背面/右侧。
        // 位置/朝向按当前默认机位调整过，让反射落在镜面中央附近。
        mirror->Location     = { 1.7f, 0.5f, 2.4f };
        mirror->Rotation.y   = -2.8f;

        // 风格化 CubeC（M16）：阶梯量化光照 + 黑色描边，验证"第二种着色模型"。
        // 用默认白纹理 + 纯色 BaseColor，扁平色块效果最直接；法线贴图不参与。
        auto* toonCube = g_scene.SpawnActor<StaticMeshActor>();
        toonCube->MeshPtr = g_assets.GetMesh(L"Procedural/Cube");
        toonCube->MaterialData.ShadingModel        = EShadingModel::Toon;
        toonCube->MaterialData.TexturePtr          = g_assets.GetTexture(AssetName::DefaultWhite);
        toonCube->MaterialData.NormalTexturePtr    = g_assets.GetTexture(AssetName::DefaultNormal);
        toonCube->MaterialData.BaseColor           = { 1.0f, 0.5f, 0.31f, 1.0f };
        toonCube->MaterialData.OutlineWidth        = 0.025f;
        toonCube->Location      = { 2.f, 0.0f, -1.f };
        toonCube->Scale         = { 0.7f, 0.7f, 0.7f };
        toonCube->RotationRate  = { 0.0f, 0.5f, 0.0f };

        // ---- M13/M14 光源：光源也是 Actor，由 SpawnActor 纳入场景管理 ----
        // 想调整光源：改这里的参数即可（点光源上限 kMaxPointLights 盏，超出会被忽略）。

        // 平行光：方向由 Rotation 决定（光线沿本地 +Z 传播）。
        // Rotation = (0.5893, 0.9153, 0) 使光线传播方向 ≈ (0.66, -0.56, 0.51)，
        // 等效于 M4 以来的 L = (-0.65, 0.55, -0.50)（表面→光源）的反方向。
        // 注意 Actor 的 pitch 约定与相机 GetForward 符号相反（见 Light.h 注释）。
        auto* sun = g_scene.SpawnActor<DirectionalLightActor>();
        sun->Rotation = { 0.5893f, 0.9153f, 0.0f };
        sun->Color    = { 1.00f, 0.96f, 0.88f, 1.0f };
        sun->Ambient  = { 0.15f, 0.17f, 0.22f, 1.0f };

        // 点光源 ×2：位置用 Actor::Location。
        // 一盏暖橙在 CubeA 右前方，一盏冷蓝在左后方：
        // 地面上能看到两片不同颜色的光斑，CubeA 同时受两盏灯影响。
        auto* warmLight = g_scene.SpawnActor<PointLightActor>();
        warmLight->Location = { 1.2f, 0.8f, -0.6f };
        warmLight->Range    = 5.0f;
        warmLight->Color    = { 1.0f, 0.55f, 0.25f, 3.0f };   // 暖橙

        auto* coolLight = g_scene.SpawnActor<PointLightActor>();
        coolLight->Location = { -0.5f, 0.5f, 1.5f };
        coolLight->Range    = 6.0f;
        coolLight->Color    = { 0.30f, 0.50f, 1.0f, 2.5f };   // 冷蓝

        // 光源位置指示器：点光源默认开启（bShowIndicator），共用立方体网格。
        // unlit 纯色（颜色 = 光源 Color）、不透明、不投影，只用于定位光源。
        g_renderer.SetLightIndicatorMesh(g_assets.GetMesh(L"Procedural/Cube"));
    }

    // ============================================================================
    // 初始化：窗口 → 输入 → 渲染器 → 资产管理 → 内容加载 → 场景 → 场景摆放
    // 任何一步失败都返回 false，由 main 统一退出。
    // ============================================================================
    bool InitializeDemo()
    {
        printf("==== TransparentCube Demo | M16: Toon Shading ====\n");
        printf("操作：按住鼠标右键环顾 | WASD 移动 | Q/E 降升 | Shift 加速\n");
        printf("      1/2 切换相机（自由/固定） | 3 后处理（关/置灰/暗化蒙版）\n");
        printf("      0 FXAA | Tab 线框 | ESC 退出\n");

        if (!g_window.Create(kWindowWidth, kWindowHeight, kWindowTitle))
            return false;

        g_input.Initialize(g_window.GetHandle());

        if (!g_renderer.Initialize(g_window))
            return false;

        // 资产管理层先建内置资产，再加载 demo 内容；最后才是场景（只剩相机绑定）
        if (!g_assets.Initialize(g_renderer.GetDevice(), g_renderer.GetContext()))
            return false;

        if (!LoadContent())
            return false;

        g_scene.Initialize();

        BuildScene();
        return true;
    }

    // ============================================================================
    // 每帧逻辑刷新（≈ 游戏线程侧）
    // 输入采样 → 调试开关 → 窗口缩放 → 相机/Actor Tick/PCM 缓存
    // ============================================================================
    void Update(float dt)
    {
        g_input.NewFrame();

        if (g_input.WasKeyPressed(VK_TAB))
        {
            g_renderer.SetWireframe(!g_renderer.IsWireframe());
            printf("线框模式: %s\n", g_renderer.IsWireframe() ? "开" : "关");
        }

        // 数字键 3 循环后处理模式：关 → 置灰 → 暗化蒙版 → 关。
        if (g_input.WasKeyPressed('3'))
        {
            g_renderer.CyclePostProcessMode();
            printf("后处理: %s\n", g_renderer.GetPostProcessModeName());
        }

        // 数字键 0 独立开关 FXAA。
        if (g_input.WasKeyPressed('0'))
        {
            g_renderer.ToggleFXAA();
            printf("FXAA: %s\n", g_renderer.IsFXAAEnabled() ? "开" : "关");
        }

        if (g_window.ConsumeResizeFlag())
            g_renderer.OnResize(g_window.GetWidth(), g_window.GetHeight());

        // 逻辑刷新允许修改场景状态；渲染刷新只读场景。
        g_scene.Tick(dt, g_input);
    }
}

// ============================================================================
// 入口：COM → Demo 初始化 → 主循环 → 退出
// ============================================================================
int main()
{
    SetConsoleOutputCP(CP_UTF8);   // 让 printf 的中文正常显示（配合编译选项 /utf-8）

    if (!g_com.IsUsable())
    {
        printf("[Main] COM 初始化失败，WIC 贴图加载不可用。\n");
        return -1;
    }

    if (!InitializeDemo())
        return -1;

    Timer timer;
    timer.Reset();   // 初始化耗时不计入第一帧

    // 主循环：每帧 = 泵消息 → 逻辑刷新 → 渲染刷新
    while (g_window.PumpMessages())
    {
        float dt = timer.Tick();
        if (dt > 0.1f) dt = 0.1f;   // 拖拽窗口/断点后防动画跳变

        Update(dt);

        // 渲染刷新（≈ 渲染线程侧）：只读场景，收集可渲染物 → Draw → Present
        g_renderer.Render(g_scene);
    }

    printf("正常退出。\n");
    return 0;
}
