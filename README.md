# Star Library

**让 GitHub Stars 不再吃灰。**

把收藏过的项目整理成中文项目库：看懂项目用途、搜索已有收藏，再根据兴趣发现值得尝试的新工具。

**约 167 KB · Windows 原生 · 单文件免安装 · 免费开源**

[下载 Windows x64](https://github.com/liuminxin45/star-library/releases/latest/download/StarLibrary.exe) · [版本与校验值](https://github.com/liuminxin45/star-library/releases/latest) · [使用说明](docs/build-and-usage.md) · [English](README.en.md)

适用于 Windows 10/11，无需额外运行环境。GitHub 同步需要你自己的 Token；AI 功能需要自行配置兼容 Chat Completions 的模型服务，可能产生服务费用。

![Star Library 星库：用中文概括浏览收藏的 GitHub 项目](docs/images/library.png)

## 能帮你做什么

| 你遇到的问题 | Star Library 的做法 |
| --- | --- |
| 收藏太多，忘了项目是做什么的 | AI 生成中文概括，在卡片中直接查看；没有概括时显示原始 About |
| 想找回以前收藏的工具 | 搜索收藏，按语言筛选、排序，点击卡片打开 GitHub |
| 想整理积累的收藏，又怕中途失败 | 增量补齐未完成项目，成功逐项保存；取消或重启后可继续 |
| 想知道自己最近关注什么 | 根据收藏生成兴趣画像，了解关注的主题与倾向 |
| 想发现相关的新工具 | 结合收藏主题、兴趣画像与“不感兴趣”反馈，精选新的实用项目 |

同步后的项目可以离线浏览，支持 JSON 导入导出和自动保存。AI 功能按需运行，启动应用不会自动调用模型。

### 从收藏中看见兴趣

画像用于回顾收藏偏好，不把 Star 等同于专业能力。

![兴趣画像：根据收藏生成兴趣叙事与六种兴趣倾向](docs/images/profile.png)

### 发现下一件想试的工具

优先寻找至少 100 Stars 的实用项目；你可以直接 Star，或标记“不感兴趣”调整后续推荐。Stars 是筛选信号，不代表质量或稳定性保证。

![项目发现：查看推荐项目的中文概括，直接 Star 或反馈不感兴趣](docs/images/discovery.png)

## 开始使用

1. **下载并打开**：[下载 StarLibrary.exe](https://github.com/liuminxin45/star-library/releases/latest/download/StarLibrary.exe)，双击运行，无需安装。
2. **连接 GitHub**：在“设置”中点击“获取 Token”，创建自己的 Token，填入后点击“连接 GitHub”。回到“星库”点击“同步”。
3. **配置 AI（可选）**：在“设置”中填写模型接口地址、模型名称和 API Key，点击“测试连接”确认配置。模型名称以服务商提供的 ID 为准。
4. **整理与探索**：在“AI 整理 ▾”中选择增量整理，再按需更新画像、进入发现页换一批。全量重做需要确认；整理范围为全部已 Star 项目，与当前搜索、筛选无关。

**Token 权限**：使用 fine-grained Token 时，开启账号权限 **Starring: Read and write**，仓库权限保留 **Metadata: Read**。如需读取私有项目 README，再为对应仓库授予 **Contents: Read**；无需 Contents 写权限。权限配置以 [GitHub 官方说明](https://docs.github.com/en/rest/activity/starring#star-a-repository-for-the-authenticated-user) 为准。

**模型地址**：支持服务根地址、版本前缀或完整 Chat Completions 地址，例如 `https://api.example.com`、`https://api.example.com/v1`、`https://api.example.com/v1/chat/completions`。应用会自动补全路径；示例地址需要换成你自己的服务地址。远程服务必须使用 HTTPS，本机服务可使用 HTTP。

遇到问题可查看 [完整使用说明与快捷键](docs/build-and-usage.md)，或 [提交 Issue](https://github.com/liuminxin45/star-library/issues)。

## 数据、隐私与费用

- **本地保存**：默认数据目录为 `%USERPROFILE%\.star-library`，包含收藏、概括、画像、备份和缓存。Token 与模型 Key 使用 Windows 加密保护，保存在独立凭据文件中，不写入星库 JSON。
- **GitHub 访问**：应用使用你验证过的 Token 同步收藏、读取项目资料，并在你操作时执行 Star / Unstar。设置页可检查官方 Release，更新由你手动下载。
- **AI 数据范围**：执行 AI 功能时，相关公开及私有项目元数据会发送给你配置的模型服务。概括缺少 About 和 Topics 的项目时，还可能发送限长 README 纯文本节选。**本地保存不等于 AI 离线处理**，请按所选服务的数据政策决定是否使用。
- **费用**：应用免费开源，不提供内置模型额度。模型调用由所选服务计费；不配置 AI 也可同步、搜索和浏览收藏。

## 常见问题

**需要一次整理完所有收藏吗？**

不需要。增量整理跳过未变化的有效概括，继续处理未完成项目。成功结果逐项保存；失败、取消或重启后可以续接，原有概括保留到新结果保存成功。

**项目没有介绍怎么办？**

缺少 About 和 Topics 时，应用尝试读取 README，提取最多 2,400 UTF-8 字节的纯文本节选。确认没有可用内容后才标记“资料不足”；权限、网络或限流问题保留待重试。

**支持 macOS / Linux 吗？**

目前只支持 Windows 10/11，界面及 AI 概括以中文为主。

## 构建

使用 C17、Win32、Direct2D 与 DirectWrite 实现。安装 VS2022 C++ 桌面开发工具与 Windows SDK，然后执行：

```powershell
.\build.cmd -Architecture x64 -Test
```

正式分发仅需 `StarLibrary.exe`，当前 x64 版经 UPX 压缩约 167 KB，仅依赖 Windows 系统 DLL。构建验证解压还原一致性并保留 400,000 字节上限。[完整构建与测试说明](docs/build-and-usage.md)。

## 反馈与支持

欢迎通过 [Issues](https://github.com/liuminxin45/star-library/issues) 提交问题与建议。报告问题时，请附上应用版本、Windows 版本和复现步骤；不要上传 Token、API Key 或含私有项目资料的完整数据文件。

如果它帮你找回了收藏中的好项目，欢迎点个 **Star**，让更多人发现它。

## 许可与致谢

采用 [MIT License](LICENSE)。JSON 使用 [yyjson](https://github.com/ibireme/yyjson)；界面参考 [ChatGPT-Desktop-UI](https://github.com/liuminxin45/ChatGPT-Desktop-UI)。[项目介绍页](https://www.liuminxin.cn/works/star-library)。
