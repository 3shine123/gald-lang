# jetic 编译器扩展极端压测 — 全部缺口已修复

本目录对 jetic 语法分析器做 clang/gcc 编译器扩展的极端压力测试，
记录**已支持**与**已修复**的构造。所有 17 个已发现的缺口均已修复，
`run_stress.sh` 23/23 全绿。

## 文件

| 文件 | 用途 | 状态 |
|------|------|------|
| `clang_stress.jeti` | clang 专属属性 + 内置函数（可运行子集） | ✅ transpile + C 编译 + 运行 |
| `gcc_stress.jeti`   | gcc 专属属性 + 内置函数（可运行子集） | ✅ transpile + C 编译 + 运行 |
| `gaps/*.jeti`       | 原缺口最小复现（现为回归测试） | ✅ 全部修复，23/23 PASS |
| `run_stress.sh`   | 一键跑 3 个 round | ✅ 23 PASS / 0 FAIL |

## 已修复缺口清单

### 解析器缺口（17 个全部修复）

| 文件 | 构造 | 修复内容 |
|------|------|----------|
| 01 | `struct S { ... } __attribute__((packed, aligned(8)));` | 结构体右花括号后解析属性，发射时放在 `}` 后 |
| 02 | `typedef __attribute__((aligned(8))) unsigned long w;` | `parse_declaration_inner` 中 `typedef` 后收集属性 |
| 03 | `int f(int) __attribute__((pure));` | 函数右括号后收集尾随属性 |
| 04 | `void *__attribute__((warn_unused_result)) f(void);` | 返回类型与函数名之间收集属性，传递到函数声明 |
| 05 | 局部变量前 `__attribute__((unused)) int local;` | `parse_statement` 中检测属性前缀，传给 `CgStmtData::Decl` |
| 06 | 参数上 `int x __attribute__((unused))` | 每个参数解析后收集属性，通过 `CstParam.attributes` 传递，`format_param_decl` 发射 |
| 07 | 数组字段后 `char name[8] __attribute__((aligned(4)));` | 字段属性已存入 `CstDecl.attributes`，codegen `Struct.fields` 3 元组携带 |
| 08 | `__builtin_offsetof(struct Pt, y)` 等类型名参数 | 新增 `TypeLiteral` 表达式变体 + `is_builtin_type_arg_start` 检测 |
| 09 | 整数字面量后缀 `u`/`U`/`L`/`LL`/`ULL` | 词法分析器十六进制分支增加后缀消耗，解析器增加 u64 回退 |
| 10 | 相邻字符串拼接 `"a" "b"` | `parse_primary` 中连续匹配 String token 并拼接 |
| 11 | 数组字段 `char name[8]` 发射为 `char[8] name;` | `split_array_type` 分割，所有字段/变量发射点改用 `{base} {name}{suffix}` |
| 12 | 可变参数 `...` 丢弃 | `AstDeclData::Function` 增加 `has_variadic` 字段，`emit_decl` 加 `, ...` |
| 13 | 属性字符串参数丢引号 `no_sanitize("address")` | `parse_attributes` 中 String token 包裹 `"` |
| 14 | `internal_linkage` + weak 冲突 | 发射时检测 `internal_linkage` 属性，跳过 `__attribute__((weak))` |
| 15 | `__attribute__((packed))` 写在 struct 名前（不生效） | 同 01，结构体后属性修复后 packed 正确生效 |
| 16 | `#ifdef`/`#else`/`#endif` 条件编译 | 预处理器增加条件栈，`#ifdef`/`#ifndef`/`#if`/`#elif`/`#else`/`#endif` 完整支持 |
| 17 | `va_arg(ap, int)` 类型参数 + `va_list` 声明 | `va_list` 加入 type_names 白名单；TypeLiteral 修复 |

### 后端宏联动

`-backend clang` → 预定义 `__clang__` + `__GNUC__`（clang 也定义 `__GNUC__` 以兼容 GCC）
`-backend gcc`   → 仅预定义 `__GNUC__`
`-backend portable` → 仅 `__GNUC__`

## 运行

```bash
./clang_gcc_stress/run_stress.sh
# round 1 clang:  transpile + clang 编译 + 运行
# round 2 gcc:    -backend gcc 属性门禁 + clang 代编译 + 运行
# round 3 gaps:   回归测试 17 个原缺口（全部应通过）
```

单文件调试：

```bash
./target/debug/jetic -rewrite-jeti -backend clang clang_gcc_stress/clang_stress.jeti -o /tmp/x.c
clang -I include -o /tmp/x /tmp/x.c include/jeti/runtime.c && /tmp/x
```

## 已知限制

- `va_list` 已修复，但 `va_arg(ap, 类型)` 需要类型名是关键字（`int`/`long` 等）才能触发 TypeLiteral 路径。若类型是 typedef 别名（如 `va_arg(ap, my_type)`），仍会走 `parse_assignment` 路径，一般能工作（my_type 是标识符，解析为变量引用）。
- C 编译警告：`aligned`/`packed` 写在 struct 名前仍会产生 clang 警告（`attribute ignored, place it after "struct"`），建议使用 `struct S { ... } __attribute__((packed));` 形式（已修复）。
- 整数字面量后缀支持：`u`/`U`/`l`/`L`/`ll`/`LL`/`ul`/`UL`/`ull`/`ULL` 均已支持。
