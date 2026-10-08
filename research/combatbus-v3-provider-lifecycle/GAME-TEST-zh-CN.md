# Phase 2D-A：Provider 生命周期实机测试

本测试只验证 V3 Provider 注册与安全注销流程，不触发 Outgoing/Incoming Dispatch，不验证伤害效果，也不安装原生 Hook。**先等待 ChatGPT 审查本 PR、源码和 DLL 哈希；审查前不要部署。**

## 阶段 A：准备

1. 创建可恢复的独立 MO2 Profile，只启用 IIF、`IIFCombatBusProviderLifecycleSmoke` 及必需依赖。
2. 确认没有其他 V3 Provider、其他 CombatBus 生命周期诊断插件或不明 F4SE DLL；如果不能确认，停止。
3. 确认 Fallout 4、`f4se_loader.exe` 和启动器未运行。
4. 备份 Profile 当前生效的 `ItemIntegrationFramework.dll`，记录备份 SHA-256 并验证可恢复。
5. 由用户手动安装经 ChatGPT 审查的 IIF DLL 和本诊断 DLL；核对文件来源、名称和 SHA-256，并确认 Profile 中实际生效的文件没有被高优先级 Mod 覆盖。

## 阶段 B：启动

1. 通过 F4SE 启动 Fallout 4 OG 1.10.163，等待到达主菜单。
2. 本轮不要主动进行战斗，也不要使用会发起 V3 Dispatch 的实验工具。
3. 检查 `Documents\My Games\Fallout4\F4SE\IIFCombatBusProviderLifecycleSmoke.log`。

## 阶段 C：检查日志

确认日志包含并通过以下项目：

- F4SE `kPostLoad` 到达，IIF 模块和 `IIF_CombatBus_QueryInterface` 导出找到。
- 错误版本、错误接口大小被拒绝，V3 interface version 为 3、size 为 64。
- Outgoing/Incoming 错误描述符版本与大小被拒绝。
- Outgoing 与 Incoming 注册各自返回 API `OK`、内部状态 `OK`、`added=1`、handle 非零且两者不同。
- 重复 Provider ID 返回 `DUPLICATE`，没有产生新 handle。
- 两方向错误 Stage 的 Unregister/Wait 均返回 `NOT_FOUND`。
- 正确 Unregister 前 Wait 返回 `NOT_FOUND`。
- 正确 Stage 的 Unregister 返回 `OK`，随后唯一指定的 WaitQuiescent 返回 `OK`。
- 过期 handle 的重复 Unregister/Wait 返回 `NOT_FOUND`。
- Outgoing callback count 和 Incoming callback count 均为 0。
- 最终汇总为 `[IIF-CB-Provider-Smoke] Phase 2D-A PASS`。

若任何状态不符、出现 callback count 非零或没有最终 PASS，保留日志并停止；不要重复启动覆盖现场。单个非成功 Wait 状态绝不授予 Provider 卸载许可。诊断插件不会调用 `FreeLibrary`，且保持加载至正常退出。

确认上述日志后，可以再检查 IIF 原有加载日志和 UI/配置/本地化等功能。这不代表 V3 已接入游戏伤害路径。

## 阶段 D：退出及回滚

正常退出游戏后保存：

- `ItemIntegrationFramework.log`
- `IIFCombatBusProviderLifecycleSmoke.log`
- F4SE 日志
- Crash Logger 日志（若有）
- 实际生效的两个 DLL 的 SHA-256

若发生异常，停止测试，不自动重试。退出游戏后恢复备份的 IIF DLL、禁用诊断 Mod，并核对恢复文件哈希等于备份哈希。将日志与哈希交给 ChatGPT 复核。

## 结论边界

此阶段没有调用 Dispatch，因此 callback count 为零只证明本次生命周期场景没有触发测试回调。它不证明并发中回调的排空、Provider DLL 动态卸载、原生 Hook 或真实战斗行为。Phase 2D-B 需要单独审查和授权。
