# 投稿准备

已按小众软件的[内容提交规则](https://meta.appinn.net/t/topic/43728)和[发现频道模板](https://meta.appinn.net/t/topic/77583)整理。下方为待发布草稿，尚未代发。使用开发者身份，注明模型服务可能收费、GitHub Token 前提，保留官方直链。发帖时可将真实运行图直接上传到论坛。

标题建议：**〖开发者自荐〗Star Library：300 多 KB 的 GitHub Stars 管理器，带 AI 整理与兴趣画像**

---

## 软件名称

Star Library

## 一句简介

300 多 KB 的单文件 Windows 程序，管理 GitHub Stars，用 AI 看懂项目、回看兴趣，并发现下一件想试的东西。

## 应用平台

Windows 10 / 11，x64，中文界面。

## 推荐类型

〖开发者自荐〗

## 是否收费

软件免费开源，MIT 许可。AI 需要自行配置模型接口和 API Key，费用取决于所选服务；软件不出售模型额度。

## 应用简介

我在 GitHub 上 Star 了不少项目，过一阵再看，经常只记得“当时觉得有用”，却想不起具体能做什么。于是做了这个小工具，把收藏放到一个更容易翻看的地方。

- **管理 Stars**：卡片浏览、搜索和语言筛选，直接 Star / Unstar，点击项目打开 GitHub。
- **一句中文看懂项目**：调用 AI 整理项目用途，未整理的保留原始 About。按资料长度和 token 预算合批，一次整理多个项目；成功一个保存一个，只补试未完成项目。支持 1 / 5 / 10 并发。
- **从收藏里认识兴趣**：用一段面向好奇心、学习与日常选择的叙事，回看六种兴趣倾向；点击比例尺可查看收藏依据。它不是能力或人格测评。
- **发现项目**：结合六维画像实际搜索 GitHub，从至少 100 Stars 的候选中精选最多六项；Stars 越多排序权重越高，“不感兴趣”会排除该仓库并降低相似主题的后续权重。无需模型自带联网能力。

一个 300 多 KB 的 exe，免安装，不需要 Node、Python、WebView2 或后台服务。收藏、概括和画像保存在本地，支持 JSON 导入导出，断网也能浏览已有内容。

设置页会自动检查新版本，点击提示即可前往 GitHub Release。也提供了“Star me”，已连接 GitHub 时可直接收藏本项目并打开项目页。

使用前需要 GitHub 账号与有 Starring 读写权限的 Token。AI 不内置模型，支持兼容 Chat Completions 的接口；执行 AI 分析时，相关仓库元数据（包括私有仓库）会发送给你配置的模型服务。

![我的真实星库](https://raw.githubusercontent.com/liuminxin45/star-library/main/docs/images/library.png)

![兴趣画像](https://raw.githubusercontent.com/liuminxin45/star-library/main/docs/images/profile.png)

![项目发现](https://raw.githubusercontent.com/liuminxin45/star-library/main/docs/images/discovery.png)

目前是早期版本，欢迎反馈具体使用问题。

## 官方网站

- [介绍页](https://www.liuminxin.cn/works/star-library)
- [GitHub 源码](https://github.com/liuminxin45/star-library)
- [下载与版本说明](https://github.com/liuminxin45/star-library/releases/latest)
