### golden/38_macros — nepa 宏展开（双轨预处理器）

**特性**：`#define` 宏体含 nepa 语法（`[recv msg]` / `@字面量` / `^{}` block）时，nepac 自行解析并在源级展开（此前直接透传给 C 编译器 = 语法错误，一直搁置）。纯 C 宏体照旧透传，由 clang 展开，行为零变化。

**双轨判据**：先 `parse_define`（剥离参数列表），再对**宏体**（非整行）做 `body_has_nepa_syntax` 探测——函数式宏体开头 `[` 前面是 `)`，若测整行会被误判为 C 下标。

**展开语义**（规范来源 ISO/IEC 9899:2011 §6.10.3，独立实现于 `crates/cpp`，与 clang 行为兼容但不翻译其代码）：
- 实参先完整展开再代入（`##`/`#` 操作数除外，用 raw 文本）
- `#param` 字符串化 raw 实参（§6.10.3.2）
- `a ## b` 粘贴两侧 raw 操作数（§6.10.3.3）
- `__VA_ARGS__` 收尾变参逗号拼接（§6.10.3.4）
- 自递归冻结（hide set 近似，§6.10.3.1 蓝漆规则）
- 函数式宏裸名（非调用位）不展开
- `\` 续行在预处理入口拼成逻辑行（行号记首行）

**用例**：
- `macros_test.np` — nepa 轨（TAG/BUMP/SAFE_TAG/LOG：对象式、函数式、嵌套实参、@literal 体）+ C 轨（TWICE/CAT/NAME：粘贴、直通），7 行数值输出
- `undef_test.np` — `#undef` 后宏不再展开（nepa 表与 defined 集同步移除，透传 C）
- `macro_edges.np` — `##`/`#` 边界语义端到端，**每行期望值取自 `clang -E -P` oracle**（见 `.np` 头注释）：`#` 空白折叠与引号转义、`()` 单空实参（§6.10.3p4）、空左操作数 `##` 占位符语义（`MK(, tag)` → `Log tag` 不粘成 `Logtag`）、两段式字符串化 `XSTR(MK(...))`、声明符位粘贴 `int CAT(np_, count)`、GNU `,##__VA_ARGS__` 逗号吞并、`#__VA_ARGS__` 全尾串化
- `pragma_passthrough.np` — `_Pragma("...")` 原位透传（宏体内禁用，见负例）
- `multiline_call.np` — 跨行调用（`\` 续行拼逻辑行，行号记首行）

**条件指令**（`#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif`，nepac 自行求值，clang 同级错误）：
- `#if` 先做宏展开再算术求值；`defined(X)`/`defined X` 操作数豁免展开（§6.10.1p4）
- 跳过的分组完全不处理：组内 `#define` 不生效（§6.10.1p6）；组内嵌套条件指令正确跳层
- 错误（报 `unterminated conditional directive` 等，不静默吞文件）：未闭合 `#if`、孤立 `#endif`/`#elif`/`#else`、`#else` 之后再出现 `#elif`/`#else`
- 负例：`tests/negative/unterminated_if.np`

**限制**（报清晰错误，不静默）：调用必须单行闭合（跨行需 `\` 续行）；宏体内不得出现 `_Pragma`。
