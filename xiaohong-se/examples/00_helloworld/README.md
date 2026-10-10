# 00_helloworld



---

## 一、本实验做什么

在小鸿SE主板（或独立模块）上运行HelloWorld程序，了解使用示例程序时，需要修改哪些相关的配置文件、如何编译、如何烧录固件。

**常见两种练法：**

- **练法 A**：只需 **1 块** 小鸿SE主板，在小鸿SE主板上的WS63芯片上运行HelloWorld程序。
- **练法 B**：只需 **1 块** 小鸿SE的独立模块，在小鸿SE的独立模块的WS63芯片上运行HelloWorld程序。

---

## 二、实验前准备

1. 已按小鸿/OpenHarmony 官方文档搭好开发环境、拉取项目代码、能正常执行 `hb` 命令（详情见 //vendor/atomgit/README_CN.md 文件）。
2. 源码根目录记为 **`A_OH60T`**（即你拉取下来的项目代码根目录，下文路径均相对此根目录）。
4. 准备好烧录工具（如 **BurnTool**）和串口工具（调试串口的波特率 **115200**）。

---

## 三、必改文件总览

把本案例编进 WS63 固件，需要修改如下四个文件：

| 序号 | 文件路径 | 改什么 |
| :---: | :--- | :--- |
| 1 | //vendor/atomgit/xiaohong-se/BUILD.gn | 根据选择 练法A、练法B来修改 |
| 2 | //vendor/atomgit/xiaohong-se/examples/BUILD.gn | 需要编译哪个示例程序就打开哪个示例程序 |
| 3 | device/soc/hisilicon/ws63v100/sdkv106/build/config/target_config/ws63/config.py | 需要编译哪个示例程序就打开哪个示例程序 |
| 4 | device/soc/hisilicon/ws63v100/sdkv106/libs_url/ws63/cmake/ohos.cmake | 需要编译哪个示例程序就打开哪个示例程序 |



---

## 四、修改编译配置

### 4.1 文件 1：//vendor/atomgit/xiaohong-se/BUILD.gn

打开：

```text
//vendor/atomgit/xiaohong-se/BUILD.gn
```

文件，根据 练法A、练法B修改如下代码：

```gn
group("xiaohong-se") {
  deps = []
# deps += [ "xiaohong:xiaohong" ]    # 练法A打开这行，练法B注释掉这行
  deps += [ "examples:demos" ]
}
```



### 4.2 文件 2：//vendor/atomgit/xiaohong-se/examples/BUILD.gn

打开：

```text
//vendor/atomgit/xiaohong-se/examples/BUILD.gn
```

文件，需要编译哪个示例程序就打开哪个示例程序，其余的注释掉：

```gn
import("//build/lite/config/component/lite_component.gni")
lite_component("demos") {
  features = [
    "00_helloworld:helloworld",      # 本例运行 00_helloworld 示例程序，打开它
#   "26_smart_fan:smart_fan_demo",
#   "29_sht30_sensor:sht30_sensor_demo",
#   "50_sle_demo_n_client_m_server:sle_demo_n_client_m_server",
  ]
}

# 注意，想要运行哪个示例程序，就打开对应的示例程序，去编译即可。
```



### 4.2 文件 3：config.py

打开：

```text
//device/soc/hisilicon/ws63v100/sdkv106/build/config/target_config/ws63/config.py
```

修改如下代码

```python
target = {
    'ws63-liteos-app': {
        ......
        'ram_component': [
            ......
            "samgr", "samgr_adapter", "samgr_source",
            "hal_iothardware", "wifiservice",
    "lvgl",
    "opus",
#   "xiaohong",                    # [ "xiaohong:xiaohong" ] 练法A打开这行，练法B注释掉这行
    "helloworld",                  # 00_helloworld，本例运行该示例程序，打开它
#   "smart_fan_demo",              # 26_smart_fan
#   "sht30_sensor_demo",           # 29_sht30_sensor
#   "sle_demo_n_client_m_server",  # 50_sle_demo_n_client_m_server
        ],
    },
}

# 注意，想要运行哪个示例程序，就打开对应的示例程序，去编译即可。
```



### 4.3 文件 4：ohos.cmake

打开：

```text
//device/soc/hisilicon/ws63v100/sdkv106/libs_url/ws63/cmake/ohos.cmake
```

修改如下代码：

```cmake
elseif(${TARGET_COMMAND} MATCHES "ws63-liteos-app")
    set(COMPONENT_LIST
        ......
        "hal_iothardware" "wifiservice"

#   "xiaohong"                    # [ "xiaohong:xiaohong" ] 练法A打开这行，练法B注释掉这行
    "helloworld"                  # 00_helloworld，本例运行该示例程序，打开它
#   "smart_fan_demo"              # 26_smart_fan
#   "sht30_sensor_demo"           # 29_sht30_sensor
#   "sle_demo_n_client_m_server"  # 50_sle_demo_n_client_m_server
    )
endif()

# 注意，想要运行哪个示例程序，就打开对应的示例程序，去编译即可。
```



### 4.4 检查上述文件修改是否一致

检查上述文件 2/3/4 的修改，确保都打开了 "00_helloworld:helloworld" 示例程序。



---

## 五、编译板端固件

在工程根目录 `A_OH60T` 打开终端，按顺序执行：

### 步骤 1：选择产品

```bash
hb set
```

交互选择（用方向键 + 回车）：

1. **系统类型**：选 **mini**
2. **产品路径**：选 **`vendor/atomgit/xiaohong-se`**（产品名一般为 **xiaohong-se**）

### 步骤 2：全量编译

先清理：

```bash
hb clean
```

然后编译：

```bash
hb build -f
```

等待结束，终端应出现 **build success** 或类似成功提示，且**无 error**（warning 可暂时忽略）。

### 步骤 3：找到烧录文件

编译成功后，固件在：

```text
out/xiaohong/xiaohong-se/ws63-liteos-app/
```

其中常见烧录文件包括 **ws63-liteos-app.bin** 等（以目录内实际文件为准）。

注意，练法A/B编译后生成的烧录文件都是这个。

---

## 六、烧录与串口验证

### 6.1 烧录

1. USB 连接对应 WS63 开发板或独立模块。
2. 用 **BurnTool**（或你们实验提供的烧录工具）选择 **ws63-liteos-app.bin** 镜像烧录。
3. **练法 A** 时：参考 //vendor/atomgit/xiaohong-se/README_CN.md 文档中 "**3. 烧录固件主板上的WS63芯片的固件**" 小节的说明烧录固件到主板上的WS63芯片中。
4. **练法 B** 时：参考 //vendor/atomgit/xiaohong-se/README_CN.md 文档中 "**4. 烧录独立模块的WS63芯片的固件**" 小节的说明烧录固件到独立模块上的WS63芯片中。

### 6.1 串口日志

```
[HelloWorldTask]: [17]HelloWorld ......
[HelloWorldTask]: [18]HelloWorld ......
[HelloWorldTask]: [19]HelloWorld ......
[HelloWorldTask]: [20]HelloWorld ......
[HelloWorldTask]: [21]HelloWorld ......
[HelloWorldTask]: [22]HelloWorld ......
[HelloWorldTask]: [23]HelloWorld ......
```

练法A/B在烧录完成后，复位，系统稳定运行后打印出如上的日志即表明示例程序运行正常。
