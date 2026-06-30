static void StartPlugin()
{
    WriteLog("BattleValueInfo 开始注册 Hook。");

    // 仅插入战斗价值行；窗口尺寸、背景、按钮、描述区由 MegaDesc 负责。
    _PI->WriteLoHook(0x5F4503, Hook_BuildCombat);
    _PI->WriteLoHook(0x5F3E75, Hook_BuildAdventure);
    _PI->WriteLoHook(0x5F491E, Hook_BuildTown);
    _PI->WriteHiHook(0x41B120, SPLICE_, EXTENDED_, THISCALL_, Hook_DlgDefProc);

    // 右键空格子显示远程力量。
    _PI->WriteHiHook(0x468440, SPLICE_, EXTENDED_, THISCALL_, Hook_ShowStatsEntry);
    _PI->WriteHiHook(0x4746B0, SPLICE_, EXTENDED_, THISCALL_, Hook_RangedPower);

    WriteLog("BattleValueInfo 已启用。Hook：FightValue(4), RangedPower(2)。");
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
        if (!cfg.enabled) {
            WriteLog("插件已被 INI 禁用。");
            return TRUE;
        }
        StartPlugin();
    }
    return TRUE;
}
