# 修改记录

## xh-kb-v3.0.0_20260907

- xiaohong_kb_ws63_20260907_support_sle_only.fwpkg  是默认的只支持SLE星闪协议的键盘端WS63的固件；
- xiaohong_kb_ws63_20260907_support_sle+ble.fwpkg    是供试用的同时支持SLE/BLE两种协议的键盘端WS63的固件；
- xiaohong_kb_bs21e_dongle.fwpkg 是 接收端Dongle BS21E的固件。

小鸿KB(WS63):  调整按键分发路径，新增键盘作为BLE外设功能，可直接被PC(蓝牙Central)扫描、连接和使用。（该功能尚未完全稳定，**默认关闭**，需开发者自行打开config_ble_enable的配置后编译固件，仅供感兴趣的开发者试用、完善、改进，欢迎提交PR。）

接收端无更新。

## xh-kb-v2.0.2_20260902

- xiaohong_kb_ws63.fwpkg 是 键盘端WS63的固件；
- xiaohong_kb_bs21e_dongle.fwpkg 是 接收端Dongle BS21E的固件。

小鸿KB(WS63): 解析Dongle下发的AtomCode的状态，显示到显示屏并点亮对应的状态灯。

接收端无更新。

## xh-kb-v2.0.1_20260901

- xiaohong_kb_ws63.fwpkg 是 键盘端WS63的固件；
- xiaohong_kb_bs21e_dongle.fwpkg 是 接收端Dongle BS21E的固件。

修复小鸿键盘接收端在接收 PC->Dongle->键盘的下行数据的异常问题，键盘可正常接收Nofify消息。

键盘端无更新。

## xh-kb-v2.0.1_20260824

- xiaohong_kb_ws63.fwpkg 是 键盘端WS63的固件；
- xiaohong_kb_bs21e_dongle.fwpkg 是 接收端Dongle BS21E的固件。

小鸿KB(WS63): 参考OH docs的轻量系统小型化适配指导文档，修改小鸿键盘的config.json配置小型化。

## xh-kb-v2.0.0_20260823

- xiaohong_kb_ws63.fwpkg 是 键盘端WS63的固件；
- xiaohong_kb_bs21e_dongle.fwpkg 是 接收端Dongle BS21E的固件。

小鸿KB(WS63): OH底座升级到7.0-Release; 裁剪掉正式固件用不上的xts子系统和hiviewdfx子系统的组件；版本改为2.0.0。

=================================================================================

## xh-kb-v1.0.0_20260814

- xiaohong_kb_ws63.fwpkg 是 键盘端WS63的固件；
- xiaohong_kb_bs21e_dongle.fwpkg 是 接收端Dongle BS21E的固件。

重新整理整个UI界面的交互流程和按键功能的定义，详情见项目根目录下的README.md文档。



## xh-kb-v1.0.0_20260811

- xiaohong_kb_ws63.fwpkg 是 键盘端WS63的固件；
- xiaohong_kb_bs21e_dongle.fwpkg 是 接收端Dongle BS21E的固件。

键盘发射端与接收端的配对、断连、重新配对功能，显示屏状态栏刷新。

新增小鸿系列产品的字库制作和使用说明。

## xh-kb-v1.0.0_20260810

- xiaohong_kb_ws63.fwpkg 是 键盘端WS63的固件；
- xiaohong_kb_bs21e_dongle.fwpkg 是 接收端Dongle BS21E的固件。

键盘发射端的断连、重连功能；整理UI显示页面内容；整理各种条件下的按键功能定义。

键盘接收端兼容MacOS系统的键码定义。

## xh-kb-v1.0.0_20260730

- xiaohong_kb_ws63.fwpkg 是 键盘端WS63的固件；
- xiaohong_kb_bs21e_dongle.fwpkg 是 接收端Dongle BS21E的固件。

小鸿Vibe Coding键盘的键盘端基础代码提交。

