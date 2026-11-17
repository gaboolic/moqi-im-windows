# 修复接续验证（2026-11-17）

分支：`fix-20261117`。本轮接续已有未提交修复，未创建提交、未切换分支、未执行实际安装/卸载/TSF 注册。历史会话搜索返回存储不可用，因此以用户提供进度及工作区实际差异恢复上下文。

## 本轮新增修复

- Launcher 声明与定义的构造函数签名统一，回归测试使用独立临时数据和日志目录；析构移除 spdlog 注册表引用，避免测试日志句柄泄漏。
- Launcher 忽略类型错误的日志配置，拒绝类型错误的 backend 参数和 profile 数据，避免 JSON 类型异常；补同步管道写失败回调测试。
- TSF 与安装助手的 64 位注册表读取改用显式 `RegOpenKeyExW` 视图，不依赖被当前 Windows 目标版本宏屏蔽的 RRF 标志。
- 语言栏按钮从明确的 `ITfLangBarItem` 接口查询 COM identity，消除多继承 IUnknown 歧义。
- 断线时清除候选/消息 UI 并请求结束旧组合区，保留已在文档中的文本；清理未完成前隔离旧回复和新 RPC。连接世代避免旧异步 edit session 应用到新连接，修复队列在重入清空后的操作。
- 系统 DLL 缺失的卸载改为固定 CLSID、明确 HKLM 32/64 视图的 checked 幂等清理，不再假报成功；不操作 HKCR/HKCU。分离 x64 子进程 System32 路径与 32 位父进程检查所需 Sysnative 路径。
- `-SkipMoqiImeCopy` 在清空 staging 前明确拒绝，防止制作缺少引擎的完整安装包及破坏旧 stage。

## 已验证

- 最终 Win32 Release 构建通过：MoqiLauncher、MoqiTextService、SetupHelper、LauncherRecoveryTests。
- 最终 x64 Release 构建通过：MoqiTextService。Launcher 和 SetupHelper 按现有 CMake 设计只生成 Win32 版本。
- Launcher recovery：最终版本通过；此前包含同步写失败和错误类型配置检查的版本连续重复 10 次通过。
- 负载校验：8 项通过，PowerShell 7 和 Windows PowerShell 均验证通过。
- 独立机器注册清理 fake 测试：4 组通过，覆盖拥有的 COM/TIP/category 引用、孤立引用、幂等/兄弟服务保留、错误传播及独立视图；另通过 SetupHelper 源文件语法检查。
- 跳过算法参数测试：2 项通过，已有 stage 保留，新 stage 不创建。
- 最终 `git diff --check` 通过；只有已有安装脚本 CRLF/LF 提示。

## 尚未验证

- 未重新制作或运行安装包，已有 stage/安装包不是本轮源码最终构建的交付物。
- 默认/中文空格安装目录、UAC 另一管理员凭据、首次安装、覆盖安装、占用文件延后替换、重启后用户启用和损坏安装卸载，需测试虚拟机验收。
- 真实 TSF 宿主中的预编辑断线、异步写锁拒绝、晚到回复和切换文档/输入法，尚无动态集成验证。
- 如果宿主始终拒绝写锁，清理仍会 pending 并暂时拦截按键，不能将构建通过理解为所有宿主都不会吞键。

详细步骤见 [回归说明](<README.md>) 和 [TSF 离线验证说明](<../MoqiTextService/OFFLINE-RECOVERY-VALIDATION.md>)。
