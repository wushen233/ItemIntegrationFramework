# Phase 2D-B2A-R3 — 原始机器码证据附件

## 身份与来源

- 目标：Fallout 4 OG 1.10.163.0；PE image base `0x140000000`。
- 分析输入：`Fallout4.exe.unpacked.exe`，SHA-256 `C53500F71AE7E9151183A67010D7959831FA48D75327B8C7A2E825CE4EC382DD`，65,503,232 bytes。
- 路径：`vendor/tools/BethesdaGhidraScripts/exes/f4/og/Fallout4.exe.unpacked.exe`；清单 ID `og-1.10.163-enriched`，Program `/f4/og/Fallout4.exe.unpacked.exe`。
- 这是解包后的 Ghidra 输入，不是运行时原始 EXE 的文件哈希。没有用该 SHA 冒充运行时模块指纹。
- 机器码由同一输入上的 scratch 定向 Ghidra/PyGhidra 与 `llvm-objdump` 读取；没有重新跑全程序反编译，也没有修改 BGS Ghidra 项目。
- CommonLib 参考：`commonlibf4.lock.json` 的 `dear-main` 锁定提交 `d0d2593f7aa3a09b0d142f1d6b1df5aa69dec6d5`。工作区 `dear-main/include/RE/A/ActorValue.h` 将 `ActorValueInfo* health` 放在偏移 `0xD8`；`ActorValueInfo.h` 说明它是 ActorValueInfo 对象。工作区活动 BGS 参考另指向 `wushen-main`，其对应 `health // 0xD8` 布局相同；profile 目录本身不是 Git checkout，不能额外声称本地文件有独立 Git HEAD 证明。

## 0x55 callsite 原始 bytes 与上下文

### `0x140C0B356`

```asm
140c0b33a: 48 8b 15 47 90 e9 04       mov rdx, qword ptr [rip + 0x4e99047]
140c0b341: 4c 8d 4c 24 30             lea r9, [rsp + 0x30]
140c0b346: 4c 8d 44 24 50             lea r8, [rsp + 0x50]
140c0b34b: f3 0f 11 54 24 30          movss dword ptr [rsp + 0x30], xmm2
140c0b351: b9 55 00 00 00             mov ecx, 0x55
140c0b356: e8 a5 59 95 ff             call 0x140560d00
140c0b35b: f3 0f 10 44 24 30          movss xmm0, dword ptr [rsp + 0x30]
140c0b361: 0f 28 bc 24 a0 00 00 00    movaps xmm7, xmmword ptr [rsp + 0xa0]
```

第五条参数由 R9 提供；R9 指向刚由 XMM2 写入的 float，原调用返回后从同一位置读取。R8 是另一局部对象/包装数据地址；其精确动态类型在此点未单独闭合。CALL 为完整五字节 `E8 rel32`，目标为 `0x140560D00`。

### `0x140CA729E`

```asm
140ca727c: 48 8d 4c 24 20             lea rcx, [rsp + 0x20]
140ca7281: 48 8b d6                   mov rdx, rsi
140ca7284: 4c 8b c0                   mov r8, rax
140ca7287: e8 c4 08 65 ff             call 0x1402f7b50
140ca728c: 4c 8d 4c 24 68             lea r9, [rsp + 0x68]
140ca7291: 4c 8d 44 24 20             lea r8, [rsp + 0x20]
140ca7296: 48 8b d3                   mov rdx, rbx
140ca7299: b9 55 00 00 00             mov ecx, 0x55
140ca729e: e8 5d 9a 8b ff             call 0x140560d00
140ca72a3: 48 8b 07                   mov rax, qword ptr [rdi]
140ca72a6: 48 8b d6                   mov rdx, rsi
140ca72a9: f3 0f 10 44 24 68          movss xmm0, dword ptr [rsp + 0x68]
140ca72af: f3 0f 58 47 10             addss xmm0, dword ptr [rdi + 0x10]
140ca72b4: 48 8b cf                   mov rcx, rdi
140ca72b7: f3 0f 11 47 10             movss dword ptr [rdi + 0x10], xmm0
```

`0x1402F7B50` receives a local destination, RDX=RSI and R8=RAX before the call; surrounding constructor use is consistent with a `BGSObjectInstance` local. At the `0x55` call R8 points to that object, while R9 points to the rating float at `[RSP+0x68]`. The caller reads that float after return and adds it into the object field. CALL bytes are `E8 5D 9A 8B FF`, resolving to `0x140560D00`.

### Legacy argument mismatch

At a five-argument Win64 call: RCX is argument 1, RDX argument 2, R8 argument 3, R9 argument 4, and caller `[RSP+0x20]` argument 5. The old `HandleEntryPoint_Hook` maps its `a3` to R8 and `a4` to R9. For both `0x55` sites it interprets `a3`/R8 as `float*`, but the actual rating float is argument 4/R9. It ignores the actual rating pointer. This is a static argument-position mismatch; a runtime crash or particular callback corruption is not claimed.

## Health ActorValue 数据流

### `FUN_140D79EB0` → `FUN_140DF7140`

入口 `0x140D79EB0`：

```asm
140d79eb0: 48 89 6c 24 20             mov [rsp+0x20], rbp
140d79eb5: 56                         push rsi
140d79eb6: 48 83 ec 50                sub rsp, 0x50
140d79eba: 48 8b f1                   mov rsi, rcx       ; Actor
140d79ec9: 48 8b 01                   mov rax, [rcx]
140d79ecc: 49 8b e8                   mov rbp, r8        ; retained incoming R8
140d79ecf: 0f 28 f9                   movaps xmm7, xmm1  ; incoming damage snapshot
140d79ed2: ff 50 20                   call qword ptr [rax+0x20]
140d79edd: 8b 86 3c 04 00 00          mov eax, [rsi+0x43c]
140d79ee6: a8 01                      test al, 1        ; state gate after shift
140d79eee: 8b 86 30 01 00 00          mov eax, [rsi+0x130]
140d79ef9: 25 00 00 1e 00             and eax, 0x1e0000
```

该函数还检查 Actor 状态、玩家/全局对象、源对象和其他分支条件；它不是无条件提交路径。正值计算分支在 `0x140D79F88` 起检查 XMM6，经过后续状态判断和倍率后才可能到更新调用。机器码不能单独证明所有目标 Actor 或所有命中都会经过该分支。

关键参数准备与调用：

```asm
140d79fef: e8 fc 11 2f ff             call 0x14006b1f0
140d79ff4: 0f 57 35 95 f1 ec 01       xorps xmm6, xmmword ptr [rip+...]
140d79ffb: ba 02 00 00 00             mov edx, 2
140d7a000: 4c 8b 80 d8 00 00 00       mov r8, [rax+0xd8]
140d7a007: 48 8b ce                   mov rcx, rsi
140d7a00a: 48 89 6c 24 20             mov [rsp+0x20], rbp
140d7a00f: 0f 28 de                   movaps xmm3, xmm6
140d7a012: e8 29 d1 07 00             call 0x140df7140
```

调用前，singleton getter 的返回在 RAX，`R8=[RAX+0xD8]`。CommonLibF4 `ActorValue` 明确把偏移 `0xD8` 标为 `health`，所以 Health 身份由传入 ActorValueInfo 指针的字段定义支持，**不是仅凭 EDX=2 判断**。XMM3 是已取反/用于扣减的值；第五参数传入入口时保存的 R8 值。EDX=2 是 `FUN_140DF7140` 的另一个模式/索引参数。

### `FUN_140DF7140`

```asm
140df7140: ...
140df7154: 48 63 ea                   movsxd rbp, edx
140df715c: 49 8b d0                   mov rdx, r8
140df7164: 49 8b f0                   mov rsi, r8
140df7167: 48 8b d9                   mov rbx, rcx
140df716a: 0f 28 fb                   movaps xmm7, xmm3
140df716d: e8 7e 1d 00 00             call 0x140df8ef0
...
140df71e8: f3 0f 58 0c af             addss xmm1, [rdi+4*rbp]
...
140df7232: 4c 8d 44 24 20             lea r8, [rsp+0x20]
140df7237: f3 0f 11 4c 24 2c          movss [rsp+0x2c], xmm1
140df723d: 48 8b d6                   mov rdx, rsi          ; forwarded Health ActorValueInfo
140df7240: 48 8b cb                   mov rcx, rbx          ; Actor
140df7243: 4c 8b cd                   mov r9, rbp
140df7246: 48 89 7c 24 20             mov [rsp+0x20], rdi
140df724b: e8 60 37 00 00             call 0x140dfa9b0
```

This forwards the Health ActorValueInfo to `FUN_140DFA9B0`, while also passing the Actor and an intermediate storage descriptor. The conditional virtual call at `0x140DF71D0` is taken only when EBP equals 2; it does not establish the AV identity.

### `FUN_140DFA9B0` storage write

```asm
140dfa9b0: ...
140dfa9c5: 4d 8b f1                   mov r14, r9
140dfa9c8: 49 8b d8                   mov rbx, r8
140dfa9cb: 48 8b f2                   mov rsi, rdx          ; Health ActorValueInfo
140dfa9ce: 48 8b e9                   mov rbp, rcx          ; Actor
140dfa9d1: e8 1a ba ea ff             call 0x140ca63f0
140dfa9d6: 84 c0                      test al, al
140dfa9d8: 0f 84 31 01 00 00          je 0x140dfab0f
140dfa9de: 48 8b 45 58                mov rax, [rbp+0x58]
140dfa9e2: 48 8d 4d 58                lea rcx, [rbp+0x58]
140dfa9f5: 48 8b d6                   mov rdx, rsi
140dfa9f8: ff 50 08                   call qword ptr [rax+0x8]
140dfaa05: f3 0f 10 0c 91             movss xmm1, [rcx+4*rdx]
140dfaa0a: 8b 43 0c                   mov eax, [rbx+0xc]
140dfaa0d: 89 04 91                   mov [rcx+4*rdx], eax
```

前置 helper 和 virtual 查询成功后，代码以由 Health ActorValueInfo 关联出的 storage/索引描述符读取当前值并写入更新值。CommonLib 类型名、传参链和这处读写共同支持“Health ActorValue 存储被更新”的结论；仍有 helper 条件、通知、派生值和其他消费者，不等于所有 DoHitMe 场景都最终扣血。

## Prediction 状态线索

`FUN_140FBF4E0` 从 TLS 索引读取/保存旧状态，在 `0x140FBF541` 将 TLS byte 置为 1；对候选 HitData flags 在 `0x140FBF584` 设置 `0x4000`（bit14），随后在 `0x140FBF5A6` 调用 `FUN_140FBE300`。CommonLibF4 `HitData` 把 bit14 命名为 `kPredictBaseDamage`。因此武器初始化及 `0x23` 共享路径可由 Prediction 调用。四个 EntryPoint callsite 不能被标记为 ActualHit 或 VerifiedAdapterCallsite。
