# golden/44 — C99 指定初始化器

**特性**：C99 designated initializer 的六种形态全部可用。此前 Ovel 的 init list 只支持位置式 `{1, 2}`，`.field = ...` 走不通。

**用例**（`designated_init.ov`，6 形态 + 1 链式，全部自带断言 + `ALL OK` 收尾）：

| 形态 | 例子 | 验证点 |
|------|------|--------|
| 1 完整指定 | `struct Point p1 = { .x = 1, .y = 2 };` | 两字段都赋值 |
| 2 部分指定 | `struct Point p2 = { .y = 5 };` | **未指定字段零填充**（`.x == 0`） |
| 3 位置+指定混合 | `struct Point p3 = { .x = 1, 7 };` | 位置式条目从**最后一个被指定字段之后**继续 → `7` 落 `.y` |
| 4 复合字面量+指定 | `NPRange r = (NPRange){ .location = 3, .length = 9 };` | cast 后紧跟 `{` 路径 |
| 5 数组元素指定 | `CGPoint pts[3] = { [0].wx = 1, [2].wy = 6 };` | 中间元素保持零值 |
| 6 嵌套成员路径 | `struct Outer o = { .in.a = 3, .tag = 9 };` | `.in.a` 跨嵌套 struct，`.in.b` 零填充 |
| 7 链式数组指定 | `CGPoint g[2] = { [1].wx = 2.5f };` | `[1].wx` 两级 designator |

位置式 `{1, 2}` 一直可用，零回归。

**pass-through 设计**：指定初始化器按普通 C 初始化器原样透传，**不引入新 IR**。AST 侧只有一个 `DesignatedInit` 节点（携带 `designators` 链 + 表达式），elaborator / checker / codegen 全链直传，codegen 原样发射 `{.x = 1, .y = 2}`。参见 `AstExprData::DesignatedInit` 与 `AstDesignator`。

**一处历史误判的修正**：本特性曾被记为"形态 4（cast 后跟 `{`）未支持"，首轮探针据此下了结论。**真因是测试文件自身笔误**——引用了未定义的 `NPRange`（`is_type_name` 为 false → 不走 cast 分支），另有 `CGPoint` 字段名写成 `x/y` 而非 `wx/wy`。两处笔误修正后 6 形态端到端 exit=0，**编译器零改动**。

> 方法论沉淀（与"类型参数被静默擦除"那次同类）：判定"某语法未实现"必须跑**最小探针**，不能只看既有测试文件的失败信息——两轮误判都源于探针量到的只是现象。

**确定性与内存管理**：本特性是纯 C 数据结构初始化，与内存管理模式无关，ARC 与 `-fno-ovel-arc` 输出逐字节相同。
