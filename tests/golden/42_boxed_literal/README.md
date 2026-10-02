# golden/42 — 装箱字面量（`@(expr)` / `@YES` / `@NO` / `@'c'`）

**特性**：`@` 字面量族补全到与 ObjC 对齐。此前只有 `@123` / `@1.5` / `@"..."` 三个 token 在 lexer 里，parser 只接了字面量数值与字符串两种——`@(x+1)`、`@YES`、`@NO`、`@'c'` 只到 lexer 就断，实测报 `expected ';' after declaration (got '@(')` / `(got boxed)`。

**用例**（`boxed_literal.np`，7 段）：

| 段 | 覆盖 | 要点 |
|----|------|------|
| 1 | `@123` / `@1.5` | 字面量自身类型选定工厂 |
| 2 | `@(i + 1)` / `@(d * 2.0)` / `@(c + 0)` | 工厂由**操作数静态类型**决定；`char + int` 提升为 int |
| 3 | `@YES` / `@NO` | BOOL 归一化成 0/1 |
| 4 | `@'A'` 与 `@(c)` | `charValue` / `intValue` 往返 |
| 5 | `[@(i * 2) intValue]` | 装箱结果直接作接收者（链式派发） |
| 6 | `@[ @1, @(i + 1), @YES ]` | 字面量数组；`(NPNumber *)[arr objectAtIndex:0]` 强转接消息发送 |
| 7 | `isEqualToNumber:` | `@(i+1)` 与 `@42`、`@YES` 与 `@(1==1)` 值相等 |

**为什么改写落在 checker 而不是 parser**：parser 没有类型；后端是 C99，没有 `_Generic` 可用。checker 按操作数静态类型选工厂（`double`/`float` → `numberWithDouble:`、`BOOL` → `numberWithBool:`、`char` → `numberWithChar:`、`long`/`long long` → `numberWithLongLong:`、其余整数 → `numberWithInt:`），复用普通消息发送节点——下游静态派发、nil 守卫、SEL 常量**零特判**（与对象下标、struct `==` 同一套机制）。

**非算术类型拒绝装箱**（负例 `tests/negative/boxed_illegal_type.np`）：

```
illegal type 'NPString *' in a boxed expression — '@(...)' accepts arithmetic and BOOL values only
```

ObjC 也有这条 "illegal type in boxed expression"；Nupa 无字符串装箱，静默把指针当数值发出去会掩盖错误。

**连带修掉的真 bug**：checker 的 `ArrayLit` 臂只返回 `id`、**从不遍历元素** → `@[ @(i + 1) ]` 里那个 `@(expr)` 永远拿不到 `expr_type`、不被改写，生成 C 把**裸 int 混进对象数组**。编译通过、运行期才炸（exit=1，前几段输出正常后静默退出，极具迷惑性）。修法：`ArrayLit` 臂逐元素 `check_expr`。

**确定性与内存管理**：本特性与内存管理模式无关，ARC 与 `-fno-nupa-arc` 输出逐字节相同。
