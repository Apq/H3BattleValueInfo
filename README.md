# BattleValueInfo

BattleValueInfo 是一个《英雄无敌3》HD Mod 插件，负责两件事：

1. 在 MegaDesc 扩展后的生物信息窗口中追加“战斗价值”。
2. 在手动战斗中，在 800x600 战场框外部上方显示双方远程/魔法输出对比面板。

本插件依赖 MegaDesc。生物信息窗口加高、背景替换、按钮替换、描述区扩展由 MegaDesc 负责。

## 功能

- 生物信息窗口追加显示单只 `fight_value`。
- 战斗中若能匹配当前 stack，会在括号内显示队首单只按剩余 HP 折算后的战斗价值。
- 手动战斗中显示远程/魔法输出对比面板，三行分别为：
  - 远程输出
  - 魔法输出
  - 总输出
- 快速战斗结果界面和战斗结果界面中隐藏对比面板。
- 从结果界面点击取消重新手动战斗后，对比面板会重新显示。

## 远程/魔法输出

远程输出：本方所有可射击远程 stack 对敌方每支存活 stack 逐一试算，取最小值作为保守输出下限。可射击判断、基础伤害和伤害修正尽量调用原版战斗函数。

魔法输出：本方英雄已掌握的白名单攻击魔法中，理论单次伤害最高的值。不检查当前魔法值，也不检查本回合是否已经施法，不按具体敌方目标计算抗性、免疫或范围命中。

总输出：远程输出 + 魔法输出。

详细算法见 [远程输出能力算法.md](远程输出能力算法.md)。

## 配置

见 [BattleValueInfo.ini](BattleValueInfo.ini)。常用项：

```ini
[Logging]
DisableLog=0

[Layout]
FightValueYOffset=8

[RangedPanel]
BackgroundImage=rp_bg.pcx
TextFont=smalfont.fnt
TextColor=4
Width=298
Height=93
Y=0
Row1Y=24
Row2Y=42
Row3Y=60

[Format]
LabelFightValue=战斗价值
```

`[RangedPanel] Y=0` 是推荐默认值。代码内部已有基础上移量 `RP_BASE_Y_OFFSET=23`，配置里的 `Y` 只用于微调；数值越大，面板越往上。

面板背景图片放在插件目录 `img` 子目录，只支持 1-plane 8-bit PCX。

## 依赖

- 英雄无敌3 HD Mod / patcher_x86
- MegaDesc 插件
- Visual Studio 18 / v145 工具集
- Win32 / x86 Release 构建

## 编译

使用 MSBuild：

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe" BattleValueInfo.vcxproj /p:Configuration=Release /p:Platform=Win32 /m
```

输出：

```text
Release\BattleValueInfo.dll
```

## 部署

先部署并启用 MegaDesc，再部署本插件：

```powershell
.\deploy.ps1
```

默认部署到：

```text
D:\Heroes3\Heroes3_2026.05.01\_HD3_Data\Packs\远优对比
```

部署内容包括 DLL、配置文件和 `img` 目录资源。
