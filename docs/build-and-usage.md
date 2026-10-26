# 构建与使用

## 构建方法

### 环境

- Windows 10/11。
- Visual Studio 2022 或 Build Tools，安装“使用 C++ 的桌面开发”及 Windows SDK。项目源码为 C17，使用 MSVC 的 C 编译器。
- Git，用于获取源码。依赖源码已固定版本并包含在仓库中，不需要额外下载包。

### 构建 x64

在 PowerShell 中执行：

```powershell
git clone https://github.com/liuminxin45/star-library.git
cd star-library
.\build.cmd -Architecture x64 -Test
```

产物为 `build\x64\StarLibrary.exe`，同时更新项目根目录的 `StarLibrary.exe`；使用 `-NoInstall` 可只构建、不替换根目录程序。仅需分发这一个 exe。构建脚本会检查 400,000 字节体积门槛及 DLL 依赖，拒绝额外的可分发运行库依赖。`-Test` 另生成包含完整 UI 自检的 `StarLibrary-ui-test.exe`，仅供开发测试。

可选构建 x86：

```powershell
.\build.cmd -Architecture x86 -Test -NoInstall
```

默认不指定架构时构建 x64，并在成功后更新项目根目录 exe；使用 `-NoInstall` 可保留正在使用的根目录程序。

### 原生 UI 检查

```powershell
$app = Start-Process .\build\x64\StarLibrary-ui-test.exe `
  -ArgumentList '--ui-test', 'ui-results.txt' -WindowStyle Hidden -PassThru -Wait
Get-Content .\ui-results.txt
if ($app.ExitCode -ne 0) { throw 'UI checks failed' }
```

核心与 UI 自动测试使用隔离数据和模拟接口，不调用真实模型或修改 GitHub Stars。正式 `StarLibrary.exe --ui-test` 仍可执行轻量界面自检；两种构建使用相同的应用界面代码，正式版仅省去完整测试用例与断言。`scripts\verify-ui.ps1` 会优先寻找同目录的完整测试程序，并使用正式程序生成演示截图。

### GitHub Actions 发布

推送到 `main` 或创建 Pull Request 时，GitHub Actions 自动构建 x64、运行核心与原生 UI 检查，并提供构建产物。

推送版本标签（例如 `0.0.1`）后，同一工作流在 GitHub 上重新构建和测试，检查 x64 架构、exe 版本、400,000 字节上限及系统 DLL 依赖，通过后自动创建 Release，附带 **StarLibrary.exe** 和 **SHA256SUMS.txt**。版本标签须与源码及资源版本一致。

## 开始使用

星库的“AI 整理 ▾”可选择增量整理或重新整理全部。增量跳过未变化的有效概括，并继续上次未完成的项目；全量需确认后执行。整理范围为全部已 Star 项目，不随搜索和语言筛选变化。新结果保存成功才替换旧概括；取消或退出后，重新启动并选择增量整理即可继续，启动不会自动调用模型。

没有 About 和 Topics 的项目自动读取 README 兜底，确认没有可用内容后才标记资料不足，不调用模型、不算失败。读取失败、权限不足、限流等保留待重试。私有仓库需要 Token 的 Contents 读取权限，不能匿名回退。

README 不抓图片或链接，最多读取 64 KiB 前缀，提取不超过 2,400 UTF-8 字节的纯文本节选，纳入现有 token 预算组批。缓存逐项保存；重启重试复用已保存节选。仓库有新推送时增量复查 README，节选未变不重新调用模型；全量和卡片重新概括则刷新读取。旧的“资料不足”项目会在下一次增量整理中自动检查 README。

1. 下载 exe，双击打开。
2. 在设置中填写 GitHub Token，点击“连接 GitHub”；“获取 Token”可直接前往 GitHub。需要 Starring 读写、Metadata 读取权限，应用不通过实际 Star 操作探测写权限。
3. 回到星库点击“同步”。已同步项目可离线浏览；所有 GitHub API 操作要求 Token。
4. 配置模型接口、模型名称和 API Key。本机无鉴权模型可以留空 Key。
5. 按需执行“AI 整理”“更新画像”与“换一批”。未变化的分类可复用；资料不足不会编造内容，失败或取消后可继续处理剩余项目。

## 数据与快捷键

默认数据目录为 `%USERPROFILE%\.star-library`，包含 `library.json`、备份、加密凭据与头像缓存。固定用户目录避免从打包应用和资源管理器启动时读取到不同的 AppData。

首次升级会检查旧 `%LOCALAPPDATA%\StarLibrary` 和应用包缓存中的旧星库，同账号优先恢复非空、较新版本，保留原文件并复制加密凭据；不同账号不自动合并。现有新目录不会被迁移覆盖。主文件损坏或缺失时，只从通过完整校验的备份恢复，并保留损坏原件；读取或权限错误不会作为空库继续运行。

数据和缓存不计入 exe 大小；头像磁盘缓存上限 20 MiB。断开 GitHub 只清除 GitHub 凭据，保留收藏与模型配置。

```powershell
# 指定独立数据目录
.\StarLibrary.exe --data-dir 'D:\MyStarLibrary'
```

| 快捷键 | 操作 |
| --- | --- |
| Ctrl+1 / 2 / 3 / 4 | 星库 / 画像 / 发现 / 设置 |
| Ctrl+F 或 Ctrl+K | 搜索 |
| Tab / Shift+Tab | 在卡片、操作与输入框间切换 |
| Enter / Space | 执行当前操作 |
| ↑ / ↓、PageUp / PageDown | 滚动 |
| F5 | 同步；发现页为换一批 |

## 许可与致谢

项目采用 [MIT License](../LICENSE)。JSON 解析使用 [yyjson](https://github.com/ibireme/yyjson)，保留其 [MIT 许可证](../vendor/yyjson/LICENSE) 和固定版本记录。界面参考 [ChatGPT-Desktop-UI](https://github.com/liuminxin45/ChatGPT-Desktop-UI) 的简约风格，使用 Windows 原生绘制实现。
