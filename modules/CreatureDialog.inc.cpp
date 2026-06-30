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

static void DrawRangedPanelToScreen(_BattleMgr_* mgr)
{
    if (!mgr || !mgr->dlg) return;
    if (o_CurrentDlg && o_CurrentDlg != mgr->dlg) return;
    _Pcx16_* screen = o_WndMgr ? o_WndMgr->screen_pcx16 : nullptr;
    if (!screen) return;

    _Dlg_* dlg = mgr->dlg;

    if (!s_battle_area_logged) {
        s_battle_area_logged = true;
        WriteLog("[RangedPanel] screen=%dx%d dlg=(%d,%d,%d,%d)",
            screen->width, screen->height,
            dlg->x, dlg->y, dlg->width, dlg->height);
    }

    int panel_w = cfg.ranged_panel_width;
    int panel_h = cfg.ranged_panel_height;
    int text_h = 17;

    // 水平居中于战场对话框
    int x = dlg->x + (dlg->width - panel_w) / 2;
    if (x < 0) x = 0;

    // 垂直：吸附到战场对话框外侧上方，Y 表示离上边缘的外侧间距
    int y = dlg->y - panel_h - cfg.ranged_panel_y;
    if (y < 0) y = 0;

    // 文字区域按面板自动分两列。
    int pad_x = 16;
    int mid_gap = 16;
    int col_w = (panel_w - pad_x * 2 - mid_gap) / 2;
    if (col_w < 40) col_w = panel_w / 2;
    int left_x = x + pad_x;
    int right_x = x + panel_w - pad_x - col_w;

    // 新战斗（dlg 变化）时重新加载背景
    if (s_last_battle_dlg != dlg) {
        s_last_battle_dlg = dlg;
        if (s_ranged_panel_bg) { s_ranged_panel_bg->DerefOrDestruct(); s_ranged_panel_bg = nullptr; }
    }
    if (!s_ranged_panel_bg) s_ranged_panel_bg = LoadPanelImageAsPcx8();
    if (s_ranged_panel_bg) {
        int bw = s_ranged_panel_bg->width;
        int bh = s_ranged_panel_bg->height;
        int draw_w = (bw < panel_w) ? bw : panel_w;
        int draw_h = (bh < panel_h) ? bh : panel_h;
        s_ranged_panel_bg->DrawToPcx16(0, 0, draw_w, draw_h, screen, x, y, FALSE);
    }

    int ranged[2] = { CalcRangedUnitsPower(mgr, 0), CalcRangedUnitsPower(mgr, 1) };
    int spell[2] = { CalcHeroSpellPower(mgr, 0), CalcHeroSpellPower(mgr, 1) };
    int total[2] = { ranged[0] + spell[0], ranged[1] + spell[1] };

    static char text[6][64];
    _snprintf(text[0], sizeof(text[0]) - 1, "%d", ranged[0]);
    _snprintf(text[1], sizeof(text[1]) - 1, "%d", ranged[1]);
    _snprintf(text[2], sizeof(text[2]) - 1, "%d", spell[0]);
    _snprintf(text[3], sizeof(text[3]) - 1, "%d", spell[1]);
    _snprintf(text[4], sizeof(text[4]) - 1, "%d", total[0]);
    _snprintf(text[5], sizeof(text[5]) - 1, "%d", total[1]);
    for (int i = 0; i < 6; ++i) text[i][sizeof(text[i]) - 1] = 0;

    _Fnt_* font = GetRangedPanelTextFont();
    if (font) {
        for (int row = 0; row < 3; ++row) {
            int yy = y + cfg.row_y[row];
            font->DrawTextToPcx16(text[row * 2], screen, left_x, yy, col_w, text_h, (_byte_)cfg.ranged_panel_text_color, 2, 0);
            font->DrawTextToPcx16(text[row * 2 + 1], screen, right_x, yy, col_w, text_h, (_byte_)cfg.ranged_panel_text_color, 0, 0);
        }
    }
}

static void UpdateRangedPanel(_BattleMgr_* mgr)
{
    DrawRangedPanelToScreen(mgr);
}


int __stdcall Hook_BattleRedraw(HiHook* h, _BattleMgr_* mgr, _bool8_ flip, _bool8_ set_battle_redraws, _bool8_ use_battle_redraws, int waiting_time, _bool8_ redraw_background, _bool8_ wait)
{
    CALL_7(void, __thiscall, h->GetDefaultFunc(), mgr, flip, set_battle_redraws, use_battle_redraws, waiting_time, redraw_background, wait);
    UpdateRangedPanel(mgr);
    return 0;
}
