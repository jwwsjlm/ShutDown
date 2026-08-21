# 构建说明

## 依赖

- Windows 7 SP1 / Windows 10 / Windows 11
- Visual Studio 2022 Desktop C++ 或 MinGW
- CMake 3.16+
- 使用 `CMakePresets.json` 时建议 CMake 3.21+

Win32++ 通过 Git submodule 挂在 `third_party/win32xx`。

## 克隆

```powershell
git clone --recursive https://github.com/jwwsjlm/ShutDown.git
```

如果已经普通克隆：

```powershell
git submodule update --init --recursive
```

## 使用 CMake Presets

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-release
ctest --preset windows-x64-release
```

32 位：

```powershell
cmake --preset windows-x86
cmake --build --preset windows-x86-release
ctest --preset windows-x86-release
```

本地 Velopack SDK 验证：

```powershell
cmake --preset windows-x64-velopack
cmake --build --preset windows-x64-velopack-release
ctest --preset windows-x64-velopack-release
```

## 使用脚本

```powershell
PowerShell -ExecutionPolicy Bypass -File scripts/build.ps1 -Architecture x64 -Test
PowerShell -ExecutionPolicy Bypass -File scripts/build.ps1 -Architecture x86 -Test
```

## Windows 7

主程序目标仍是 Windows 7 SP1 起。Windows 7 检查更新依赖系统 TLS/SHA-2 更新；系统过旧时，关机功能仍可用，但 HTTPS 更新检查可能失败。
