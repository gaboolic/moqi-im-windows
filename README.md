# Moqi IM for Windows

Windows 端的输入法前端，负责把 `moqi-ime` 后端接入 **Microsoft Text Services Framework (TSF)**，并提供候选窗、语言栏按钮、组合串上屏等 Windows 平台能力。

当前状态：已实现 **Rime / 中州韵** 输入法接入，**fcitx5** 输入法接入中。

项目 TSF 层核心依赖：[`libIME2`](https://github.com/EasyIME/libIME2)。

## 运行架构

```
┌─────────────────────────────────────────────┐
│           Windows 应用程序                 │
│      (记事本 / Word / 浏览器 / 其他)        │
├─────────────────────────────────────────────┤
│  MoqiTextService.dll                        │
│  - TSF 接口实现                             │
│  - 按键事件/组合串/候选窗处理               │
│  - 通过命名管道连接 Launcher                │
├─────────────────────────────────────────────┤
│  MoqLauncher.exe                            │
│  - 读取 backends.json                       │
│  - 管理后端进程生命周期                     │
│  - 在命名管道与后端 stdin/stdout 之间转发消息│
├─────────────────────────────────────────────┤
│  moqi-ime\server.exe                        │
│  - Go 后端进程                              │
│  - 加载 input_methods\*\ime.json            │
│  - 返回候选、状态、上屏文本等               │
└─────────────────────────────────────────────┘
```

当前默认部署中，`backends.json` 指向 `moqi-ime\server.exe`；Windows 侧通过本地命名管道连到 `MoqLauncher`，`MoqLauncher` 再把请求按行写入后端的标准输入，并从标准输出读取响应。

## 核心职责

- **TSF 文本服务**：实现 `ITfTextInputProcessor`、`ITfKeyEventSink` 等接口
- **Windows UI**：管理候选窗口、消息窗口、语言栏按钮和 preserved keys
- **协议转换**：把按键与状态序列化为 JSON 请求，并应用后端返回的组合串、候选和提交文本
- **后端接入**：通过 `MoqLauncher` 读取 `backends.json`，按语言配置 GUID 路由到对应后端

## 与 `moqi-ime` 的关系

本仓库不实现拼音解析、候选生成、词库管理等输入法核心逻辑，这些能力由 `moqi-ime` 提供。

Windows 侧的职责主要是：

- 把 TSF 生命周期和按键事件转发给后端
- 根据后端返回结果更新候选窗、组合串和状态
- 从已安装后端目录扫描 `input_methods/*/ime.json`，为语言配置提供元数据与配置入口

因此，`moqi-im-windows` 与 `moqi-ime` 需要配套部署。

## 技术栈

- **语言**：C++
- **框架**：Microsoft TSF、Win32 API
- **进程/IO**：命名管道、子进程 `stdin/stdout` 转发、`libuv`
- **数据格式**：JSON（`jsoncpp`）
- **日志**：`spdlog`
- **构建**：CMake + Visual Studio 2022 / MSBuild

## 源码布局

- `MoqiTextService`：TSF 文本服务，产出 `MoqiTextService.dll`
- `MoqLauncher`：后端启动器与消息转发器，产出 `MoqLauncher.exe`
- `libIME2`：IME/TSF 基础库，也是本项目 TSF 层的核心依赖，来源：[`EasyIME/libIME2`](https://github.com/EasyIME/libIME2)
- `libuv`：Launcher 的事件循环与进程/管道依赖
- `backends.json`：后端清单，定义后端名称、启动命令和工作目录

## 构建

前置：**Visual Studio 2022**、**CMake 3.21+**、Windows SDK。

1. 先在 `moqi-ime` 仓库构建后端，例如生成 `server.exe`
2. 在本仓库根目录执行：

   `build.bat`

该脚本会生成：

- `build\Release\MoqLauncher.exe`
- `build\Release\MoqiTextService.dll`（Win32）
- `build64\Release\MoqiTextService.dll`（x64）

## 安装部署

以管理员身份打开 PowerShell（若需写入 `Program Files`），在仓库根执行：

`powershell -ExecutionPolicy Bypass -File .\install.ps1`

常用参数：

- `-MoqiImeSource <path>`：指定要复制的 `moqi-ime` 源码树
- `-SkipMoqiImeCopy`：只更新 DLL / Launcher，不复制后端目录

安装后的目录布局（64 位 Windows）：

- `%ProgramFiles(x86)%\MoqiIM\`
- `%ProgramFiles%\MoqiIM\`

其中：

- `MoqLauncher.exe`、Win32 `MoqiTextService.dll`、`backends.json`、`moqi-ime\` 安装到 `%ProgramFiles(x86)%\MoqiIM\`
- x64 `MoqiTextService.dll` 安装到 `%ProgramFiles%\MoqiIM\`

安装脚本会：

- 复制上述文件
- 对已有 DLL 调用对应位数的 `regsvr32`
- 将 `MoqLauncher.exe` 写入当前用户的开机启动项

## 说明

- `backends.json` 目前默认内容为 `moqi-ime\server.exe`，其 `workingDir` 也是 `moqi-ime`
- 更换 TSF CLSID 或显示名称后，通常需要在系统“语言”设置中重新添加输入法
- 如果只更新后端实现而不重装 DLL，可配合 `-SkipMoqiImeCopy` 或直接替换已安装后端目录

## 参考文档

- [Microsoft TSF 文档](https://docs.microsoft.com/en-us/windows/win32/tsf/text-services-framework)
- [ITfTextInputProcessor 接口](https://docs.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itftextinputprocessor)
