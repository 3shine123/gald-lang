# 34 — async/await 里程碑 1（路线图待做 #4）

方法体含 `@await` 即 async（无标注、链式传染）；`@await` 是上下文关键词（C 超集：`int await = 1;` 必须编过）。

| 文件 | 期望 |
|------|------|
| `async_test.np` | 编译通过，stdout 见 `.out`（async void 入口 + 跨调用链双 await，`result=18`） |

```bash
./target/debug/nepac run tests/golden/34_async/async_test.np
```

## 已定设计（用户拍板，2026-09）

1. **入口 = async void 方法**：同步上下文调用 async void 合法（blocking wrapper 语义）；同步调**非 void** async → 编译错 + 修复提示。参照 C# `async void` vs `async Task`（无死锁风险：单线程协作泵到完成必有进展）。
2. **语句位 `await f();` = 求值后丢弃**。
3. **break/continue 跨 await 放行**（M2 状态跳转）；**@noarc 跨 await 放行**（无 jmp_buf）；**@try 跨 await M1 报错**（jmp_buf 不能跨挂起点，替代：try 包在循环外）。
4. **bridge header**：async void 发 wrapper 声明；非 void async 跳过（M2）。

## M1 实现形态（本目录覆盖）

**不拆函数签名**（vtable 槽位不变、跨 TU 安全、bridge header 零特判）。desugar 在 AST 层改写 async 方法体：

```c
/* int r = await n;  → */ 
NPTask * __nepa_task = (NPTask *)(nepa_task_create(0, self, 0));
int r = (nepa_task_resume(__nepa_task), n);
nepa_task_join(__nepa_task);
```

- `await e` → `(nepa_task_resume(task), e)`：Comma 求值，先推进状态机（M1 entry=NULL，resume 即完成返回 1——纯 API 契约钩子），后取 e 的值。
- 方法体首尾包 `nepa_task_create` / `nepa_task_join`（真实 task 生命周期：calloc 分配、join 后释放）。
- **检查层**（`nepa_async::check_unit`，desugar 前跑原始 AST）：
  - `@try` 跨 await → error（try/catch/finally 任一含 await 即报）
  - 同步上下文（非 async 方法 + **顶层函数含 main**）调非 void async → error + 提示
- runtime（`include/nepa/runtime.{h,c}`）：`NPTask{state,finished,entry,self_obj,frame,result,parent}` + create/resume/finish/join 四个 API（host 用 calloc/free；freestanding 用户提供分配器后可用）。

## 里程碑路线

- **M1（本目录）**：task 驱动 + await 钩子 + 检查层，行为=同步执行，API 契约就位
- **M2**：真状态机——`@await` 拆段进 `switch(t->state)`、活过挂起点的局部提升进 frame（仿 @try 的 TRY_LIFT 提升先例）、break/continue → 状态跳转、ARC 在任务退出汇合点统一结算、bridge header wrapper
- **M3**：调度器 `nepa_run_all` + 任务图（`parent` 字段已预留）/ I/O

## 负例（tests/negative/ 见 async_sync_call.np）

`main` 里 `[f compute:1]`（compute 是非 void async）→ 
```
[async] main: cannot call suspending method 'compute:' from non-async context —
async methods with a return value must be awaited from another async method;
wrap the call in an async void entry method
```
