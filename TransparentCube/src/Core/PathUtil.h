#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <filesystem>
#include <string>

// ============================================================================
// 运行期资源路径解析
//
// 为什么不能直接用 "assets/xxx.png"：
//   相对路径是相对“当前工作目录”解析的，不是相对 exe 所在目录。
//   - VS 里 F5：工作目录 = $(ProjectDir)，能直接找到项目里的 assets；
//   - 双击 exe：工作目录 = exe 所在目录，此时应找 exe 旁边的 assets 副本。
//
// 查找顺序：
//   1. 当前工作目录（开发时优先，改完资源重启即可生效）
//   2. exe 所在目录（构建时已由 CopyRuntimeContent 把 assets/shaders 复制过去）
//   3. 都没找到：返回原相对路径，让调用方打印出有意义的错误
// ============================================================================
namespace PathUtil
{
    inline std::wstring ResolveContentPath(const wchar_t* relative)
    {
        namespace fs = std::filesystem;

        if (fs::exists(relative))
            return relative;

        wchar_t exePath[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
        {
            const fs::path candidate = fs::path(exePath).parent_path() / relative;
            if (fs::exists(candidate))
                return candidate.wstring();
        }

        return relative;
    }
}
