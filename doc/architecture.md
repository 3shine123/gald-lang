# Nopa 当前架构

> 本文是当前架构的**唯一事实来源**，面向新贡献者。
> 历史决策与迁移过程见：`doc/stable_slots_plan.md`（stable slots / multi-TU 迁移史）、`doc/arc_intern_uaf.md`（ARC intern UAF 修复记录）。
> 用户向文档见 `README.md` / `CHINESE.md`。

## 1. 编译流程

Nopa 是 Objective-C 风格语法转译到 C99 的静态语言。`nopac` 的流水线（Rust，`crates/`）：

```text
.np/.nh 源文件
  → 预处理（#import 内联、C 轨道分离）
  → lexer / parser → CST
  → elaborator（@interface/impl 匹配、协议合并）
  → binder（符号表、selector 注册、协议方法传播）
  → 各降级 pass（eh desugar → defer splice → pattern-switch → ARC 注入 → checker）
  → codegen → C99
  → C 编译器（clang/gcc）→ .o → 链接 libnopa.a (+ libnopafoundation.a)
```

要点：

- **无运行时消息派发**——没有 `objc_msgSend`，全部方法是静态 vtable 派发，选择子在编译期解析为槽位索引。
- checker 默认开启（协议一致性、类型兼容、格式串检查等）；`-fno-checker` 可关。
- EH 默认后端 `checked`（见 §9）。
- 链接期自动带上 `libnopa.a`（运行时）与 `libnopafoundation.a`（若找到，见 §7）。

## 2. `.nh` / `.np` 语义

| 扩展名 | 角色 | 说明 |
|---|---|---|
| `.nh` | Nopa 头文件 | 声明：`@interface`、`@protocol`、typedef、struct。只经 `#import` 内联，从不直接编译。 |
| `.np` | Nopa 实现文件 | 定义：`@implementation`、函数、`int main`。两种用法：① **独立 translation unit 编译**（multi-TU 模式）；② 在 self-contained 模式下被 `#import` **内联**进主文件。 |

`#import` 只对 `.nh`/`.np` 做 nopa 内联；`#include` 的 `.h`/`.c` 原样透传给 C 编译器。nopac 总是定义 `__NOPA__` 宏，供需要同时被纯 C 编译器使用的头文件做 `#ifdef` 分支。

`.nh` 头文件在 multi-TU 模式下**必须保留完整 struct 布局**（ivar 区）——因为 Nopa 是 C 超集，`@public` ivar 直访、struct 字段访问、成员函数指针调用都是合法用户代码，客户端 TU 需要完整布局，不能把 opaque struct 当默认规则。

## 3. self-contained 与 multi-TU 的区别

### self-contained / unity mode

```nopa
#import <Foundation/Foundation.np>   // 自包含伞头：声明 + 实现全部内联
```

- Foundation 的实现被 `#import` 内联进你的 TU，**无需任何库**。
- 内联进来的实现**不是 main 文件**，其类元数据是**弱符号**（R2 规则，见 §4）。
- 适合单文件、快速试验、兼容旧构建方式。缺点：每个使用 Foundation 的 TU 都复制一份实现，编译慢、体积大。

### precompiled Foundation / multi-TU mode（推荐工程模式）

```nopa
#import <Foundation/Foundation.nh>   // 纯声明伞头：零实现内联
```

```bash
./tools/build-foundation-lib.sh                    # 构建一次 → libnopafoundation.a
nopac app.np -o app                                # nopac 自动找到并链接库
# 或显式：
nopac app.np -I include -L target/foundation -lnopafoundation -o app
```

- Foundation 只在库里存一份；你的 TU 只生成自己的代码 + 弱占位元数据。
- 多个 TU 时用多输入：`nopac main.np lib.np -o app`（`.o`/`.a` 也可直接作输入）。
- auto-link 规则：仅**纯声明客户端**自动链库；TU 内联了 Foundation 实现（`Foundation.np`，直接或经导入的 `.nh` 传递）时跳过，避免库的强 vtable 赢得弱合并而触发 `__sig` 中止。

## 4. owner / strong / weak metadata 规则（R2）

「owner」= **该类的 `@implementation` 位于本 TU 的 main 文件**（不是经 `#import` 带进来的）。

| TU 角色 | 生成的类元数据（vtable / meta vtable / `NOPA_CLASS_$_X` / getClass） |
|---|---|
| owner TU | **强符号**（真实表） |
| 纯声明客户端 / 内联方 | **弱桩**（槽位保留，未实现的方法槽为 NULL） |

后果：

- 两个 TU 都在各自 main 文件实现同一个类 → **链接期 duplicate symbol**（这是故意的、清晰的错误；不是运行期错位）。
- 链接时库/owner 的强表胜出，客户端弱桩被合并掉——这就是"纯声明客户端保持弱，正是正确"的原因。
- **不需要任何手动旗标**：`-fstrong-metadata` 已从编译器删除，归属由"谁的 main 文件持有 `@implementation`"自动推导。

## 5. vtable 公共段、私有段与 `__sig`（R1/R3，已完成）

每个类一个 uniform `struct nopa_vtable`（函数指针数组），布局分两段：

```text
[公共段] 该类在 #import 展开后（即共享 .nh）声明的方法，字母序
[私有段] 仅在本 TU main 文件出现的方法，字母序，追加在尾部
```

- **R1**（字段顺序）：公共段内任意方法的槽位偏移在所有 TU 中一致；私有段只影响自己 TU 的尾部。
- **私有方法**只能在 owner TU 内派发（跨 TU 没有槽位）——把要跨 TU 调的方法写进共享 `.nh`。
- **R3**（`__sig` 校验）：启动期签名校验只覆盖**公共段**（本 TU 见到的声明集），允许尾部私有段差异。签名不一致（例如某 TU 的 `.nh` 过期）会大声 abort 并打印双方方法集，而不是静默读错槽位。
- **R2**（实例归属）见 §4。三条均已落地并由 `tests/multi_tu/`（12 用例）守护。

## 6. class metadata：静态初始化

- owner TU 通过**静态 `NFClass` 定义初始化**类元数据（编译期常量，`__data` 段），不依赖运行期构造调用。
- `nopa_metaInit` 保留为**兼容路径的幂等回填**：仅 self-contained 单 TU 构建（`#import "*.np"` 伞形内联）中，弱合并后真正被执行的那份会补齐静态初始化没覆盖的部分。正常 owner / multi-TU 路径**不依赖**它。

## 7. Foundation 构建方式

预编译库由 `tools/build-foundation-lib.sh` 以**逐 TU wrapper** 方式构建：

1. 对每个 Foundation 类实现（`NFObject.np` … `NFError.np`），脚本生成一个 wrapper TU = `#import <Foundation/Foundation.nh>`（声明面）+ 该 `.np` 全文（实现）；
2. 逐个转译、C 编译成 `.o`——每个 wrapper 的 main 文件就是该 `.np` 本身，所以 R2 判定每个类都是 owner，元数据为**强符号**；
3. `ar` 归档为 `libnopafoundation.a`，`nm` 校验强符号（9/9）。

细节：伞头 `Foundation.np` 不是类，被构建脚本显式跳过（归档它会重复内联全部实现）；库经 nopac auto-link 自动链接（查找顺序：二进制旁 → `target/foundation` → `/opt/nopa/lib` → `/usr/local/lib/nopa` → 用户 `-L`），仅对纯声明客户端生效；`-ffreestanding`、shared 模式、显式 `-lnopafoundation` 均抑制自动链接。

## 8. 泛型：单态化为主，裸拼写擦除兼容

- **主模型是编译期单态化**：`Box<T>` / `NFArray<T>` / `NFDictionary<K,V>` 等泛型类按使用点实例化（专属结构体、方法副本、vtable/元数据，`T` 以 `TypePrim::Param` 哨兵按参数名替换）；checker 把接收者的 `type_args` 代入方法签名做元素类型检查。
- **裸容器拼写保留擦除兼容行为**：不带实参的 `NFArray`（无 `<...>`）按 `id` 擦除处理，行为与历史版本一致、零迁移；裸 → 特化赋值会发 warning（`-Werror` 可升级），特化 → 裸静默。
- `@[...]` 字面量全元素同型时推断为 `NFArray<X>`，混合回退裸 `NFArray`。

## 9. 异常处理：checked 默认，legacy 回退

| 后端 | 旗标 | 机制 | 限制 |
|---|---|---|---|
| `checked`（**默认**） | `-eh checked`（默认即开启） | 显式错误旗标 + 调用点检查链，零 `setjmp/longjmp`；ARC 每帧正常结算 | `@throw` 只能作整条语句；守卫密度由异常效果分析收窄（零 `@try` 程序零守卫） |
| `legacy` | `-eh legacy`（别名 `-eh sjlj`） | 旧 SJLJ：`setjmp/longjmp` + TLS 异常槽 | 跨函数 `@throw` 会跳过中间帧的 scope-end release（中间帧 owned 对象泄漏） |

## 10. 当前已知限制（如实）

- **legacy EH 跨帧泄漏**：见 §9；需要跨帧异常安全用默认 checked。
- **裸泛型只有 warning**：裸容器拼写是擦除兼容行为（无元素类型检查），真正的元素检查需要显式 type args。
- **categories / 协议继承的跨 TU 场景**支持有限（详见 `ROADMAP.md`）。
- **泛型类方法返回类型的实例化**不完整（`ROADMAP.md`）。
- **无 debug info**（不生成 DWARF）。

## 11. 推荐命令示例

```bash
# 构建 nopac（Rust，debug 二进制在 target/debug/nopac）
cargo build --release

# 单测 + 全量集成测试（test_all.sh 是 test_all.py 的并行包装）
cargo test --workspace
./test_all.sh -j4

# 看 nopac 生成的 C（排查第一步）
nopac -rewrite-nopa app.np -o app.c

# self-contained 模式（单文件、快速试验）
nopac run hello.np

# multi-TU（多个 nopa 源 + .o/.a 混合输入）
nopac main.np lib.np -o app

# 预编译 Foundation 库（推荐工程模式）
./tools/build-foundation-lib.sh      # 构建一次
nopac app.np -o app                  # 纯声明客户端：auto-link

# 其他常用
nopac -trace-refcount app.np         # 引用计数静态追踪
nopac app.np -emit-bridge-header app.h -o app.c   # C 桥接头
nopac -ffreestanding kernel.np       # 裸机（自备运行时）
```

