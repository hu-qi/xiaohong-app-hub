# 修改记录

## 20261001_oh7.0_v2.1.0【+ fbb_ws63 sdk固件】

```
20261001_oh7.0_v2.1.0 目录下：
xiaohong_20261001_ohos_log_error_v2.0.2_1_disable_lcd&lvgl.fwpkg
xiaohong_20261001_ohos_log_error_v2.0.2_2_enable_tls.fwpkg
xiaohong_20261001_ohos_log_error_v2.1.0.fwpkg + xiaohong_20261001_ohos_log_error_v2.1.0_ota.bin
```

小鸿AI(WS63): 

xiaohong_20261001_ohos_log_error_v2.0.2_1_disable_lcd&lvgl.fwpkg：纯语音对话版本（关闭显示屏和LVGL任务），增大语音对话缓存，切换新的ota服务器，优化字库下载流程。

xiaohong_20261001_ohos_log_error_v2.0.2_2_enable_tls.fwpkg：纯语音对话版本。打开Mongoose的TLS特性，支持https协议；OTA升级包、字库下载切换为使用Mongoose+https方式。

xiaohong_20261001_ohos_log_error_v2.1.0.fwpkg + xiaohong_20261001_ohos_log_error_v2.1.0_ota.bin：小鸿AI(WS63):  正常版本v2.1.0与OTA升级包。

xiaohong_20261001_liteos-m_log_error_v9.9.9.fwpkg：小鸿AI(WS63):  fbb_ws63 版本，同步更新上述修改。

## 20260828_oh7.0_v2.0.2【+ fbb_ws63 sdk固件】

```
20260828_oh7.0_v2.0.2 目录下：
xiaohong_20260828_ohos_log_error_v2.0.2.fwpkg
xiaohong_20260828_ohos_log_error_v2.0.2_ota.bin
xiaohong_20260828_liteos-m_log_error_v9.9.9.fwpkg  # 不带OpenHarmony框架仅使用海思fbb_ws63 sdk的固件
```

小鸿AI(WS63): UI的显示缓存小量增大，稍微提升对话文字刷新效果；更多的内存用于增大下行语音数据缓存Slot，以便更大限度提升语音播放流畅性。

新增一个不带OpenHarmony框架仅使用海思fbb_ws63 sdk的固件（xiaohong_20260828_liteos-m_log_error_v9.9.9.fwpkg），版本v9.9.9，对话表情和对话状态显示蓝色。

## 20260824_oh7.0_v2.0.1

```
20260824_oh7.0_v2.0.1 目录下：
xiaohong_20260824_ohos_log_error_v2.0.1.fwpkg
xiaohong_20260824_ohos_log_error_v2.0.1_ota.bin
```

小鸿AI/KB/SE(WS63): 参考OH docs的轻量系统小型化适配指导文档，修改小鸿的config.json配置小型化。

空闲内存空间可增大45KB左右，取其中12KB增大UI的显示缓存，有效提升对话文字刷新效果；再取其中8KB增大下行语音数据缓存Slot，可提升语音播放流畅性。

## 20260823_oh7.0_v2.0.0

```
20260823_oh7.0_v2.0.0 目录下：
xiaohong_20260823_ohos_log_error_v2.0.0.fwpkg
xiaohong_20260823_ohos_log_error_v2.0.0_ota.bin
```

小鸿AI(WS63): OH底座升级到7.0-Release; 裁剪掉正式固件用不上的xts子系统和hiviewdfx子系统的组件；版本改为2.0.0。

