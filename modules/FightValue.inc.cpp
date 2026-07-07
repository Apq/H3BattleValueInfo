// ========== 战斗价值显示 ==========
// 在生物信息对话框中显示战斗价值。

// 返回 dlg 内指定 id 的控件；用于判断是否已插入战斗价值、读取数量文本。
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

// 从文本控件字符串中读取首个正整数；数量文本(id=204)通常是纯数字。
static int ParseFirstPositiveInt(const char* s)
{
    if (!s) return 0;
    while (*s && (*s < '0' || *s > '9')) s++;
    int v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

// 根据生物 id 查询单只 fight_value；全局生物表 [0x6747B0]，结构大小 0x74，fight_value 偏移 +0x3C。
static int GetCreatureFightValueById(int creature_id)
{
    if (creature_id < 0 || creature_id >= 197) return 0;
    char* table = *(char**)0x6747B0;
    if (!table) return 0;
    if (IsBadReadPtr(table + creature_id * 0x74 + 0x3C, sizeof(int))) return 0;
    return *(int*)(table + creature_id * 0x74 + 0x3C);
}

// 战斗中按队首剩余 HP 折算单只当前配置值；非战斗或找不到栈时返回 0。
static int GetCurrentStackFightValueEstimate(int creature_id, int count, int fight_value)
{
    if (creature_id < 0 || count <= 0 || fight_value <= 0 || !o_BattleMgr) return 0;

    int matches = 0;
    int estimate = 0;
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            const _BattleStack_& s = o_BattleMgr->stack[side][slot];
            if (s.creature_id != creature_id || s.count_current != count) continue;
            ++matches;
            int hp = s.creature.hit_points;
            if (hp <= 0) return 0;
            int lost_hp = s.lost_hp;
            if (lost_hp < 0) lost_hp = 0;
            if (lost_hp >= hp) lost_hp = hp - 1;
            estimate = (fight_value * (hp - lost_hp) + hp / 2) / hp;
            if (matches > 1) return 0;
        }
    }
    return matches == 1 ? estimate : 0;
}

// 在名称行 Y 坐标基础上按配置下移后新增生物配置值；已有 id=3008/3009 时跳过，避免 BUILD/DefProc 重复插入。
static void AddFightValueLine(_Dlg_* dlg, int fight_value, int current_value)
{
    if (!dlg || fight_value <= 0) return;
    if (FindDlgItem(dlg, 3008) || FindDlgItem(dlg, 3009)) return;

    char* name_item = FindDlgItem(dlg, 203);
    short y = (short)((name_item ? *(short*)(name_item + 0x1A) : 41) + cfg.fight_value_y_offset);

    h3::H3DlgText* label = h3::H3DlgText::Create(
        25, y, 130, 17,
        cfg.label_fight_value, "smalfont.fnt",
        4, 3009, h3::eTextAlignment::TOP_LEFT, 0);
    if (label) {
        dlg->AddItemToOwnArrayList((h3::H3DlgItem*)label);
    }

    static char buf[64];
    if (current_value > 0)
        _snprintf(buf, sizeof(buf) - 1, "%d(%d)", fight_value, current_value);
    else
        _snprintf(buf, sizeof(buf) - 1, "%d", fight_value);
    buf[sizeof(buf) - 1] = 0;

    h3::H3DlgText* value = h3::H3DlgText::Create(
        148, y, 128, 17, buf, "smalfont.fnt",
        4, 3008, h3::eTextAlignment::TOP_RIGHT, 0);
    if (value) {
        dlg->AddItemToOwnArrayList((h3::H3DlgItem*)value);
    }
}

static bool IsMegaDescLoaded()
{
    return _P && _P->GetInstance((char*)"HD.Plugin.MegaDesc") != nullptr;
}

static void TryAddFightValueLine(_Dlg_* dlg)
{
    __try {
        if (!IsMegaDescLoaded()) return;
        if (!dlg || IsBadReadPtr(dlg, 0x64) || dlg->width != 298 || !FindDlgItem(dlg, 200)) return;
        if (FindDlgItem(dlg, 3009) || FindDlgItem(dlg, 3008)) return;

        int creature_id = *(int*)((char*)dlg + 0x60);
        if (creature_id < 0 || creature_id >= 197) return;

        int single_fv = GetCreatureFightValueById(creature_id);
        if (single_fv <= 0) return;

        char* count_item = FindDlgItem(dlg, 204);
        const char* count_text = nullptr;
        if (count_item && !IsBadReadPtr(count_item + 0x38, 1))
            count_text = *(_char_**)(count_item + 0x34);
        int count = ParseFirstPositiveInt(count_text);
        int current_value = GetCurrentStackFightValueEstimate(creature_id, count, single_fv);

        AddFightValueLine(dlg, single_fv, current_value);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[FightValue] SEH exception during TryAddFightValueLine dlg=%p", dlg);
    }
}

// BUILD 阶段 hook：窗口构建完成后插入战斗价值行。
int __stdcall Hook_BuildCombat(LoHook* h, HookContext* c)
{
    _Dlg_* dlg = (_Dlg_*)c->ebx;
    TryAddFightValueLine(dlg);
    return EXEC_DEFAULT;
}

int __stdcall Hook_BuildAdventure(LoHook* h, HookContext* c)
{
    _Dlg_* dlg = (_Dlg_*)c->esi;
    TryAddFightValueLine(dlg);
    return EXEC_DEFAULT;
}

int __stdcall Hook_BuildTown(LoHook* h, HookContext* c)
{
    _Dlg_* dlg = (_Dlg_*)c->esi;
    TryAddFightValueLine(dlg);
    return EXEC_DEFAULT;
}
