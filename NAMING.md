# Nupa 命名规范（Nupa Naming Convention）

> 状态：2026-08-13 更新（对齐当前 codegen/runtime 实际符号）
> 对标 Objective-C Runtime 命名体系：Nupa 有的必须跟 ObjC 的分类和命名方式一致，没有的坚决不引入。

---

## 1. 分类体系总览

| 类别 | ObjC 例子 | Nupa 例子 | 命名规则 |
|---|---|---|---|
| 框架类 | `NSObject`, `NSString` | `NPObject`, `NPString`, `NPArray` | CamelCase + `NP` 前缀 |
| 运行时 C API（基础） | `objc_alloc`, `objc_release` | `nupa_alloc`, `nupa_release` | snake_case + `nupa_` 前缀 |
| 运行时 C API（方法级） | `objc_autoreleasePoolPush` | `nupa_autoreleasepoolPush` | 直译 ObjC 方法名的 CamelCase |
| 运行时元数据变量 | `objc_class` 外部符号 | `NUPA_CLASS_$_X` | 全大写 + `_` / `$_` 分隔 |
| 运行时类型 | `objc_object`, `objc_class` | `struct nupa_vtable`, `struct nupa_root` | snake_case + `nupa_` 前缀 |
| 编译器内部符号 | `__block_impl`, `_cmd` | `__nupa_byref_X`, `_cmd` | `__` 前缀（编译器保留） |
| Block 展开（gcc/portable） | `__block_invoke_(...)` | `__nupa_block_{N}` | `__nupa_` 前缀 |
| Ivar 私有变量 | `_name`（属性合成） | `_name`（@synthesize） | `_` 单下划线前缀 |

---

## 2. 框架类（CamelCase + NP 前缀）

ObjC 的 `NS`/`CF`/`CG` 前缀 → Nupa 用 `NP`。

| 当前 | 规范 | 对应 ObjC |
|---|---|---|
| `NPObject` | ✅ | `NSObject` |
| `NPClass` | ✅ | 运行时元类型 |
| `NPString` | ✅ | `NSString` |
| `NPMutableString` | ✅ | `NSMutableString` |
| `NPArray` | ✅ | `NSArray` |
| `NPMutableArray` | ✅ | `NSMutableArray` |
| `NPDictionary` | ✅ | `NSDictionary` |
| `NPMutableDictionary` | ✅ | `NSMutableDictionary` |

> 类名用 `NP`（Nupa）前缀，与 ObjC 的 `NS` 一一对应；后续新增容器（`NPSet`/`NPOrderedSet` 等）沿用。

---

## 3. 运行时 C API（`nupa_` 前缀）

ObjC 用 `objc_` 前缀 → Nupa 用 `nupa_`。

### 3.1 基础内存 / 引用计数（snake_case）

| 符号 | 对应 | 说明 |
|---|---|---|
| `nupa_alloc` | `objc_alloc` | 分配实例 |
| `nupa_init` | `objc_init` | 初始化 |
| `nupa_retain` / `nupa_release` | `objc_retain`/`objc_release` | RC +/−1 |
| `nupa_autorelease` | `objc_autorelease` | 池化释放 |
| `nupa_free` / `nupa_malloc` | 裸机分配器 | bump allocator |

### 3.2 方法级 / 池 / 元数据（直译 ObjC 方法名，CamelCase）

| 符号 | 说明 |
|---|---|
| `nupa_metaInit` | 初始化类元数据（弱符号） |
| `nupa_autoreleasepoolPush` / `nupa_autoreleasepoolPop` | 自动释放池 |
| `nupa_stringFromCstr` | C 串 → `NPString`（codegen 弱发射） |
| `nupa_isKindOf` | 类型判断（保留 CamelCase，不转 snake） |
| `nupa_array_create` | `@[...]` 字面量的运行时构造 |
| `nupa_dictionary_create` | `@{...}` 字面量的运行时构造（交替 key/value varargs） |
| `nupa_weakRegister` / `nupa_weakUnregister` / `nupa_weakClearAll` | 弱引用 |

> **关于 snake 与 Camel 混用**：基础内存/RC 函数沿用早期 snake_case（`nupa_alloc`…）；后加的、直接对应某条 ObjC 方法语义的函数按 ObjC 方法名 CamelCase（`nupa_metaInit`、`nupa_autoreleasepoolPush`）。两者都以 `nupa_` 前缀开头，不冲突。

---

## 4. 运行时类型（snake_case + nupa_ 前缀）

| 当前 | 说明 |
|---|---|
| `struct nupa_root` | 隐式根类 |
| `struct nupa_vtable` | 统一实例 VTable |
| `struct nupa_X_meta_vtable` | 类（meta）VTable |
| `enum nupa_vtable_index` | 全局方法索引枚举 |
| `nupa_autoreleasepool_t` | 自动释放池句柄 |

---

## 5. 类元数据变量（全大写 + `$_` / `_` 分隔）

Nupa 每个类生成一组全大写的元数据符号，分隔符随后端：

| 后端 | 分隔符 | 例子 |
|---|---|---|
| clang / gcc | `$_` | `NUPA_CLASS_$_NPString` |
| portable | `_` | `NUPA_CLASS_NPString` |

| 符号 | 例子（NPString） | 说明 |
|---|---|---|
| `NUPA_CLASS_$_X` | `NUPA_CLASS_$_NPString` | 类对象实例 |
| `NUPA_VTABLE_$_X` | `NUPA_VTABLE_$_NPString` | 实例 VTable |
| `NUPA_META_VTABLE_$_X` | `NUPA_META_VTABLE_$_NPString` | 类（meta）VTable |
| `NUPA_GETCLASS_$_X` | `NUPA_GETCLASS_$_NPString` | 返回 `&NUPA_CLASS_$_X` |

> 根类：`NUPA_CLASS_$_nupa_root`；另有 `NUPA_ROOT_DEFINED` 宏标记根类已定义。

---

## 6. 选择器常量（`__nupa_sel_` 前缀）

```
__nupa_sel_{selector名}   selector 用 `_` 代替 `:` 
__nupa_sel_init                       → init
__nupa_sel_stringWithUTF8String_      → stringWithUTF8String:
__nupa_sel_timsort_count_using_       → timsort:count:using:
```

常量本体是 `static const SEL`（`.name` + FNV-1a `.hash`）。

---

## 7. Block 展开命名（`__nupa_` 前缀，gcc/portable）

| 符号 | 例子 | 说明 |
|---|---|---|
| `__nupa_block_{N}` | `__nupa_block_0` | 每个字面量的静态 invoke 函数 |
| `struct __nupa_block_layout_{N}` | `struct __nupa_block_layout_0` | 每个字面量的布局 | 
| `struct __nupa_block_header` | `struct __nupa_block_header` | 所有展开块共享的头（isa/flags/reserved/invoke） |
| `__nupa_byref_{变量名}` | `__nupa_byref_counter` | `__block` 变量包装 struct |

---

## 8. 编译器内部符号（`__` 前缀）

| 当前 | 说明 |
|---|---|
| `__nupa_sel_NAME` | 选择器常量 |
| `__nupa_tmp_{N}` | 表达式中临时变量 |
| `__nupa_pool` | `@autoreleasepool` 池变量 |
| `__nupa_exception_buf` / `__nupa_exception_value` | `@try/@catch` 异常状态 |
| `__nupa_saved` / `__nupa_state` | 嵌套 try 的 jmp_buf 保存 |
| `_cmd` / `self` | 同 ObjC |

---

## 9. Ivar 命名（`_` 单下划线前缀）

```nupa
@property int age;
@synthesize age = _age;   // ivar 名为 _age
```

实例方法参数/局部变量**不用**下划线；只有合成 ivar 用 `_` 前缀。

---

## 10. 桥接头命名（`--emit-bridge-header`）

C 侧调用 Nupa 对象方法的包装函数：

```
nupa_{类名}_{方法名}             数组类方法名
nupa_NPString_stringWithUTF8String_   → 类方法
nupa_NPString_UTF8String              → 实例方法

格外显式初始化：nupa_metaInit()
```

见 README/CHINESE「C 桥接」小节。

---

## 11. 不引入的 ObjC 运行时特性（当前 & 未来规划）

当前**不引入**（VTable 编译期固定）：

| ObjC 特性 | 原因 |
|---|---|
| `objc_msgSend` | Nupa 用 vtable 下标直接派发 |
| `objc_getClass` / `objc_setClass` / `class_addMethod` | 类元数据编译期定死 |
| `objc_setAssociatedObject` / `object_setIvar` | ivar 布局编译期固定 |
| Method Swizzling | VTable 编译期固定，不支持运行时替换 |
| `NSInvocation` / `NSProxy` | 过于动态 |
| `respondsToSelector` / `mirroring` | VTable 索引固定，运行时无动态查找 |

> **未来规划（主静辅动）**：为模组/游戏预留"边界动态"，已出规划文档 `~/Desktop/Nupa-主静辅动-动态特性规划.md`。届时将**新增**：
> - `nupa_classNamed(const char *)`（运行时类注册表）
> - `nupa_respondsToSelector(id, SEL)`
> - `nupa_performSelector...`（受限签名）
> - `nupa_overrideMethod(...)`（vtable 槽替换 = 模组覆盖）
> 核心热路径保持静态；不引入 `objc_msgSend` 全量动态派发。

---

## 12. 已解决的历史命名

以下变更**已完成**，不再作为待办：

| 符号 | 旧 → 新 | 状态 |
|---|---|---|
| 隐式根类 | `__nupa_root` → `nupa_root` | ✅ 完成（`struct nupa_root`、`nupa_root_init` 等） |
| 根类元数据 | `nupa___nupa_root_class` → `NUPA_CLASS_$_nupa_root` | ✅ 完成（随 §5 全大写规范） |
| 元数据变量 | `nupa_{类}_class` → `NUPA_CLASS_$_X` | ✅ 完成 |
| 类型判断 | `nupa_isKindOf` 保留 CamelCase（不回退 snake） | ✅ 定案 |

---

## 13. 快速记忆

- **类**：`NP` + CamelCase（`NPArray`）
- **运行时函数**：`nupa_` + （基础 snake / 方法级 Camel）
- **元数据**：`NUPA_{KIND}_$_名字`
- **编译器符号**：`__nupa_` 开头
- **ivar**：`_` 单下划线
- **C 侧桥接**：`nupa_{类}_{方法}`