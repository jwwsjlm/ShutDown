# ShutDown

一个面向 Windows 的 Qt 5/6 + C++17 定时关机工具。

当前 CMake 会优先使用 Qt 6，找不到 Qt 6 时自动回退到 Qt 5.15。Qt 5.15.2
在新 Qt Online Installer 中可能不再显示；如果安装器只有 Qt 6.x，可以直接安装
当前可用的 Qt 6.x，项目无需修改源码即可构建。Qt 6 不再支持 Windows 7，Windows 7 需要单独
准备 Qt 5.15 工具链。

## 功能

- 指定日期/时间关机
- 小时、分钟、秒倒计时
- 暂停、继续、取消、立即关机
- 系统托盘与最小化到托盘
- QSettings 持久化，重启时确认恢复
- `ExitWindowsEx` → `InitiateSystemShutdownEx` → `shutdown.exe` 三级执行
- 可选 `schtasks.exe` 一次性系统任务兜底
- `QLockFile` 单实例保护
- GitHub Release 多源并行更新检查、代理回退、下载进度、SHA-256 校验和自动重启安装

## 构建

需要 Qt 5.15.x、CMake 3.16+ 和支持 C++17 的 MSVC/MinGW：

```powershell
cmake -S D:\code\ShutDown -B D:\code\ShutDown\build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=H:\Dev\Qt\6.5.3\mingw_64
cmake --build D:\code\ShutDown\build --config Release
ctest --test-dir D:\code\ShutDown\build -C Release --output-on-failure
```

也可以在 Qt 安装到 `H:\Dev\Qt` 或 `C:\Qt` 后直接运行自动探测脚本：

```powershell
PowerShell -ExecutionPolicy Bypass -File D:\code\ShutDown\scripts\build.ps1 -Test
```

## GitHub Actions

`.github/workflows/windows-ci.yml` 会在 Push、Pull Request 和 `v*` 标签时自动执行：

- 安装 Qt 6.5.x MSVC 2019 64-bit
- 使用 Visual Studio 2022 编译
- 运行 CTest
- 使用 `windeployqt` 打包并上传 Windows x64 artifact
- 对 `v*` 标签自动创建 GitHub Release 并上传 `ShutDown-windows-x64.zip`

发布新版本时，请同步修改 `D:\code\ShutDown\CMakeLists.txt` 中的 `project(... VERSION ...)`，
然后创建同名标签，例如版本 `1.0.1` 使用标签 `v1.0.1`。程序会将内置版本号与 GitHub Release
的 `tag_name` 比较，并在确认 SHA-256（如果 GitHub 提供 digest）后下载和安装。

更新检查会并行尝试 GitHub API、jsDelivr 静态源和若干 GitHub 代理；代理仅作为网络回退，
最终版本号以可验证的 GitHub Release 响应为准。下载失败会自动切换下一个地址。

工作流当前固定使用已验证的 action 版本：

- `actions/checkout@v7.0.1`
- `jurplel/install-qt-action@v4.3.1`
- `actions/upload-artifact@v7.0.1`

部署时执行 Qt 对应版本的 `windeployqt.exe ShutDown.exe`。系统任务和关机 API 可能受本机组策略、UAC、域策略或未保存应用阻止；“强制关闭”应谨慎启用。
