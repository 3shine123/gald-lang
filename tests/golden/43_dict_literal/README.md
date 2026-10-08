# golden/43 — 字典字面量（`@{ key: value }`）

**特性**：`@{ @"k": @1, ... }` 字典字面量，配套 `NPDictionary`（不可变）/ `NPMutableDictionary`（可变）两个新类。存储照搬 `NPArray`（两个平行对象数组 + count），查找是**线性扫描**——这些字典都很小，真上哈希表还得跨每个键类保证 `hash`/`isEqual:` 成对正确。

**先修掉的 parser bug（比缺口本身更严重）**：**多对字典此前从未解析成功过**。值用的是 `parse_expression()`，它会吃逗号运算符，把 `@1, @"b"` 吞成一个 `Comma` 表达式，于是第二对的 `:` 撞上 `expected '}' after literal (got ':')`。单对能用（键用的是 `parse_assignment()`）。修法：值改用 `parse_assignment()`（数组字面量早就是这个层级）。

**`@{}` = 空字典**（ObjC 语义：`@{}` 与 `@[]` 是两个字面量）；无冒号的非空 `@{a, b}` 保留宽容的数组回退。

**键相等语义 = 值语义（本轮用户拍板）**：`isEqual:` 在 `nepa_root` 上仍是**指针身份**（同 `NSObject`），但 `NPString` / `NPNumber` 各自重写为**值相等**（委托 `isEqualToString:` / `isEqualToNumber:`，ObjC 的 NSString/NSNumber 正是如此）。这是字典键可用性的语义保证（2026-09-30 更新：字面量 interning 已落地，同内容字面量现为同一对象；值相等仍是语义保证）。`NPDictionary` 自身也按内容实现 `isEqual:`（同 count 且每键映射到相等的值）。

**用例**（`dict_literal.np`，9 段）：

| 段 | 覆盖 |
|----|------|
| 1 | 多对字面量 + `count` |
| 2 | 用新写字面量查键（值语义关键用例；interning 落地后同内容即同对象，仍按 `isEqual:` 命中）+ 未命中返回 nil |
| 3 | `@{}` 空字典 |
| 4 | `allKeys` / `allValues` 保持插入序 |
| 5 | `@(expr)` 值 + 数字键（NPNumber 值语义） |
| 6 | 数组字面量内嵌套字典字面量 |
| 7 | 可变字典插入 / 等键替换（非追加）/ 删除 |
| 8 | `copy` 不可变 + 内容 `isEqual:` |
| 9 | `description` |

**非对象条目拒绝**（负例 `tests/negative/dict_illegal_entry.np`）：

```
illegal type 'int' in a dictionary literal — keys and values must be Objective-C objects
```

对齐 ObjC 的 "collection element of type 'int' is not an Objective-C object"；裸标量发进 `nepa_dictionary_create` 的 vararg 会编译通过、运行期读垃圾。checker 逐**键与值**都检查（这一段修复同时让字面量内层的 `@(expr)` 装箱能触发改写）。

**codegen**：`@{k: v, ...}` → `nepa_dictionary_create(n, k1, v1, ..., kn, vn)`——弱符号 helper（仿 `nepa_array_create`），仅当本 TU 内有 `NPDictionary` 时发射。nil 键跳过（不给扫描留洞）、nil 值存 NULL。

**确定性与内存管理**：本特性与内存管理模式无关，ARC 与 `-fno-nepa-arc` 输出逐字节相同。

**已知限制（如实）**：`NPDictionary` / `NPMutableDictionary` **已接泛型**（`NPDictionary<K, V>` 双参数特化），但 `objectForKeyedSubscript:`（`d[@"k"]`）**刻意未声明**——checker 的对象下标改写只认 `objectAtIndex:`，声明它反而会宣传一个会被发到错 selector 的写法。
