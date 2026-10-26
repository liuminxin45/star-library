# Star Library

**Put your GitHub Stars back to use.**

Browse your saved repositories with AI-generated Chinese summaries, explore your interests, and discover useful tools based on what you star.

**Under 400 KB · Native Windows · Portable single executable · Free and open source**

[Download for Windows x64](https://github.com/liuminxin45/star-library/releases/latest/download/StarLibrary.exe) · [Releases & checksums](https://github.com/liuminxin45/star-library/releases/latest) · [简体中文](README.md)

Runs on Windows 10/11 with no additional runtime. The interface and AI summaries are primarily in Chinese. GitHub features require your own token; AI features require a separately configured Chat Completions-compatible service, which may charge for usage.

![Star Library: browse saved GitHub repositories with Chinese summaries](docs/images/library.png)

## What you can do

- **Understand saved projects** with short Chinese summaries, falling back to the original About when no summary is available.
- **Find a tool you saved** with search, language filters, and sorting. Open a repository directly from its card.
- **Organize at your own pace** with incremental AI processing. Results are saved per project, so you can resume after cancellation, failure, or restart.
- **Explore your interests** through a profile based on your starred repositories. Stars reflect interests, not proof of professional ability.
- **Discover useful projects** based on your collection and feedback. Recommendations prioritize practical tools with at least 100 Stars; star counts do not guarantee quality or stability.

Synced repositories remain available for offline browsing. The app supports autosave and JSON import/export. AI tasks run on demand, never automatically at startup.

### Explore your interests

![Interest profile: a narrative and six interest dimensions based on saved repositories](docs/images/profile.png)

### Discover something to try

![Discovery: Chinese summaries, Star actions, and not-interested feedback](docs/images/discovery.png)

## Get started

1. **Download and run** [StarLibrary.exe](https://github.com/liuminxin45/star-library/releases/latest/download/StarLibrary.exe). No installation is needed.
2. **Connect GitHub** in Settings (`设置`). Use Get Token (`获取 Token`) to create your token, then Connect GitHub (`连接 GitHub`). Return to the library (`星库`) and sync (`同步`).
3. **Optionally configure AI** with your service URL, model ID, and API key. Click Test Connection (`测试连接`) to check the configuration. Model IDs come from your provider.
4. **Organize and explore** using incremental processing under `AI 整理 ▾`, then update your profile or refresh discoveries. Full reprocessing requires confirmation. Organization covers all starred repositories, regardless of the current search or filter.

For a fine-grained token, enable the **Starring: Read and write** account permission and retain **Metadata: Read** repository access. Reading private repository READMEs also requires **Contents: Read** for those repositories; Contents write access is unnecessary. See [GitHub's permission documentation](https://docs.github.com/en/rest/activity/starring#star-a-repository-for-the-authenticated-user).

The model URL can be a service root, version prefix, or full Chat Completions endpoint, such as `https://api.example.com`, `https://api.example.com/v1`, or `https://api.example.com/v1/chat/completions`. Replace these examples with your provider's URL. Remote services require HTTPS; local services may use HTTP.

See the [build and usage guide, in Chinese](docs/build-and-usage.md) for more details and keyboard shortcuts.

## Data, privacy, and cost

- **Local storage:** your library, summaries, profile, backups, and cache are stored under `%USERPROFILE%\.star-library` by default. GitHub tokens and model keys use Windows encryption in a separate credentials file, not the library JSON.
- **GitHub access:** the app uses your verified token to sync Stars and read repository information, and performs Star / Unstar actions when you request them. Settings can check official releases; downloads are manual.
- **AI requests:** relevant public and private repository metadata is sent to your configured model service when you run AI features. Summaries for projects without About or Topics may also use a plain-text README excerpt capped at 2,400 UTF-8 bytes. **Local storage does not mean offline AI processing.** Review your provider's data policy before using AI features.
- **Cost:** the app is free and open source. It does not include model credits; your provider may charge for requests. Sync, search, and browsing work without AI configuration.

## Build

Implemented in C17 with Win32, Direct2D, and DirectWrite. Install Visual Studio 2022 desktop C++ tools and the Windows SDK, then run:

```powershell
.\build.cmd -Architecture x64 -Test
```

Only `StarLibrary.exe` is needed for distribution. The build enforces a 400,000-byte limit and Windows system DLL dependencies. See the [full build and test guide](docs/build-and-usage.md).

## Feedback and support

[Report a bug or suggest an improvement](https://github.com/liuminxin45/star-library/issues). Include your app version, Windows version, and steps to reproduce. Keep tokens, API keys, and private repository data out of reports.

If Star Library helps you rediscover useful projects, consider giving it a **Star**.

## License and acknowledgments

[MIT License](LICENSE). JSON parsing uses [yyjson](https://github.com/ibireme/yyjson). The interface draws inspiration from [ChatGPT-Desktop-UI](https://github.com/liuminxin45/ChatGPT-Desktop-UI). [Project website, in Chinese](https://www.liuminxin.cn/works/star-library).
