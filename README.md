# ShutDown

轻量级 Windows 定时关机工具，使用 **C++17 + Win32++** 构建，不依赖额外 GUI 框架 DLL 或 OpenSSL DLL。

## 下载

程序内可以手动检查新版本，确认后会打开 GitHub 下载页面。发布包只提供便携版：

- [Windows x64 便携版](https://github.com/jwwsjlm/ShutDown/releases/latest/download/ShutDown-win-x64-Portable.zip)
- [Windows x86 便携版](https://github.com/jwwsjlm/ShutDown/releases/latest/download/ShutDown-win-x86-Portable.zip)

完整版本说明见 [Latest Release](https://github.com/jwwsjlm/ShutDown/releases/latest)。

## 功能

- 指定日期/时间关机
- 小时、分钟、秒倒计时
- 暂停、继续、取消、立即关机
- 最小化隐藏到系统托盘，托盘菜单显示倒计时
- `ExitWindowsEx` 与 `InitiateSystemShutdownEx` 双路径关机
- 可选 `schtasks.exe` 一次性系统任务兜底
- 手动检查 GitHub Release 更新

## 快速构建

```powershell
git clone --recursive https://github.com/jwwsjlm/ShutDown.git
cd ShutDown
cmake --preset windows-x64
cmake --build --preset windows-x64-release
ctest --preset windows-x64-release
```

如果已经普通克隆：

```powershell
git submodule update --init --recursive
```

更多构建方式见 [`docs/BUILD.md`](docs/BUILD.md)。

## 发布

发布流程见 [`docs/RELEASE.md`](docs/RELEASE.md)。

## 第三方依赖

- [`third_party/win32xx`](third_party/win32xx)：Win32++，以 Git submodule 形式引用。

## 许可

主程序代码按仓库现有许可发布；`third_party/win32xx/license.txt` 随发布包提供 Win32++ MIT 许可文本。
