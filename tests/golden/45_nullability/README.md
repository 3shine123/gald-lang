# golden/45 — Nullability（`nullable` / `nonnull` / `NP_ASSUME_NONNULL`）

**特性**：ObjC 2015 年最大的语言演进在 Jeti 落地。三态标注 + 区域默认 + 分支内流敏感窄化，**纯编译期、零运行时成本**（codegen 从不读 `nulls` 字段 —— 与 ObjC 的 `NS_ASSUME_NONNULL` 同构）。

## 语法：两种拼写都支持

```objc
- (void)f:(nonnull NPString *)s;        // 前置（ObjC 宏展开后的形态）
- (void)g:(NPString * _Nonnull)s;       // 后置（clang 头文件里的形态）
- (nullable NPString *)maybe;
@property (nonatomic, copy, nullable) NPString *title;   // 属性位
NP_ASSUME_NONNULL_BEGIN               // 区域默认：未标注指针 → nonnull
- (void)h:(NPString *)s;
NP_ASSUME_NONNULL_END
```

| 态 | 拼写 | 含义 |
|---|---|---|
| nonnull | `nonnull` / `_Nonnull` | 承诺非 nil，传 nil 是 bug |
| nullable | `nullable` / `_Nullable` | 显式可为 nil，调用方**必须**处理 |
| unspecified | 不写 / `_Null_unspecified` | 未表态（与显式 unspecified 是两件事，故有第四态） |

**为什么必须两套拼写**：ObjC 的 `nullable` 是宏（展开成 `_Nullable`），而 clang **自己的头文件**大量用后置写法。后置不是可选糖 —— 当基类型是宏展开时它是唯一可用的写法。

## 关键设计决策

### 1. 上下文关键词，**不进 KW_TABLE**

硬关键词会让 `int nullable = 5;` 变成语法错误 —— **违反 C 超集铁律**（AGENTS.md）。与项目既有的 `in` / `when` / `copy` / `retain` 同一套处理。

**歧义消解（两 token 前瞻，纯形状、不查类型表）**：`nullable` / `nonnull` 在两个位置可以是**名字**而非注解：
- 声明符位：`int nullable = 5;`（词后紧跟 `=` / `;` / `,`）
- 类型位：`nullable foo = 5;`（词 + 名 + `=`，需两 token 前瞻）

⚠️ **刻意不查类型表** —— 类型可以在文件后面才声明（前向引用），要求 `is_type_name` 会让那些注解静默失效。
⚠️ 前瞻必须**同时保存/还原 `previous`**：`advance()` 会覆盖它，污染后续所有 `previous_text()` 调用方（声明符名、`@selector`…）。

属性位 `@property (nullable)` 单独在 `parse_property` 里按文本匹配 —— 括号内不可能有声明，无歧义。

### 2. 指针包装层必须**携带注解上浮**

`nullable NPString *s` 把注解解析到内层 named type，但 checker 读的是**最外层**类型。首版没上浮 → 85 次诊断钩子调用中 `param.nulls` 全是 `Unspecified`，诊断静默失效。指针与 block 两个包装层都要 `ptr.nulls = t.nulls`。

### 3. `NP_ASSUME_NONNULL_*` 是**真语法，不是宏**

ObjC 那两个标记是宏（内部 `_Pragma("clang assume_nonnull begin")`），**jetic 明确拒绝宏体内的 `_Pragma`**（`crates/cpp` —— 源级展开器没有调用点位置可放）。所以标记只能在 jetic 侧实现。

**规划时的一个错误更正**：原写"原样透传给 clang 让它生效"是错的 —— jetic 自己应用默认值，裸标识符到 C 侧会让 clang 报未声明。正确做法是**改写成 clang pragma**（`_Pragma("clang assume_nonnull begin")`，走既有 `RawLine` 通道），两侧同时生效。

### 4. 区域**按文件作用域**（实测修掉的真 bug）

parser 读的是**一个内联缓冲区**（`grep -c source_map crates/parser/src/parser.rs` = 0），没有 `#include` 边界。首版区域跨 `#import` 泄漏：导入头里未标注的 `NPString *s` 静默变 nonnull。

修法：注入 `SourceMap`，记录区域开启所在文件，`region_applies_at(line)` 只在同文件内生效。

**这一条是实测出来的，不是推演出来的** —— 探针经历三轮才拿到可信结论：
1. 第一版探针用 `setObject:forKey:` → 参数是泛型 `K`/`V` 非指针，区域默认压根不碰 → 测不到东西
2. 第二版用 `addObject:` → 同类问题 + `T` 是 `Param`
3. 第三版自建头文件、用**普通指针**参数、且**无 `@implementation`**（排除"后声明覆盖签名"的混淆）→ 立刻测出泄漏

⚠️ 同时修掉一个混淆项：`collect_signatures` 按 selector 键控，**后声明覆盖前声明**。所以区域只包住 `@interface` 而 `@implementation` 在区外时，诊断会失效 —— 实测确认（把 impl 移入区域后报错正常触发）。

## 流敏感窄化（M1 最简版）

```objc
if (s) { [sink need:s]; }              // then 分支
if (!s) { return; } [sink need:s];     // 早退窄化 ← 最常见写法
if (s != nil) { ... }                  // 显式 != nil（nil/NULL/0/NO 都算）
```

- 只做**分支内 + 早退**；窄化集合用**作用域栈**（`narrowed: Vec<HashSet<String>>`），不能漏出分支
- `exits_unconditionally` **刻意不含 `break`/`continue`** —— 它们只退出内层循环/switch，要判对得知道外层是什么，而**错的窄化会掩盖真 nil 解引用**，方向最危险
- M1 不做：循环内窄化、复杂控制流合并（保守取交集，与 ARC `RefVal` 的 `Unknown` 哲学一致）

## 诊断清单

| 诊断 | 触发 | 档位 |
|---|---|---|
| `nullable value passed to nonnull parameter` | `nullable` 实参传给 `nonnull` 形参 | **error** |
| `NP_ASSUME_NONNULL_BEGIN without a matching …_END` | 区域未闭合（否则后续所有指针静默变 nonnull） | error |
| `NP_ASSUME_NONNULL_BEGIN inside an existing nonnull region` | 重复开启 | error |

**只报"可证"的情况**（`arg_nullability` 只读变量/强转/消息发送三种形态的注解），其余一律不报 —— 避免洪水，与 ObjC `-Wnullable-to-nonnull-conversion` 同档。

⚠️ 诊断文案里出现的是**类型名**（`'NPString'`）而非形参名（`'v'`）—— checker 读的 AST 类型节点上没带参数名。

## ARC 交互（豁免名单已落地）

`jeti_release`（runtime.c:222）/ `jeti_retain`（:214）/ `jeti_autorelease`（:246）的 C 契约都是 nil 安全（`if (!obj) return;` / `return NULL;`）。checker 的 `NIL_SAFE_RUNTIME_FNS` 白名单把这一契约**显式化**：这三个函数的调用不做 nullable→nonnull 传递检查。

危害探针实证过为何必须豁免：若一个 jeti 可见声明把 `jeti_release` 标成 `nonnull`，每个含 nullable 局部的 ARC 程序都会编译失败——ARC 注入的 scope-end release 传的值本就可能为 nil，这正是文档化的调用约定（release-before-nil 也是正常 MRC 惯用法）。豁免按**函数名**生效，同时覆盖 ARC 注入与手写调用。

## M2 补齐（原 M1 限制，已解决）

- ✅ **out-param**：`NPError * _Nullable *`——后置注解跟随它前面的星号，标**中间层**（错误对象 nullable；out 指针本身由被调方写入，必然有效）。**前缀注解 + 多级指针被拒绝**：clang 同款拒绝（实测 `_Nonnull Widget * *` → "nullability specifier cannot be applied to non-pointer type 'Widget'"——它拒绝猜层级），报错指引改用后置拼写。配套 Foundation 新增 **`NPError` 类**（`include/Foundation/NPError.{nh,np}`：code/domain/userInfo/localizedDescription + `parseErrorWithMessage:` / `fileIOErrorWithMessage:` 工厂）。
- ✅ **block 参数/返回**：三条解析路径（block 类型参数、block 字面量返回、typedef）+ checker 调用点检查。⚠️ 实测坑：**typedef 形式的变量类型只是别名**（`is_block` 为假），必须经 `typedef_blocks` 表解析才能查——不补这条，Foundation 风格（typedef 块）代码几乎全漏。⚠️ 另一实测坑：`T * _Nullable s` 里的**下划线保留字豁免**是关键——无豁免时歧义守卫把 `_Nullable` 误读成"类型名"、`s` 误读成声明符，注解被静默丢弃（该豁免在修 `int nullable = 5;` 回归时曾被误删，CST 断言抓回）。
- ✅ **nil 字面量传 nonnull**：静态已知 null，比 maybe-nil 更严重，直接报 error（clang `-Wnonnull` 同款行为，已对 SDK 实证）。只判指针位（`is_pointer` / id / instancetype / Class）。
- 仍不做：值域解引用检查（nullable 返回值直接当指针用）—— ObjC 至今也不做
- 无跨 TU 传播（纯编译期，不影响 vtable 布局，`__sig` 不变 ✅）

## 测试

- `nullability.jeti`（17 段）—— 正例：两种拼写 × 各落点、属性位、区域默认/例外/`_Null_unspecified` opt-out、6 种窄化形态、nullable 返回值流入局部、**NPError out-param（双指针中间层）、带注解 block（typedef + 字面量 × 两种拼写）、ARC 释放 nullable 局部（nil-safe 白名单）**。`.out` 验证：重跑 STABLE、ARC/MRC 输出 SAME。
- `tests/nullable_c_superset_test.jeti` —— **C 超集守护**：`int nullable = 5;` / `struct nullable_s { int nonnull; }` / 形参名 / typedef 名 / 嵌套作用域遮蔽
- `crates/parser/tests/nullability_probe.rs`（**16 项**）—— CST 层精确断言（含双指针层级、双指针前缀拒绝、block 参数/返回两种拼写）。**端到端跑通证明不了任何事**：codegen 从不读 `nulls`，注解丢了照样编译运行
- 负例（均 exit=1，头注释记录期望报错文案）：
  - `tests/negative/null_to_nonnull.jeti` —— nullable 变量传 nonnull 参数
  - `tests/negative/block_nullable_to_nonnull.jeti` —— nullable 传 **typedef 块**的 nonnull 参数
  - `tests/negative/null_literal_to_nonnull.jeti` —— 字面量 nil 传 nonnull
- `tests/negative/` 已在 `test_all.py` glob 排除（否则被当普通 FAIL）
