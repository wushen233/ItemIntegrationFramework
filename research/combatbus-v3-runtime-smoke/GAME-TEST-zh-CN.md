# IIF CombatBus V3 游戏内基础设施冒烟测试

**用途：** 验证测试版 IIF 在 Fallout 4 OG 1.10.163 中加载，并由独立诊断插件实际调用 V3 `QueryInterface`。本测试不验证 WRF、CSF 或 PAS 的新计算效果，也不安装新战斗 Hook。

## 测试前

1. 先等待 ChatGPT 审查测试源码、构建结果和本流程。未获批准前不要部署测试 DLL。
2. 退出 Fallout 4、`f4se_loader.exe` 和相关启动器进程。
3. 备份当前 MO2 IIF 插件 DLL 到测试记录目录，记下原文件 SHA-256。
4. 确认测试 DLL 的游戏版本目标是 OG 1.10.163，并记录其 SHA-256。
5. 建立可恢复的 MO2 测试 Profile。测试时不要覆盖工作区、游戏目录或其他 Profile 的 IIF 文件。
6. 确认测试 Profile 没有实验性 V3 Provider。诊断插件的 Outgoing identity 测试要求 IIF V3 Outgoing Registry 为空；现有只使用旧 IIF 接口的插件可以保留。

## 执行

1. 仅在批准后，将 IIF 测试 DLL 和 `IIFCombatBusRuntimeSmoke.dll` 手动放入该 MO2 Profile 对应的 `F4SE/Plugins` 覆盖目录。
2. 使用 F4SE 启动 Fallout 4。记录是否到达主菜单；这只证明游戏和插件启动流程未立即失败。
3. 加载测试存档，等待至少数秒，让 F4SE `kPostLoad` 诊断回调完成。
4. 检查 `Documents\My Games\Fallout4\F4SE\ItemIntegrationFramework.log`，确认 IIF 加载、旧 `kGameLoaded` 接口广播及旧系统日志。
5. 检查 `Documents\My Games\Fallout4\F4SE\IIFCombatBusRuntimeSmoke.log`，确认：
   - IIF 模块和 `IIF_CombatBus_QueryInterface` 导出已找到；
   - V3 正确版本返回成功且函数表完整；
   - 错误版本、错误调用方大小、错误结构内大小均被拒绝；
   - 空 Registry 下 Outgoing 返回 `NO_PROVIDERS`，Health 数值保持不变；
   - Incoming 的 `CONFIDENCE_UNKNOWN` 被拒绝，伤害值未变化。
6. 单独检查 IIF 原有 UI、配置读取、本地化和已有消息交换功能。
7. 如测试 Profile 保留使用旧接口的战斗插件，按原有功能验证 Legacy CombatBus。不要把这解释为 V3 Hook 已运行。
8. 正常退出游戏，然后复制并保存 IIF、诊断插件、F4SE 和游戏崩溃日志。

## 结果判读

- 到达主菜单不等同于 `QueryInterface` 成功；必须在诊断日志中看到 V3 Query 的成功记录。
- `OUTGOING ... pass=true` 只有在本次运行确认没有 V3 Provider 时才有效。若状态不是 `NO_PROVIDERS`，停止并报告 Registry 非空，勿将结果作为 identity 验收。
- Incoming 诊断刻意不伪造 `VERIFIED_ADAPTER_CALLSITE`。其 `INVALID_CONTEXT` 且原值不变是 fail-closed 检查，不是 Incoming identity 测试，也不代表真实承伤路径已接入。
- 本测试不证明 WRF/CSF/PAS 实际计算、原生 Hook 安全性或游戏伤害行为。

## 崩溃与回滚

若游戏崩溃或出现异常：

1. 不要重复启动覆盖现场；保留 crash log、F4SE 日志和当前测试文件哈希。
2. 退出所有进程后，从备份恢复原 IIF DLL，并从测试 Profile 移除诊断插件。
3. 再启动前核对恢复后的 DLL SHA-256 与备份一致。
4. 将日志、测试 Profile 状态和复现步骤交给 ChatGPT 审查。
