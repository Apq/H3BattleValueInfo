// ========== 战斗价值显示与远程输出对比 ==========

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

static _Fnt_* GetRangedPanelTextFont();
static unsigned int ComputeStackChecksum(_BattleMgr_* mgr);

static const int RP_BASE_Y_OFFSET = 23;

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
    if (g_disable_log) return;
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
    if (g_disable_log) return;
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
    TryAddFightValueLine((_Dlg_*)c->ebx);
    return EXEC_DEFAULT;
}
int __stdcall Hook_BuildAdventure(LoHook* h, HookContext* c)
{
    TryAddFightValueLine((_Dlg_*)c->esi);
    return EXEC_DEFAULT;
}
int __stdcall Hook_BuildTown(LoHook* h, HookContext* c)
{
    TryAddFightValueLine((_Dlg_*)c->esi);
    return EXEC_DEFAULT;
}

// ========== 战场顶部远程输出面板 ==========

static int SafeCalcBaseDamage(_BattleStack_* shooter)
{
    __try {
        return THISCALL_2(int, 0x442E80, shooter, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static int SafeCanShoot(_BattleStack_* shooter, _BattleStack_* target)
{
    __try {
        return THISCALL_2(bool, 0x442610, shooter, target);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static int SafeCalcDamageBonuses(_BattleStack_* shooter, _BattleStack_* target, int base_damage, int* fireshield)
{
    __try {
        return THISCALL_7(int, 0x443C60, shooter, target, base_damage, TRUE, TRUE, 0, fireshield);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static bool SafeDoesWearArtifact(_Hero_* hero, int art_id)
{
    __try {
        return THISCALL_2(bool, 0x4E2C90, hero, art_id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static int SafeGetEffectiveSpellLevel(_Hero_* hero, int spell_id, int land_mod)
{
    __try {
        return THISCALL_3(int, 0x4E52F0, hero, spell_id, land_mod);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static int SafeGetSpellSpecBonus(_Hero_* hero, int spell_id, int damage)
{
    __try {
        return THISCALL_4(int, 0x4E6260, hero, spell_id, 1, damage);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static int SafeGetLandModifier(_Hero_* hero)
{
    __try {
        return THISCALL_1(int, 0x4E5210, hero);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static bool SafeCalcRangedUnitsPower(_BattleMgr_* mgr, int side, int* out);
static bool SafeCalcHeroSpellPower(_BattleMgr_* mgr, int side, int* out);

static int CalcRangedOutputToTarget(_BattleMgr_* mgr, int side, _BattleStack_* target)
{
    if (!mgr || side < 0 || side > 1 || !target || target->count_current <= 0) return 0;

    __int64 output = 0;
    for (int slot = 0; slot < 21; ++slot) {
        _BattleStack_* shooter = &mgr->stack[side][slot];
        if (shooter->count_current <= 0 || shooter->creature.shots <= 0) continue;
        if (shooter->blinded || shooter->paralyzed || shooter->forgetfulness_level > 0) continue;

        bool can_shoot = SafeCanShoot(shooter, target);
        int base_damage = can_shoot ? SafeCalcBaseDamage(shooter) : 0;

        if (!can_shoot) continue;
        if (base_damage <= 0) continue;

        int fireshield_damage = 0;
        int damage = SafeCalcDamageBonuses(shooter, target, base_damage, &fireshield_damage);
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

static bool SafeCalcRangedUnitsPower(_BattleMgr_* mgr, int side, int* out)
{
    if (!out) return false;
    __try {
        *out = CalcRangedUnitsPower(mgr, side);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[RangedOverlayPanel] SEH exception during ranged calc side=%d mgr=%p", side, mgr);
        return false;
    }
}

static int GetHeroEffectiveSpellLevel(_Hero_* hero, int spell_id)
{
    if (!hero || spell_id < 0 || spell_id >= 70) return 0;

    // 原版：_Hero_::GetEffectiveSpellLevel @ 0x4E52F0。
    // this=hero, arg1=spell_id, arg2=land_modifier。
    // 会综合英雄气/水/火/土魔法等级、魔法地形、飞行术等特殊规则。
    int land_modifier = SafeGetLandModifier(hero);
    int level = SafeGetEffectiveSpellLevel(hero, spell_id, land_modifier);
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
    if ((school & SSF_AIR) && SafeDoesWearArtifact(hero, AID_ORB_OF_THE_FIRMAMENT)) boosted = true;
    if ((school & SSF_EARTH) && SafeDoesWearArtifact(hero, AID_ORB_OF_SILT)) boosted = true;
    if ((school & SSF_FIRE) && SafeDoesWearArtifact(hero, AID_ORB_OF_TEMPESTUOUS_FIRE)) boosted = true;
    if ((school & SSF_WATER) && SafeDoesWearArtifact(hero, AID_ORB_OF_DRIVING_RAIN)) boosted = true;

    if (boosted) damage = damage * 150 / 100;
    return damage;
}

static int CalcHeroSpellPower(_BattleMgr_* mgr, int side)
{
    if (!mgr || side < 0 || side > 1) return 0;
    if (!o_Spell) return 0;

    _Hero_* hero = mgr->hero[side];
    if (!hero || (uint32_t)hero < 0x00400000 || (uint32_t)hero > 0x20000000) return 0;

    // 只要求英雄有魔法书；不检查当前魔法值，也不检查本回合是否已经施法。
    if (hero->doll_art[AS_SPELL_BOOK].id == -1 || hero->doll_art[AS_SPELL_BOOK].id == (short)0xFFFF) return 0;

    // 禁魔地形检查：spec_terr_type == 2 为诅咒之地，双方只能施放 1 级魔法。
    // spec_terr_type == 1 为魔法平原，法术以专家级施放（由 GetEffectiveSpellLevel 处理）。
    bool cursed_ground = (mgr->spec_terr_type == 2);

    // 禁魔披风（Recanter's Cloak）：佩戴方只能施放 1-2 级魔法。
    bool recanter = SafeDoesWearArtifact(hero, AID_RECANTERS_CLOAK);

    // 综合最高可用法术等级：诅咒之地→1，禁魔披风→2，否则→5。
    int max_spell_level = 5;
    if (cursed_ground) max_spell_level = 1;
    else if (recanter) max_spell_level = 2;

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
    // homm3.h 中 power 定义在 +1142(0x476)，但实际那是 attack。
    // 原版代码中主属性在 0x476~0x479，顺序是 ATK/DEF/SPW/KNO。
    // spell power 在 0x478 (1144 decimal)。
    unsigned char* raw = (unsigned char*)hero;
    int sp_spw = raw[0x478];

    // 英雄特长表：用于魔力特长的等级加成。
    int hero_id = *(int*)((char*)hero + 0x1a);
    int hero_level = *(short*)((char*)hero + 0x55);
    _ptr_ spec_table = *(_ptr_*)0x679C80;
    int spec_type = -1;
    int spec_param = -1;
    if (spec_table && hero_id >= 0 && hero_id < 156) {
        spec_type = *(int*)(spec_table + hero_id * 0x28);
        spec_param = *(int*)(spec_table + hero_id * 0x28 + 4);
    }

    int spell_power = sp_spw;  // 使用正确的 spell power 偏移
    if (spell_power < 1) spell_power = 1;

    for (int i = 0; i < sizeof(kCountedDamageSpells) / sizeof(kCountedDamageSpells[0]); ++i) {
        int spell_id = kCountedDamageSpells[i];
        if (spell_id < 0 || spell_id >= 70) continue;
        if (!hero->spell[spell_id]) continue;

        _Spell_& spell = o_Spell[spell_id];

        // 跳过被禁魔地形/禁魔披风限制的法术等级
        if (spell.level > max_spell_level) continue;

        int level = GetHeroEffectiveSpellLevel(hero, spell_id);
        int base_damage = spell.effect[level] + spell.eff_power * spell_power;
        int damage = base_damage;

        // 法术特长：原版函数按英雄等级、特长法术、目标生物等级计算额外加成。
        // 当前“魔法输出能力”不考虑敌方目标，无目标估算参数固定用 1。
        int spec_bonus = SafeGetSpellSpecBonus(hero, spell_id, damage);
        damage += spec_bonus;

        // 魔力 Sorcery：Basic +5%，Advanced +10%，Expert +15%。
        // 英雄特长(spec_type=0, spec_param=25=HSS_SORCERY)会随等级增强 sorcery。
        // 公式（浮点）：effective_mult = 1 + sorcery_pct + sorcery_pct * (hero_level / 20.0)
        int sorcery = hero->second_skill[HSS_SORCERY];

        bool has_sorcery_spec = (spec_type == 0 && spec_param == HSS_SORCERY);
        if (sorcery > 0) {
            double sorcery_pct;
            if (sorcery == 1) sorcery_pct = 0.05;
            else if (sorcery == 2) sorcery_pct = 0.10;
            else sorcery_pct = 0.15;  // >=3 expert

            if (has_sorcery_spec) {
                // 魔力特长：sorcery 效果随英雄等级增强
                sorcery_pct = sorcery_pct * (1.0 + (double)hero_level / 20.0);
            }

            damage = (int)((double)damage * (1.0 + sorcery_pct));
        }

        damage = ApplySpellDamageArtifactBonuses(hero, spell_id, damage);

        if (damage > best) best = damage;
    }
    return best;
}

static bool SafeCalcHeroSpellPower(_BattleMgr_* mgr, int side, int* out)
{
    if (!out) return false;
    __try {
        *out = CalcHeroSpellPower(mgr, side);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[RangedOverlayPanel] SEH exception during spell calc side=%d mgr=%p", side, mgr);
        return false;
    }
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

static unsigned int ComputeStackChecksum(_BattleMgr_* mgr)
{
    if (!mgr) return 0;
    unsigned int hash = 2166136261u; // FNV offset

#define BVI_HASH_INT(v) do { hash ^= (unsigned)(v); hash *= 16777619u; } while (0)

    BVI_HASH_INT(mgr->spec_terr_type);
    BVI_HASH_INT(mgr->hero_casted[0]);
    BVI_HASH_INT(mgr->hero_casted[1]);
    BVI_HASH_INT(mgr->stacks_count[0]);
    BVI_HASH_INT(mgr->stacks_count[1]);
    BVI_HASH_INT(mgr->current_mon_side);
    BVI_HASH_INT(mgr->current_mon_index);
    BVI_HASH_INT(mgr->current_active_side);
    BVI_HASH_INT(mgr->move_type);
    BVI_HASH_INT(mgr->attacker_coord);
    BVI_HASH_INT(mgr->finished);
    BVI_HASH_INT((uint32_t)mgr->active_stack >> 4);

    for (int side = 0; side < 2; ++side) {
        BVI_HASH_INT((uint32_t)mgr->hero[side] >> 4);
        if (mgr->hero[side] && (uint32_t)mgr->hero[side] > 0x00400000 && (uint32_t)mgr->hero[side] < 0x20000000) {
            _Hero_* h = mgr->hero[side];
            BVI_HASH_INT(h->doll_art[AS_SPELL_BOOK].id);
            BVI_HASH_INT(((unsigned char*)h)[0x478]);
            BVI_HASH_INT(((unsigned char*)h)[0x479]);
            BVI_HASH_INT(h->second_skill[HSS_SORCERY]);
            for (int i = 0; i < 70; ++i) {
                if (h->spell[i]) BVI_HASH_INT((i << 8) | h->spell[i]);
            }
        }
        for (int slot = 0; slot < 21; ++slot) {
            const _BattleStack_& s = mgr->stack[side][slot];
            BVI_HASH_INT(s.count_current);
            BVI_HASH_INT(s.count_before_attack);
            BVI_HASH_INT(s.creature_id);
            BVI_HASH_INT(s.hex_ix);
            BVI_HASH_INT(s.def_group_ix);
            BVI_HASH_INT(s.army_slot_ix);
            BVI_HASH_INT(s.count_at_start);
            BVI_HASH_INT(s.lost_hp);
            BVI_HASH_INT(s.creature.shots);
            BVI_HASH_INT(s.creature.attack);
            BVI_HASH_INT(s.creature.defence);
            BVI_HASH_INT(s.creature.damage_min);
            BVI_HASH_INT(s.creature.damage_max);
            BVI_HASH_INT(s.creature.hit_points);
            BVI_HASH_INT(s.active_spell_count);
            BVI_HASH_INT(s.active_spell_duration[SPL_BLESS]);
            BVI_HASH_INT(s.active_spell_duration[SPL_CURSE]);
            BVI_HASH_INT(s.active_spell_duration[SPL_PRECISION]);
            BVI_HASH_INT(s.active_spell_duration[SPL_FORGETFULNESS]);
            BVI_HASH_INT(s.active_spell_duration[SPL_BLIND]);
            BVI_HASH_INT(s.active_spell_duration[SPL_PARALYZE]);
            BVI_HASH_INT(s.active_spell_level[SPL_BLESS]);
            BVI_HASH_INT(s.active_spell_level[SPL_CURSE]);
            BVI_HASH_INT(s.active_spell_level[SPL_PRECISION]);
            BVI_HASH_INT(s.active_spell_level[SPL_FORGETFULNESS]);
            BVI_HASH_INT(s.bless_damage);
            BVI_HASH_INT(s.curse_damage);
            BVI_HASH_INT(s.bloodlust_effect);
            BVI_HASH_INT(s.precision_effect);
            BVI_HASH_INT(s.weakness_effect);
            BVI_HASH_INT(s.prayer_effect);
            BVI_HASH_INT(s.slayer_type);
            BVI_HASH_INT(s.frenzy_multiplier * 1000.0f);
            BVI_HASH_INT(s.shield_effect * 1000.0f);
            BVI_HASH_INT(s.air_shield_effect * 1000.0f);
            BVI_HASH_INT(s.blinded);
            BVI_HASH_INT(s.paralyzed);
            BVI_HASH_INT(s.forgetfulness_level);
            BVI_HASH_INT(s.morale);
            BVI_HASH_INT(s.luck);
            BVI_HASH_INT(s.is_done);
        }
    }
#undef BVI_HASH_INT
    return hash;
}

static bool SafeComputeStackChecksum(_BattleMgr_* mgr, unsigned int* out)
{
    if (!out) return false;
    __try {
        *out = ComputeStackChecksum(mgr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = 0;
        WriteLog("[RangedOverlayPanel] SEH exception during checksum mgr=%p", mgr);
        return false;
    }
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

static DDBackgroundSurface* CreateEmptyDDSurface(int w, int h)
{
    if (w <= 0 || h <= 0 || !o_DD) return nullptr;
    DDSURFACEDESC ddsd;
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    ddsd.dwWidth = w;
    ddsd.dwHeight = h;

    LPDIRECTDRAWSURFACE surf = nullptr;
    HRESULT hr = o_DD->CreateSurface(&ddsd, &surf, nullptr);
    if (FAILED(hr) || !surf) {
        WriteLog("[DDBackground] Create empty surface failed hr=0x%08X w=%d h=%d", hr, w, h);
        return nullptr;
    }

    DDBackgroundSurface* bg = new DDBackgroundSurface();
    bg->dd_surface = surf;
    bg->width = w;
    bg->height = h;
    return bg;
}

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
    if (rp < rawSize) {
        WriteLog("[DDBackground] incomplete PCX data decoded=%d expected=%d", rp, rawSize);
        free(raw);
        free(data);
        return nullptr;
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
    if (!palFound) {
        WriteLog("[DDBackground] PCX palette not found");
        free(raw);
        free(data);
        return nullptr;
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
        dd_surf->Unlock(nullptr);
        dd_surf->Release();
        free(raw);
        return nullptr;
    }

    dd_surf->Unlock(nullptr);
    free(raw);

    DDBackgroundSurface* bg = new DDBackgroundSurface();
    bg->dd_surface = dd_surf;
    bg->width = w;
    bg->height = h;
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
    __try {
        RECT src_rect = { 0, 0, bg->width, bg->height };
        RECT dst_rect = { dst_x, dst_y, dst_x + dst_w, dst_y + dst_h };
        HRESULT hr = o_DDSurfaceBackBuffer->Blt(&dst_rect, bg->dd_surface, &src_rect, DDBLT_WAIT, nullptr);
        if (FAILED(hr)) {
            WriteLog("[DDBackground] Blt failed hr=0x%08X", hr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[DDBackground] SEH exception during Blt dst=(%d,%d,%d,%d)", dst_x, dst_y, dst_w, dst_h);
    }
}

static bool CopyBackBufferToSurface(DDBackgroundSurface* dst, int src_x, int src_y, int w, int h)
{
    if (!dst || !dst->dd_surface || !o_DDSurfaceBackBuffer || w <= 0 || h <= 0) return false;
    __try {
        RECT src_rect = { src_x, src_y, src_x + w, src_y + h };
        RECT dst_rect = { 0, 0, w, h };
        HRESULT hr = dst->dd_surface->Blt(&dst_rect, o_DDSurfaceBackBuffer, &src_rect, DDBLT_WAIT, nullptr);
        if (FAILED(hr)) {
            WriteLog("[DDBackground] Save underlay failed hr=0x%08X", hr);
            return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[DDBackground] SEH exception during save underlay rect=(%d,%d,%d,%d)", src_x, src_y, w, h);
        return false;
    }
}

static bool RestoreSurfaceToBackBuffer(DDBackgroundSurface* src, int dst_x, int dst_y, int w, int h)
{
    if (!src || !src->dd_surface || !o_DDSurfaceBackBuffer || w <= 0 || h <= 0) return false;
    __try {
        RECT src_rect = { 0, 0, w, h };
        RECT dst_rect = { dst_x, dst_y, dst_x + w, dst_y + h };
        HRESULT hr = o_DDSurfaceBackBuffer->Blt(&dst_rect, src->dd_surface, &src_rect, DDBLT_WAIT, nullptr);
        if (FAILED(hr)) {
            WriteLog("[DDBackground] Restore underlay failed hr=0x%08X", hr);
            return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[DDBackground] SEH exception during restore underlay rect=(%d,%d,%d,%d)", dst_x, dst_y, w, h);
        return false;
    }
}

static void SafeDrawText(_Fnt_* font, _Pcx16_* screen, const char* text, int x, int y, int w, int h, int color, int align)
{
    if (!font || !screen || !text) return;
    __try {
        font->TextDraw(screen, text, x, y, w, h, (eTextColor)color, (eTextAlignment)align);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[RangedOverlayPanel] SEH exception during TextDraw text='%s' rect=(%d,%d,%d,%d)", text, x, y, w, h);
    }
}

static void ClearScreenRegion(_Pcx16_* screen, int x, int y, int w, int h)
{
    if (!screen || !screen->buffer || w <= 0 || h <= 0) return;
    __try {
        int sw = screen->width;
        int sh = screen->height;
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > sw) w = sw - x;
        if (y + h > sh) h = sh - y;
        if (w <= 0 || h <= 0) return;
        // 填充透明色（0），清除旧文字残留
        for (int row = 0; row < h; ++row) {
            unsigned short* p = (unsigned short*)screen->buffer + (y + row) * sw + x;
            for (int col = 0; col < w; ++col) {
                p[col] = 0;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[RangedOverlayPanel] SEH exception during ClearScreenRegion rect=(%d,%d,%d,%d)", x, y, w, h);
    }
}


class RangedOverlayPanel
{
public:
    RangedOverlayPanel()
    {
        ResetRuntimeState();
    }

    void MarkDirty(const char* /*reason*/)
    {
        dirty_ = true;
    }

    void BeginManualBattle(_BattleMgr_* mgr, const char* reason)
    {
        last_mgr_ = mgr;
        active_ = true;
        manual_battle_started_ = true;
        suppressed_for_result_ = false;
        stack_checksum_ = 0;
        ResetText();
        MarkDirty(reason);
    }

    void Destroy()
    {
        active_ = false;
        manual_battle_started_ = false;
        suppressed_for_result_ = false;
        RestoreUnderlay();
        ReleaseBackground();
        ResetText();
        if (o_BattleMgr) o_BattleMgr->RedrawBattlefield(FALSE, TRUE, FALSE, 0, TRUE, FALSE);
    }

    void Draw(_BattleMgr_* mgr)
    {
        DrawImpl(mgr);
    }

    void DrawImpl(_BattleMgr_* mgr)
    {
        // 面板只认战斗管理器持有的主窗口；activeWindow 仅用于结果框检测。
        _Dlg_* dlg = (mgr && mgr->dlg && (uint32_t)mgr->dlg > 0x10000) ? mgr->dlg : nullptr;
        _Dlg_* active_dlg = GetActiveDlg();
        _Pcx16_* screen = o_WndMgr ? o_WndMgr->screen_pcx16 : nullptr;
        if (!screen || !dlg) return;

        // 新战斗管理器出现时，重置运行态并置脏：进入战斗后首次绘制计算一次。
        if (last_mgr_ != mgr) {
            last_mgr_ = mgr;
            active_ = false;
            manual_battle_started_ = false;
            suppressed_for_result_ = false;
            stack_checksum_ = 0;
            ResetText();
            MarkDirty("new battle manager");
        }

        // 新战斗窗口出现时，重新加载背景并置脏；位置仍按战斗 dlg 计算。
        if (last_dlg_ != dlg) {
            last_dlg_ = dlg;
            stack_checksum_ = 0;
            ResetText();
            ReleaseBackground();
            MarkDirty("new battle dialog");
        }

        if (!manual_battle_started_) return;

        if (IsReplayableQuickBattleResultDlg(active_dlg)) {
            SuppressForResult("replayable quick battle result", mgr, active_dlg);
            return;
        }
        if (IsBattleOverByEngine(mgr)) {
            SuppressForResult("battle over", mgr, active_dlg);
            return;
        }
        if (IsHiddenBattleByEngine(mgr)) {
            SuppressForResult("hidden battle", mgr, active_dlg);
            return;
        }
        // 从结果界面返回：当前不再处于结果/隐藏/结束状态时即可恢复。
        if (suppressed_for_result_ && manual_battle_started_) {
            active_ = true;
            suppressed_for_result_ = false;
            stack_checksum_ = 0;
            ResetText();
            MarkDirty("re-enter manual battle");
        }

        if (!active_) return;
        if (suppressed_for_result_) return;

        int panel_w = cfg.ranged_panel_width;
        int panel_h = cfg.ranged_panel_height;
        int text_h = 17;

        int battle_x = (screen->width - 800) / 2;
        int battle_y = (screen->height - 600) / 2;
        if (battle_x < 0) battle_x = 0;
        if (battle_y < 0) battle_y = 0;

        int x = battle_x + (800 - panel_w) / 2;
        if (x < 0) x = 0;
        int y = battle_y - panel_h - RP_BASE_Y_OFFSET - cfg.ranged_panel_y;
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
            SaveUnderlay(x, y, draw_w, draw_h);
            last_x_ = x;
            last_y_ = y;
            last_w_ = draw_w;
            last_h_ = draw_h;
            DrawDDBackground(bg_, x, y, draw_w, draw_h);
            // 同步清除 screen_pcx16 面板区域的旧文字，避免多帧叠加
            if (screen && screen->buffer) {
                ClearScreenRegion(screen, x, y, draw_w, draw_h);
            }
        } else {
            return;
        }

        unsigned int checksum = 0;
        bool checksum_ok = SafeComputeStackChecksum(mgr, &checksum);
        if (!checksum_ok) return;

        // 正常帧只画缓存文本；dirty、空缓存或栈状态变化时才重算。
        if (dirty_ || !text_[0][0] || checksum != stack_checksum_) {
            Recalculate(mgr, dirty_ ? "dirty" : (!text_[0][0] ? "empty cache" : "stack changed"));
        }

        _Fnt_* font = GetRangedPanelTextFont();
        if (font) {
            for (int row = 0; row < 3; ++row) {
                int yy = y + cfg.row_y[row];
                SafeDrawText(font, screen, text_[row * 2], left_x, yy, col_w, text_h, cfg.ranged_panel_text_color, 2);
                SafeDrawText(font, screen, text_[row * 2 + 1], right_x, yy, col_w, text_h, cfg.ranged_panel_text_color, 0);
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
        bg_load_failed_ = false;
        saved_under_ = nullptr;
        last_dlg_ = nullptr;
        last_mgr_ = nullptr;
        active_ = false;
        manual_battle_started_ = false;
        suppressed_for_result_ = false;
        last_x_ = -1;
        last_y_ = -1;
        last_w_ = 0;
        last_h_ = 0;
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
        if (!bg_ && !bg_load_failed_) {
            char modulePath[MAX_PATH];
            GetModuleFileNameA(g_hModule, modulePath, MAX_PATH);
            char* slash = strrchr(modulePath, '\\');
            if (slash) slash[1] = 0; else modulePath[0] = 0;
            char path[2048];
            _snprintf(path, sizeof(path) - 1, "%simg\\%s", modulePath, cfg.ranged_panel_image);
            path[sizeof(path) - 1] = 0;
            bg_ = CreateDDBackground(path);
            bg_load_failed_ = (bg_ == nullptr);
        }
    }

    void ReleaseBackground()
    {
        DestroyDDBackground(bg_);
        bg_ = nullptr;
        bg_load_failed_ = false;
    }

    void ReleaseUnderlay()
    {
        DestroyDDBackground(saved_under_);
        saved_under_ = nullptr;
    }

    void SaveUnderlay(int x, int y, int w, int h)
    {
        if (saved_under_ && last_x_ == x && last_y_ == y && last_w_ == w && last_h_ == h) {
            return; // 已经保存过原始像素，避免把面板自身保存进去
        }
        if (saved_under_) {
            RestoreUnderlay();
        }
        saved_under_ = CreateEmptyDDSurface(w, h);
        if (saved_under_) {
            if (!CopyBackBufferToSurface(saved_under_, x, y, w, h)) {
                ReleaseUnderlay();
            }
        }
    }

    void RestoreUnderlay()
    {
        if (saved_under_ && last_x_ >= 0 && last_y_ >= 0 && last_w_ > 0 && last_h_ > 0) {
            RestoreSurfaceToBackBuffer(saved_under_, last_x_, last_y_, last_w_, last_h_);
        }
        ReleaseUnderlay();
    }

    void SuppressForResult(const char* reason, _BattleMgr_* mgr, _Dlg_* active_dlg)
    {
        bool was_active = active_;
        (void)reason;
        (void)mgr;
        (void)active_dlg;
        active_ = false;
        suppressed_for_result_ = true;
        if (was_active) RestoreUnderlay();
        ReleaseBackground();
        ResetText();
    }

    void Recalculate(_BattleMgr_* mgr, const char* reason)
    {
        if (!mgr) return;

        unsigned int checksum = 0;
        if (!SafeComputeStackChecksum(mgr, &checksum)) return;

        int ranged[2] = { 0, 0 };
        int spell[2] = { 0, 0 };
        if (!SafeCalcRangedUnitsPower(mgr, 0, &ranged[0]) ||
            !SafeCalcRangedUnitsPower(mgr, 1, &ranged[1]) ||
            !SafeCalcHeroSpellPower(mgr, 0, &spell[0]) ||
            !SafeCalcHeroSpellPower(mgr, 1, &spell[1])) {
            return;
        }
        stack_checksum_ = checksum;
        int total[2] = { ranged[0] + spell[0], ranged[1] + spell[1] };

        _snprintf(text_[0], sizeof(text_[0]) - 1, "%d", ranged[0]);
        _snprintf(text_[1], sizeof(text_[1]) - 1, "%d", ranged[1]);
        _snprintf(text_[2], sizeof(text_[2]) - 1, "%d", spell[0]);
        _snprintf(text_[3], sizeof(text_[3]) - 1, "%d", spell[1]);
        _snprintf(text_[4], sizeof(text_[4]) - 1, "%d", total[0]);
        _snprintf(text_[5], sizeof(text_[5]) - 1, "%d", total[1]);
        for (int i = 0; i < 6; ++i) text_[i][sizeof(text_[i]) - 1] = 0;

        dirty_ = false;
        (void)reason;
    }

    DDBackgroundSurface* bg_;
    bool bg_load_failed_;
    DDBackgroundSurface* saved_under_;
    _Dlg_* last_dlg_;
    _BattleMgr_* last_mgr_;
    bool active_;
    bool manual_battle_started_;
    bool suppressed_for_result_;
    int last_x_;
    int last_y_;
    int last_w_;
    int last_h_;
    bool dirty_;
    unsigned int stack_checksum_;
    char text_[6][64];
};

static RangedOverlayPanel s_ranged_overlay_panel;

static _Fnt_* GetRangedPanelTextFont()
{
    const char* name = cfg.ranged_panel_text_font;
    if (!name || !name[0]) return H3Font::Load("smalfont.fnt");
    if (_stricmp(name, "tiny.fnt") == 0 || _stricmp(name, "tiny") == 0) return H3Font::Load("tiny.fnt");
    if (_stricmp(name, "smalfont.fnt") == 0 || _stricmp(name, "smalfont") == 0) return H3Font::Load("smalfont.fnt");
    if (_stricmp(name, "medfont.fnt") == 0 || _stricmp(name, "medfont") == 0) return H3Font::Load("medfont.fnt");
    if (_stricmp(name, "bigfont.fnt") == 0 || _stricmp(name, "bigfont") == 0) return H3Font::Load("bigfont.fnt");
    if (_stricmp(name, "calli10r.fnt") == 0 || _stricmp(name, "calli10r") == 0) return H3Font::Load("calli10r.fnt");
    return H3Font::Load("smalfont.fnt");
}

static void MarkRangedPanelDirty(const char* reason)
{
    s_ranged_overlay_panel.MarkDirty(reason);
}

static void BeginRangedPanelManualBattle(_BattleMgr_* mgr, const char* reason)
{
    s_ranged_overlay_panel.BeginManualBattle(mgr, reason);
}

static void UpdateRangedPanel(_BattleMgr_* mgr)
{
    s_ranged_overlay_panel.Draw(mgr);
}


int __stdcall Hook_BattleRedraw(HiHook* h, _BattleMgr_* hook_mgr, _bool8_ flip, _bool8_ set_battle_redraws, _bool8_ use_battle_redraws, int waiting_time, _bool8_ redraw_background, _bool8_ wait)
{
    _BattleMgr_* mgr = hook_mgr;
    THISCALL_7(void, h->GetDefaultFunc(), hook_mgr, flip, set_battle_redraws, use_battle_redraws, waiting_time, redraw_background, wait);
    UpdateRangedPanel(mgr);
    return 0;
}

// Combat_StartBattle @ 0x4781C0：进入战斗时先置脏，首次绘制计算。
int __stdcall Hook_CombatStartBattle(HiHook* h, _BattleMgr_* mgr)
{
    int result = THISCALL_1(int, h->GetDefaultFunc(), mgr);
    BeginRangedPanelManualBattle(mgr, "combat start");
    return result;
}

// MagicSystem_2 @ 0x464F10：战斗内施法流程。原函数返回后置脏，下一次绘制刷新。
int __stdcall Hook_CombatCastSpell(HiHook* h, _BattleMgr_* mgr, int x, int y)
{
    THISCALL_3(void, h->GetDefaultFunc(), mgr, x, y);
    MarkRangedPanelDirty("spell cast");
    return 0;
}

