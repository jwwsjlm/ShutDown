# ShutDown

轻量级 Windows 定时关机工具，使用 **C++17 + Win32++** 构建，不依赖额外 GUI 框架 DLL 或 OpenSSL DLL。

## 功能

- 指定日期/时间关机
- 小时、分钟、秒倒计时
- 暂停、继续、取消、立即关机
- 最小化隐藏到系统托盘，双击托盘图标恢复
- 关闭按钮直接退出；存在活动任务时可选择保留或取消
- `ExitWindowsEx` → `InitiateSystemShutdownEx` → `shutdown.exe` 三级执行
- 可选 `schtasks.exe` 一次性系统任务兜底
- 使用 `%APPDATA%\ShutDown\settings.ini` 保存任务状态
- Velopack 安装版自动更新、下载进度和重启安装

## 构建

支持和依赖：

- 运行目标：Windows 7 SP1、Windows 10、Windows 11
- Windows 7 检查更新依赖系统可用的 TLS/SHA-2 更新；如果系统过旧，关机功能仍可用，但 HTTPS 更新检查可能失败
- Visual Studio 2022（含 Desktop C++）
- CMake 3.16+

Win32++ 通过 Git submodule 挂在 `third_party/win32xx`，CMake 会直接使用，无需额外安装 GUI 框架。

首次克隆或普通 `git clone` 后，需要初始化子模块：

```powershell
git submodule update --init --recursive
```

也可以直接递归克隆：

```powershell
git clone --recursive https://github.com/jwwsjlm/ShutDown.git
```

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

GitHub Release 使用 Velopack 发布安装版和便携包：

- `ShutDown-win-x64-Setup.exe`
- `ShutDown-win-x86-Setup.exe`
- `ShutDown-win-x64-Portable.zip`
- `ShutDown-win-x86-Portable.zip`
- `ShutDown-<version>-win-x64-full.nupkg`
- `ShutDown-<version>-win-x86-full.nupkg`
- `releases.win-x64.json` / `releases.win-x86.json`

推荐使用 `Setup.exe` 安装版。安装版使用 Velopack 管理后续更新，会依据安装时的架构和 channel
读取对应的 release feed，并由 Velopack 在程序退出后替换文件和重启。

不再保留旧版自写更新器迁移资产；便携版或未通过 Velopack 安装的构建不会执行自动更新。
程序运行时不需要额外 GUI 框架 DLL、平台插件或 OpenSSL DLL。

## GitHub Actions

`.github/workflows/windows-ci.yml` 会分别构建 x64 和 x86，执行 CTest，并在推送 `v*` 标签时生成 Velopack 安装器、便携包、nupkg 和 release feed。
workflow 只负责编译、测试、打包和发布当前 tag，不会自动删除旧 Release、旧 tag 或历史版本。
普通 push 到 `main` 不触发 CI；避免每次 release commit 产生额外的 `chore: release ...` run。

每个版本的 Release 必须同时提供更新说明，文件放在 `release-notes/vX.Y.Z.md`；可复制
`release-notes/TEMPLATE.md` 创建新版本说明。工作流会把该文件直接作为 GitHub Release 正文；
如果缺少该文件，发布流程会失败，避免发布空泛说明。

发布新版本时先创建对应的 `release-notes/vX.Y.Z.md`，再创建标签。GitHub Actions 的 tag
构建会显式传入 `SHUTDOWN_VERSION_OVERRIDE`，并自动把程序版本、`FILEVERSION` 和
`PRODUCTVERSION` 设置为当前标签版本，例如 `v2.0.8` 会生成 `2.0.8.0`：

```powershell
git tag v2.0.8
git push origin v2.0.8
```

## 许可

主程序代码按仓库现有许可发布；`third_party/win32xx/license.txt` 随发布包提供 Win32++ MIT 许可文本。
