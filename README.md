# Codex 用量查看器

Windows 中文桌面程序，显示 Codex 的 5 小时额度、每周额度、可用重置次数及重置明细。时间按 Windows 当前时区显示。

程序通过本机 `codex app-server` 只读查询，沿用现有登录状态，不需要额外 API Key，不创建对话、不发送模型请求，也不兑换重置次数。

## 运行

环境要求：Windows 10/11 x64、已安装且加入 `PATH` 的 Codex CLI，以及可用的本机 Codex 登录状态。首个适配版本为 Codex CLI **0.169.0**；其他版本需要兼容相同查询接口。

双击 [bin/x64/Release/CodexUsage.exe](bin/x64/Release/CodexUsage.exe)。Release 静态链接 MFC 和 C/C++ 运行库，运行时无需另外安装这两项组件。程序会隐藏启动 PowerShell 和常驻 app-server，正常窗口最小化到任务栏，关闭窗口即退出并回收自己启动的子进程。

也可解压 [便携包](dist/CodexUsage-x64.zip)，直接运行其中的 `CodexUsage.exe`。下面是使用模拟数据的[界面预览](docs/界面预览.png)。

界面提供：

- 额度剩余百分比、进度条、下次恢复时间。
- 服务端返回的可用重置总数，以及类型、状态、发放时间、到期时间、名称和说明。
- 刷新间隔、应用、立即刷新，以及连接状态和最后成功更新时间。

默认窗口采用紧凑布局，内容区域面积约为原版的三分之一，字号不变。重置明细支持横向和纵向滚动，也可以拖大窗口查看更多内容。

服务端没有提供的信息显示“未提供”，首次查询失败显示“暂无数据”；后续失败保留上次成功数据，并提示“数据未更新”。无法可靠翻译的自由文本保留原文并标注“服务端原文”。明确没有到期时间的明细显示“长期有效”。

首版不提供托盘、自启动或历史统计。

## 刷新与配置

默认每 **3 秒**查询额度和重置次数。启动时、每 60 秒、重置次数变化时和手动点击“立即刷新”时读取完整明细。普通轮询保留已有明细，明细有独立更新时间。

在刷新间隔中输入 **1～3600 的整数秒数**，点击“应用”或按回车后立即生效并保存。配置位于 EXE 同目录下的 `CodexUsage.ini`：

```ini
[Settings]
RefreshIntervalSeconds=3
```

配置缺失或内容非法时使用 3 秒。保存失败会弹窗提示，设置仍在本次运行中生效；需要持久保存时，请将程序放在当前用户可写入的目录。

同一时间只有一个查询，自动刷新从上次查询结束后计时。查询过程中连续点击“立即刷新”，最多合并为一次待执行的完整查询。单次请求超时为 15 秒，连续失败按 3、6、12、24、60 秒重试；成功后恢复用户设置的间隔，手动刷新可以立即重试。

## 编译

安装 Visual Studio 2022 的“使用 C++ 的桌面开发”、v143 工具集、Windows SDK，以及 **C++ MFC for latest v143 build tools（x86 和 x64）**。使用 C++17，工程仅包含 x64 Debug 和 Release；Release 使用静态 MFC 与 `/MT`，Debug 使用 `/MTd`。

在项目根目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Configuration All -Package
```

也可以用 VS2022 打开 [CodexUsage.sln](CodexUsage.sln)，选择 `Release | x64` 后生成；或在 VS2022 Developer PowerShell 中直接编译：

```powershell
msbuild .\CodexUsage.sln /m /p:Configuration=Release /p:Platform=x64
msbuild .\CodexUsage.sln /m /p:Configuration=Debug /p:Platform=x64
```

输出目录分别为 `bin\x64\Release` 和 `bin\x64\Debug`。

窗口和 EXE 共用内嵌的[程序图标](src/res/CodexUsage.png)，包含 16～256 像素的 8 种尺寸，无需随程序分发图标文件。修改图标样式可编辑 `scripts/generate-icon.ps1`，运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\generate-icon.ps1` 后重新编译。

## 验证

测试覆盖数据解析、配置以及模拟 app-server 的界面与进程行为。GUI 验证需要 Python 3 和可用的 Windows 桌面会话。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test.ps1 -Integration
python .\tests\gui_validation.py --soak-seconds 600
```

模拟服务测试不代表真实账户查询已经通过。当前开发环境的真实查询受到沙箱对本机 Codex 目录访问的限制，尚未完成真实额度与 CLI 显示的核对。实际执行结果、持续运行指标及限制见 [验证记录](docs/验证记录.md)。

在正常用户桌面中可运行 `python .\tests\real_service_probe.py` 重新检查真实连接；结果保存在 `tests/real_service_result.json`，不读取或保存认证凭据。若仍失败，可先在普通 PowerShell 中执行 `codex --version`、`codex login status`，确认安装和登录情况。

## 实现与依赖

实现细节见 [协议与实现](docs/协议与实现.md)。项目随源码提供 [nlohmann/json 3.11.3](https://github.com/nlohmann/json/tree/v3.11.3) 单头文件，许可证为 [MIT](third_party/LICENSE.json)。
