# golden/42 — `switch` 模式匹配（M1）

## 语法

```objc
switch (subject) {
    case NPString *s:      // 类型绑定 → isKindOfClass:
        ...                //   体内 `s` 已是 (NPString *)subject
    case > 100:            // 悬空比较 → `subject > 100`
    case > 0 && < 100:     // 区间
    case @"literal":       // 对象字面量 → isEqual:（值语义）
    case @42:              // 装箱字面量 → isEqual:
    case T *x when x.count > 3:   // when guard（上下文关键词）
        ...
    default:
        ...
}
```

## 落地方式

parser 识别 `case` 标签形态 → 含任一模式臂时整个 switch 降级为
`SwitchPat`（平面臂表）→ `crates/pattern`（pipeline Step 3.95）在 checker
之前改写成 `goto`/`if` 状态机 + C 标签。**codegen 零改动**。

```
{ NPObject *__ovel_sw = (NPObject *)subject;      /* 对象 subject */
  __auto_type __ovel_sw = subject;                /* 标量 subject */
  if (<arm0 test>) goto __ovel_case_0_1;
  ...
  __ovel_case_0_1: { ... } goto __ovel_sw1_end;
  __ovel_case_0_d: { ... }
  __ovel_sw1_end: ; }
```

- **每个降级 switch 有独立标签命名空间**（`__ovel_sw{id}_*`）——C 标签是
  函数作用域的，嵌套模式 switch 否则会发重复标签名（硬 C 错误）。
- **subject 只求值一次**；`break` 作用域感知改写为 `goto __ovel_sw{id}_end`，
  arm 体内的嵌套循环/内层 switch 的 `break` 不受影响。
- **fallthrough 保持 C 语义**：臂体末尾不追加隐式跳转，无 `break` 即落入下
  一臂（用例 7 验证 `t7=11`）。
- **对象 subject 声明为 `NPObject *`**（不是 `__auto_type`）：本 pass 跑在
  checker 之前，静态类型未知，`__auto_type` 会继承指针类型并被 checker
  判为"对象初始化标量"。标量 subject 保持 `__auto_type`（eh pass 先例）。

## 模式分类判据（parser `parse_case_pattern`）

| 形态 | 判据 | 降级测试 |
|------|------|----------|
| `T *name`（T 为已注册类型名） | 源切片扫描：标识符 `*` 标识符（+ 可选 `when` guard）→ `:` | `ovel_isKindOfClass(...)` + 臂内 `(T *)subject` 别名声明 |
| `>` / `<` / `>=` / `<=` 开头 | 悬空比较 → 表达式树里替换 subject | `subject OP <expr>` |
| `@"..."` / `@N` / `@YES` / `@'c'` / `@(expr)` | `is_object_literal`（与 `crates/pattern` 的 `is_object_literal_expr` **必须一致**） | `[subject isEqual:<lit>]` |
| `when <expr>` | 上下文关键词（lexer 不硬编码） | 追加 `&& <expr>` |
| 其他 | 普通 C 常量 | `subject == <const>` |

## M1 限制（如实记录）

- **常量臂与模式臂不可混用**：报
  `switch mixes plain constant 'case' arms with pattern arms — M1 cannot lower
  both in one switch; split them into separate switches`（负例
  `tests/negative/switch_mixed_arms.ov`）。C 路径需要原始 body，模式路径丢弃它。
- **非恒定标签被拦截**：`case [obj msg]:` / `case f():` / `case a = b:` 报
  `case label is not a constant expression`（负例
  `tests/negative/switch_case_msg_send.ov`）。此前会原样透传进生成的 C，
  报错指向生成代码、无法对应源码。
- **嵌套模式**未做跨层 case 作用域（内层 switch 的 `case` 归内层）——C 与
  Ovel 语义一致，未验证。
- `case <enum const>:` 走普通 C 路径（`is_object_literal` 假），零回归。

## M2 候补

- 常量臂与模式臂混合降级（`switch` 保留原始 body，按臂分流）。
- 穷尽性分析（对象 subject + `Bind` 臂缺 `default` 时警告）。
- `case is` / `case as` 模式（as 需类型转换语义）。
- 或模式（`case NPString *s, NPNumber *n:`）。
