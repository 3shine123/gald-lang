# 30 — for-in 遍历：`for (T x in coll)`

`for (T x in coll)` 遍历语法（**Sep 2026** 新增）。

| 文件 | 期望 |
|------|------|
| `for_in_test.gm` | 运行成功，stdout 见 `for_in_test.out` |

`.out` 是程序 stdout 快照。`test_all.py` 只校验退出码（不比对 `.out`），输出有变化时需同步更新快照。

```bash
./target/debug/galdc run tests/golden/30_for_in/for_in_test.gm
```

## 语法

采用 **ObjC 规范形**：`for (T x in coll)`，其中 `T` 是循环变量的类型（`id` 亦可）。

```objc
NFArray *arr = @[ @"one", @"two", @"three" ];
for (NFString *s in arr) {
    printf("%s\n", [s UTF8String]);
}
```

`for_in_test.gm` 覆盖 3 个场景：单层遍历、嵌套 for-in、空数组（循环体不执行）。

## 实现：parser 层 desugar

for-in 在 **parser 阶段**就展开成普通 C for 循环：

```c
{ T *__gald_fi = <coll>;            /* 借用别名：只求值一次，ARC 不参与 */
  for (size_t __gald_fi_i = 0; __gald_fi_i < [__gald_fi count]; __gald_fi_i++) {
      T x = [__gald_fi objectAtIndex:__gald_fi_i];
      ...body...
  } }
```

**为什么在 parser 做**：codegen 的 emit 阶段拿不到方法元数据（vtable 下标、nil 守卫模板），无法手写 `[coll count]` 这样的消息派发。desugar 之后，下游 checker / ARC / codegen 看到的都是普通的 `For` + `Decl` + `MsgSend`，**零改动**继承既有行为：

- nil 安全：`[nil count] == 0`，集合为 nil 时循环体不执行
- 元素借用语义：循环变量是元素别名，不 retain / release，ARC 无需注入
- 集合只求值一次（`__gald_fi` 暂存），即使表达式有副作用也安全

这也是仓库的一贯做法：先例有 `@42` → `[NFNumber numberWithInt:]`、`arrayWithObjects:` → `@[...]`。原则是**每个新特性都走 desugar，不引入新 IR 机制**。

## 检测方式

`Parser::scan_for_in_header()`——source-slice 扫描（与 `peek_colon_colon`、label 识别同款）：从当前 token 起扫，depth-0 出现 `in` 关键字、且在匹配的 `)` 之前、且没有 depth-0 的 `;`，即判定为 for-in 头。字符串/字符字面量会被跳过。

**不用投机 parse + 回溯**：`parse_declaration` 消费 `;` 时会在 `in` 处 `consume(Semicolon)` 记 error，即使回滚也会污染解析器错误状态（`error_count` / `has_error` 不回退）。

## 已知残留

CST / AST / Cg 三层的 `ForIn` 变体与各阶段（elaborator / checker / arc / trace / codegen）的 match 臂仍然存在，但**已无构造路径**（parser 不再生成该节点）。enum 变体不构造不触发 `deny(dead_code)`，故编译安静；后续可整体删除（连带 trace 的 `"for-in"` loop 标记）。
