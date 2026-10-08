# IIF CombatBus V3 游戏运行时基础设施测试

本流程仅验证 IIF 加载、V3 接口发现和安全的拒绝行为。它不验证 WRF、CSF、PAS 的实际战斗效果，不安装原生战斗 Hook，也不修改游戏内存。先完成阶段 A；阶段 B 是独立的可选测试，必须由用户另外确认。

## 阶段 A：默认安全 Query-only 测试

使用默认关闭 `outgoing_identity_smoke` 的诊断构建。该构建不会调用 `dispatch_outgoing`，不需要 Registry 为空的假设。

### 准备

1. 等待 ChatGPT 审查此 PR、源码、构建结果和测试流程。审查前不要部署 DLL。
2. 退出 Fallout 4、`f4se_loader.exe` 和相关启动器。
3. 建立独立、可恢复的 MO2 测试 Profile。只启用 IIF、诊断插件和它们必需的依赖。
4. 备份该 Profile 当前使用的 IIF DLL，记录备份 SHA-256。
5. 核对将要测试的 IIF 和 query-only 诊断 DLL 的文件名与 SHA-256；只手动部署已审查的构建。
6. 确认诊断 DLL 是默认 query-only 版本，不是 `_OutgoingOptIn` 版本。

### 启动及检查

1. 使用 F4SE 启动 Fallout 4。进入主菜单不是 QueryInterface 成功的证据。
2. F4SE `kPostLoad` 在存档加载前发生。先检查 `Documents\My Games\Fallout4\F4SE\IIFCombatBusRuntimeSmoke.log`，确认日志到达 `kPostLoad`，并检查：
   - IIF 模块及 `IIF_CombatBus_QueryInterface` 导出已找到；
   - V3 正确版本和完整函数表查询成功；
   - 错误版本、错误调用方结构大小、错误结构内大小均被拒绝；
   - Incoming `CONFIDENCE_UNKNOWN` 返回 `INVALID_CONTEXT`，伤害值不变；
   - 日志明确显示 `query-only build: synthetic Outgoing Dispatch is disabled`。
3. 同时检查 `Documents\My Games\Fallout4\F4SE\ItemIntegrationFramework.log`，确认 IIF 加载日志。存档加载前先保存上述日志。
4. 只有阶段 A 查询检查完成后，才加载测试存档并检查旧 IIF UI、配置、本地化等功能。
5. 如需检查旧 CombatBus 消费者，可在单独的兼容性 Profile 中按其原有方式测试。不要把旧路径行为解释成 V3 战斗 Hook 已运行。
6. 正常退出游戏，保存 IIF、诊断插件、F4SE 和崩溃日志。

## 阶段 B：可选 Outgoing identity

仅在用户另外确认并且阶段 A 完成后进行。它会向 Outgoing Registry 分发模拟 attacker/weapon 指针；生产 ABI 没有 Registry 为空的查询接口，因此只有严格满足下列前提时才可运行：

1. 使用独立的最小 MO2 Profile，只启用 IIF、诊断插件及必需依赖。
2. 检查 Profile 内所有 F4SE 插件 DLL，确认没有其他 DLL 能注册 V3 Provider；如果来源或行为不确定，停止，不运行此阶段。
3. 使用 `outgoing_identity_smoke=y` 单独构建的 `_OutgoingOptIn` 诊断 DLL，不能复用 query-only DLL。
4. 手动部署前记录并核对该可选 DLL 的 SHA-256，并确保当前安装的是该哈希对应的文件。
5. 启动游戏，检查 Outgoing 结果必须为 `NO_PROVIDERS` 且数值不变。任何其他状态都视为测试失败：保存日志、停止测试，不再运行战斗场景。
6. 此测试只检查空 Registry 时的接口分发 identity；它不是实际武器计算或真实战斗验证。

阶段 B 仍不会传入真实游戏对象，也不会注册 Provider 或伪造 `VERIFIED_ADAPTER_CALLSITE`。

## 崩溃与回滚

若任一阶段崩溃或出现异常，不要重复启动覆盖现场。保存当前 DLL 哈希、F4SE/IIF/诊断日志和复现步骤；退出所有进程后，从备份恢复原 IIF DLL，并从该测试 Profile 移除诊断插件。再次启动前确认恢复文件哈希与备份相同。
