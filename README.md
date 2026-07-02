# BattleValueInfo — 英雄无敌3 战斗价值信息插件

## 简介

BattleValueInfo 是一个《英雄无敌3》HD Mod 插件，在生物信息窗口中追加显示战斗价值，并可在战斗中右键空白格显示双方远程力量对比。

本插件依赖 MegaDesc：窗口加高、背景替换、按钮替换、描述区扩展由 MegaDesc 负责。

## 功能

- 生物信息窗口追加显示生物配置值：单只 `fight_value`，与数量无关；战斗中会在括号内显示队首单只按剩余 HP 折算后的值
- 战斗中顶部居中显示远程力量面板：远程部队力量、英雄攻击魔法、总值三行两列；英雄攻击魔法行当前先占位为 0

## 配置

见 `BattleValueInfo.ini`：

```ini
[Logging]
; 0 = 不禁止，正常写日志并保留最近 30 个（默认）
; 1 = 禁止，不生成日志，也不扫描/清理旧日志
DisableLog=0

[Layout]
FightValueYOffset=8

[RangedPanel]
BackgroundImage=rp_bg.pcx
TextFont=smalfont.fnt
TextColor=6
Width=500
Height=120
Y=8

[Format]
LabelFightValue=Fight Value
LabelOurSide=Ours
LabelEnemySide=Enemy
```

## 依赖

- HD Mod / patcher_x86
- MegaDesc 插件项目

## 编译

- Visual Studio v145 工具集
- Release | Win32
- 仅支持 x86
- `/Zp1` 结构对齐

## 安装

先部署 MegaDesc，再部署本插件。将生成的 `BattleValueInfo.dll` 与 `BattleValueInfo.ini` 放入 HD Mod 的 `_HD3_Data\Packs\战斗价值信息\` 目录，在 HD Launcher 中启用插件。
