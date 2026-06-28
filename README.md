# BattleValueInfo — 战场部队战斗价值显示插件

## 简介

BattleValueInfo 是一个《英雄无敌3》HD Mod 插件，在生物信息窗口中追加显示战斗价值，并提供右键空白格时的远程力量对比。

## 功能

### 功能一：部队战斗价值

- 右键部队 → 原信息窗口底部追加 `战斗价值：XXXXX`
- 窗口高度从原版 311 扩展到 487，容纳描述文本区扩大

### 功能二：双方远程力量对比

- 右键**空白六宫格**（非部队所在格）→ 弹出独立对比窗口
- 统计双方所有远程兵种的战斗价值总和

### 配置控制

- INI 文件 `[General] Enabled` 为总开关
- `[Layout]` 控制窗口尺寸和文本布局
- `[Format]` 自定义显示标签文字

## 依赖

仅依赖原版游戏 EXE + HD Mod 框架（`patcher_x86.hpp`）。不依赖 ZCN2.dll、H3.TextColor.dll、ERA、SoD_SP 或其他插件。

## 编译要求

- Visual Studio（v145 工具集）
- 仅支持 x86（32 位）
- 结构成员对齐：1 字节（/Zp1）
- 源码结构：主文件 `BattleValueInfo.cpp` + 4 个 `modules/*.inc.cpp`（通过 `#include` 单翻译单元编译）

## 安装

将编译生成的 `BattleValueInfo.dll` 放入 HD Mod 的 `_HD3_Data\Packs\战斗价值信息\` 目录，在 HD Launcher 中启用插件。
