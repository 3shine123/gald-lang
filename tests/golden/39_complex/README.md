### golden/39_complex — C99 `_Complex` 透传 + 虚数字面量

**特性**：`float _Complex` / `double _Complex` 类型声明、typedef、形参全程透传；虚数后缀字面量（`2.0i`/`1e3j`，§6.4.4.2）按 raw 文本发射（`FloatRaw` 通道），虚部不丢失。

**实现**：
- lexer：`_Complex` 入 KW_TABLE（`KeywordKind::Complex`）；浮点扫描消费 `i/I/j/J` 后缀
- CST/AST：`CstType/AstType.is_complex` 标志；`CstExprData/AstExprData::FloatRaw(String)`
- parser：`parse_type_name` 末尾消费 `_Complex`（基类型分支外）；字面量带虚数后缀 → `FloatRaw`
- codegen：`float _Complex` 回显 + `FloatRaw` 原样发射
- checker：虚数字面量按 double 对账（复数算术语义交 clang，零误报）

**用例**：`complex_test.np` — typedef、混算、虚数字面量、`creal/cimag/cabs`，5 行输出（期望值取自 clang 实测）。

**已知限制**：nopa checker 无复数类型推导（`float _Complex` ↔ `double _Complex` 的窄化不告警，clang `-Wall` 也只报 unused——语义由生成的 C 交 clang 保证）；`2.0i` 的 f64 值仍是 2.0（checker 内部），仅发射层保真。
