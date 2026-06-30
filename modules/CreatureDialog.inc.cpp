// ========== 战斗价值显示与远程力量对比 ==========

static int s_diag_dlgdefproc_count = 0;

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

static void DestroyRangedPanel();

static const int RP_ID_BG = 32000;
static const int RP_ID_TEXT0 = 32001;

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
    return *(int*)(table + creature_id * 0x74 + 0x3C);
}

// 战斗中按队首剩余 HP 折算单只当前配置值；非战斗或找不到栈时返回 0。
static int GetCurrentStackFightValueEstimate(int creature_id, int count, int fight_value)
{
    if (creature_id < 0 || count <= 0 || fight_value <= 0 || !o_BattleMgr) return 0;

    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            const _BattleStack_& s = o_BattleMgr->stack[side][slot];
            if (s.creature_id != creature_id || s.count_current != count) continue;
            int hp = s.creature.hit_points;
            if (hp <= 0) return 0;
            int lost_hp = s.lost_hp;
            if (lost_hp < 0) lost_hp = 0;
            if (lost_hp >= hp) lost_hp = hp - 1;
            return (fight_value * (hp - lost_hp) + hp / 2) / hp;
        }
    }
    return 0;
}

// 在名称行 Y 坐标基础上按配置下移后新增生物配置值；已有 id=3008/3009 时跳过，避免 BUILD/DefProc 重复插入。
static void AddFightValueLine(_Dlg_* dlg, int fight_value, int current_value)
{
    if (!dlg || fight_value <= 0) return;
    if (FindDlgItem(dlg, 3008) || FindDlgItem(dlg, 3009)) return;

    char* name_item = FindDlgItem(dlg, 203);
    short y = (short)((name_item ? *(short*)(name_item + 0x1A) : 41) + cfg.fight_value_y_offset);

    _DlgStaticText_* label = _DlgStaticText_::Create(25, y, 130, 17,
        cfg.label_fight_value, "smalfont.fnt", 4, 3009, 0 /*HLEFT*/, 0);
    if (label) dlg->AddItemToOwnArrayList(label);

    static char buf[64];
    if (current_value > 0)
        _snprintf(buf, sizeof(buf) - 1, "%d(%d)", fight_value, current_value);
    else
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
    char* count_item = FindDlgItem(dlg, 204);
    const char* count_text = count_item ? *(_char_**)(count_item + 0x34) : nullptr;
    int count = ParseFirstPositiveInt(count_text);
    int current_value = GetCurrentStackFightValueEstimate(creature_id, count, single_fv);

    AddFightValueLine(dlg, single_fv, current_value);
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
    if (o_BattleMgr && s_diag_dlgdefproc_count < 80) {
        ++s_diag_dlgdefproc_count;
        WriteLog("[Diag] DlgDefProc #%d dlg=%p rect=(%d,%d,%d,%d) currentDlg=%p battleDlg=%p msg=(%d,%d,%d)",
            s_diag_dlgdefproc_count, dlg,
            dlg ? dlg->x : -1, dlg ? dlg->y : -1, dlg ? dlg->width : -1, dlg ? dlg->height : -1,
            o_CurrentDlg, o_BattleMgr ? o_BattleMgr->dlg : nullptr,
            msg ? msg->type : -1, msg ? msg->subtype : -1, msg ? msg->item_id : -1);
    }
    if (o_BattleMgr && o_BattleMgr->dlg && dlg && dlg != o_BattleMgr->dlg) {
        WriteLog("[Diag] non-battle dlg while battle active: dlg=%p battleDlg=%p", dlg, o_BattleMgr->dlg);
    }
    TryAddFightValueLine(dlg);
    return CALL_2(int, __thiscall, h->GetDefaultFunc(), dlg, msg);
}

// ========== 战场顶部远程力量面板 ==========

static int CalcRangedUnitsPower(_BattleMgr_* mgr, int side)
{
    if (!mgr || side < 0 || side > 1) return 0;
    int power = 0;
    for (int slot = 0; slot < 21; slot++) {
        const _BattleStack_& s = mgr->stack[side][slot];
        if (s.count_current > 0 && s.creature.shots > 0)
            power += s.creature.fight_value * s.count_current;
    }
    return power;
}

static int CalcHeroSpellPower(_BattleMgr_* mgr, int side)
{
    // TODO: 攻击魔法对应的远程力量公式待定；先保留面板行位。
    return 0;
}

static bool s_battle_end_logged = false;

static bool IsBattleEnded(_BattleMgr_* mgr)
{
    if (!mgr) return true;
    int alive[2] = { 0, 0 };
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            if (mgr->stack[side][slot].count_current > 0) ++alive[side];
        }
    }
    bool ended = alive[0] == 0 || alive[1] == 0;
    if (ended && !s_battle_end_logged) {
        s_battle_end_logged = true;
        WriteLog("[RangedPanel] battle ended by alive stacks: left=%d right=%d", alive[0], alive[1]);
    }
    return ended;
}

static bool ReadWholeFile(const char* path, unsigned char** outData, int* outSize)
{
    *outData = nullptr;
    *outSize = 0;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0 || size > 32 * 1024 * 1024) { CloseHandle(h); return false; }
    unsigned char* data = (unsigned char*)malloc(size);
    if (!data) { CloseHandle(h); return false; }
    DWORD read = 0;
    bool ok = ReadFile(h, data, size, &read, nullptr) && read == size;
    CloseHandle(h);
    if (!ok) { free(data); return false; }
    *outData = data;
    *outSize = (int)size;
    return true;
}

// ---- 8-bit PCX → _Pcx8_（保留索引+调色板，颜色精准）----
static _Pcx8_* DecodePcx8File(const char* path)
{
    unsigned char* data = nullptr;
    int size = 0;
    if (!ReadWholeFile(path, &data, &size)) return nullptr;
    if (size < 128) { free(data); return nullptr; }

    int bpp     = data[3];
    int xmin    = *(short*)(data + 4);
    int ymin    = *(short*)(data + 6);
    int xmax    = *(short*)(data + 8);
    int ymax    = *(short*)(data + 10);
    int nplanes = data[65];
    int bpl     = *(short*)(data + 66);
    int w = xmax - xmin + 1;
    int h = ymax - ymin + 1;

    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) { free(data); return nullptr; }
    if (!(nplanes == 1 && bpp == 8)) { free(data); return nullptr; }  // 只处理 8-bit

    int rawSize = bpl * h;
    unsigned char* raw = (unsigned char*)malloc(rawSize);
    if (!raw) { free(data); return nullptr; }

    int pos = 128, rp = 0;
    while (rp < rawSize && pos < size) {
        unsigned char b = data[pos++];
        if ((b & 0xC0) == 0xC0) {
            int cnt = b & 0x3F;
            if (pos >= size) break;
            unsigned char val = data[pos++];
            for (int i = 0; i < cnt && rp < rawSize; ++i) raw[rp++] = val;
        } else {
            raw[rp++] = b;
        }
    }

    // 调色板
    unsigned char pal[768] = {0};
    bool palFound = false;
    if (size >= 769 && data[size - 769] == 0x0C) {
        memcpy(pal, data + (size - 768), 768);
        palFound = true;
    }
    if (!palFound) {
        for (int i = size - 769; i >= 0; --i) {
            if (data[i] == 0x0C && i + 768 < size) {
                memcpy(pal, data + i + 1, 768);
                palFound = true;
                break;
            }
        }
    }

    _Pcx8_* pcx8 = _Pcx8_::CreateNew((char*)"bv_panel", w, h);
    if (!pcx8) { free(raw); free(data); return nullptr; }

    for (int y = 0; y < h; y++)
        memcpy((unsigned char*)pcx8->buffer + y * pcx8->scanline_size, raw + y * bpl, w);

    // 设置 palette24
    for (int i = 0; i < 256; i++) {
        pcx8->palette24.colors[i].r = pal[i * 3];
        pcx8->palette24.colors[i].g = pal[i * 3 + 1];
        pcx8->palette24.colors[i].b = pal[i * 3 + 2];
    }
    // 设置 palette16 (RGB565)
    for (int i = 0; i < 256; i++)
        pcx8->palette16.colors[i] = RGB565_fromR8G8B8(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]);

    pcx8->ref_count = 0x10000;
    free(raw);
    free(data);
    return pcx8;
}

// ---- 24-bit PCX / PNG / JPG / BMP → 量化到游戏调色板 → _Pcx8_ ----
static unsigned char* DecodePcxFile(const char* path, int* outW, int* outH)
{
    unsigned char* data = nullptr;
    int size = 0;
    if (!ReadWholeFile(path, &data, &size)) return nullptr;
    if (size < 128) { free(data); return nullptr; }

    int bpp      = data[3];
    int xmin     = *(short*)(data + 4);
    int ymin     = *(short*)(data + 6);
    int xmax     = *(short*)(data + 8);
    int ymax     = *(short*)(data + 10);
    int nplanes  = data[65];
    int bpl      = *(short*)(data + 66);
    int w = xmax - xmin + 1;
    int h = ymax - ymin + 1;

    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) { free(data); return nullptr; }

    int rawSize = bpl * nplanes * h;
    unsigned char* raw = (unsigned char*)malloc(rawSize);
    if (!raw) { free(data); return nullptr; }

    int pos = 128, rp = 0;
    while (rp < rawSize && pos < size) {
        unsigned char b = data[pos++];
        if ((b & 0xC0) == 0xC0) {
            int cnt = b & 0x3F;
            if (pos >= size) break;
            unsigned char val = data[pos++];
            for (int i = 0; i < cnt && rp < rawSize; ++i) raw[rp++] = val;
        } else {
            raw[rp++] = b;
        }
    }

    unsigned char* rgb = (unsigned char*)malloc(w * h * 3);
    if (!rgb) { free(raw); free(data); return nullptr; }

    if (nplanes == 3 && bpp == 8) {
        for (int y = 0; y < h; y++) {
            int base = y * bpl * nplanes;
            for (int x = 0; x < w; x++) {
                rgb[(y * w + x) * 3]     = raw[base + x];
                rgb[(y * w + x) * 3 + 1] = raw[base + bpl + x];
                rgb[(y * w + x) * 3 + 2] = raw[base + 2 * bpl + x];
            }
        }
    } else if (nplanes == 1 && bpp == 8) {
        unsigned char pal[768] = {0};
        bool palFound = false;
        if (size >= 769 && data[size - 769] == 0x0C) {
            memcpy(pal, data + (size - 768), 768);
            palFound = true;
        }
        if (!palFound) {
            for (int i = size - 769; i >= 0; --i) {
                if (data[i] == 0x0C && i + 768 < size) {
                    memcpy(pal, data + i + 1, 768);
                    palFound = true;
                    break;
                }
            }
        }
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                unsigned char idx = raw[y * bpl + x];
                rgb[(y * w + x) * 3]     = pal[idx * 3];
                rgb[(y * w + x) * 3 + 1] = pal[idx * 3 + 1];
                rgb[(y * w + x) * 3 + 2] = pal[idx * 3 + 2];
            }
        }
    } else {
        free(raw); free(data); free(rgb);
        return nullptr;
    }

    free(raw);
    free(data);
    *outW = w;
    *outH = h;
    return rgb;
}

// ---- 统一图片加载：按扩展名自适应 ----
// .pcx → PCX 解码器；.png/.jpg/.bmp/… → stb_image
// 输出 RGB888（3 bytes/pixel），调用方 free()。
static unsigned char* DecodeImageFile(const char* path, const char* filename, int* outW, int* outH)
{
    const char* ext = strrchr(filename, '.');
    bool isPcx = ext && _stricmp(ext, ".pcx") == 0;

    if (isPcx)
        return DecodePcxFile(path, outW, outH);

    // stb_image 路径
    unsigned char* fileData = nullptr;
    int fileSize = 0;
    if (!ReadWholeFile(path, &fileData, &fileSize)) return nullptr;

    int w = 0, h = 0, comp = 0;
    unsigned char* rgba = stbi_load_from_memory(fileData, fileSize, &w, &h, &comp, 4);
    free(fileData);
    if (!rgba) return nullptr;

    unsigned char* rgb = (unsigned char*)malloc(w * h * 3);
    if (!rgb) { stbi_image_free(rgba); return nullptr; }
    for (int i = 0; i < w * h; ++i) {
        rgb[i * 3]     = rgba[i * 4];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    stbi_image_free(rgba);
    *outW = w;
    *outH = h;
    return rgb;
}

static _Pcx8_* QuantizeRgbAsPcx8(unsigned char* rgb, int w, int h, _Pcx8_* palSrc, const char* resName)
{
    if (!rgb || !palSrc || w <= 0 || h <= 0) return nullptr;
    _Pcx8_* pcx8 = _Pcx8_::CreateNew((char*)resName, w, h);
    if (!pcx8) return nullptr;
    pcx8->SetPaletteFrom(palSrc);

    unsigned char* dst = (unsigned char*)pcx8->buffer;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned char* p = rgb + (y * w + x) * 3;
            int best = 0;
            int bestD = 0x7FFFFFFF;
            for (int i = 1; i < 256; i++) {
                int dr = (int)p[0] - (int)palSrc->palette24.colors[i].r;
                int dg = (int)p[1] - (int)palSrc->palette24.colors[i].g;
                int db = (int)p[2] - (int)palSrc->palette24.colors[i].b;
                int d = dr * dr + dg * dg + db * db;
                if (d < bestD) { bestD = d; best = i; if (d == 0) break; }
            }
            dst[y * pcx8->scanline_size + x] = (unsigned char)best;
        }
    }
    pcx8->ref_count = 0x10000;
    return pcx8;
}

static _Pcx8_* LoadPanelImageAsPcx8()
{
    if (!cfg.ranged_panel_image[0]) return nullptr;

    char modulePath[MAX_PATH];
    GetModuleFileNameA(g_hModule, modulePath, MAX_PATH);
    char* slash = strrchr(modulePath, '\\');
    if (slash) slash[1] = 0; else modulePath[0] = 0;

    char path[2048];
    _snprintf(path, sizeof(path) - 1, "%simg\\%s", modulePath, cfg.ranged_panel_image);
    path[sizeof(path) - 1] = 0;

    const char* ext = strrchr(cfg.ranged_panel_image, '.');
    bool isPcx = ext && _stricmp(ext, ".pcx") == 0;
    if (isPcx) {
        _Pcx8_* indexed = DecodePcx8File(path);
        if (indexed) return indexed;
    }

    int w = 0, h = 0;
    unsigned char* rgb = DecodeImageFile(path, cfg.ranged_panel_image, &w, &h);
    if (!rgb) return nullptr;

    _Pcx8_* palSrc = o_LoadPcx8((char*)"DlgBluBk.PCX");
    _Pcx8_* pcx8 = palSrc ? QuantizeRgbAsPcx8(rgb, w, h, palSrc, "bv_panel") : nullptr;
    if (palSrc) palSrc->DerefOrDestruct();
    free(rgb);
    return pcx8;
}

static _Pcx8_* s_ranged_panel_bg = nullptr;
static _Dlg_* s_last_battle_dlg = nullptr;
static _BattleMgr_* s_last_battle_mgr = nullptr;
static bool s_ranged_panel_active = true;
static bool s_ranged_panel_suppressed_for_result = false;
static int s_ranged_panel_last_x = -1;
static int s_ranged_panel_last_y = -1;
static int s_ranged_panel_last_w = 0;
static int s_ranged_panel_last_h = 0;

static void HideRangedPanelItems(_Dlg_* dlg)
{
    if (!dlg) return;
    for (int id = RP_ID_BG; id <= RP_ID_TEXT0 + 5; ++id) {
        _DlgItem_* item = (_DlgItem_*)FindDlgItem(dlg, (short)id);
        if (item) item->Hide();
    }
}

static void ShowRangedPanelItems(_Dlg_* dlg)
{
    if (!dlg) return;
    for (int id = RP_ID_BG; id <= RP_ID_TEXT0 + 5; ++id) {
        _DlgItem_* item = (_DlgItem_*)FindDlgItem(dlg, (short)id);
        if (item) item->Show();
    }
}

static void EnsureRangedPanelItems(_BattleMgr_* mgr, int x, int y, int panel_w, int panel_h, int left_x, int right_x, int col_w, int text_h)
{
    if (!mgr || !mgr->dlg) return;
    _Dlg_* dlg = mgr->dlg;

    if (!FindDlgItem(dlg, RP_ID_BG)) {
        if (!s_ranged_panel_bg) s_ranged_panel_bg = LoadPanelImageAsPcx8();
        _DlgStaticPcx8_* bg = _DlgStaticPcx8_::Create(x, y, panel_w, panel_h, RP_ID_BG, (char*)"DlgBluBk.PCX");
        if (bg) {
            if (bg->pcx8) bg->pcx8->DerefOrDestruct();
            bg->pcx8 = s_ranged_panel_bg;
            if (s_ranged_panel_bg) ++s_ranged_panel_bg->ref_count;
            bg->width = (unsigned short)panel_w;
            bg->height = (unsigned short)panel_h;
            dlg->AddItemToOwnArrayList(bg);
            dlg->AttachItem(bg, 30000);
        }

        const char* fontName = cfg.ranged_panel_text_font[0] ? cfg.ranged_panel_text_font : "smalfont.fnt";
        for (int i = 0; i < 6; ++i) {
            int row = i / 2;
            int xx = (i % 2 == 0) ? left_x : right_x;
            int align = (i % 2 == 0) ? 2 : 0;
            int yy = y + cfg.row_y[row];
            _DlgStaticText_* t = _DlgStaticText_::Create(xx, yy, col_w, text_h, (char*)"0", (char*)fontName,
                (_dword_)cfg.ranged_panel_text_color, RP_ID_TEXT0 + i, align, -1);
            if (t) { dlg->AddItemToOwnArrayList(t); dlg->AttachItem(t, 30001 + i); }
        }
        WriteLog("[RangedPanel] dlg items created: dlg=%p pos=(%d,%d,%d,%d)", dlg, x, y, panel_w, panel_h);
    }

    ShowRangedPanelItems(dlg);
}

static void DestroyRangedPanel()
{
    s_ranged_panel_active = false;
    if (s_ranged_panel_bg) { s_ranged_panel_bg->DerefOrDestruct(); s_ranged_panel_bg = nullptr; }
    if (o_BattleMgr) o_BattleMgr->RedrawBattlefield(FALSE, TRUE, FALSE, 0, TRUE, FALSE);
}

static _Fnt_* GetRangedPanelTextFont()
{
    const char* name = cfg.ranged_panel_text_font;
    if (!name || !name[0]) return o_Smalfont_Fnt;
    if (_stricmp(name, "tiny.fnt") == 0 || _stricmp(name, "tiny") == 0) return o_Tiny_Fnt;
    if (_stricmp(name, "smalfont.fnt") == 0 || _stricmp(name, "smalfont") == 0) return o_Smalfont_Fnt;
    if (_stricmp(name, "medfont.fnt") == 0 || _stricmp(name, "medfont") == 0) return o_Medfont_Fnt;
    if (_stricmp(name, "bigfont.fnt") == 0 || _stricmp(name, "bigfont") == 0) return o_Bigfont_Fnt;
    if (_stricmp(name, "calli10r.fnt") == 0 || _stricmp(name, "calli10r") == 0) return o_Calli10R_Fnt;
    return o_Smalfont_Fnt;
}

static bool s_battle_area_logged = false;
static int s_diag_battle_redraw_count = 0;
static int s_diag_panel_draw_count = 0;
static bool s_panel_was_drawn = false; // 上一帧是否画面板，用于战斗结束时清理

// ---- 脏标记：只在战场状态变化时才重新计算 ----
static unsigned int s_stack_checksum = 0;
static char s_cached_text[6][64] = {{0}};

static unsigned int ComputeStackChecksum(_BattleMgr_* mgr)
{
    if (!mgr) return 0;
    unsigned int hash = 2166136261u; // FNV offset
    for (int side = 0; side < 2; ++side)
        for (int slot = 0; slot < 21; ++slot) {
            const _BattleStack_& s = mgr->stack[side][slot];
            hash ^= (unsigned)(s.count_current & 0xFFFF);
            hash *= 16777619u;
            hash ^= (unsigned)(s.creature_id & 0xFF);
            hash *= 16777619u;
        }
    return hash;
}
static void LogDlgBrief(const char* tag, _Dlg_* dlg)
{
    if (!dlg) {
        WriteLog("[Diag] %s dlg=null", tag);
        return;
    }
    WriteLog("[Diag] %s dlg=%p rect=(%d,%d,%d,%d) currentDlg=%p battleDlg=%p active=%d lastPanel=(%d,%d,%d,%d)",
        tag, dlg, dlg->x, dlg->y, dlg->width, dlg->height,
        o_CurrentDlg, o_BattleMgr ? o_BattleMgr->dlg : nullptr,
        s_ranged_panel_active ? 1 : 0,
        s_ranged_panel_last_x, s_ranged_panel_last_y, s_ranged_panel_last_w, s_ranged_panel_last_h);
}

static void DrawRangedPanelToScreen(_BattleMgr_* mgr)
{
    if (!mgr || !mgr->dlg) return;
    _Pcx16_* screen = o_WndMgr ? o_WndMgr->screen_pcx16 : nullptr;
    if (!screen) return;

    _Dlg_* dlg = mgr->dlg;

    if (s_diag_panel_draw_count < 20) {
        ++s_diag_panel_draw_count;
        WriteLog("[Diag] DrawRangedPanel #%d mgr=%p dlg=%p active=%d currentDlg=%p",
            s_diag_panel_draw_count, mgr, dlg, s_ranged_panel_active ? 1 : 0, o_CurrentDlg);
    }

    // 新战斗管理器出现时，重置结果锁。
    if (s_last_battle_mgr != mgr) {
        s_last_battle_mgr = mgr;
        s_ranged_panel_suppressed_for_result = false;
        s_ranged_panel_active = true;
        s_stack_checksum = 0;
        memset(s_cached_text, 0, sizeof(s_cached_text));
        s_panel_was_drawn = false;
    }

    // 新战斗（dlg 变化）时重新启用并重新加载背景。
    if (s_last_battle_dlg != dlg) {
        s_last_battle_dlg = dlg;
        s_ranged_panel_active = true;
        s_battle_end_logged = false;
        s_stack_checksum = 0;
        memset(s_cached_text, 0, sizeof(s_cached_text));
        s_panel_was_drawn = false;
        if (s_ranged_panel_bg) { s_ranged_panel_bg->DerefOrDestruct(); s_ranged_panel_bg = nullptr; }
    }

    if (!s_ranged_panel_active) {
        s_panel_was_drawn = false;
        return;
    }
    if (s_ranged_panel_suppressed_for_result) return;
    if (IsBattleEnded(mgr)) {
        s_panel_was_drawn = false;
        DestroyRangedPanel();
        return;
    }

    if (!s_battle_area_logged) {
        s_battle_area_logged = true;
        WriteLog("[RangedPanel] screen=%dx%d dlg=(%d,%d,%d,%d)",
            screen->width, screen->height,
            dlg->x, dlg->y, dlg->width, dlg->height);
    }

    int panel_w = cfg.ranged_panel_width;
    int panel_h = cfg.ranged_panel_height;
    int text_h = 17;

    // 直接屏幕坐标：水平居中于战场对话框，吸附到外侧上方。
    int x = dlg->x + (dlg->width - panel_w) / 2;
    if (x < 0) x = 0;
    int y = dlg->y - panel_h - cfg.ranged_panel_y;
    if (y < 0) y = 0;

    // 文字区域按面板自动分两列。
    int pad_x = 16;
    int mid_gap = 16;
    int col_w = (panel_w - pad_x * 2 - mid_gap) / 2;
    if (col_w < 40) col_w = panel_w / 2;
    int left_x = x + pad_x;
    int right_x = x + panel_w - pad_x - col_w;

    if (!s_ranged_panel_bg) s_ranged_panel_bg = LoadPanelImageAsPcx8();
    if (s_ranged_panel_bg) {
        int bw = s_ranged_panel_bg->width;
        int bh = s_ranged_panel_bg->height;
        int draw_w = (bw < panel_w) ? bw : panel_w;
        int draw_h = (bh < panel_h) ? bh : panel_h;
        s_ranged_panel_last_x = x;
        s_ranged_panel_last_y = y;
        s_ranged_panel_last_w = draw_w;
        s_ranged_panel_last_h = draw_h;
        // 画面板背景
        s_ranged_panel_bg->DrawToPcx16(0, 0, draw_w, draw_h, screen, x, y, FALSE);
        s_panel_was_drawn = true;

    }

    unsigned int checksum = ComputeStackChecksum(mgr);
    if (checksum != s_stack_checksum || !s_cached_text[0][0]) {
        s_stack_checksum = checksum;
        int ranged[2] = { CalcRangedUnitsPower(mgr, 0), CalcRangedUnitsPower(mgr, 1) };
        int spell[2] = { CalcHeroSpellPower(mgr, 0), CalcHeroSpellPower(mgr, 1) };
        int total[2] = { ranged[0] + spell[0], ranged[1] + spell[1] };

        _snprintf(s_cached_text[0], sizeof(s_cached_text[0]) - 1, "%d", ranged[0]);
        _snprintf(s_cached_text[1], sizeof(s_cached_text[1]) - 1, "%d", ranged[1]);
        _snprintf(s_cached_text[2], sizeof(s_cached_text[2]) - 1, "%d", spell[0]);
        _snprintf(s_cached_text[3], sizeof(s_cached_text[3]) - 1, "%d", spell[1]);
        _snprintf(s_cached_text[4], sizeof(s_cached_text[4]) - 1, "%d", total[0]);
        _snprintf(s_cached_text[5], sizeof(s_cached_text[5]) - 1, "%d", total[1]);
        for (int i = 0; i < 6; ++i) s_cached_text[i][sizeof(s_cached_text[i]) - 1] = 0;

        WriteLog("[RangedPanel] recalculated checksum=%u ranged=(%d,%d) spell=(%d,%d) total=(%d,%d)",
            checksum, ranged[0], ranged[1], spell[0], spell[1], total[0], total[1]);
    }

    _Fnt_* font = GetRangedPanelTextFont();
    if (font) {
        for (int row = 0; row < 3; ++row) {
            int yy = y + cfg.row_y[row];
            font->DrawTextToPcx16(s_cached_text[row * 2], screen, left_x, yy, col_w, text_h, (_byte_)cfg.ranged_panel_text_color, 2, 0);
            font->DrawTextToPcx16(s_cached_text[row * 2 + 1], screen, right_x, yy, col_w, text_h, (_byte_)cfg.ranged_panel_text_color, 0, 0);
        }
    }
}

static void UpdateRangedPanel(_BattleMgr_* mgr)
{
    DrawRangedPanelToScreen(mgr);
}


static void CleanPanelBeforeResult()
{
    s_panel_was_drawn = false;
    s_ranged_panel_suppressed_for_result = true;
    s_ranged_panel_active = false;
    if (s_ranged_panel_bg) { s_ranged_panel_bg->DerefOrDestruct(); s_ranged_panel_bg = nullptr; }
}

int __stdcall Hook_BattleRedraw(HiHook* h, _BattleMgr_* mgr, _bool8_ flip, _bool8_ set_battle_redraws, _bool8_ use_battle_redraws, int waiting_time, _bool8_ redraw_background, _bool8_ wait)
{
    CALL_7(void, __thiscall, h->GetDefaultFunc(), mgr, flip, set_battle_redraws, use_battle_redraws, waiting_time, redraw_background, wait);
    UpdateRangedPanel(mgr);
    return 0;
}

// LoHook: 在 0x476DA0 (AI 决策主函数) 内部、调用 CPResult 之前拦截。
// 0x477200 和 0x4772B0 是 push 参数序列的起始，我们在 LoHook 里提前清屏。
int __stdcall Hook_PreCombatResult_A(LoHook* h, HookContext* c)
{
    WriteLog("[RangedPanel] PreCombatResult_A (0x477200) panel_was_drawn=%d", s_panel_was_drawn ? 1 : 0);
    CleanPanelBeforeResult();
    return EXEC_DEFAULT;
}

int __stdcall Hook_PreCombatResult_B(LoHook* h, HookContext* c)
{
    WriteLog("[RangedPanel] PreCombatResult_B (0x4772B0) panel_was_drawn=%d", s_panel_was_drawn ? 1 : 0);
    CleanPanelBeforeResult();
    return EXEC_DEFAULT;
}

void __stdcall Hook_CombatResultDlg(HiHook* h, void* resultDlg, int a1, int a2, int a3, int a4, int a5, int a6)
{
    WriteLog("[Diag] 0x46FE20 CPResult construct active=%d panel_was_drawn=%d lastPanel=(%d,%d,%d,%d)",
        s_ranged_panel_active ? 1 : 0, s_panel_was_drawn ? 1 : 0,
        s_ranged_panel_last_x, s_ranged_panel_last_y, s_ranged_panel_last_w, s_ranged_panel_last_h);
    s_ranged_panel_suppressed_for_result = true;
    s_ranged_panel_active = false;
    if (s_ranged_panel_bg) { s_ranged_panel_bg->DerefOrDestruct(); s_ranged_panel_bg = nullptr; }

    CALL_7(void, __thiscall, h->GetDefaultFunc(), resultDlg, a1, a2, a3, a4, a5, a6);
}

void __stdcall Hook_CombatResultRun(HiHook* h, void* resultDlg)
{
    // 结果窗口是模态的；这里只停止面板后续重绘，不再尝试擦外侧旧像素。
    s_ranged_panel_suppressed_for_result = true;
    s_ranged_panel_active = false;
    CALL_1(void, __thiscall, h->GetDefaultFunc(), resultDlg);
}

void __stdcall Hook_CombatResultDestroy(HiHook* h, void* resultDlg)
{
    WriteLog("[Diag] 0x4715C0 CPResult destroy enter this=%p active=%d", resultDlg, s_ranged_panel_active ? 1 : 0);
    CALL_1(void, __thiscall, h->GetDefaultFunc(), resultDlg);
    WriteLog("[Diag] 0x4715C0 CPResult destroy leave this=%p active=%d", resultDlg, s_ranged_panel_active ? 1 : 0);
    // 战斗伤亡界面关闭后，如果取消回到战斗，允许下一次战斗重绘重新创建面板。
    s_ranged_panel_suppressed_for_result = false;
    s_ranged_panel_active = true;
}

int __stdcall Hook_WndMgrRunDlg(HiHook* h, void* wndMgr, void* dlg, void* proc, int a3)
{
    WriteLog("[Diag] 0x602AE0 WndMgrRun wnd=%p dlg=%p proc=%p a3=%d battleMgr=%p battleDlg=%p currentDlg=%p active=%d",
        wndMgr, dlg, proc, a3, o_BattleMgr, o_BattleMgr ? o_BattleMgr->dlg : nullptr, o_CurrentDlg,
        s_ranged_panel_active ? 1 : 0);
    return CALL_4(int, __thiscall, h->GetDefaultFunc(), wndMgr, dlg, proc, a3);
}
