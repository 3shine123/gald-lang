# tests/eh_diff — 异常语义差分测试(gald vs 真 ObjC)

每个用例是一对文件:`X.gm`(gald 语义用例)+ `X.m`(**等价的真 ObjC 程序**)。
ObjC 侧由系统 clang `-fobjc-arc` 编译运行,作为该语义的**基准实现**;gald 侧用
`galdc run X.gm -eh checked` 走用户路径;两侧输出(NFLog/fprintf 都走 stderr)
逐行 diff,不一致即 FAIL。

## 用途

"异常语义与 ObjC 一致"不是口头保证,而是可回归的标准。checked 后端
(`crates/eh`,Swift 式旗标传播)的每条 ObjC 行为——跨帧释放、语句中断、
finally 顺序、rethrow、typed 链——都由这里的用例锁定。

## 运行

```bash
./run_eh_diff.sh                      # 全部用例
GALDC=target/release/galdc ./run_eh_diff.sh   # 指定二进制(默认 target/debug)
```

输出:`PASS/FAIL/KFAIL` 逐用例 + 汇总。`KFAIL` = `.gm` 头部标了
`KNOWN-FAIL` 的用例(对应功能未落地,预期 diff,不计失败);功能落地后
**删除 KNOWN-FAIL 标记**,转为真用例。

## 用例清单与语义依据

| 用例 | ObjC 语义(基准) |
|------|------------------|
| 01_cross_frame_release | 异常穿透中间帧时,该帧 ARC 管理的 owned 局部必须释放(sjlj 泄漏、checked 已对) |
| 02_stmt_interrupt | 同一表达式内 `@throw` 立即中断:同语句的后续调用不得执行(clang 与 gald codegen 均按左到右求值,用例锁定该行为;严格说 C 求值顺序未指定,若未来 codegen 右到左,两侧仍一致) |
| 03_nested_finally | 内层 `@finally` 在异常传播给外层 catch **之前**执行;外层 finally 在 catch 之后执行 |
| 04_rethrow | catch 内 `@throw e` 重抛到**外层**,不得重入本层 catch |
| 05_typed_chain | typed catch 按 isa 匹配,不匹配放行给外层 |
| 06_block_throw | block 字面量内 `@throw` 可传播到外层 @try(ObjC 允许;KNOWN-FAIL→block desugar 落地) |
| 07_uncaught | 未捕获异常:输出 `*** Terminating app due to uncaught exception ...` 后 abort(只比措辞行,栈回溯不可复现;KNOWN-FAIL→uncaught guard 落地) |

## 已知偏差(设计决定,非遗漏)

- **异常穿越纯 C 帧**:unwind 可以,checked 不能(旗标无人检查会静默吞)。
  对策:bridge header wrapper 加旗标检查 → 明确 abort 而非静默。纯 C 帧没有
  gald 对象,不存在清理问题,这是唯一与 ObjC 的行为差,记录于 AGENTS.md。
