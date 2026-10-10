# examples

这里是xiaohong-se项目的示例程序，请先阅读 //vendor/atomgit/xiaohong-se/README_CN.md 文档，了解整个项目的详情。

## 列表

| 名称                                           | 说明                                                         |
| ---------------------------------------------- | ------------------------------------------------------------ |
| 00_helloworld                                  | 简单的HelloWorld程序                                         |
| 26_smart_fan                                   | 智能风扇模组控制程序，可以通过KEY键控制风扇的开关；了解GPIO的控制。 |
| 29_sht30_sensor                                | 温湿度传感器，自动读取温湿度数据，打印到终端；了解I2C总线的原理和设备驱动开发。 |
| 50_sle_demo_n_client_m_server                  | 纯软件的星闪功能，实现多个客户端与多个服务器之间的组网和通信；了解星闪的工作流程。【注意查看BUILD.gn文件的config设置，server和client的编译配置都在这里】 |
| 51_sle_demo_1_client_29_1_server_26            | 将29示例程序作为客户端、26示例程序作为服务端，实现星闪的1v1组网和通信，温度升高到33度（手按住温湿度传感器提升温度）时，打开风扇；低于33度时，关闭风扇。 |
| 52_sle_demo_2_client_26_29_1_server_xiaohongse | 给小鸿SE项目增加一个SLE Server任务，通过 config_sle_server_enbable 来打开关闭该功能；在温湿度传感器模组配套使用52_sle_demo_2_client_26_29_1_server_xiaohongse示例程序；可实现模组与小鸿SE主板的组网、通信、数据查询与上报。 |



## 使用

请按以下步骤修改代码，并编译出镜像，烧录到对应的模块上。

### 步骤1

修改：//vendor/atomgit/xiaohong-se/BUILD.gn

```
group("xiaohong-se") {
  deps = []

# deps += [ "xiaohong:xiaohong" ]  # 编译在 xiaohong-se 主板上的WS63芯片上运行的示例程序，打开这行
                                   # 编译在 xiaohong-se 独立模块的WS63芯片上运行的示例程序，关闭这行
  deps += [ "examples:demos" ]     # 编译 examples 目录下的示例程序
}

# 注意：
# 在独立的模块中运行 examples 目录下的示例程序，不需要运行 xiaohong-se 的ai agent程序
# 需要注释掉 [ "xiaohong:xiaohong" ]，可能跑不起来，容易造成困扰
# 在xiaohong-se主板上的WS63芯片上运行 examples 目录下的示例程序，并与主板上的P4芯片进行数据交换，
# 则需要同时打开 [ "xiaohong:xiaohong" ] 和 [ "examples:demos" ]
```

### 步骤2

修改：//vendor/atomgit/xiaohong-se/examples/BUILD.gn

```
import("//build/lite/config/component/lite_component.gni")
lite_component("demos") {
  features = [
#   "00_helloworld:helloworld",
#   "26_smart_fan:smart_fan_demo",
#   "29_sht30_sensor:sht30_sensor_demo",
#   "50_sle_demo_n_client_m_server:sle_demo_n_client_m_server",
  ]
}

# 注意，想要运行哪个示例程序，就打开对应的示例程序，去编译即可。
```

### 步骤3

修改：//device/soc/hisilicon/ws63v100/sdkv106/build/config/target_config/ws63/config.py

```
target = {
    'ws63-liteos-app': {
        ......
        'ram_component': [
            ......
            "samgr", "samgr_adapter", "samgr_source",
            "hal_iothardware", "wifiservice",
    "lvgl",
    "opus",
#   "xiaohong",                    # [ "xiaohong:xiaohong" ]
#   "helloworld",                  # 00_helloworld
#   "smart_fan_demo",              # 26_smart_fan
#   "sht30_sensor_demo",           # 29_sht30_sensor
#   "sle_demo_n_client_m_server",  # 50_sle_demo_n_client_m_server
        ],
    },
}

# 注意，想要运行哪个示例程序，就打开对应的示例程序，去编译即可。
```

### 步骤4

修改：//device/soc/hisilicon/ws63v100/sdkv106/libs_url/ws63/cmake/ohos.cmake

```
elseif(${TARGET_COMMAND} MATCHES "ws63-liteos-app")
    set(COMPONENT_LIST
        ......
        "hal_iothardware" "wifiservice"

#   "xiaohong"                    # [ "xiaohong:xiaohong" ]
#   "helloworld"                  # 00_helloworld
#   "smart_fan_demo"              # 26_smart_fan
#   "sht30_sensor_demo"           # 29_sht30_sensor
#   "sle_demo_n_client_m_server"  # 50_sle_demo_n_client_m_server
    )
endif()

# 注意，想要运行哪个示例程序，就打开对应的示例程序，去编译即可。
```

## 编译

执行 **hb build -f** 命令编译出固件，将生成的固件烧录到独立的模块中去即可。

