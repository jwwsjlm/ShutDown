# 发布说明

## 版本号

`CMakeLists.txt` 里的 `SHUTDOWN_BASE_VERSION` 是本地默认版本。GitHub Actions tag 构建时会自动从 tag 传入 `SHUTDOWN_VERSION_OVERRIDE`，并同步到程序标题、`FILEVERSION` 和 `PRODUCTVERSION`。

## Release notes

每个版本需要创建：

```text
release-notes/vX.Y.Z.md
```

可以从 `release-notes/TEMPLATE.md` 复制。模板里的 `vX.Y.Z` 和 `<version>` 不需要手动替换，GitHub Actions 发布时会自动替换成当前 tag 和纯数字版本号。

## 发布命令

```powershell
git tag -a vX.Y.Z -m "vX.Y.Z"
git push origin vX.Y.Z
```

workflow 只在 `v*` tag 上发布，普通 push 到 `main` 不触发构建。

## 发布资产

Release 只上传用户直接使用的便携包：

- `ShutDown-win-x64-Portable.zip`
- `ShutDown-win-x86-Portable.zip`
