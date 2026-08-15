// ========== 远程对比面板依赖的窗口/战斗状态辅助函数 ==========

// 返回 dlg 内指定 id 的控件。
static char* FindDlgItem(_Dlg_* dlg, short target_id)
{
    if (!dlg) return nullptr;
    if (IsBadReadPtr(dlg, 0x3C)) return nullptr;
    unsigned* vec = (unsigned*)((char*)dlg + 0x30);
    if (IsBadReadPtr(vec, 12)) return nullptr;
    char** data = (char**)vec[1];
    unsigned cnt = (vec[2] >= vec[1]) ? (vec[2] - vec[1]) / 4 : 0;
    if (!data || cnt > 256 || IsBadReadPtr(data, cnt * sizeof(char*))) return nullptr;
    for (unsigned i = 0; i < cnt; i++) {
        char* it = data[i];
        if (IsBadReadPtr(it, 0x12)) continue;
        if (it && *(short*)(it + 0x10) == target_id) return it;
    }
    return nullptr;
}

static _Dlg_* GetActiveDlg()
{
    char* wndMgr = (char*)*(uint32_t*)0x6992D0;
    if (!wndMgr) return nullptr;
    uint32_t aw = *(uint32_t*)(wndMgr + 0x50);
    if (aw >= 0x01000000 && aw <= 0x20000000) return (_Dlg_*)aw;
    return nullptr;
}

static bool IsReplayableQuickBattleResultDlg(_Dlg_* dlg)
{
    // HD Mod Replayable Quick Battle adds the "cancel/replay" button with id 0x1FB.
    return dlg && FindDlgItem(dlg, 0x1FB) != nullptr;
}

static bool IsBattleOverByEngine(_BattleMgr_* mgr)
{
    if (!mgr || IsBadReadPtr(mgr, sizeof(void*))) return true;
    return THISCALL_1(_bool8_, 0x465410, mgr) != 0;
}

static bool IsHiddenBattleByEngine(_BattleMgr_* mgr)
{
    if (!mgr || IsBadReadPtr(mgr, sizeof(void*))) return true;
    return THISCALL_1(_bool8_, 0x46A080, mgr) != 0;
}
