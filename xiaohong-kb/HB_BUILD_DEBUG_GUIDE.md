# hb build -f 构建失败全流程排查与修复指南（0 → 1）

> 适用场景：OpenHarmony 小型系统（liteos_m）+ HiSilicon WS63 芯片（ws63v100 SDK），
> 执行 `hb build -f` 时编译/链接失败，需要从现象出发一步步定位根因并修复。
>
> 本文以真实案例为主线，完整记录了一次构建失败的 **现象 → 定位 → 原理 → 修复 → 验证** 全过程，
> 共涉及 **3 个连环错误**，最终构建成功并产出固件包。

---

## 目录

1. [背景与环境](#1-背景与环境)
2. [问题复现](#2-问题复现)
3. [错误一：libhelloworld.a 没有构建规则](#3-错误一libhelloworlda-没有构建规则)
4. [错误二：fs_adapt_* 符号未定义（链接失败）](#4-错误二fs_adapt_-符号未定义链接失败)
5. [错误三：CONFIG_LFS_PARTITION_ID 未定义（编译失败）](#5-错误三config_lfs_partition_id-未定义编译失败)
6. [最终验证与产物](#6-最终验证与产物)
7. [改动清单汇总](#7-改动清单汇总)
8. [经验总结：构建问题的通用排查方法论](#8-经验总结构建问题的通用排查方法论)

---

## 1. 背景与环境

### 1.1 这是什么项目

这是一个基于 **OpenHarmony 小型系统（mini system）** 的物联网项目，芯片平台为
**HiSilicon WS63**（RISC-V 双核），核心 SDK 位于：

```
device/soc/hisilicon/ws63v100/sdkv106/
```

产品（product）名称为 `xiaohong-kb`，应用源码在：

```
vendor/atomgit/xiaohong/
```

构建系统是 OpenHarmony 标准构建工具 `hb`（HarmonyOS Builder），
它内部会依次驱动 GN → Ninja → 并回调 SDK 自己的 CMake/Make 构建链
（`hm_build.sh` → `python3 build.py -c ... ws63-liteos-app`）。

### 1.2 关键路径速查

| 含义 | 路径 |
|---|---|
| SDK 根目录 | `device/soc/hisilicon/ws63v100/sdkv106/` |
| 目标平台配置文件 | `sdkv106/build/config/target_config/ws63/config.py` |
| 菜单配置（Kconfig） | `sdkv106/build/config/target_config/ws63/menuconfig/acore/*.config` |
| OHOS 组件链接列表 | `sdkv106/libs_url/ws63/cmake/ohos.cmake` |
| SDK 构建输出 | `sdkv106/output/ws63/acore/ws63-liteos-app/` |
| hb 构建输出 | `out/xiaohong/xiaohong-kb/` |
| 构建错误日志 | `out/xiaohong/xiaohong-kb/error.log` |
| 构建详细日志 | `out/xiaohong/xiaohong-kb/build.log` |

### 1.3 环境信息

```bash
$ uname -a
Linux ubuntu-gnu-linux-24-04-3.local 6.x.x ... x86_64

# 编译目标：RISC-V 32 位（riscv32-linux-musl 工具链）
# 编译工具链：sdkv106/tools/bin/compiler/riscv/cc_riscv32_musl_105/cc_riscv32_musl_fp/
```

---

## 2. 问题复现

在项目根目录执行标准全量构建命令：

```bash
cd /home/parallels/xiaohong
hb build -f
```

> `-f` 表示 full build，即强制全量重新构建（不清除历史缓存则可能掩盖问题）。

### 2.1 现象

构建过程耗时较长（首次约 5 分钟以上），最终以失败结束，日志末尾出现：

```
[OHOS ERROR] [NINJA] exceptions.ohos_exception.OHOSException: COMPILE Failed!
[OHOS ERROR] [NINJA] Code:        4000
[OHOS ERROR] [NINJA] Error Type:  Ninja build error
[OHOS ERROR] [NINJA] Description: An unknown error occurred while executing 'ninja -C'.
[OHOS ERROR] [NINJA] Solution:    no solution
```

**重点提示**：`hb` 的报错信息本身不含具体错误，它只会指引你去看两个日志文件：

```
out/xiaohong/xiaohong-kb/error.log    # 错误详情（编译器/链接器输出）
out/xiaohong/xiaohong-kb/build.log    # 完整构建过程
```

### 2.2 第一步：先看 error.log

```bash
tail -60 out/xiaohong/xiaohong-kb/error.log
```

这是排查的第一原则：**先看错误日志，不要猜**。

---

## 3. 错误一：libhelloworld.a 没有构建规则

### 3.1 现象

`error.log` 末尾出现典型的 make 错误：

```
make[2]: *** 没有规则可制作目标
"/home/parallels/xiaohong/device/soc/hisilicon/ws63v100/sdkv106/interim_binary/ws63/libs/ohos/ws63-liteos-app/libhelloworld.a"，
由"ws63-liteos-app.elf" 需求。 停止。
make[2]: *** 正在等待未完成的任务....
make[1]: *** [CMakeFiles/Makefile2:5367：CMakeFiles/ws63-liteos-app.dir/all] 错误 2
make: *** [Makefile:91：all] 错误 2
```

翻译成人话：

> 链接 `ws63-liteos-app.elf` 时需要 `libhelloworld.a` 这个库，
> 但整个构建系统里**没有任何一条规则**能生成这个库。

### 3.2 定位：谁在引用 helloworld？

这是关键的排查思路：**一个不存在的构建目标，一定是某个配置文件里残留了引用**。
用全文搜索定位：

```bash
cd device/soc/hisilicon/ws63v100/sdkv106

# 在所有配置类文件中搜索 helloworld
grep -rn "helloworld" \
  --include="*.txt" --include="*.cmake" --include="*.mk" \
  --include="*.gn" --include="*.gni" .
```

命中两个“真凶”（其余命中均为构建输出目录的自动生成文件，可忽略）：

```python
# ① build/config/target_config/ws63/config.py  约第 107 行
    "xiaohong",
    "helloworld",     # ← 多出来的
```

```cmake
# ② libs_url/ws63/cmake/ohos.cmake  约第 41 行
    "xiaohong" "helloworld"           # ← 多出来的
```

### 3.3 原理：这两个文件是干什么的

- **`config.py`**：定义各构建目标（target）的组件列表 `components`。
  列表里的每一项会被编译成 `lib<名字>.a`，最终链接进 `ws63-liteos-app.elf`。
- **`ohos.cmake`**：定义 OHOS（OpenHarmony）侧组件与 SDK 库的对应关系，
  `COMPONENT_LIST` 中列出的名字会被安装/链接为 `lib<名字>.a`。

两处都出现 `helloworld`，说明**曾有人想把一个叫 helloworld 的示例应用加进构建**，
但该示例只有 `BUILD.gn`（属于 hb/GN 侧），**SDK 的 CMake 构建系统里根本没有它的源文件与 CMakeLists.txt**，
于是 `libhelloworld.a` 就成了一个“只被引用、无法生成”的悬空目标。

验证一下 helloworld 是否真的没有源码：

```bash
find device/soc/hisilicon/ws63v100/sdkv106 -iname "*helloworld*" 2>/dev/null
# 结果：SDK 内不存在任何 helloworld 源码/目录（仅 vendor 下的示例目录）
```

### 3.4 修复

由于 helloworld 应用源码并不存在于 SDK 构建体系中（只有 vendor 侧的 GN 示例），
最合理的修复是 **从组件列表里移除残留引用**：

```python
# config.py
    "xiaohong",
-   "helloworld",
```

```cmake
# ohos.cmake
-   "xiaohong" "helloworld"
+   "xiaohong"
```

### 3.5 小结

| 项目 | 内容 |
|---|---|
| 错误类型 | Make：No rule to make target |
| 根因 | 组件列表残留 `helloworld` 引用，但 SDK 无对应构建规则 |
| 修复 | 从 `config.py` 与 `ohos.cmake` 移除 `helloworld` |
| 教训 | 新增/删除应用时，务必同步维护 `config.py` 的组件列表 |

---

## 4. 错误二：fs_adapt_* 符号未定义（链接失败）

### 4.1 现象

修复错误一后重新构建，错误变了——说明**构建前进了**。此时报的是**链接期**错误：

```
ld: .../interim_binary/ws63/libs/ohos/ws63-liteos-app/libhal_file_static.a(libhal_file_static.littlefs_xts_adapt.o):
     in function `.L0 ':
littlefs_xts_adapt.c:(.text.HalFileOpen+0x18): undefined reference to `fs_adapt_open'
littlefs_xts_adapt.c:(.text.HalFileClose+0x18): undefined reference to `fs_adapt_close'
littlefs_xts_adapt.c:(.text.HalFileRead+0x18):  undefined reference to `fs_adapt_read'
littlefs_xts_adapt.c:(.text.HalFileWrite+0x18): undefined reference to `fs_adapt_write'
littlefs_xts_adapt.c:(.text.HalFileStat+0x18):  undefined reference to `fs_adapt_stat'
collect2: 错误： ld 返回 1
make[2]: *** [CMakeFiles/ws63-liteos-app.dir/build.make:272：ws63-liteos-app.elf] 错误 1
```

翻译成人话：

> 预编译库 `libhal_file_static.a` 里的 `littlefs_xts_adapt.o` 调用了
> `fs_adapt_open / close / read / write / stat` 这组函数，
> 但链接时**整个工程里没人提供这些函数的实现**。

### 4.2 定位：谁定义了 fs_adapt_* ？

先搜“谁定义了这些函数”，再搜“谁引用了这些函数”：

```bash
# ① 找定义（实现）
grep -rln "fs_adapt_open" --include="*.c" . | grep -v output

# 命中：
# ./middleware/chips/ws63/littlefs/littlefs_adapt.c   ← 定义在这
# ./middleware/utils/fs/adapter/xts/littlefs_xts_adapt.c  ← 这是引用方
# ./application/samples/wifi/ohos_connect/hilink_adapt/adapter/hilink_kv_adapter.c
```

再确认 `littlefs_adapt.c` 归属哪个组件：

```bash
cat middleware/chips/ws63/littlefs/CMakeLists.txt
```

关键一行：

```cmake
set(COMPONENT_NAME "littlefs_adapt_ws63")
```

也就是说：**`fs_adapt_*` 由组件 `littlefs_adapt_ws63` 提供**。

### 4.3 定位：该组件为什么没被编译链接？

回到 `config.py` 的组件列表，找到这一行：

```python
    "little_fs", # "littlefs_adapt_ws63",
```

真相大白：**`littlefs_adapt_ws63` 被注释掉了**！
SDK 的 CMake 构建系统会遍历组件列表，只构建列表里出现的组件。
`littlefs_adapt_ws63` 不在列表中 → `liblittlefs_adapt_ws63.a` 根本没被编译 →
链接时 `fs_adapt_*` 符号缺失。

> 顺带说明 `little_fs` 是什么：它是 littlefs 文件系统**本体**（`open_source/littlefs` 的
> `lfs.c / lfs_util.c`，由 `build/cmake/open_source/littlefs.cmake` 定义组件 `little_fs`），
> 而 `littlefs_adapt_ws63` 是 **WS63 平台的适配层**（分区操作、加解锁、mount 实现）。
> 二者是“库 + 平台适配”的关系，缺一不可。

**为什么之前能编译过？** 对比 `ws63_liteos_xts.config` 的组件列表可以发现，
xts 目标里两个组件都启用了；而 app 目标里 `littlefs_adapt_ws63` 被注释。
一旦预编译的 `libhal_file_static.a`（OHOS 侧产物，内含 `littlefs_xts_adapt.o`）被更新，
其引用的 `fs_adapt_*` 就找不到实现，链接立刻断裂。

### 4.4 修复

取消注释，让适配层组件参与构建：

```python
-   "little_fs", # "littlefs_adapt_ws63",
+   "little_fs", "littlefs_adapt_ws63",
```

### 4.5 小结

| 项目 | 内容 |
|---|---|
| 错误类型 | 链接期：undefined reference |
| 根因 | 组件 `littlefs_adapt_ws63` 被注释，`fs_adapt_*` 无实现 |
| 修复 | 在 `config.py` 组件列表中启用 `littlefs_adapt_ws63` |
| 教训 | 预编译库（interim_binary）更新后，SDK 侧依赖组件必须保持启用 |

---

## 5. 错误三：CONFIG_LFS_PARTITION_ID 未定义（编译失败）

### 5.1 现象

修复错误二后再次构建，错误又变了——`littlefs_adapt_ws63` 组件**开始编译了**，
但编译 `littlefs_adapt.c` 时报**宏未定义**：

```
middleware/chips/ws63/littlefs/littlefs_adapt.c:97:49:
  错误： 'CONFIG_LFS_PARTITION_ID' undeclared (first use in this function);
  did you mean 'CONFIG_EFUSE_REGION_NUM'?
    errcode_t ret_val = uapi_partition_get_info(CONFIG_LFS_PARTITION_ID, &info);
                                                    ^~~~~~~~~~~~~~~~~~~~~~~
make[2]: *** [.../CMakeFiles/littlefs_adapt_ws63.dir/littlefs_adapt.c.obj] 错误 1
```

翻译成人话：

> 源码第 97 行用了 `CONFIG_LFS_PARTITION_ID` 这个宏，
> 但编译器头文件里（mconfig.h / menuconfig.h）**根本没有这个宏的定义**。

### 5.2 定位：这个宏应该从哪来？

WS63 SDK 的宏由 Kconfig 系统生成：`menuconfig` 配置（`.config` 文件）
→ 由 `build/script/usr_config.py`（基于 kconfiglib）生成 `mconfig.h`，
再通过编译参数 `-include ../../mconfig.h` 注入每个编译单元。

先看三个候选配置里这个宏是否存在：

```bash
grep -n "LFS" build/config/target_config/ws63/menuconfig/acore/ws63_liteos_app.config
# 539:# CONFIG_MIDDLEWARE_SUPPORT_LFS is not set   ← 开关是关的！
# 540:CONFIG_LFS_PARTITION_ID=33                    ← 宏值却在

grep -n "LFS" build/config/target_config/ws63/menuconfig/acore/ws63_liteos_xts.config
# 516:CONFIG_MIDDLEWARE_SUPPORT_LFS=y               ← xts 里开关是开的！
# 517:CONFIG_LFS_PARTITION_ID=33
```

再看生成出来的头文件里有没有：

```bash
grep -n "LFS" output/ws63/acore/ws63-liteos-app/kernel/liteos/.../menuconfig/include/menuconfig.h
# 结果：什么都没有
```

### 5.3 原理：Kconfig 的门控机制

关键就在这里：

```bash
# app 配置（报错的）：
# CONFIG_MIDDLEWARE_SUPPORT_LFS is not set   ← 父开关关闭
CONFIG_LFS_PARTITION_ID=33                    ← 子选项残留

# xts 配置（能编过的）：
CONFIG_MIDDLEWARE_SUPPORT_LFS=y               ← 父开关打开
CONFIG_LFS_PARTITION_ID=33
```

在 Kconfig 体系中，`CONFIG_LFS_PARTITION_ID` 等 LFS 相关选项通常被定义在
`CONFIG_MIDDLEWARE_SUPPORT_LFS` 这个**父选项的门控（depends on / if）之下**。
当父开关为 `# ... is not set` 时：

- 子选项即使残留 `=33` 这样的赋值行，**也不会被写入生成的头文件**；
- 编译器自然看不到 `CONFIG_LFS_PARTITION_ID`，直接报 undeclared。

也就是说：**第 540 行的宏值是个“死配置”**，只要父开关不开，它永远进不了编译环境。

### 5.4 修复

把父开关打开，与 xts 配置保持一致：

```bash
# build/config/target_config/ws63/menuconfig/acore/ws63_liteos_app.config
-# CONFIG_MIDDLEWARE_SUPPORT_LFS is not set
+CONFIG_MIDDLEWARE_SUPPORT_LFS=y
 CONFIG_LFS_PARTITION_ID=33
```

重新构建时，SDK 的 `cmake_builder.py` 会重新执行
`mconfig("savemenuconfig", ...)` 重新生成 `mconfig.h`，
`CONFIG_LFS_PARTITION_ID` 就会进入编译环境。

### 5.5 小结

| 项目 | 内容 |
|---|---|
| 错误类型 | 编译期：undeclared identifier |
| 根因 | Kconfig 父开关 `CONFIG_MIDDLEWARE_SUPPORT_LFS` 未开启，子宏不进入头文件 |
| 修复 | 在 app 的 menuconfig 中开启 `CONFIG_MIDDLEWARE_SUPPORT_LFS=y` |
| 教训 | `.config` 里残留 `=数字` 不等于宏生效，要看父开关与生成的头文件 |

---

## 6. 最终验证与产物

### 6.1 重新构建

三处修复全部完成后，再次执行全量构建：

```bash
cd /home/parallels/xiaohong
hb build -f
```

> 构建耗时较长，建议后台运行并轮询日志：
> ```bash
> nohup hb build -f > /tmp/hb_build.log 2>&1 &
> tail -f /tmp/hb_build.log
> ```

### 6.2 验证点

1. **日志无错误**：`grep -cE "OHOS ERROR" /tmp/hb_build.log` 结果为 `0`；
2. **SDK 侧产物生成**：`ws63-liteos-app.bin` 与 `ws63-liteos-app.elf` 出现在
   `sdkv106/output/ws63/acore/ws63-liteos-app/`；
3. **适配层库已链接**：`liblittlefs_adapt_ws63.a` 出现在 SDK 输出目录；
4. **hb 最终打包成功**：固件包出现在 hb 输出目录。

### 6.3 最终产物

```
out/xiaohong/xiaohong-kb/ws63-liteos-app/
├── ws63-liteos-app_all.fwpkg          # 全量固件包（约 1.67 MB）
└── ws63-liteos-app_load_only.fwpkg    # 仅加载固件包（约 1.54 MB）
```

至此，`hb build -f` 从失败到成功，全程跑通。

---

## 7. 改动清单汇总

本次修复共修改 3 个文件（均在 `device/soc/hisilicon/ws63v100/sdkv106/` 内）：

| # | 文件 | 改动 | 解决 |
|---|---|---|---|
| 1 | `build/config/target_config/ws63/config.py` | 移除 `"helloworld"`；启用 `littlefs_adapt_ws63` | 错误一、二 |
| 2 | `libs_url/ws63/cmake/ohos.cmake` | 移除 `"helloworld"` | 错误一 |
| 3 | `build/config/target_config/ws63/menuconfig/acore/ws63_liteos_app.config` | `CONFIG_MIDDLEWARE_SUPPORT_LFS` 由 not set 改为 `y` | 错误三 |

> 注：工作区还有两处**历史遗留**的未提交改动（`script/efuse.csv`、`hm_build.sh` 中
> 的 `QEMU_LD_PREFIX` 与 `-j4`），与本次问题无关，未做修改。

---

## 8. 经验总结：构建问题的通用排查方法论

一次成功的构建问题排查，可以沉淀为下面这套**可复用的方法论**：

### 8.1 三层错误分类，对症下药

| 错误阶段 | 典型报错特征 | 排查入口 |
|---|---|---|
| **配置期** | `No rule to make target`、`unknown target` | 组件列表（config.py / *.cmake）残留引用 |
| **编译期** | `undeclared identifier`、`error: 'XXX'` | Kconfig 开关未开、头文件路径缺失 |
| **链接期** | `undefined reference to 'XXX'` | 提供该符号的组件未编译/未链接 |

判断“构建是否前进了”最简单的方法：**每次报错的位置/类型变了，说明修复生效了**。

### 8.2 五个排查步骤（0 → 1）

1. **复现**：原样执行 `hb build -f`，把完整日志存下来；
2. **读日志**：先看 `error.log` 的最后 50 行，找**第一条**真正的错误（忽略前面的 warning）；
3. **找源头**：对错误中出现的符号/文件名全仓库 `grep`，区分「定义方」与「引用方」；
4. **查配置**：核对组件列表（config.py / ohos.cmake）、Kconfig 开关（menuconfig/*.config）与生成头文件（mconfig.h / menuconfig.h）；
5. **最小修复**：一次只改一个点，改完重新构建，验证错误是否前进/消失。

### 8.3 三个容易踩的坑

- **`hb` 的报错信息不直接**：`OHOSException: COMPILE Failed` 只是壳，
  真正的错误永远在 `error.log` / `build.log` 里，务必去读；
- **`.config` 里有值 ≠ 宏生效**：Kconfig 父开关关闭时，子选项不会进入生成头文件，
  判断标准是**生成后的 `mconfig.h` / `menuconfig.h`**；
- **预编译库会“过时”**：`interim_binary/` 下的预编译 OHOS 库更新后，
  SDK 侧依赖的适配组件必须保持启用，否则链接必然断裂。

### 8.4 常用排查命令速查

```bash
# 搜索符号定义/引用
grep -rln "符号名" --include="*.c" .

# 查看组件归属
cat <组件目录>/CMakeLists.txt        # 看 set(COMPONENT_NAME "...")

# 检查组件是否在列表里
grep -n "组件名" build/config/target_config/ws63/config.py

# 检查 Kconfig 开关与宏
grep -n "宏名" build/config/target_config/ws63/menuconfig/acore/*.config
grep -n "宏名" output/ws63/acore/ws63-liteos-app/**/menuconfig.h

# 确认产物是否生成
ls -la output/ws63/acore/ws63-liteos-app/ws63-liteos-app.{bin,elf}
ls -la out/<product>/<product>-kb/ws63-liteos-app/*.fwpkg
```

---

*本文基于真实构建排障过程整理，命令与路径均来自实际项目。*
*如有环境差异（芯片、SDK 版本、产品名），请按第 8 节方法论适配。*
