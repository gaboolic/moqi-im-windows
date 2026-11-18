# 安装与启动恢复回归

## 自动测试（不安装、不注册 TSF）

Windows + Visual Studio 2022，先按项目说明配置 protobuf/protoc：

```powershell
cmake -S . -B build-vs32 -A Win32 -DMOQI_BUILD_TESTS=ON
cmake --build build-vs32 --config Release --target MoqiLauncher MoqiTextService SetupHelper LauncherRecoveryTests
ctest --test-dir build-vs32 -C Release -R launcher-recovery --output-on-failure
cmake --build build-vs64 --config Release --target MoqiTextService
pwsh -NoProfile -File SetupHelper/Test-PayloadValidation.ps1
pwsh -NoProfile -File SetupHelper/Test-MachineRegistrationCleanup.ps1
pwsh -NoProfile -File SetupHelper/Test-InstallSkipBackend.ps1
```

[LauncherRecoveryTests.cpp](<LauncherRecoveryTests.cpp>) 使用唯一临时目录和唯一管道，不启动托盘、不使用真实算法、不修改系统输入法：

- 缺少算法程序或工作目录时启动失败但不崩溃；失败的 libuv handle 正常释放。
- 真实 protobuf 管道返回 GUID 映射缺失和算法启动失败错误，保留请求序号。
- 自动重试限频；显式重启可立即启动已停止的算法。
- 启动即退出的子进程被回收，无无限重启风暴；随后仍可恢复。
- 中文、空格路径启动成功；损坏的 JSON 返回失败而非抛异常。
- 未连接管道的同步写入错误会通知调用方，不遗留 libuv 请求。
- 使用临时数据/日志目录，不读取或写入真实用户的 Launcher 配置；服务销毁后释放日志句柄。

[负载校验测试](<../SetupHelper/Test-PayloadValidation.ps1>) 校验完整负载、零 profile、缺图标、PE 架构错误、无效 GUID、损坏 JSON、重复 GUID、目录穿越。打包前也会运行同一套负载校验器。

[机器注册清理测试](<../SetupHelper/Test-MachineRegistrationCleanup.ps1>) 使用 MSVC 编译纯内存 fake，不写真实注册表；覆盖 COM/TIP/category 引用、兄弟服务保留、幂等、错误传播及视图隔离。可用 `-VcVarsPath` 指定本机 MSVC 环境脚本。

[跳过算法参数测试](<../SetupHelper/Test-InstallSkipBackend.ps1>) 验证不完整负载在清空 stage 前被拒绝，旧 stage 不受影响。

[TSF 离线恢复验证说明](<../MoqiTextService/OFFLINE-RECOVERY-VALIDATION.md>) 记录断线组合区清理的行为及尚待真实宿主验证的限制；永久拒绝写锁时会暂时拦截按键，不能据编译通过就视为端到端通过。

## 必须在测试虚拟机完成的人工验收

构建和静态检查不能替代以下 Windows TSF/安装器端到端测试：

1. 全新安装：默认目录和自定义中文/空格目录分别安装，Win+Space 可选墨奇；聚焦记事本出现中/英模式，能输入中文。
2. 用户身份：普通用户提供另一个管理员账号凭据安装；原登录用户应启用 profile 并拥有 Launcher/算法进程，不能归属 SYSTEM/另一个管理员。直接从提升进程安装时按提示注销重登。
3. 覆盖安装：保持 x86/x64 应用打开使旧 DLL 被占用，验证重启提示、延迟替换、SYSTEM 机器补注册、登录用户启用。检查 `RegistrationPending` 清零和补注册任务执行结果。
4. 冷启动：墨奇设为默认输入法，重启后无需先切到其他输入法再切回；同时验证 32 位与 64 位编辑器。
5. 故障恢复（仅测试 VM）：移走算法程序，启动/输入应显示失败状态或通知而非崩溃/吞掉全部英文；恢复文件后点击“重启墨奇引擎”能够恢复。
6. 图标恢复：首次传入不存在的图标路径后补回图标，同路径应能重试；重复 addButton 不产生重复按钮；检查语言栏 AddItem 失败日志和重试。
7. 断线：已有预编辑时关闭算法，宿主不应挂死；重启算法后能继续输入。检查无晚到提交导致重复字符。
8. 用户选择：成功安装后手动禁用墨奇，普通重新登录不应因用户初始化步骤重复强制启用；新安装 generation 才执行一次启用。

日志在当前用户 `%LOCALAPPDATA%\MoqiIM\Log`。安装的机器步骤可能以提升账号/SYSTEM 执行，需同时检查对应身份的安装日志与计划任务结果。测试中保留安装目录、安装账号、登录账号、系统版本、x86/x64 宿主和故障日志，以区分注册、通信和算法启动失败。
