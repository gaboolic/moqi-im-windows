# Moqi IM for Windows - Windows 输入法 TSF 层

## 项目简介

Moqi IM for Windows 是一个基于 **Microsoft Text Services Framework (TSF)** 开发的 Windows 平台输入法适配层。

## 架构定位

```
┌─────────────────────────────────────────────┐
│           Windows 应用程序                 │
│      (记事本 / Word / 浏览器 / 其他)        │
├─────────────────────────────────────────────┤
│      Moqi IM Windows (本项目)               │
│  ┌─────────────────────────────────────┐   │
│  │   TSF 接口实现 (ITfTextInputProcessor) │ │
│  │   - 按键拦截 (ITfKeyEventSink)       │   │
│  │   - 候选窗口 (ITfCandidateListUI)    │   │
│  │   - 状态窗口                         │   │
│  └─────────────────────────────────────┘   │
│  ┌─────────────────────────────────────┐   │
│  │   与引擎通信层 (RPC/Cgo/其他)        │   │
│  └─────────────────────────────────────┘   │
├─────────────────────────────────────────────┤
│        Moqi IME 引擎 (moqi-ime)             │
│        Go 语言实现的输入法核心              │
└─────────────────────────────────────────────┘
```

## 核心职责

- **TSF 接口实现**：实现 `ITfTextInputProcessor` 等必要接口
- **UI 组件**：候选窗口、状态栏、语言栏按钮
- **按键处理**：拦截并转发按键事件到引擎
- **文本提交**：将引擎返回的候选文本提交到应用程序
- **与引擎通信**：通过 Cgo / gRPC / REST 等方式调用 `moqi-ime`

## 与 Moqi IME 引擎的关系

本项目 **依赖** `moqi-ime` 项目作为其输入法引擎核心：
- 本层负责 Windows 平台特定的 UI 和 TSF 交互
- 拼音解析、候选生成、词库管理等核心逻辑由 `moqi-ime` 提供

### 支持的输入模式

Windows 输入法将支持 `moqi-ime` 引擎提供的两种输入模式：

| 模式 | 说明 | 状态栏指示 |
|------|------|-----------|
| **拼音输入** | 标准汉语拼音输入 | 显示 "拼" |
| **码表输入** | 支持五笔、郑码等码表 | 显示 "码" |

用户可通过语言栏按钮或热键切换输入模式。

## 技术栈

- **语言**: C++ (TSF 接口实现) / C (与 Go 引擎通信)
- **框架**: Microsoft Text Services Framework (TSF)
- **通信**: 待定 (Cgo / gRPC / REST / COM)
- **构建**: CMake / MSBuild

## 目录规划 (待定)

```
moqi-im-windows/
├── tsf/                  # TSF 接口实现
│   ├── dllmain.cpp       # DLL 入口
│   ├── text_service.cpp  # ITfTextInputProcessor 实现
│   ├── key_event.cpp     # ITfKeyEventSink 实现
│   └── candidate_ui.cpp  # 候选窗口 UI
├── ui/                   # UI 组件
│   ├── candidate_window.cpp
│   ├── status_window.cpp
│   └── langbar_button.cpp
├── engine_bridge/        # 与引擎通信层
│   └── go_bridge.cpp     # Cgo / RPC 调用
├── common/               # 公共工具
├── resources/            # 资源文件
└── tests/                # 测试
```

## 开发状态

🚧 项目初始化中...

## 参考文档

- [Microsoft TSF 文档](https://docs.microsoft.com/en-us/windows/win32/tsf/text-services-framework)
- [ITfTextInputProcessor 接口](https://docs.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itftextinputprocessor)
