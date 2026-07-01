// ========== 战斗价值显示与远程输出对比 ==========

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
static _Fnt_* GetRangedPanelTextFont();
static unsigned int ComputeStackChecksum(_BattleMgr_* mgr);

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

// ========== 战场顶部远程输出面板 ==========

static int CalcRangedOutputToTarget(_BattleMgr_* mgr, int side, _BattleStack_* target)
{
    if (!mgr || side < 0 || side > 1 || !target || target->count_current <= 0) return 0;

    __int64 output = 0;
    for (int slot = 0; slot < 21; ++slot) {
        _BattleStack_* shooter = &mgr->stack[side][slot];
        if (shooter->count_current <= 0 || shooter->creature.shots <= 0) continue;

        // 交给原版 CanShoot 判断贴脸、障碍、城墙、弹药、幻影神弓/金弓等射击可行性。
        // 如果不能射击该目标，则这个 shooter 对该目标的远程输出按 0 计。
        if (!shooter->CanShoot(target)) continue;

        // 原版基础伤害：_BattleStack_::CalcBaseDamage(0) @ 0x442E80。
        // 原版射击流程也是先调用 0x442E80，再把结果传给 Calc_Damage_Bonuses(0x443C60)。
        // 这里不再手写平均伤害，以便祝福、诅咒、蛊惑/多头等基础伤害相关状态走原版逻辑。
        int base_damage = CALL_2(int, __thiscall, 0x442E80, shooter, 0);
        if (base_damage <= 0) continue;

        int fireshield_damage = 0;
        int damage = shooter->Calc_Damage_Bonuses(target, base_damage, TRUE, TRUE, 0, &fireshield_damage);
        if (damage > 0) output += damage;
    }
    if (output > 0x7FFFFFFF) return 0x7FFFFFFF;
    return (int)output;
}

static int CalcRangedUnitsPower(_BattleMgr_* mgr, int side)
{
    if (!mgr || side < 0 || side > 1) return 0;

    int enemy_side = 1 - side;
    bool has_target = false;
    int min_output = 0x7FFFFFFF;

    for (int slot = 0; slot < 21; ++slot) {
        _BattleStack_* target = &mgr->stack[enemy_side][slot];
        if (target->count_current <= 0) continue;

        has_target = true;
        int output = CalcRangedOutputToTarget(mgr, side, target);
        if (output < min_output) min_output = output;
    }
    return has_target ? min_output : 0;
}

static int GetHeroEffectiveSpellLevel(_Hero_* hero, int spell_id)
{
    if (!hero || spell_id < 0 || spell_id >= 70) return 0;

    // 原版：_Hero_::GetEffectiveSpellLevel @ 0x4E52F0。
    // this=hero, arg1=spell_id, arg2=land_modifier。
    // 会综合英雄气/水/火/土魔法等级、魔法地形、飞行术等特殊规则。
    int land_modifier = hero->GetLandModifierUnder();
    int level = CALL_3(int, __thiscall, 0x4E52F0, hero, spell_id, land_modifier);
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    return level;
}

static int ApplySpellDamageArtifactBonuses(_Hero_* hero, int spell_id, int damage)
{
    if (!hero || spell_id < 0 || spell_id >= 70 || damage <= 0) return damage;

    // 四系灵球：对应学派法术最终伤害 +50%。
    // 这里按 o_Spell.school_flags 判断法术学派；多学派法术只加成一次。
    const unsigned school = o_Spell[spell_id].school_flags;
    bool boosted = false;
    if ((school & SSF_AIR) && hero->DoesWearArtifact(AID_ORB_OF_THE_FIRMAMENT)) boosted = true;
    if ((school & SSF_EARTH) && hero->DoesWearArtifact(AID_ORB_OF_SILT)) boosted = true;
    if ((school & SSF_FIRE) && hero->DoesWearArtifact(AID_ORB_OF_TEMPESTUOUS_FIRE)) boosted = true;
    if ((school & SSF_WATER) && hero->DoesWearArtifact(AID_ORB_OF_DRIVING_RAIN)) boosted = true;

    if (boosted) damage = damage * 150 / 100;
    return damage;
}

static int CalcHeroSpellPower(_BattleMgr_* mgr, int side)
{
    if (!mgr || side < 0 || side > 1) return 0;

    _Hero_* hero = mgr->hero[side];
    if (!hero) return 0;

    // 只要求英雄有魔法书；不检查当前魔法值，也不检查本回合是否已经施法。
    if (hero->doll_art[AS_SPELL_BOOK].id == -1) return 0;

    static const int kCountedDamageSpells[] = {
        SPL_MAGIC_ARROW,            // ID 15, Lv1, Magic Arrow，魔法神箭
        SPL_ICE_BOLT,               // ID 16, Lv2, Ice Bolt，霹雳寒冰
        SPL_LIGHTNING_BOLT,         // ID 17, Lv2, Lightning Bolt，霹雳闪电
        SPL_FROST_RING,             // ID 20, Lv3, Frost Ring，寒冰魔环
        SPL_FIREBALL,               // ID 21, Lv3, Fireball，连珠火球
        SPL_METEOR_SHOWER,          // ID 23, Lv4, Meteor Shower，流星火雨
        SPL_IMPLOSION,              // ID 18, Lv5, Implosion，雷鸣爆弹
        SPL_TITANS_LIGHTNING_BOLT,  // ID 57, Lv5, Titan's Lightning Bolt，泰坦之箭
    };

    int best = 0;
    int spell_power = hero->power;
    if (spell_power < 1) spell_power = 1;

    for (int i = 0; i < sizeof(kCountedDamageSpells) / sizeof(kCountedDamageSpells[0]); ++i) {
        int spell_id = kCountedDamageSpells[i];
        if (spell_id < 0 || spell_id >= 70) continue;
        if (!hero->spell[spell_id]) continue;

        int level = GetHeroEffectiveSpellLevel(hero, spell_id);

        _Spell_& spell = o_Spell[spell_id];
        int damage = spell.effect[level] + spell.eff_power * spell_power;

        // 法术特长：原版函数按英雄等级、特长法术、目标生物等级计算额外加成。
        // 当前“魔法输出能力”不考虑敌方目标，无目标估算参数固定用 1。
        damage += hero->GetSpell_Specialisation_Bonuses(spell_id, 1, damage);

        // 魔力 Sorcery：Basic +5%，Advanced +10%，Expert +15%。
        int sorcery = hero->second_skill[HSS_SORCERY];
        if (sorcery == 1) damage = damage * 105 / 100;
        else if (sorcery == 2) damage = damage * 110 / 100;
        else if (sorcery >= 3) damage = damage * 115 / 100;

        damage = ApplySpellDamageArtifactBonuses(hero, spell_id, damage);

        if (damage > best) best = damage;
    }
    return best;
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

// 通过 DirectDraw 离屏 surface 绘制背景图。
// 解码 PCX → RGBA → 创建 DD 离屏 surface → Lock 写入像素 → Blt 到 backbuffer。
// 完全绕开 HD_TC2 hook 和 screen_pcx16 buffer。

struct DDBackgroundSurface
{
    LPDIRECTDRAWSURFACE dd_surface;
    int width;
    int height;
};

static DDBackgroundSurface* CreateDDBackground(const char* pcx_path)
{
    // 解码 PCX
    unsigned char* data = nullptr;
    int size = 0;
    if (!ReadWholeFile(pcx_path, &data, &size)) return nullptr;
    if (size < 128) { free(data); return nullptr; }

    int bpp = data[3];
    int xmin = *(short*)(data + 4);
    int ymin = *(short*)(data + 6);
    int xmax = *(short*)(data + 8);
    int ymax = *(short*)(data + 10);
    int nplanes = data[65];
    int bpl = *(short*)(data + 66);
    int w = xmax - xmin + 1;
    int h = ymax - ymin + 1;

    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) { free(data); return nullptr; }
    if (!(nplanes == 1 && bpp == 8)) { free(data); return nullptr; }

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
    free(data);

    // 创建 DirectDraw 离屏 surface (16-bit RGB565)
    DDSURFACEDESC ddsd;
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    ddsd.dwWidth = w;
    ddsd.dwHeight = h;

    LPDIRECTDRAWSURFACE dd_surf = nullptr;
    HRESULT hr = o_DD->CreateSurface(&ddsd, &dd_surf, nullptr);
    if (FAILED(hr) || !dd_surf) {
        WriteLog("[DDBackground] CreateSurface failed hr=0x%08X w=%d h=%d", hr, w, h);
        free(raw);
        return nullptr;
    }

    // Lock surface 并写入 RGB565 像素
    DDSURFACEDESC lock_desc;
    memset(&lock_desc, 0, sizeof(lock_desc));
    lock_desc.dwSize = sizeof(lock_desc);
    hr = dd_surf->Lock(nullptr, &lock_desc, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, nullptr);
    if (FAILED(hr)) {
        WriteLog("[DDBackground] Lock failed hr=0x%08X", hr);
        dd_surf->Release();
        free(raw);
        return nullptr;
    }

    _word_* dst_row = (_word_*)((_byte_*)lock_desc.lpSurface);
    int dst_pitch_pixels = lock_desc.lPitch >> 1;

    // 检测像素格式
    DDPIXELFORMAT pf;
    memset(&pf, 0, sizeof(pf));
    pf.dwSize = sizeof(pf);
    dd_surf->GetPixelFormat(&pf);

    WriteLog("[DDBackground] locked w=%d h=%d pitch=%d bpp=%d RMask=0x%08X GMask=0x%08X BMask=0x%08X",
        lock_desc.dwWidth, lock_desc.dwHeight, lock_desc.lPitch,
        pf.dwRGBBitCount, pf.dwRBitMask, pf.dwGBitMask, pf.dwBBitMask);

    if (pf.dwRGBBitCount == 16) {
        // 16-bit：直接写 RGB565
        for (int y = 0; y < h; y++) {
            _word_* d = dst_row + y * dst_pitch_pixels;
            _byte_* s = raw + y * bpl;
            for (int x = 0; x < w; x++) {
                unsigned char idx = s[x];
                d[x] = RGB565_fromR8G8B8(pal[idx * 3], pal[idx * 3 + 1], pal[idx * 3 + 2]);
            }
        }
    } else if (pf.dwRGBBitCount == 32) {
        // 32-bit：写 RGBA
        _dword_* d32 = (_dword_*)lock_desc.lpSurface;
        int dst_pitch32 = lock_desc.lPitch >> 2;
        for (int y = 0; y < h; y++) {
            _dword_* d = d32 + y * dst_pitch32;
            _byte_* s = raw + y * bpl;
            for (int x = 0; x < w; x++) {
                unsigned char idx = s[x];
                d[x] = (0xFF << 24) | (pal[idx * 3] << 16) | (pal[idx * 3 + 1] << 8) | pal[idx * 3 + 2];
            }
        }
    } else {
        WriteLog("[DDBackground] unsupported bpp=%d", pf.dwRGBBitCount);
    }

    dd_surf->Unlock(nullptr);
    free(raw);

    DDBackgroundSurface* bg = new DDBackgroundSurface();
    bg->dd_surface = dd_surf;
    bg->width = w;
    bg->height = h;
    WriteLog("[DDBackground] created w=%d h=%d", w, h);
    return bg;
}

static void DestroyDDBackground(DDBackgroundSurface* bg)
{
    if (!bg) return;
    if (bg->dd_surface) { bg->dd_surface->Release(); bg->dd_surface = nullptr; }
    delete bg;
}

static void DrawDDBackground(DDBackgroundSurface* bg, int dst_x, int dst_y, int dst_w, int dst_h)
{
    if (!bg || !bg->dd_surface) return;
    RECT src_rect = { 0, 0, bg->width, bg->height };
    RECT dst_rect = { dst_x, dst_y, dst_x + dst_w, dst_y + dst_h };
    HRESULT hr = o_DDSurfaceBackBuffer->Blt(&dst_rect, bg->dd_surface, &src_rect, DDBLT_WAIT, nullptr);
    if (FAILED(hr)) {
        WriteLog("[DDBackground] Blt failed hr=0x%08X", hr);
    }
}


class RangedOverlayPanel
{
public:
    RangedOverlayPanel()
    {
        ResetRuntimeState();
    }

    void MarkDirty(const char* reason)
    {
        dirty_ = true;
        if (reason) WriteLog("[RangedOverlayPanel] dirty: %s", reason);
    }

    void Destroy()
    {
        active_ = false;
        ReleaseBackground();
        ResetText();
        if (o_BattleMgr) o_BattleMgr->RedrawBattlefield(FALSE, TRUE, FALSE, 0, TRUE, FALSE);
    }

    void Draw(_BattleMgr_* mgr)
    {
        if (!mgr || !mgr->dlg) return;
        _Pcx16_* screen = o_WndMgr ? o_WndMgr->screen_pcx16 : nullptr;
        if (!screen) return;

        _Dlg_* dlg = mgr->dlg;

        if (diag_draw_count_ < 20) {
            ++diag_draw_count_;
            WriteLog("[Diag] RangedOverlayPanel::Draw #%d mgr=%p dlg=%p active=%d currentDlg=%p",
                diag_draw_count_, mgr, dlg, active_ ? 1 : 0, o_CurrentDlg);
        }

        // 新战斗管理器出现时，重置运行态并置脏：进入战斗后首次绘制计算一次。
        if (last_mgr_ != mgr) {
            last_mgr_ = mgr;
            active_ = true;
            suppressed_for_result_ = false;
            stack_checksum_ = 0;
            ResetText();
            MarkDirty("new battle manager");
        }

        // 新战斗窗口出现时，重新加载背景并置脏；位置仍按战斗 dlg 计算。
        if (last_dlg_ != dlg) {
            last_dlg_ = dlg;
            active_ = true;
            s_battle_end_logged = false;
            stack_checksum_ = 0;
            ResetText();
            ReleaseBackground();
            MarkDirty("new battle dialog");
        }

        if (!active_) return;
        if (suppressed_for_result_) return;
        if (IsBattleEnded(mgr)) {
            Destroy();
            return;
        }

        if (!battle_area_logged_) {
            battle_area_logged_ = true;
            WriteLog("[RangedOverlayPanel] screen=%dx%d dlg=(%d,%d,%d,%d)",
                screen->width, screen->height,
                dlg->x, dlg->y, dlg->width, dlg->height);
        }

        int panel_w = cfg.ranged_panel_width;
        int panel_h = cfg.ranged_panel_height;
        int text_h = 17;

        // 独立 overlay panel 的屏幕坐标：位置保持旧逻辑，水平居中于战斗窗口，吸附到外侧上方。
        int x = dlg->x + (dlg->width - panel_w) / 2;
        if (x < 0) x = 0;
        int y = dlg->y - panel_h - cfg.ranged_panel_y;
        if (y < 0) y = 0;

        int pad_x = 16;
        int mid_gap = 16;
        int col_w = (panel_w - pad_x * 2 - mid_gap) / 2;
        if (col_w < 40) col_w = panel_w / 2;
        int left_x = x + pad_x;
        int right_x = x + panel_w - pad_x - col_w;

        EnsureBackground();
        if (bg_) {
            int draw_w = (bg_->width < panel_w) ? bg_->width : panel_w;
            int draw_h = (bg_->height < panel_h) ? bg_->height : panel_h;
            last_x_ = x;
            last_y_ = y;
            last_w_ = draw_w;
            last_h_ = draw_h;
            // 通过 DirectDraw Blt 绘制到 backbuffer，绕开 HD_TC2。
            DrawDDBackground(bg_, x, y, draw_w, draw_h);
        }

        // 正常帧只画缓存文本；dirty 或空缓存时才重算。
        if (dirty_ || !text_[0][0]) {
            Recalculate(mgr, dirty_ ? "dirty" : "empty cache");
        }

        _Fnt_* font = GetRangedPanelTextFont();
        if (font) {
            for (int row = 0; row < 3; ++row) {
                int yy = y + cfg.row_y[row];
                font->DrawTextToPcx16(text_[row * 2], screen, left_x, yy, col_w, text_h, (_byte_)cfg.ranged_panel_text_color, 2, 0);
                font->DrawTextToPcx16(text_[row * 2 + 1], screen, right_x, yy, col_w, text_h, (_byte_)cfg.ranged_panel_text_color, 0, 0);
            }
        }
    }

    int LastX() const { return last_x_; }
    int LastY() const { return last_y_; }
    int LastW() const { return last_w_; }
    int LastH() const { return last_h_; }
    bool Active() const { return active_; }

private:
    void ResetRuntimeState()
    {
        bg_ = nullptr;
        last_dlg_ = nullptr;
        last_mgr_ = nullptr;
        active_ = true;
        suppressed_for_result_ = false;
        last_x_ = -1;
        last_y_ = -1;
        last_w_ = 0;
        last_h_ = 0;
        battle_area_logged_ = false;
        diag_draw_count_ = 0;
        dirty_ = true;
        stack_checksum_ = 0;
        ResetText();
    }

    void ResetText()
    {
        memset(text_, 0, sizeof(text_));
        dirty_ = true;
    }

    void EnsureBackground()
    {
        if (!bg_) {
            char modulePath[MAX_PATH];
            GetModuleFileNameA(g_hModule, modulePath, MAX_PATH);
            char* slash = strrchr(modulePath, '\\');
            if (slash) slash[1] = 0; else modulePath[0] = 0;
            char path[2048];
            _snprintf(path, sizeof(path) - 1, "%simg\\%s", modulePath, cfg.ranged_panel_image);
            path[sizeof(path) - 1] = 0;
            bg_ = CreateDDBackground(path);
        }
    }

    void ReleaseBackground()
    {
        DestroyDDBackground(bg_);
        bg_ = nullptr;
    }

    void Recalculate(_BattleMgr_* mgr, const char* reason)
    {
        if (!mgr) return;

        unsigned int checksum = ComputeStackChecksum(mgr);
        stack_checksum_ = checksum;
        int ranged[2] = { CalcRangedUnitsPower(mgr, 0), CalcRangedUnitsPower(mgr, 1) };
        int spell[2] = { CalcHeroSpellPower(mgr, 0), CalcHeroSpellPower(mgr, 1) };
        int total[2] = { ranged[0] + spell[0], ranged[1] + spell[1] };

        _snprintf(text_[0], sizeof(text_[0]) - 1, "%d", ranged[0]);
        _snprintf(text_[1], sizeof(text_[1]) - 1, "%d", ranged[1]);
        _snprintf(text_[2], sizeof(text_[2]) - 1, "%d", spell[0]);
        _snprintf(text_[3], sizeof(text_[3]) - 1, "%d", spell[1]);
        _snprintf(text_[4], sizeof(text_[4]) - 1, "%d", total[0]);
        _snprintf(text_[5], sizeof(text_[5]) - 1, "%d", total[1]);
        for (int i = 0; i < 6; ++i) text_[i][sizeof(text_[i]) - 1] = 0;

        dirty_ = false;
        WriteLog("[RangedOverlayPanel] recalculated reason=%s checksum=%u ranged=(%d,%d) spell=(%d,%d) total=(%d,%d)",
            reason ? reason : "unknown", checksum, ranged[0], ranged[1], spell[0], spell[1], total[0], total[1]);
    }

    DDBackgroundSurface* bg_;
    _Dlg_* last_dlg_;
    _BattleMgr_* last_mgr_;
    bool active_;
    bool suppressed_for_result_;
    int last_x_;
    int last_y_;
    int last_w_;
    int last_h_;
    bool battle_area_logged_;
    int diag_draw_count_;
    bool dirty_;
    unsigned int stack_checksum_;
    char text_[6][64];
};

static RangedOverlayPanel s_ranged_overlay_panel;

static void DestroyRangedPanel()
{
    s_ranged_overlay_panel.Destroy();
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

static void MarkRangedPanelDirty(const char* reason)
{
    s_ranged_overlay_panel.MarkDirty(reason);
}

static void UpdateRangedPanel(_BattleMgr_* mgr)
{
    s_ranged_overlay_panel.Draw(mgr);
}


int __stdcall Hook_BattleRedraw(HiHook* h, _BattleMgr_* mgr, _bool8_ flip, _bool8_ set_battle_redraws, _bool8_ use_battle_redraws, int waiting_time, _bool8_ redraw_background, _bool8_ wait)
{
    CALL_7(void, __thiscall, h->GetDefaultFunc(), mgr, flip, set_battle_redraws, use_battle_redraws, waiting_time, redraw_background, wait);
    UpdateRangedPanel(mgr);
    return 0;
}

// Combat_StartBattle @ 0x4781C0：进入战斗时先置脏，首次绘制计算。
int __stdcall Hook_CombatStartBattle(HiHook* h, _BattleMgr_* mgr)
{
    int result = CALL_1(int, __thiscall, h->GetDefaultFunc(), mgr);
    MarkRangedPanelDirty("combat start");
    return result;
}

// MagicSystem_2 @ 0x464F10：战斗内施法流程。原函数返回后置脏，下一次绘制刷新。
int __stdcall Hook_CombatCastSpell(HiHook* h, _BattleMgr_* mgr, int x, int y)
{
    CALL_3(void, __thiscall, h->GetDefaultFunc(), mgr, x, y);
    MarkRangedPanelDirty("spell cast");
    return 0;
}

// FUN_004746B0：战斗行动处理主函数，覆盖人类/电脑 stack 行动后的状态变化。
// 返回后置脏；绘制时只在 dirty=true 时重算，不会每帧完整重算。
int __stdcall Hook_CombatActionHandler(HiHook* h, _BattleMgr_* mgr, int msg)
{
    int result = CALL_2(int, __thiscall, h->GetDefaultFunc(), mgr, msg);
    MarkRangedPanelDirty("stack action");
    return result;
}
