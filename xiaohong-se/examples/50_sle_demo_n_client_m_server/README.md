# 50_sle_demo_n_client_m_server



---

## 一、本实验做什么

在小鸿/小鸿SE的（N x M）个独立模块上运行本示例程序。

通过学习本示例程序，了解修改哪些相关的配置文件、如何编译、如何烧录固件。

了解如何在多个星闪服务端和客户端之间进行星闪组网、通信。

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
| 1 | //vendor/atomgit/xiaohong-se/BUILD.gn | 需要编译哪个示例程序就打开哪个示例程序 |
| 2 | //vendor/atomgit/xiaohong-se/examples/BUILD.gn | 需要编译哪个示例程序就打开哪个示例程序 |
| 3 | //device/soc/hisilicon/ws63v100/sdkv106/build/config/target_config/ws63/config.py | 需要编译哪个示例程序就打开哪个示例程序 |
| 4 | //device/soc/hisilicon/ws63v100/sdkv106/libs_url/ws63/cmake/ohos.cmake | 需要编译哪个示例程序就打开哪个示例程序 |
| 5 | //vendor/atomgit/xiaohong-se/examples/50_sle_demo_n_client_m_server/BUILD.gn | 修改示例程序本身的配置 |



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
# deps += [ "xiaohong:xiaohong" ]    # 注释掉这行
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
#   "00_helloworld:helloworld",
#   "26_smart_fan:smart_fan_demo",
#   "29_sht30_sensor:sht30_sensor_demo",
    "50_sle_demo_n_client_m_server:sle_demo_n_client_m_server",  # 本例运行这个示例程序，打开它
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
#   "xiaohong",                    # [ "xiaohong:xiaohong" ] 释掉这行
#   "helloworld",                  # 00_helloworld
#   "smart_fan_demo",              # 26_smart_fan
#   "sht30_sensor_demo",           # 29_sht30_sensor
    "sle_demo_n_client_m_server",  # 50_sle_demo_n_client_m_server，本例运行该示例程序，打开它
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

#   "xiaohong"                    # [ "xiaohong:xiaohong" ] 注释掉这行
#   "helloworld"                  # 00_helloworld
#   "smart_fan_demo"              # 26_smart_fan
#   "sht30_sensor_demo"           # 29_sht30_sensor
    "sle_demo_n_client_m_server"  # 50_sle_demo_n_client_m_server，本例运行该示例程序，打开它
    )
endif()

# 注意，想要运行哪个示例程序，就打开对应的示例程序，去编译即可。
```



### 4.4 检查上述文件修改是否一致

检查上述文件 2/3/4 的修改，确保都打开了 "50_sle_demo_n_client_m_server:sle_demo_n_client_m_server" 示例程序。



### 4.5 修改角色配置

打开

```
//vendor/atomgit/xiaohong-se/examples/50_sle_demo_n_client_m_server/BUILD.gn
```

文件，根据需要修改如下几个字段：

```
declare_args() {
  config_sle_role = 1  # [1]:sle server      / [0]:sle client
  config_sle_api  = 1  # [1]:OH API          / [0]:WS63 SDK API
  config_mac_addr = 0  # [1]:custom mac addr / [0]:random mac addr
}

static_library("sle_demo_n_client_m_server") {
  defines = [ "SLE_SERVER_NAME=\"n_client_m_server\"" ]
  sources = []
  include_dirs = []

  if (config_sle_role == 1) {
    sources += [
      "sle_server_demo_0.c",
      "sle_server_adv.c",
      "sle_server.c",
    ]
    defines += [ "SLE_N_CLIENT_1_SERVER=2" ]  # N 默认每个Server最多可以连接N个Client
  } else {
    sources += [
      "sle_client_demo_0.c",
      "sle_client.c",
    ]
    defines += [ "SLE_1_CLIENT_M_SERVER=2" ]  # M 默认每个Client最多可以连接M个Server
  }

```

修改角色  config_sle_role 的配置，修改 SLE_N_CLIENT_1_SERVER、SLE_1_CLIENT_M_SERVER 的数值。

本实验至少要编译两次：config_sle_role=0，config_sle_role=1 各编译一次：

config_sle_role=0 时生成的固件，烧录到 N 个客户端模块上；

config_sle_role=1 时生成的固件，烧录到 M 个服务端模块上；



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

---

## 六、烧录与串口验证

### 6.1 烧录

1. USB 连接对应 WS63 开发板或独立模块。
2. 用 **BurnTool**（或你们实验提供的烧录工具）选择 **ws63-liteos-app.bin** 镜像烧录。
3. 参考 //vendor/atomgit/xiaohong-se/README_CN.md 文档中 "**4. 烧录独立模块的WS63芯片的固件**" 小节的说明烧录固件到多个独立模块的WS63芯片中。

### 6.1 串口日志

在烧录完成后，客户端和服务端分别复位，系统稳定运行后分别通过串口工具打印出如下的日志。

客户端日志大概如下：

```
[  SleClientTask         ] ========================================================
[  conn/ready/ntf status ] max[2] conn[2] ready[2] ntf[2]
[sle_client_send_data]==>> len[27]{Client[0x00] heart beat: 25}
[sle_client_broadcast_msg] ssapc_write_req conn_id[0x00] handle[0x0011]  ret[0x00]:OK
[sle_client_broadcast_msg] ssapc_write_req conn_id[0x01] handle[0x0011]  ret[0x00]:OK
[sle_client_write_cfm_cbk] client_id[0x00] conn_id[0x00] handle[0x0011] type[0x00] status[0x00]
[sle_client_write_cfm_cbk] client_id[0x00] conn_id[0x01] handle[0x0011] type[0x00] status[0x00]
[sle_client_read_cfm_cbk ] ---------------------------------------------------------
[sle_client_read_cfm_cbk ] client_id[0x00] conn_id[0x00] status[0x00] handle[0x0000] type[0x0A] len[47]
[sle_client_read_cfm_cbk ] -> sle_client_recv_data::
[sle_client_recv_data]<<== server_addr[0x12] conn_id[0x00] len[47]{Default ACK_From_Server: conn_id[0x00] addr[12]}
[sle_client_read_cfm_cbk ] ---------------------------------------------------------
[sle_client_read_cfm_cbk ] client_id[0x00] conn_id[0x01] status[0x00] handle[0x0000] type[0x0A] len[47]
[sle_client_read_cfm_cbk ] -> sle_client_recv_data::
[sle_client_recv_data]<<== server_addr[0xE1] conn_id[0x01] len[47]{Default ACK_From_Server: conn_id[0x00] addr[E1]}

```

服务端日志大概如下：

```
[  SleServerTask        ] ========================================================
[  conn status          ] max[2] conn[1]
[sle_server_send_data   ] conn_cnt[1] len[42]{====>>>>cnt[29] 4E:2A:5D:79:83:12 ====>>>>}
[sle_server_send_notify_to_conn] >>>> server_id[0x01] conn[0x00] len[42]{====>>>>cnt[29] 4E:2A:5D:79:83:12 ====>>>>}
[ssaps_write_request_cbk] ------------------------------------------<0
[ssaps_write_request_cbk] server_id[0x01] conn_id[0x00] handle[0x0011] type[0x00] need_rsp[0] status[0x00] len[28]
[sle_server_recv_data   ] <<<<<<<<<<<<<<< conn_id[0x00] client_addr[0x5C] len[28]{Client[0x00] heart beat: 43}
[ssaps_write_request_cbk] sle_server_send_notify_to_conn(default rsp msg:[null, 0])
[sle_server_send_notify_to_conn] >>>> server_id[0x01] conn[0x00] len[47]{Default ACK_From_Server: conn_id[0x00] addr[12]}
[ssaps_write_request_cbk] ------------------------------------------>5
[ssaps_read_request_cbk ] server_id[0x01] conn_id[0x00] status[0x00] handle[0x0011] type[0x00] need_rsp[0]

```

