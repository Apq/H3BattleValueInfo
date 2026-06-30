// ========== 战斗价值显示与远程力量对比 ==========

// 返回 dlg 内指定 id 的控件；用于判断是否已插入战斗价值、读取数量文本。
static char* FindDlgItem(_Dlg_* dlg, short target_id)
{
    if (!dlg) return nullptr;
    unsigned* vec = (unsigned*)((char*)dlg + 0x30);
    char** data = (char**)vec[1];
    unsigned cnt = (vec[2] >= vec[1]) ? (vec[2] - vec[1]) / 4 : 0;
    for (unsigned i = 0; i < cnt; i++) {
        char* it = data[i];
        if (it && *(short*)(it + 0x10) == target_id) return it;
    }
    return nullptr;
}

static void WriteWideFileUtf8BomIfEmpty(HANDLE h)
{
    LARGE_INTEGER pos;
    pos.QuadPart = 0;
    if (SetFilePointerEx(h, pos, &pos, FILE_END) && pos.QuadPart == 0) {
        DWORD written = 0;
        const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        WriteFile(h, bom, 3, &written, nullptr);
    }
}

static void AppendUtf8LogLine(const char* text)
{
    if (!g_log_path_w[0]) return;
    HANDLE h = CreateFileW(g_log_path_w, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteWideFileUtf8BomIfEmpty(h);
    DWORD written = 0;
    WriteFile(h, text, (DWORD)strlen(text), &written, nullptr);
    WriteFile(h, "\r\n", 2, &written, nullptr);
    CloseHandle(h);
}

static void WriteLog(const char* fmt, ...)
{
    char line[1024];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int off = _snprintf(line, sizeof(line) - 1, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    if (off < 0) off = 0;
    if (off >= (int)sizeof(line)) off = (int)sizeof(line) - 1;

    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line + off, sizeof(line) - off - 1, fmt, ap);
    va_end(ap);
    line[sizeof(line) - 1] = 0;
    AppendUtf8LogLine(line);
}

// 根据生物 id 查询单只 fight_value；全局生物表 [0x6747B0]，结构大小 0x74，fight_value 偏移 +0x3C。
static int GetCreatureFightValueById(int creature_id)
{
    if (creature_id < 0 || creature_id >= 197) return 0;
    char* table = *(char**)0x6747B0;
    if (!table) return 0;
    return *(int*)(table + creature_id * 0x74 + 0x3C);
}

// 在名称行 Y 坐标基础上按配置下移后新增生物配置值；已有 id=3008/3009 时跳过，避免 BUILD/DefProc 重复插入。
static void AddFightValueLine(_Dlg_* dlg, int fight_value)
{
    if (!cfg.show_fight_value || !dlg || fight_value <= 0) return;
    if (FindDlgItem(dlg, 3008) || FindDlgItem(dlg, 3009)) return;

    char* name_item = FindDlgItem(dlg, 203);
    short y = (short)((name_item ? *(short*)(name_item + 0x1A) : 41) + cfg.fight_value_y_offset);

    _DlgStaticText_* label = _DlgStaticText_::Create(25, y, 130, 17,
        cfg.label_fight_value, "smalfont.fnt", 4, 3009, 0 /*HLEFT*/, 0);
    if (label) dlg->AddItemToOwnArrayList(label);

    static char buf[32];
    _snprintf(buf, sizeof(buf) - 1, "%d", fight_value);
    buf[sizeof(buf) - 1] = 0;

    _DlgStaticText_* value = _DlgStaticText_::Create(148, y, 128, 17, buf, "smalfont.fnt", 4, 3008, 2 /*HRIGHT*/, 0);
    if (value) dlg->AddItemToOwnArrayList(value);
}

static bool IsMegaDescLoaded()
{
    return _P && _P->GetInstance((char*)"HD.Plugin.MegaDesc") != nullptr;
}

static void TryAddFightValueLine(_Dlg_* dlg)
{
    if (!IsMegaDescLoaded()) return;
    if (!dlg || dlg->width != 298 || !FindDlgItem(dlg, 200)) return;

    int creature_id = *(int*)((char*)dlg + 0x60);
    int single_fv = GetCreatureFightValueById(creature_id);

    AddFightValueLine(dlg, single_fv);
}

// BUILD 阶段 hook：窗口构建完成后插入战斗价值行。
int __stdcall Hook_BuildCombat(LoHook* h, HookContext* c)
{
    DoLogCleanupOnce();
    TryAddFightValueLine((_Dlg_*)c->ebx);
    return EXEC_DEFAULT;
}
int __stdcall Hook_BuildAdventure(LoHook* h, HookContext* c)
{
    DoLogCleanupOnce();
    TryAddFightValueLine((_Dlg_*)c->esi);
    return EXEC_DEFAULT;
}
int __stdcall Hook_BuildTown(LoHook* h, HookContext* c)
{
    DoLogCleanupOnce();
    TryAddFightValueLine((_Dlg_*)c->esi);
    return EXEC_DEFAULT;
}

// DefProc 兜底：如果 BUILD 阶段未命中，则在消息处理时补插一次。
int __stdcall Hook_DlgDefProc(HiHook* h, _Dlg_* dlg, _EventMsg_* msg)
{
    TryAddFightValueLine(dlg);
    return CALL_2(int, __thiscall, h->GetDefaultFunc(), dlg, msg);
}

// ========== 右键空格子 → 双方远程力量对比 ==========

static bool g_stats_shown = false;

int __stdcall Hook_ShowStatsEntry(HiHook* h, _BattleMgr_* mgr, _BattleStack_* stack, int right_click)
{
    g_stats_shown = true;
    return CALL_3(int, __thiscall, h->GetDefaultFunc(), mgr, stack, right_click);
}

int __stdcall Hook_RangedPower(HiHook* h, _BattleMgr_* mgr, _EventMsg_* msg)
{
    if (!cfg.show_ranged_power
        || !msg || msg->type != MT_MOUSEBUTTON || msg->subtype != MST_RBUTTONDOWN)
        return CALL_2(int, __thiscall, h->GetDefaultFunc(), mgr, msg);

    g_stats_shown = false;
    int ret = CALL_2(int, __thiscall, h->GetDefaultFunc(), mgr, msg);

    if (!g_stats_shown) {
        int power[2] = {0, 0};
        for (int side = 0; side < 2; side++) {
            for (int slot = 0; slot < 21; slot++) {
                const _BattleStack_& s = mgr->stack[side][slot];
                if (s.count_current > 0 && s.creature.shots > 0)
                    power[side] += s.creature.fight_value * s.count_current;
            }
        }

        static char popup[256];
        int total = power[0] + power[1];
        if (total > 0)
            _snprintf(popup, sizeof(popup) - 1, "%s: %d (%d%%)\n%s: %d (%d%%)",
                cfg.label_ours,  power[0], power[0] * 100 / total,
                cfg.label_enemy, power[1], power[1] * 100 / total);
        else
            _snprintf(popup, sizeof(popup) - 1, "%s: 0\n%s: 0", cfg.label_ours, cfg.label_enemy);
        popup[sizeof(popup) - 1] = 0;

        CALL_12(void, __fastcall, 0x4F6C00, popup, 1, -1, -1, -1, 0, -1, 0, -1, 0, -1, 0);
        WriteLog("远程力量对比 我方=%d 敌方=%d", power[0], power[1]);
    }
    return ret;
}
