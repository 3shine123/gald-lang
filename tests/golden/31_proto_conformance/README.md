# 31 — Checker 协议一致性检查

类的 `<Proto>` 声明是否被真正实现，由 checker 强制校验（**Sep 2026** 新增）。

| 文件 | 期望 |
|------|------|
| `proto_conformance_test.gm` | 编译通过，stdout 见 `proto_conformance_test.out` |

`.out` 是程序 stdout 快照。`test_all.py` 只校验退出码（不比对 `.out`），输出有变化时需同步更新快照。

```bash
./target/debug/galdc run tests/golden/31_proto_conformance/proto_conformance_test.gm
```

## 行为

声明遵循某协议的类，必须实现该协议（含父协议）的全部**必需**方法：

```objc
@protocol Drawable
@required
- (void)draw;
- (int)area;
@optional
- (void)highlight;      // 可选，不实现不报错
@end

@protocol Colored <Drawable>
@required
- (int)color;
@end

@interface Square <Colored>
@end
```

`Square` 需实现 `draw` / `area` / `color`；`highlight` 是 `@optional`，不实现不报错。正例 `proto_conformance_test.gm` 正是这个结构（协议继承链 + 可选方法豁免）。

未实现时报错，格式与其它 checker 诊断一致（`file:line:col: error:`）：

```
[checker] proto_missing_method.gm:10:1: class 'Circle' does not implement
required method 'draw' from protocol 'Drawable'
```

## 负例

**故意失败**的用例不在本目录，在 **`tests/negative/`**——它们期望编译失败，与 golden 目录"快照程序 stdout"的语义相反：

- `tests/negative/proto_missing_method.gm` — `Circle <Drawable>` 未实现必需的 `draw`
- `tests/negative/protocol_fail.gm` — 同类样本（原在 `golden/07_protocols/`，历史遗留的故意失败用例，无 `.out` 快照）

`tests/negative/` 已在 `test_all.py` 的 glob 中排除（`"negative" not in p.parts`），避免被当作普通 FAIL。验证方式：

```bash
./target/debug/galdc run tests/negative/proto_missing_method.gm   # 期望非 0 退出 + 上述错误信息
```

## 实现要点

检查入口：`crates/checker/src/lib.rs` 的 `check_protocol_conformance`，在 `check()` 走完所有 decl 之后运行（且仅在无既有错误时执行）。

- **AST 需带协议列表**：`AstDeclData::Class` 新增 `protocols: Vec<String>`。此前 elaborator 用 `..` 把 CST 的 `protocols` 直接丢弃，一致性检查无从谈起。
- **只算带 body 的方法**：binder 的 `propagate_protocol_methods` 会把协议方法**声明**（无 body）克隆进 `@interface`，目的是让跨 TU 的 vtable 布局稳定。那些只是占位，不能算"已实现"，否则负例会静默通过。
- **按类名聚合判定**：协议通常挂在 `@interface` 而非 `@implementation` 上，所以用"该类在 TU 内是否有 impl"来判定，而不是当前 decl 的 `is_implementation` 标志。
- **父协议递归**：`parents` 逐层展开，带 `seen` 去重防环。
- **跨 TU 安全**：无 `@implementation` 的类（header-only，实现链接自别处）整体跳过。
- **协议名解析**：先查原名，再查 `ns::proto` 形式（命名空间内协议）。
- **selector 归一化**：binder 存多段 selector 带尾冒号（`deployShield:`），AST 方法名可能不带，比较时 `trim_end_matches(':')`。不归一化会让 `ultimate_megafusion_test.gm` 等 15 个测试误报。

## 配套的 parser 修复

`@interface X <Proto>` 的 `<Proto>` 此前被**泛型 type-params 块**吃掉（`match_token(Less)` 先于协议解析执行），协议列表永远是空的——这是本检查能工作的前置条件。

修法：`<...>` 块内的名字若**全部**是已声明类型（`@protocol` 会把名字注册进类型表），归入 protocols；否则归入 type_params。泛型类 `Box<T>` 不受影响。`@interface X : Super <Proto>` 的第二个 `<...>` 块与前者合并到同一列表。

## 协议组合 `P & Q`（Sep 2026 追加，路线图待做 #3）

`&` 把协议名连成一个**合取列表**（与 `,` 同语义）：接收者必须**同时**满足全部协议。

| 文件 | 期望 |
|------|------|
| `proto_intersection_test.gm` | 编译通过（组合声明 + 交集类型正例），stdout 见 `.out` |
| `tests/negative/proto_intersection_missing.gm` | 编译必须失败：`class 'Bad' does not implement required method 'serialize'` |

```objc
@protocol Renderable <Drawable & Serializable>   // ① 组合声明：binder 把 P、Q 的 required 并入 R 的 parents
void render(id<Drawable & Serializable> item);   // ② 交集类型：checker 验证静态类型覆盖两者
```

**实现**：parser 三处协议列表循环（`parse_type_full` 的 `<...>`、`@interface` 的两个 `<...>` 块、`@protocol <parents>`）的逗号分支扩为 `Comma || Ampersand`，落同一 `Vec<String>`——下游（binder `parents` 去重、elaborator 透传、checker `for proto in protocols` 逐个验证）零改动。**codegen 零改动**：统一 VTable 下协议类型只是 checker 的编译期约束标签，不影响槽位与派发。
