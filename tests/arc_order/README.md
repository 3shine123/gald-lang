# ARC 求值顺序回归套件（`tests/arc_order/`）

> 修复：**ARC 在退出表达式（`return` / `@throw`）求值之前就释放了它要读取的局部变量**，
> 造成 use-after-free。同时修掉两个同族缺陷（过度跳过导致的泄漏、`@throw` 越界释放
> 导致的双释放）。

## 规则

> **所有表达式必须先完成求值，再执行离开作用域所需的 ARC release。**

三条落地规则：

| # | 规则 | 反例（修复前） |
|---|---|---|
| 1 | 退出表达式**读取**了某 owned 局部 → 先求值到临时变量，再 release，最后 return/throw | `return [[h text] length] - 5;` 先 `nupa_release(h)` → UAF |
| 2 | 所有权**转移**只认两类：裸局部 `return h;`，或显式的 `return nupa_autorelease(h);` / `return [h autorelease];` | `return [h text];` 曾被当作转移 → `h` 永不释放（泄漏） |
| 3 | `@throw` 只释放**最近 `@try` 边界以内**的局部；边界之外的局部在 handler 返回后仍然有效 | `@throw` 释放了函数级局部，函数尾再释放一次 → 双释放 |

规则 1 的 lowering：

```c
__auto_type __nupa_arc_ret_N = <完整求值的退出表达式>;   /* 只在表达式确实读局部时生成 */
nupa_release(<局部>);                                    /* 逆序，与既有 ARC 一致 */
return __nupa_arc_ret_N;                                 /* 或 @throw __nupa_arc_ret_N */
```

仅在必要时引入临时变量（表达式不读任何待释放局部时直接插 release，不加临时变量）。临时变量是**已算好值的普通副本**，不引入额外 retain/release。

被 block 字面量捕获的局部同样适用规则 1：`return blk() - 7;` 文本上没有出现 `h`，但
`blk` 捕获了 `h`，所以按"可能需要调用 block"处理，同样走临时变量 lowering。

## 用例（16 × 6 组合 = 96）

| # | 用例 | 断言 |
|---|---|---|
| 01 | `return [[h text] length] - 5;`（顶层 Binary 用局部） | rc=0 且 `dealloc` 恰好一次 |
| 02 | `return [w text];`（返回消息发送） | 局部被释放（dealloc 出现）且返回值有效 |
| 03 | `return [h offset] + [h offset] - 4;`（复杂二元，两次发送） | 同上 |
| 04 | 两个 owned 局部同一表达式 | 两个 dealloc，各一次 |
| 05 | `if (cond) { return ...; }` + 后续 `return` | 两条路径各释放一次；不因分支 return 而漏掉落出路径 |
| 06 | `@throw [[h reason] description];` | 先求值后释放；handler 捕获正常 |
| 07 | `@try { return ...; } @finally { ... }` | finally **执行**，且 order 为 finally → release → 返回 |
| 08 | block 捕获局部并在退出表达式里调用 | block 调用完成后才释放 |
| 09 | 单层跨作用域 `goto` | 离开的块内 owned 局部被释放 |
| 10 | 多层嵌套 `goto` | 两层**逆序**释放（内层先） |
| 11 | `goto` 从循环体内跳出到循环外 label | 每次迭代/跳转各释放一次 |
| 13 | `goto` 与 `@try/@finally` 组合 | 跳出内层块释放局部，`@finally` 仍执行 |
| 14 | owned ivar（两个 live + 一个 nil），**无用户 dealloc** | ARC 合成的 dealloc 逆序释放，nil 安全 |
| 15 | 同前但**有用户 dealloc 且手动释放 ivar** | 各释放**一次**（ARC 不叠加，避免 double free） |
| 16 | `@finally` 内**再次 throw** | 向**外**传播，不重入本层 catch |
| 17 | `@finally` 内 `return` | 覆盖 try 的返回值；finally 先跑，局部释放一次 |

（编号 12 未使用；`^int(void)` 的 parser 回归在 `tests/block_void_params_test.np`。）

组合：`{default, -eh checked, -eh legacy} × {ARC, MRC}`。

- ARC：stdout 必须等于 `.out` 且 exit 0
- MRC：只断言 exit 0（MRC 不注入 release，不打印 dealloc）
- `.np` 的 `NPLog` 输出走 **stderr**（`runtime.c`），runner 合并两个流后比对

```bash
NPAC=target/release/nupac ./tests/arc_order/run_arc_order.sh
```

另有 ASan 强验证（见下）。

## 修复位置

| 文件 | 改动 |
|---|---|
| `crates/arc/src/arc.rs` | 新增 `insert_exit_cleanup`（规则 1/2）、`transferred_var`（规则 2）、`collect_captures_*`（block 捕获感知）、`throw_floor`（规则 3）；移除 `clear_all` 与"return 前盲插 release" |
| `crates/eh/src/lib.rs` | checked 后端：`rewrite_try` 把 `@finally` 体 splice 到 try 体内每个 `return` 之前（新的 `splice_finally_before_exits`）；新增 `pub fn splice_finally_exits` 供 legacy 使用 |
| `crates/nupac/src/pipeline.rs` | legacy（sjlj）路径新增 `@finally` 早退 splice pass（Step 3.92，在 ARC 之前） |

### 为什么不重复释放

- 每个退出点各自插入清理，**按路径**执行：分支里的 `return` 只在其路径上释放；
  落出路径由作用域末尾释放。移除了旧的 `clear_all`（它会在分析层面"吞掉"后续路径的释放 → 泄漏）。
- 作用域以无条件退出语句（`return`/`@throw`）结尾时，作用域末尾**不再**插入（该路径已由退出点处理）。
- 退出点之后的语句不可达，停止分析（消除死代码与 `-trace-refcount` 噪声）。
- `@throw` 按 `throw_floor` 限定范围，避免与 handler 之后的作用域末尾释放撞车。

## 退出点之外的两类清理（09–15）

### `goto`（09–12）

C 的 label 是**函数级**的，`goto` 可以跳出任意嵌套块——跳过的那些作用域的 owned
局部原本无人释放（pattern-switch lowering 生成的正是这个形状：arm body 在嵌套
compound 里，`goto __nupa_swN_end` 跳出去）。

判定用**作用域路径**（从函数体到该语句列表的语句索引序列）而非单纯深度：

| 关系 | 处理 |
|---|---|
| label 路径是 goto 路径的**前缀** | 跳出：按逆序释放离开的作用域 |
| label 是其所在列表的**首条**语句 | 跳入块首，不会跳过初始化器（pattern-switch 形状）：放行，并按最近公共祖先释放 |
| 其它（跳入块中部 / 跳到兄弟块） | **拒绝编译**：`goto '...' jumps into a nested or sibling scope past its first statement` |

被释放的变量**不**从作用域登记中移除：其它未走这条 goto 的路径仍会在作用域末尾释放它们，因此每条路径恰好释放一次。

负例：`tests/negative/arc_goto_into_scope.np`。

### owned ivar（14–15）

规则（比 ObjC 窄，刻意为之）：

| 类的情况 | 行为 |
|---|---|
| **没有**自定义 `dealloc` | ARC 合成 dealloc（wrapper）：先链到父类 dealloc 入口，再**逆序**释放本类的 owned ivar |
| **有**自定义 `dealloc` | ARC 不介入——body 可能已经手动释放了 ivar（`NPObject_release(_x)` / `[_x release]`），叠加就是 double free |

判定 owned ivar：仅**单级**对象指针（`NPObject **` 这类 C 数组排除——它曾导致
`NPArray._items` 被当作对象释放而崩溃），排除 weak / 函数指针 / block / C 标量指针；
`id` 计入。释放用 `nupa_release`（nil 安全，部分初始化对象安全）。

判定「自定义 dealloc」必须看 `method_owners == 本类`——`method_names` 也含**继承**条目，
否则任何 `NPObject` 子类都会被当作"写了 dealloc"而失去合成。

**MRC（`-fno-nupa-arc`）下完全不生成**（`CgUnit.no_arc` 门控）：手写保留计数的程序里
ivar 归程序员所有。

## 修复位置（汇总）

| 文件 | 改动 |
|---|---|
| `crates/arc/src/arc.rs` | `insert_exit_cleanup`（退出表达式先求值后清理）、`transferred_var`（所有权转移只认裸局部/autorelease）、`collect_captures_*`（block 捕获感知）、`throw_floor`（`@throw` 按最近 `@try` 限界）、`build_goto_plan` + `walk_labels_gotos`（`goto` 作用域路径分析）、移除 `clear_all` |
| `crates/eh/src/lib.rs` | `rewrite_try` 把 `@finally` 体 splice 到 try 体内每个 `return` 前（`splice_finally_before_exits`）；`pub fn splice_finally_exits` 供 legacy 使用 |
| `crates/nupac/src/pipeline.rs` | legacy 的 `@finally` 早退 splice pass（Step 3.92）；ARC 错误上报 |
| `crates/codegen/src/codegen.rs` | `emit_arc_dealloc_wrappers`（合成 ivar 释放）、`owned_ivars_of` / `is_owned_object_ivar_type`、`CgUnit.no_arc` |
| `crates/parser/src/parser.rs` | block 字面量参数表中的无名 `void` 视为空参数（`^int(void)` ≡ `^int()`） |

## 已知缺口（仍开放）

1. `-trace-refcount` 是**静态**模拟：同一对象在互斥路径上各释放一次会被它累加报
   `double-release`。判真实重复释放请以 **ASan + 实际运行**为准。

## ASan 强验证

```bash
for c in tests/arc_order/[0-9][0-9]_*.np; do
  ./target/release/nupac -rewrite-nupa -o /tmp/a.c "$c"
  clang -fsanitize=address -fblocks -w -I include -o /tmp/a /tmp/a.c include/nupa/runtime.c
  /tmp/a
done
```

结果：8/8 clean（无 heap-use-after-free、无 double-free）。修复前 `01`/`03`/`04`/`05`/`06`/`07`/`08`
在 `-eh legacy` 下报 UAF，`08` 在全部模式上报 UAF。

> ⚠️ `-trace-refcount` 是**静态**模拟，在"同一对象在互斥路径上各释放一次"的写法下会
> 报 `double-release`（把各路径累加）。判断真实重复释放请以 **ASan + 实际运行**为准，
> 不要只看 trace。
