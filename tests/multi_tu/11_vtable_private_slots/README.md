# 11_vtable_private_slots — 稳定槽位（3b）的验收用例

## 这个用例在证明什么

两个 TU 分工完全合法：

* `lib.gm` 实现 `Widget`，外加一个**头文件没声明**的私有方法 `privateHelper`；
* `main.gm` 只通过 `model.gh` 的声明调用 `Widget`，另外定义自己的类 `App`。

没有哪个 TU 与另一个 TU 对 `Widget` 的理解冲突。但 Gald 目前是
**每个 TU 一个 uniform vtable（字段 = 该 TU 见过的全部实例方法，按字母序）**，
于是：

```
lib  : 43 个字段  sig=0xedd6fb9b1c8b2ce1
main : 43 个字段  sig=0x07f3ae69bac7647b   ← 布局不同
```

链接器把两个 vtable 实例 weak 合并成一份，派发就会读到错误的槽位。编译器
当前用 `gald_verify_vtable_sig` **大声失败**（这正是 `EXPECT_FAIL` 的原因）：

```
gald: fatal: vtable layout mismatch across translation units.
```

**所以本用例今天是 EXPECT_FAIL。** 它是 3b（稳定槽位）的靶子：改造完成后，
这个用例应当**正常运行并输出 `run=42`**，届时删除本目录的 `EXPECT_FAIL`
与 `EXPECT_FAIL_MATCH` 两个标记文件即可。

## 注意与 09_layout_mismatch 的区别

`09_layout_mismatch` 里两个 TU **各自实现同一个类**且方法不同 —— 那是真正的
非法用法，改造后仍应继续大声失败。本用例是**不同类 + 公共声明一致**的合法场景。

## 期望的最终行为

```text
run=42
```

退出码 0；两个 TU 的 vtable 公共部分布局一致，各自的私有方法只影响自己的尾部槽位。
