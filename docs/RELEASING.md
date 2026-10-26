# GitHub 发布清单

## 创建仓库

1. 在 GitHub 创建新的公开仓库，例如 `BrightnessSync-Windows`。本目录已包含 `README.md`、`.gitignore` 和 `LICENSE`，创建空仓库即可。
2. 把本目录的源码与文档上传到仓库根目录。`dist/` 不需要提交。
3. 检查 GitHub 首页能正确显示中英文 README、MIT 许可证以及 `docs/` 中的说明。

如果使用 Git 命令，在**本目录**执行，并将占位地址替换为你创建的仓库地址：

```powershell
git init
git add .
git commit -m "Initial open source release"
git branch -M main
git remote add origin https://github.com/YOUR-ACCOUNT/YOUR-REPOSITORY.git
git push -u origin main
```

## 发布二进制

1. 按[构建说明](BUILD.md)运行 `build.ps1`。
2. 按[测试方案](TESTING.md)在目标 Windows 版本和显示器组合上验证。ARM64 版本目前尚未经过真机验证，正式标注支持前需要补测。
3. 对发布文件签名；记录每个文件的 SHA-256。
4. 新建 GitHub Release，把 `dist/` 中的三个 EXE 作为附件上传。普通用户优先下载 `BrightnessSync-native-universal.exe`。
5. Release 说明应写明验证过的系统架构、显示器组合、已知限制和升级步骤。

当前代码只包含用户态程序。显示器 Extension Driver、Software Component、Windows Update 分发、特定显示器型号筛选等产品化工作需要另建适配与验证流程。

