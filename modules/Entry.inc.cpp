// ========== 插件入口与 Hook 注册 ==========

// 前向声明（RangedPanel.inc.cpp 需要的依赖）
static _Dlg_* GetActiveDlg();
static bool IsReplayableQuickBattleResultDlg(_Dlg_* dlg);
static bool IsBattleOverByEngine(_BattleMgr_* mgr);
static bool IsHiddenBattleByEngine(_BattleMgr_* mgr);

static void StartPlugin()
{
    WriteLog("BattleValueInfo 开始注册 Hook。");

    // 生物信息窗口：只在 BUILD 阶段直接画战斗价值文字
    _PI->WriteLoHook(0x5F4503, Hook_BuildCombat);
    _PI->WriteLoHook(0x5F3E75, Hook_BuildAdventure);
    _PI->WriteLoHook(0x5F491E, Hook_BuildTown);

    // 战斗动画循环 @ 0x495C50：每帧调用 Refresh()，画面板。
    // 这是比 0x493FC0 更内层的渲染点，CombatAnimation 插件也使用这个点。
    _PI->WriteHiHook(0x495C50, SPLICE_, EXTENDED_, THISCALL_, Hook_CycleCombatScreen);

    // backbuffer Blt 完成后的点 @ 0x600430：在这个位置画面板不会被后续渲染覆盖。
    _PI->WriteLoHook(0x600430, Hook_AfterBlt);

    // 远程/魔法输出重算事件：进入战斗、施法后。
    _PI->WriteHiHook(0x4781C0, SPLICE_, EXTENDED_, THISCALL_, Hook_CombatStartBattle);
    _PI->WriteHiHook(0x464F10, SPLICE_, EXTENDED_, THISCALL_, Hook_CombatCastSpell);

    WriteLog("BattleValueInfo 已启用。Hook：FightValue(3), RangedPanel(1), RangedPanelEvents(2)。");
}

// ========== DllMain ==========

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    static bool initialized = false;
    if (reason == DLL_PROCESS_ATTACH && !initialized) {
        initialized = true;
        g_hModule = hModule;
        GetModuleFileNameA(hModule, g_ini_path, MAX_PATH);
        char* dot = strrchr(g_ini_path, '.');
        if (dot) strcpy(dot, ".ini");
        g_disable_log = ReadDisableLogFromIniFileA(g_ini_path);
        SetupDatedLogPathAndCleanup(hModule);
        WriteLog("BattleValueInfo 正在加载。");
        _P = GetPatcher();
        if (!_P) {
            WriteLog("GetPatcher 失败；插件将保持未激活状态。");
            return TRUE;
        }
        _PI = _P->CreateInstance("HD.Plugin.BattleValueInfo");
        if (!_PI) {
            WriteLog("CreateInstance 失败；插件将保持未激活状态。");
            return TRUE;
        }
        ReadConfig();
        StartPlugin();
    }
    return TRUE;
}
