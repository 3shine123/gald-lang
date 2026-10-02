# 稳定 vtable 槽位（3b）实施计划

> 状态：**待实施**。验收用例已入库：`tests/multi_tu/11_vtable_private_slots/`

## 1. 问题

Gald 为每个 TU 生成**一个 uniform `struct gald_vtable`**，字段是「该 TU 见过的全部实例方法」，
无 manifest 时按**字母序**排列（`crates/codegen/src/codegen.rs` 的 `None => sort()` 分支）。

于是两个 TU 只要方法集不同，布局就不同：

```
lib  : 43 字段  sig=0xedd6fb9b1c8b2ce1
main : 43 字段  sig=0x07f3ae69bac7647b
gald: fatal: vtable layout mismatch across translation units.
```

而链接器会把两个 vtable 实例 **weak 合并成一份**，所以一旦 sig 校验被绕过（或场景更隐蔽），
派发就会读到错误的槽位。当前靠 `gald_verify_vtable_sig` **大声失败**兜底 —— 这是安全网，
不是解药：**合法**的两 TU 分工也会被拒。

复现（已入库）：

* `lib.gm` 实现 `Widget` + 一个头文件没声明的私有方法 `privateHelper`；
* `main.gm` 只通过 `model.gh` 调用 `Widget`，并定义自己的类 `App`。

两者对 `Widget` 的公共认知完全一致，却被判为布局冲突。

## 2. 目标

**让 vtable 的「公共部分」在所有 TU 中布局一致；私有方法只影响自己 TU 的尾部槽位。**

验收标准：

1. `tests/multi_tu/11_vtable_private_slots/` 正常运行并输出 `run=42`（届时删除该目录的
   `EXPECT_FAIL` 与 `EXPECT_FAIL_MATCH`）；
2. `tests/multi_tu/09_layout_mismatch/` **继续** `EXPECT_FAIL` —— 两个 TU 各自实现同一个类且
   方法集不同，那是真正非法的用法，必须继续大声报错；
3. 全部门槛不回归：`test_all` / `eh_matrix` / `arc_order` / `eh_diff` / trace / `clang_gcc_stress`。

## 3. 三条规则

### R1 —— 字段顺序：公共段 + 私有段

```text
[公共段] 来自 #import 展开（即头文件）声明的方法，字母序
[私有段] 仅在本 TU 主文件里出现的方法，字母序（追加在尾部）
```

私有段在尾部是关键：**公共段内任意方法的 offset 在两侧 TU 中完全一致**。
（manifest 模式下已有同样的规则：“Methods unknown to the manifest … are appended at the end”。）

### R2 —— 实例归属：谁实现，谁生成

`GALD_VTABLE_$_X`、`GALD_CLASS_$_X` 等元数据只由**本 TU 拥有该类 `@implementation`** 的一方生成，
其余 TU 只发 `extern` 声明。

否则：lib 的 `Widget` 实例（41 + `privateHelper`）会被 main 生成的实例（41 个字段）weak 合并覆盖，
lib 自己的私有方法调用就会越界。

### R3 —— 签名校验面向公共段

`gald_verify_vtable_sig` 比较的应是**公共段**（本 TU 见到的声明集）的签名，
允许尾部私有段存在差异。这样 R1 + R2 生效后，校验从“拦合法场景”变回“抓真非法场景”。

## 4. 为什么这三条足够

| 场景 | 结果 |
|---|---|
| main 调用 `Widget.show`（公共） | offset 在公共段内，两侧一致 ✓ |
| lib 调用自己的 `privateHelper` | 走 lib 生成的 `Widget` 实例，私有段是自己写的 ✓ |
| main 调用 `App.run` | 走 main 生成的 `App` 实例 ✓，别人不碰 |
| 两个 TU 都实现同一个类且方法不同（09 用例） | 仍是非法：R2 会让两边都定义实例 → 链接器报冲突，或 R3 判定公共段不符 → 继续大声失败 ✓ |

## 5. 分步实施

| 步 | 内容 | 验证方式 |
|---|---|---|
| **A** | pipeline 收集「公共方法集」（来自导入文件的方法声明，用 `source_map` 判定），传入 codegen | 单元可读：打印集合内容 |
| **B** | R1：`None` 分支改为「先字母序 sort，再稳定地把私有段排到尾部」 | 对比两个 TU 的 `struct gald_vtable`：公共段 offset 应一致 |
| **C** | R2：元数据 / vtable 实例只由实现该类的 TU 生成 | `11_vtable_private_slots` 应能跑通（sig 校验此时可能仍需 D） |
| **D** | R3：sig 校验改为公共段 | 11 用例输出 `run=42`；09 用例仍 EXPECT_FAIL |
| **E** | 删除 11 用例的 EXPECT_FAIL 标记，跑全套回归 | 全部门槛绿 |

## 6. 风险与回退

* **风险点**：R2 触及元数据生成条件（`GALD_VTABLE_$_X`、`GALD_CLASS_$_X`、`GALD_GETCLASS_$_X`），
  import 链（diamond、category）下的“谁拥有实现”判定需要仔细处理。
* **回退**：每步独立提交，`git reset --hard <上一步>` 即可；`11_vtable_private_slots` 的
  `EXPECT_FAIL` 标记本身就是“尚未完成”的断言，任何一步倒退都会让它继续 PASS（failed as expected），
  不会静默变成假绿。
* **不要**顺手去动 `weak` 的默认策略（那是 P3 的话题）；本计划只解决布局一致性。

## 7. C 的探索结论（第一次尝试，2026-10-02，已回退）

A+B 完成后动手做 C，试了两条路，都只走到一半：

1. **私有方法不进 vtable** —— 有效，且比"公共段 + 私有段分区"更干净。实测两个 TU 的
   `struct gald_vtable` 字段**从"73 相同 + 1 不同"变成 73 = 73 完全一致**，私有方法改为
   直接调用 `Owner_method(recv, sel, ...)`（只有定义它的 TU 能走到这里，所以不需要槽位）。
   这条规则本身应当保留。

2. **vtable 实例改 `extern`**（只让实现类的 TU 生成实例）—— 方向正确但**不够**：
   编译通过、sig 冲突消失，运行时段错误。原因是**只看到声明的 TU 仍会生成
   `GALD_META_VTABLE_$_X_inst`（类方法派发用）和 `GALD_CLASS_$_X` 的空实例**，其 NULL 槽
   可能在 weak 合并中胜出。

**卡点**：类元数据的生成分散在 **6 个 `for cm in &unit.classes` 循环**里
（vtable / meta vtable / class / getclass / 前置声明 …），要做 "实例归属" 就得让它们全部
遵循同一条件，并配套一片 `extern` 声明区。改动面比预估大，且中途无法保证不引入回归，
因此**回退**（`git checkout` 掉未提交的改动；A+B 已在 `255b322` 里）。

**方向性发现（重要）**：本计划原以为"3b 是 P3 的地基"，但 C 的尝试说明**依赖可能是反的**：

* 在**自包含模式**下，Foundation 的实现被每个 TU 展开，于是"这个类归谁实现"根本无从判断
  （每个 TU 都"实现"了 NFString）——这正是 C 卡住的根源；
* 若先做 **P3（预编译库 + 纯声明头）**，用户 TU 里就**只有自己的 `@implementation`**，
  "实例归属"就退化成简单的"用户 vs 库"两分，C 会容易得多。

**建议的下一步顺序**：A+B（已完成）→ **P3**（预编译 Foundation 库 + 纯声明头）→ 回到 C
（此时只需处理"用户自己的类"，元数据归属自然清晰）。

在 C 完成之前，`11_vtable_private_slots` 保持 `EXPECT_FAIL`：它是"尚未完成"的断言，
任何回退都会让它继续以"预期失败"通过，不会静默变成假绿。

## 8. P3 进展（2026-10-02）

### 已完成

* **`include/Foundation/Foundation.decl.gh`** —— 纯声明头（14 行）。只需剥掉
  `Foundation.gh` 里的 9 行 `#import "*.gm"`：**其它 `.gh` 本来就是纯声明**，所以这一步是
  删 9 行，不是重新组织头文件。
* **库构建已验证**（86K）：

  ```bash
  galdc -rewrite-gald -fstrong-metadata include/Foundation/Foundation.gh -o Foundation.c -I include   # 3175 行
  clang -c Foundation.c -o Foundation.o -I include
  ar rcs libgaldfoundation.a Foundation.o
  ```

  ⚠️ 建库**必须**带 `-fstrong-metadata`（P3 落地时新增）：否则库里的元数据仍是
  `weak`，客户 TU 的空桩会在链接期胜出 → 段错误（详见下方「剩余一步」）。

* **实测效果**（`#import <Foundation/Foundation.decl.gh>` + 链接库）：

  | | 自包含模式（现状） | P3 模式 |
  |---|---|---|
  | 生成的 C | 含 ~3000 行 Foundation 实现 | **2085 行总计** |
  | `__attribute__((weak))` | 193 | **36** |

  也就是说：**你要的"生成代码干净"已经达成**，剩下的只是让它也能正确运行。

### 剩余一步 —— 已完成 ✅（2026-10-02，第二趟）

**问题**：用户 TU 只看到声明，所以它仍然生成 `GALD_VTABLE_$_NFString` 之类的**空桩**；
链接器的 weak 合并可能选中空桩而不是库里的真表，于是 `[s length]` 走空指针 → 段错误（rc=139）。

**实现**：

1. **CLI 标志 `-fstrong-metadata`** —— `crates/galdc/src/main.rs`（手动解析两处 + clap 补全
   定义 + `DOUBLE_TO_SINGLE` + 帮助文本），透传 `Pipeline::strong_metadata` →
   `CgUnit::strong_metadata`。
2. **codegen 元数据 emit 条件化**（`crates/codegen/src/codegen.rs`，`emit_unit_with_headers`
   内的一个 `meta_weak` 前缀）：默认仍是 `__attribute__((weak))`，`-fstrong-metadata` 时去掉：
   * `GALD_VTABLE_$_X` 实例（vtable 循环）
   * `GALD_META_VTABLE_$_X_inst`（meta-vtable 循环）
   * `GALD_GETCLASS_$_X`（getClass 循环）
   * `gald_metaInit` / `gald_meta_init`
   * （`GALD_CLASS_$_X` 本身是 tentative definition —— common 符号，天然合并；其**内容**由
     `gald_metaInit` 写入，所以把 `gald_metaInit` 变强就够了，变量无需改动。）
3. **`collect_public_methods` 判定改为「在 `@interface` 中声明」**
   （`crates/galdc/src/pipeline.rs`，即 `is_implementation: false`）。
   * **保留了「来自导入文件」的过滤**：若一并去掉，主文件自己的 `@interface`（如
     `11_vtable_private_slots` 里的 `App`）会挤进公共段，反而把公共方法错位。文档原话
     「不依赖 source_map」不准确，实测必须两者同时成立。
   * 原先两侧布局不一致的真正来源是 `NFError.localizedDescription`：它只在导入的
     `@implementation`（`.gm`）里出现，库 TU 当成公共、客户 TU 当成私有 → 布局分叉。
     改判后两侧的公共集完全一致（sig 相等）。

### 验证（P3 端到端）

```text
sig：库 TU == 客户 TU（0xbe99f65a63e20021）
弱符号库（不加 -fstrong-metadata）：rc=139（空桩胜出 → 段错误）
强符号库（-fstrong-metadata）：len=5 s=hello，rc=0   ✅
```

回归门槛：`test_all` 343/351（0 failed）、`cargo test --workspace` 149/149、
`tests/multi_tu/run_multi_tu.sh` 11/11（`09`、`11` 仍 `EXPECT_FAIL`，未假绿）。

> 工具：`Foundation.decl.gh` 引用的两个脚本已补入库
> （`tools/make-decl-headers.sh`、`tools/build-foundation-lib.sh`）。建库**统一走脚本**——
> 它自带 `-fstrong-metadata`，并用 `nm` 校验元数据的确是强符号（弱符号会让守卫直接失败）。
> 保护测试：`tests/strong_metadata/run_strong_metadata_test.sh`（强符号 / 弱客户端链接 /
> 双强符号必须 duplicate symbol 三项）。
>
> 顺带修掉一个与本计划无关的既有 ARC 缺陷：`full_syntax_test.gm` 的 intern 字符串
> heap-use-after-free（`test_all` 里那唯一一个 `SUSPECT` 来源）→ 已修复，见
> `doc/arc_intern_uaf.md`，回归守护 `tests/arc_intern/`。
