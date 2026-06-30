// BattleValueInfo.cpp
// 英雄无敌3 SoD 插件：部队战斗价值显示 + 双方远程力量对比。
// 目标版本：Shadow of Death（SOD = 0xFFFFE403），仅 x86。
// 依赖 MegaDesc 负责生物信息窗口扩展、背景、按钮、描述区布局。

#include "homm3.h"
#include <stdarg.h>
#include <wchar.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "deps/stb_image.h"

Patcher*         _P  = nullptr;
PatcherInstance* _PI = nullptr;

// 模块按顺序包含到同一个翻译单元，保证 patcher 全局对象和静态辅助函数共享同一份状态。
#include "modules/ConfigLog.inc.cpp"
#include "modules/CreatureDialog.inc.cpp"
#include "modules/Entry.inc.cpp"
