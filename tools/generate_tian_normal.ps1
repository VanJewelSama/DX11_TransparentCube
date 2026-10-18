#requires -Version 5.1
<#
  开发期资产工具：生成“田”字面板的法线贴图。

  说明：
  - 这个脚本不属于 DemoMain 的运行时流程，只在手动生成资产时运行；
  - 输出是 assets/normal_tian.png，作为普通纹理资产提交到仓库；
  - 高度场设计：
      * 四个象限是平坦的高台；
      * 中间横竖两条“田”字线是凹槽，凹槽两侧有平滑倒角；
      * 整个面板外边缘有一圈向下的倒角；
  - 由高度场用中心差分计算切线空间法线，编码到 RGB：
      * 平坦区域约为 (128,128,255)；
      * R = +U / G = +V / B = +N；
      * 使用 B = dP/dv 的约定，法线为 (-dH/du, -dH/dv, 1)。

  用法（在仓库根目录或 tools 目录运行均可）：
    powershell -ExecutionPolicy Bypass -File tools\generate_tian_normal.ps1
    powershell -ExecutionPolicy Bypass -File tools\generate_tian_normal.ps1 -Size 1024
#>
param(
    [string]$OutputPath = (Join-Path $PSScriptRoot "..\TransparentCube\assets\normal_tian.png"),
    [int]$Size = 512
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$source = @"
using System;
using System.Drawing;
using System.Drawing.Imaging;

public static class TianNormalMapGenerator
{
    private static float SmoothStep(float edge0, float edge1, float x)
    {
        if (edge0 == edge1)
            return x < edge0 ? 0.0f : 1.0f;

        float t = Math.Max(0.0f, Math.Min(1.0f, (x - edge0) / (edge1 - edge0)));
        return t * t * (3.0f - 2.0f * t);
    }

    private static float Lerp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    public static void Generate(string path, int size)
    {
        if (size < 16)
            throw new ArgumentOutOfRangeException("size");

        float[] height = new float[size * size];

        float center = (size - 1) * 0.5f;
        float halfWidth   = size * 0.020f;   // 凹槽底部半宽
        float grooveBevel = size * 0.025f;   // 凹槽两侧倒角宽度
        float outerBevel  = size * 0.055f;   // 外边缘倒角宽度
        float edgeHeight  = 0.60f;           // 外边缘高度（内侧高台 = 1.0）
        float grooveDepth = 0.30f;           // 凹槽深度

        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                float fx = x;
                float fy = y;

                // 1. 外边缘倒角：从边缘的 edgeHeight 平滑升到内部 1.0
                float edgeDist = Math.Min(
                    Math.Min(fx, (size - 1) - fx),
                    Math.Min(fy, (size - 1) - fy));
                float edgeT = SmoothStep(0.0f, outerBevel, edgeDist);
                float h = Lerp(edgeHeight, 1.0f, edgeT);

                // 2. “田”字中间横竖两条凹槽：min(|x-cx|, |y-cy|) 形成十字形
                float dx = Math.Abs(fx - center);
                float dy = Math.Abs(fy - center);
                float grooveDist = Math.Min(dx, dy);
                float grooveT = 1.0f - SmoothStep(halfWidth, halfWidth + grooveBevel, grooveDist);
                h -= grooveDepth * grooveT;

                height[y * size + x] = h;
            }
        }

        // 3. 高度场 → 切线空间法线（中心差分）
        const float normalStrength = 9.0f;
        Bitmap bitmap = new Bitmap(size, size, PixelFormat.Format32bppArgb);

        for (int y = 0; y < size; ++y)
        {
            int ym = Math.Max(0, y - 1);
            int yp = Math.Min(size - 1, y + 1);

            for (int x = 0; x < size; ++x)
            {
                int xm = Math.Max(0, x - 1);
                int xp = Math.Min(size - 1, x + 1);

                float hL = height[y * size + xm];
                float hR = height[y * size + xp];
                float hUp = height[ym * size + x];     // 纹理 v 较小的一侧
                float hDown = height[yp * size + x];   // 纹理 v 较大的一侧

                // 纹理 v 向下，副切线 B = dP/dv：
                //   法线 = normalize(-dH/du, -dH/dv, 1)
                // 其中 dH/dv = hDown - hUp，所以 ny = -(hDown - hUp) = hUp - hDown。
                float nx = -(hR - hL) * normalStrength;
                float ny =  (hUp - hDown) * normalStrength;
                float nz = 1.0f;

                float invLen = 1.0f / (float)Math.Sqrt(nx * nx + ny * ny + nz * nz);
                nx *= invLen;
                ny *= invLen;
                nz *= invLen;

                int r = (int)Math.Round((nx * 0.5f + 0.5f) * 255.0f);
                int g = (int)Math.Round((ny * 0.5f + 0.5f) * 255.0f);
                int b = (int)Math.Round((nz * 0.5f + 0.5f) * 255.0f);

                bitmap.SetPixel(x, y, Color.FromArgb(255, r, g, b));
            }
        }

        bitmap.Save(path, ImageFormat.Png);
        bitmap.Dispose();
    }
}
"@

Add-Type -TypeDefinition $source -ReferencedAssemblies System.Drawing

$fullPath = [System.IO.Path]::GetFullPath($OutputPath)
$dir = [System.IO.Path]::GetDirectoryName($fullPath)
if (-not (Test-Path $dir))
{
    New-Item -ItemType Directory -Path $dir | Out-Null
}

[TianNormalMapGenerator]::Generate($fullPath, $Size)
Write-Output "Generated: $fullPath ($Size x $Size)"
