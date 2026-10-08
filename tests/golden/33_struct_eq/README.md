# 33 — struct `==` / `!=` 值比较（路线图待做 #2）

两个**同 tag 值 struct** 的 `==`/`!=` 由编译器降级为逐字段比较函数调用。

| 文件 | 期望 |
|------|------|
| `struct_eq_test.np` | 编译通过，stdout 见 `.out` |

```bash
./target/debug/nepac run tests/golden/33_struct_eq/struct_eq_test.np
```

## 语义（勘误后定位）

实测 C 对 `a == b`（两个 struct 值）是**硬报错**（`invalid operands to binary expression`），不是静默指针比较——所以这是**可用性缺口**（否则要手写 15 行逐字段函数），不是修正确性 bug。

## 降级形态

```c
/* checker 改写： a == b → nepa_struct_eq_Point(a, b)； a != b → (nepa_struct_eq_Point(a, b) == 0) */
static int nepa_struct_eq_Point(struct Point a, struct Point b) {
    return a.x == b.x && a.y == b.y;
}
```

字段比较规则：

| 字段种类 | 比较方式 |
|----------|----------|
| 嵌套 struct（值） | 递归调用 `nepa_struct_eq_<inner>`（闭包依赖自动展开） |
| 数组字段 | `memcmp(a.f, b.f, sizeof a.f) == 0` |
| 指针字段 | `a.f == b.f`（地址比较，C 常规语义，不递归解引用） |
| 标量 | `a.f == b.f` |

## 实现要点

- **checker**（`crates/checker/src/lib.rs` `maybe_rewrite_struct_eq`）：`check_expr` 包装层在 Binary(op 12/13) 两侧 `expr_type` 都是同一值 struct（`is_struct && !is_pointer`，取 `name` 作 tag）时改写；tag 记入 `struct_eq_tags`。类型推断来自既有 `scope_vars`（变量声明类型含 `is_struct` 标记）。
- **pipeline**（`crates/nepac/src/pipeline.rs`）：`checker.struct_eq_tags` 在 checker 之后取出（`-fno-checker` 时为空，struct 比较退回 C 原生行为=clang 报错），传入 `cg.struct_eq_tags`。
- **codegen**（`emit_struct_eq_functions`）：从 CgUnit 的 Typedef/Struct 声明收集字段表，按 tag 发射 `static int nepa_struct_eq_<tag>(struct Tag a, struct Tag b)`；发射位置在 Section 5 struct 定义之后（需完整字段类型）、按需发射（只发射用到的 tag，无 unused 告警）。
- **函数返回 `int`（0/1）而非 `bool`**：`!=` 用 `(eq == 0)` 表达，不需要新的 Unary 编码；`eq` 调用本身也是纯 FuncCall——AST/codegen 层零新增节点。
- ⚠️ **传递给 eq 函数的是 struct 按值拷贝**（C 语义 `sizeof a` 入参），与 C 的赋值/传参拷贝语义一致；大 struct 有拷贝开销，可接受（C 用户手写比较函数同样如此）。

## 连带修复的 parser bug

字段名 `in` 曾炸掉 struct 解析：`in` 为 for-in 落地时加入的 lexer 硬关键词，而 `parse_struct_body` 的字段名只认 `Identifier`——`struct Config in;`（C 完全合法）报 `expected ';' after field (got keyword)`。修法：字段名消费点接受 `KeywordKind::In` 与上下文关键词（`is_contextual_kw_ident()`），与 `copy`/`retain` 当标识符的既有处理同款。

## 边界

- 两侧不同 struct 类型：不改写，交给 C 硬报错（类型不匹配本来就该报）。
- struct 指针（`p == &a`、`p1 == p2`）：`is_pointer` 排除，保持 C 地址比较语义，不变。
- `-fno-checker` 下不改写（checker 是改写源头）。
