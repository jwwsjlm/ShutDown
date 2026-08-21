# ShutDown

轻量级 Windows 定时关机工具，使用 **C++17 + Win32++** 构建，不依赖 Qt、Qt DLL 或 OpenSSL DLL。

## 功能

- 指定日期/时间关机
- 小时、分钟、秒倒计时
- 暂停、继续、取消、立即关机
- 最小化隐藏到系统托盘，双击托盘图标恢复
- 关闭按钮直接退出；存在活动任务时可选择保留或取消
- `ExitWindowsEx` → `InitiateSystemShutdownEx` → `shutdown.exe` 三级执行
- 可选 `schtasks.exe` 一次性系统任务兜底
- 使用 `%APPDATA%\ShutDown\settings.ini` 保存任务状态
- GitHub Release 多源更新检查、架构匹配、下载进度、SHA-256 校验和自动重启安装

## 构建

依赖：

- Windows 10/11 或 Windows Server
- Visual Studio 2022（含 Desktop C++）
- CMake 3.16+

Win32++ 已 vendor 到 `third_party/win32xx`，CMake 会直接使用，无需额外安装 GUI 框架。

```powershell
cmake -S D:\code\ShutDown -B D:\code\ShutDown\build -G "Visual Studio 17 2022" -A x64
cmake --build D:\code\ShutDown\build --config Release
ctest --test-dir D:\code\ShutDown\build -C Release --output-on-failure
```

构建 32 位版本时，将架构改为 `Win32`：

```powershell
cmake -S D:\code\ShutDown -B D:\code\ShutDown\build-x86 -G "Visual Studio 17 2022" -A Win32
cmake --build D:\code\ShutDown\build-x86 --config Release
```

也可以使用脚本：

```powershell
PowerShell -ExecutionPolicy Bypass -File D:\code\ShutDown\scripts\build.ps1 -Architecture x64 -Test
PowerShell -ExecutionPolicy Bypass -File D:\code\ShutDown\scripts\build.ps1 -Architecture x86 -Test
```

## 发布包

发布采用 ZIP，而不是强行追求单文件。每个 ZIP 只包含：

```text
ShutDown.exe
Win32xx-MIT.txt
```

GitHub Release 提供：

- `ShutDown-windows-x64.zip`
- `ShutDown-windows-x86.zip`

更新器依据当前进程位数选择对应 ZIP，不会让 32 位程序下载 x64 包。程序运行时不需要 Qt DLL、平台插件或 OpenSSL DLL。

## GitHub Actions

`.github/workflows/windows-ci.yml` 会分别构建 x64 和 x86，执行 CTest，并在推送 `v*` 标签时上传两个 ZIP 到 Release。

每个版本的 Release 必须同时提供更新说明，文件放在 `release-notes/vX.Y.Z.md`；可复制
`release-notes/TEMPLATE.md` 创建新版本说明。工作流会把该文件直接作为 GitHub Release 正文，
即使忘记创建文件也会生成一个兜底说明，不会发布空白 Release。

发布新版本时同步修改 `CMakeLists.txt` 中的项目版本并创建标签，例如：

```powershell
git tag v2.0.0
git push origin v2.0.0
```

## 许可

主程序代码按仓库现有许可发布；`third_party/win32xx/license.txt` 随发布包提供 Win32++ MIT 许可文本。
