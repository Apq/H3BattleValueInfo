// ========== 战场顶部远程输出面板 ==========
// 核心逻辑：RangedOverlayPanel 类及所有相关计算、绘制函数。

static const int RP_BASE_Y_OFFSET = 23;

// 前向声明（供类内部使用）
static _Fnt_* GetRangedPanelTextFont();
static void BuildRangedSideSummary(_BattleMgr_* mgr, int side, char* out, int out_size);

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
        // 当前"魔法输出能力"不考虑敌方目标，无目标估算参数固定用 1。
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

// 通过 H3 原生 _Pcx16_ 绘制背景图。
// 解码 8-bit PCX → 填充 _Pcx16_ buffer（RGB565 或 ARGB8888）。
// 然后用 DrawToPcx16 画到 screen_pcx16，由 HD_TC2 正常合成。

static _Pcx16_* CreatePcx16Background(const char* pcx_path)
{
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
        WriteLog("[Pcx16BG] incomplete PCX data decoded=%d expected=%d", rp, rawSize);
        free(raw); free(data); return nullptr;
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
    if (!palFound) { free(raw); return nullptr; }

    _Pcx16_* pcx = _Pcx16_::Create(w, h);
    if (!pcx || !pcx->buffer) {
        //WriteLog("[CreatePcx16BG] Create FAILED w=%d h=%d scanline=%d", w, h, pcx ? pcx->scanlineSize : 0);
        free(raw); return nullptr;
    }
    //WriteLog("[CreatePcx16BG] Create OK w=%d h=%d scanlineSize=%d mode=%s",
    //    w, h, pcx->scanlineSize, H3BitMode::Get() == 4 ? "32bpp" : "16bpp");

    if (H3BitMode::Get() == 4) {
        // 32-bit ARGB8888 模式
        for (int y = 0; y < h; ++y) {
            _dword_* row = (_dword_*)(pcx->buffer + y * pcx->scanlineSize);
            _byte_* s = raw + y * bpl;
            for (int x = 0; x < w; ++x) {
                unsigned char idx = s[x];
                row[x] = (0xFFu << 24) | ((unsigned)pal[idx * 3] << 16) | ((unsigned)pal[idx * 3 + 1] << 8) | pal[idx * 3 + 2];
            }
        }
        // 打印调试信息
        _dword_* firstRow = (_dword_*)(pcx->buffer);
        _dword_* midRow = (_dword_*)(pcx->buffer + (h/2) * pcx->scanlineSize);
        //WriteLog("[CreatePcx16BG] first pixels: [%08X, %08X, %08X, %08X]",
        //    firstRow[0], firstRow[1], firstRow[2], firstRow[3]);
        //WriteLog("[CreatePcx16BG] mid pixels: [%08X, %08X, %08X, %08X]",
        //    midRow[0], midRow[1], midRow[2], midRow[3]);
        bool allSame = true;
        _dword_ firstPix = firstRow[0];
        for (int i = 0; i < w && allSame; ++i) if (firstRow[i] != firstPix) allSame = false;
        //WriteLog("[CreatePcx16BG] first row allSame=%d palette sample: [%02X%02X%02X, %02X%02X%02X, %02X%02X%02X]",
        //    allSame ? 1 : 0,
        //    pal[0], pal[1], pal[2], pal[3], pal[4], pal[5], pal[6], pal[7], pal[8]);
        bool anyWhite = false;
        for (int yy = 0; yy < h && !anyWhite; ++yy) {
            _dword_* r = (_dword_*)(pcx->buffer + yy * pcx->scanlineSize);
            for (int xx = 0; xx < w && !anyWhite; ++xx) if ((r[xx] & 0x00FFFFFF) >= 0x00C0C0C0) anyWhite = true;
        }
        //WriteLog("[CreatePcx16BG] anyWhitePixel=%d", anyWhite ? 1 : 0);
    } else {
        // 16-bit RGB565 模式
        for (int y = 0; y < h; ++y) {
            _word_* row = (_word_*)(pcx->buffer + y * pcx->scanlineSize);
            _byte_* s = raw + y * bpl;
            for (int x = 0; x < w; ++x) {
                unsigned char idx = s[x];
                row[x] = RGB565_fromR8G8B8(pal[idx * 3], pal[idx * 3 + 1], pal[idx * 3 + 2]);
            }
        }
        // 打印前几个像素用于调试
        _word_* firstRow = (_word_*)(pcx->buffer);
        //WriteLog("[CreatePcx16BG] first pixels: [%04X, %04X, %04X, %04X]",
        //    firstRow[0], firstRow[1], firstRow[2], firstRow[3]);
        // 打印中间行
        _word_* midRow = (_word_*)(pcx->buffer + (h/2) * pcx->scanlineSize);
        //WriteLog("[CreatePcx16BG] mid pixels: [%04X, %04X, %04X, %04X]",
        //    midRow[0], midRow[1], midRow[2], midRow[3]);
        // 检查是否有不同颜色
        bool allSame = true;
        _word_ firstPix = firstRow[0];
        for (int i = 0; i < w && allSame; ++i) if (firstRow[i] != firstPix) allSame = false;
        //WriteLog("[CreatePcx16BG] first row allSame=%d palette sample: [%02X%02X%02X, %02X%02X%02X, %02X%02X%02X]",
        //    allSame ? 1 : 0,
        //    pal[0], pal[1], pal[2], pal[3], pal[4], pal[5], pal[6], pal[7], pal[8]);
    }

    free(raw);
    return pcx;
}

static void SafeDrawTextToScreen(_Fnt_* font, _Pcx16_* target, const char* text, int x, int y, int w, int h, int color, int align)
{
    if (!font || !target || !text) return;
    __try {
        font->TextDraw(target, text, x, y, w, h, (eTextColor)color, (eTextAlignment)align);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteLog("[RangedOverlayPanel] SEH exception during TextDraw text='%s' rect=(%d,%d,%d,%d)", text, x, y, w, h);
    }
}

static const _word_ TEXT_MASK_SENTINEL_565 = 0xF81F;
static const _dword_ TEXT_MASK_SENTINEL_8888 = 0xFFFF00FF;

static bool EnsureTextMask(_Pcx16_** mask, int w, int h)
{
    if (!mask || w <= 0 || h <= 0) {
        return false;
    }
    if (*mask && (*mask)->width == w && (*mask)->height == h && (*mask)->buffer) return true;
    if (*mask) {
        (*mask)->Destroy();
        *mask = nullptr;
    }
    *mask = _Pcx16_::Create(w, h);
    return *mask && (*mask)->buffer;
}

static void FillPcx16Mask(_Pcx16_* pcx)
{
    if (!pcx || !pcx->buffer) return;
    // 用 sentinel 颜色填充，这样 DrawPcx16ToBackBuffer 可以跳过透明区域
    // 只有文字会被绘制到战场背景上
    if (H3BitMode::Get() == 4) {
        for (int y = 0; y < pcx->height; ++y) {
            _dword_* row = (_dword_*)(pcx->buffer + y * pcx->scanlineSize);
            for (int x = 0; x < pcx->width; ++x) row[x] = TEXT_MASK_SENTINEL_8888;
        }
    } else {
        for (int y = 0; y < pcx->height; ++y) {
            _word_* row = (_word_*)(pcx->buffer + y * pcx->scanlineSize);
            for (int x = 0; x < pcx->width; ++x) row[x] = TEXT_MASK_SENTINEL_565;
        }
    }
}

// ============================================================================
// GDI 批量绘制：使用 CreateDIBSection 批量传输，性能比逐像素 SetPixel 快几十倍
// ============================================================================

static bool DrawPcx16WithGDI(_Pcx16_* src, int screen_x, int screen_y)
{
    if (!src || !src->buffer) return false;
    HWND hwnd = H3Hwnd::Get();
    if (!hwnd) return false;

    HDC hdc = GetDC(hwnd);
    if (!hdc) return false;

    bool ok = false;
    __try {
        int w = src->width;
        int h = src->height;

        // 创建兼容 DC 和 DIB 位图
        HDC memDC = CreateCompatibleDC(hdc);
        if (!memDC) { ReleaseDC(hwnd, hdc); return false; }

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;  // 自上而下
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP hBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hBmp || !bits) {
            DeleteDC(memDC);
            ReleaseDC(hwnd, hdc);
            return false;
        }

        HGDIOBJ oldBmp = SelectObject(memDC, hBmp);

        // 转换 16位/32位像素到 24位 BGR（DIB stride = w * 3，无 padding）
        int bpp = H3BitMode::Get() == 4 ? 32 : 16;
        int src_sl = src->scanlineSize;
        if (bpp == 16) {
            for (int y = 0; y < h; ++y) {
                _word_* src_row = (_word_*)((_byte_*)src->buffer + y * src_sl);
                _byte_* dst_row = (_byte_*)bits + y * (w * 3);
                for (int x = 0; x < w; ++x) {
                    _word_ c565 = src_row[x];
                    dst_row[x * 3 + 0] = ((c565 & 0x1F) << 3);
                    dst_row[x * 3 + 1] = ((c565 >> 5) & 0x3F) << 2;
                    dst_row[x * 3 + 2] = ((c565 >> 11) & 0x1F) << 3;
                }
            }
        } else {
            for (int y = 0; y < h; ++y) {
                _dword_* src_row = (_dword_*)((_byte_*)src->buffer + y * src_sl);
                _byte_* dst_row = (_byte_*)bits + y * (w * 3);
                for (int x = 0; x < w; ++x) {
                    _dword_ c = src_row[x];
                    dst_row[x * 3 + 0] = c & 0xFF;
                    dst_row[x * 3 + 1] = (c >> 8) & 0xFF;
                    dst_row[x * 3 + 2] = (c >> 16) & 0xFF;
                }
            }
        }

        // 批量 BitBlt 到屏幕
        ok = BitBlt(hdc, screen_x, screen_y, w, h, memDC, 0, 0, SRCCOPY) != 0;

        SelectObject(memDC, oldBmp);
        DeleteObject(hBmp);
        DeleteDC(memDC);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    ReleaseDC(hwnd, hdc);
    return ok;
}

static bool DrawPcx16TextWithGDI(_Pcx16_* src, int screen_x, int screen_y)
{
    if (!src || !src->buffer) return false;
    HWND hwnd = H3Hwnd::Get();
    if (!hwnd) return false;

    HDC hdc = GetDC(hwnd);
    if (!hdc) return false;

    bool ok = false;
    __try {
        int w = src->width;
        int h = src->height;

        // 创建兼容 DC 和 DIB 位图
        HDC memDC = CreateCompatibleDC(hdc);
        if (!memDC) { ReleaseDC(hwnd, hdc); return false; }

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP hBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hBmp || !bits) {
            DeleteDC(memDC);
            ReleaseDC(hwnd, hdc);
            return false;
        }

        HGDIOBJ oldBmp = SelectObject(memDC, hBmp);

        // 填充白色背景，跳过透明像素
        memset(bits, 0xFF, h * w * 3);

        int bpp = H3BitMode::Get() == 4 ? 32 : 16;
        int src_sl = src->scanlineSize;
        if (bpp == 16) {
            for (int y = 0; y < h; ++y) {
                _word_* src_row = (_word_*)((_byte_*)src->buffer + y * src_sl);
                _byte_* dst_row = (_byte_*)bits + y * (w * 3);
                for (int x = 0; x < w; ++x) {
                    _word_ c565 = src_row[x];
                    if (c565 != TEXT_MASK_SENTINEL_565) {
                        dst_row[x * 3 + 0] = ((c565 & 0x1F) << 3);
                        dst_row[x * 3 + 1] = ((c565 >> 5) & 0x3F) << 2;
                        dst_row[x * 3 + 2] = ((c565 >> 11) & 0x1F) << 3;
                    }
                }
            }
        } else {
            for (int y = 0; y < h; ++y) {
                _dword_* src_row = (_dword_*)((_byte_*)src->buffer + y * src_sl);
                _byte_* dst_row = (_byte_*)bits + y * (w * 3);
                for (int x = 0; x < w; ++x) {
                    _dword_ c = src_row[x];
                    if (c != TEXT_MASK_SENTINEL_8888) {
                        dst_row[x * 3 + 0] = c & 0xFF;
                        dst_row[x * 3 + 1] = (c >> 8) & 0xFF;
                        dst_row[x * 3 + 2] = (c >> 16) & 0xFF;
                    }
                }
            }
        }

        // 批量 BitBlt 到屏幕
        ok = BitBlt(hdc, screen_x, screen_y, w, h, memDC, 0, 0, SRCCOPY) != 0;

        SelectObject(memDC, oldBmp);
        DeleteObject(hBmp);
        DeleteDC(memDC);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    ReleaseDC(hwnd, hdc);
    return ok;
}

static int GetBackBufferBpp(LPDIRECTDRAWSURFACE surface)
{
    if (!surface) return H3BitMode::Get() == 4 ? 32 : 16;
    DDPIXELFORMAT pf;
    memset(&pf, 0, sizeof(pf));
    pf.dwSize = sizeof(pf);
    if (SUCCEEDED(surface->GetPixelFormat(&pf))) {
        if (pf.dwRGBBitCount == 16 || pf.dwRGBBitCount == 32)
            return (int)pf.dwRGBBitCount;
    }
    return H3BitMode::Get() == 4 ? 32 : 16;
}

static _word_ RGB888To565(int r, int g, int b)
{
    return (_word_)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

static _dword_ RGB565To8888(_word_ c)
{
    int r = ((c >> 11) & 0x1F) << 3;
    int g = ((c >> 5) & 0x3F) << 2;
    int b = (c & 0x1F) << 3;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static _word_ RGB8888To565(_dword_ c)
{
    return RGB888To565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

static bool DrawPcx16ToBackBuffer(_Pcx16_* src, int dst_x, int dst_y, bool skip_sentinel)
{
    if (!src || !src->buffer || !o_DDSurfaceBackBuffer) return false;
    static int s_call_idx = 0;
    int call_idx = s_call_idx++;
    //WriteLog("[DrawBB] #%d src=%p(%dx%d) dst=(%d,%d) skip=%d",
    //    call_idx, (void*)src, src->width, src->height, dst_x, dst_y, skip_sentinel ? 1 : 0);

    __try {
        DDSURFACEDESC desc;
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        HRESULT hr = o_DDSurfaceBackBuffer->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, nullptr);
        if (FAILED(hr) || !desc.lpSurface) {
            return false;
        }

        int bpp = GetBackBufferBpp(o_DDSurfaceBackBuffer);
        int dst_w = (int)desc.dwWidth;
        int dst_h = (int)desc.dwHeight;
        int dst_lpitch = (int)desc.lPitch;
        if (dst_w <= 0) dst_w = o_WndMgr && o_WndMgr->screen_pcx16 ? o_WndMgr->screen_pcx16->width : 800;
        if (dst_h <= 0) dst_h = o_WndMgr && o_WndMgr->screen_pcx16 ? o_WndMgr->screen_pcx16->height : 600;

        int src_x0 = 0;
        int src_y0 = 0;
        int copy_w = src->width;
        int copy_h = src->height;
        if (dst_x < 0) { src_x0 = -dst_x; copy_w += dst_x; dst_x = 0; }
        if (dst_y < 0) { src_y0 = -dst_y; copy_h += dst_y; dst_y = 0; }
        if (dst_x + copy_w > dst_w) copy_w = dst_w - dst_x;
        if (dst_y + copy_h > dst_h) copy_h = dst_h - dst_y;

        if (copy_w > 0 && copy_h > 0) {
            bool src32 = H3BitMode::Get() == 4;
            for (int y = 0; y < copy_h; ++y) {
                _byte_* src_row = src->buffer + (src_y0 + y) * src->scanlineSize;
                _byte_* dst_row = (_byte_*)desc.lpSurface + (dst_y + y) * desc.lPitch;
                if (bpp == 32) {
                    // backbuffer 是 XRGB8888（4字节/pixel，X=0xFF 无意义，仅作对齐）
                    // src 是 ARGB8888。必须按字节写，只复制 RGB，alpha 丢弃（保持 backbuffer X=0xFF）
                    _byte_* d = (_byte_*)desc.lpSurface + (dst_y + y) * dst_lpitch + dst_x * 4;
                    if (src32) {
                        for (int x = 0; x < copy_w; ++x) {
                            // backbuffer 是 BGRX 格式（蓝色在低位字节）
                            // ARGB8888: tmp = A<<24 | R<<16 | G<<8 | B
                            // 需要提取 R/G/B 并按 BGR 顺序写入
                            _dword_ tmp = *(_dword_*)((_byte_*)src_row + src_x0 * 4 + x * 4);
                            if (!skip_sentinel || tmp != TEXT_MASK_SENTINEL_8888) {
                                d[x * 4 + 0] = (_byte_)tmp;                  // B
                                d[x * 4 + 1] = (_byte_)(tmp >> 8);           // G
                                d[x * 4 + 2] = (_byte_)(tmp >> 16);          // R
                                // X 通道保持原值
                            }
                        }
                    } else {
                        // 16bpp src → BGRX backbuffer
                        _word_* s = (_word_*)src_row + src_x0;
                        for (int x = 0; x < copy_w; ++x) {
                            _word_ c16 = s[x];
                            if (!skip_sentinel || c16 != TEXT_MASK_SENTINEL_565) {
                                d[x * 4 + 0] = ((c16 & 0x1F) << 3);             // B
                                d[x * 4 + 1] = ((c16 >> 5) & 0x3F) << 2;        // G
                                d[x * 4 + 2] = ((c16 >> 11) & 0x1F) << 3;       // R
                                // X 通道保持 0xFF
                            }
                        }
                    }
                } else {
                    _word_* d = (_word_*)((_byte_*)desc.lpSurface + (dst_y + y) * dst_lpitch) + dst_x;
                    if (src32) {
                        _dword_* s = (_dword_*)src_row + src_x0;
                        for (int x = 0; x < copy_w; ++x) {
                            _dword_ c = s[x];
                            if (!skip_sentinel || c != TEXT_MASK_SENTINEL_8888) d[x] = RGB8888To565(c);
                        }
                    } else {
                        _word_* s = (_word_*)src_row + src_x0;
                        for (int x = 0; x < copy_w; ++x) {
                            _word_ c = s[x];
                            if (!skip_sentinel || c != TEXT_MASK_SENTINEL_565) d[x] = c;
                        }
                    }
                }
            }
        }

        bool ok = copy_w > 0 && copy_h > 0;
        //WriteLog("[DrawBB] #%d LockOK bpp=%d clip_ok copy_w=%d copy_h=%d ret=%d",
        //    call_idx, bpp, copy_w, copy_h, ok);
        o_DDSurfaceBackBuffer->Unlock(nullptr);
        return ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        //WriteLog("[DrawBB] #%d EXCEPTION", call_idx);
        return false;
    }
}

static void AppendText(char* out, int out_size, const char* fmt, ...)
{
    if (!out || out_size <= 0) return;
    int len = (int)strlen(out);
    if (len < 0 || len >= out_size - 1) return;

    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(out + len, out_size - len - 1, fmt, ap);
    va_end(ap);
    out[out_size - 1] = 0;
}

static void BuildRangedSideSummary(_BattleMgr_* mgr, int side, char* out, int out_size)
{
    if (!out || out_size <= 0) return;
    out[0] = 0;
    if (!mgr || side < 0 || side > 1) {
        _snprintf(out, out_size - 1, "invalid");
        out[out_size - 1] = 0;
        return;
    }

    __try {
        int live = 0;
        int shooters = 0;
        int can_shooters = 0;
        int total_count = 0;
        char detail[640];
        detail[0] = 0;

        int enemy_side = 1 - side;
        for (int slot = 0; slot < 21; ++slot) {
            _BattleStack_* s = &mgr->stack[side][slot];
            if (s->count_current <= 0) continue;

            ++live;
            total_count += s->count_current;

            int can_target_count = 0;
            bool ranged_unit = s->creature.shots > 0;
            if (ranged_unit && !s->blinded && !s->paralyzed && s->forgetfulness_level <= 0) {
                ++shooters;
                for (int target_slot = 0; target_slot < 21; ++target_slot) {
                    _BattleStack_* target = &mgr->stack[enemy_side][target_slot];
                    if (target->count_current <= 0) continue;
                    if (SafeCanShoot(s, target)) ++can_target_count;
                }
                if (can_target_count > 0) ++can_shooters;
            }

            if (ranged_unit && (int)strlen(detail) < 520) {
                AppendText(detail, sizeof(detail),
                    "#%d:id=%d cnt=%d shot=%d can=%d done=%d blind=%d para=%d fgt=%d dmg=%d-%d;",
                    slot, s->creature_id, s->count_current, s->creature.shots, can_target_count,
                    s->is_done, s->blinded, s->paralyzed, s->forgetfulness_level,
                    s->creature.damage_min, s->creature.damage_max);
            }
        }

        _snprintf(out, out_size - 1, "live=%d units=%d shooters=%d canShooters=%d %s",
            live, total_count, shooters, can_shooters, detail);
        out[out_size - 1] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        _snprintf(out, out_size - 1, "seh");
        out[out_size - 1] = 0;
    }
}

// 前向声明（供类内部使用）
static LPDIRECTDRAWSURFACE CreateEmptyDDSurface(int w, int h);
static bool CreateDDBackground(const char* pcx_path, LPDIRECTDRAWSURFACE surface, int* out_w, int* out_h);
static void ClearScreenRegion(_Pcx16_* screen, int x, int y, int w, int h);
static void SafeDrawTextToScreenPcx16(_Fnt_* font, _Pcx16_* screen, const char* text, int x, int y, int w, int h, int color, int align);
static bool BltDDSurfaceToBackBuffer(LPDIRECTDRAWSURFACE src_surface, int dst_x, int dst_y, int w, int h);
static bool CopyBackBufferToSurface(LPDIRECTDRAWSURFACE dst_surface, int src_x, int src_y, int w, int h);
static bool RestoreSurfaceToBackBuffer(LPDIRECTDRAWSURFACE src_surface, int dst_x, int dst_y, int w, int h);

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

    void SetBattleDlg(_Dlg_* dlg)
    {
        battle_dlg_ = dlg;
    }

    bool HasBattleDlg() const { return battle_dlg_ != nullptr; }

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
        battle_dlg_ = nullptr; // 战斗结束后清空
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
        _Dlg_* dlg = (mgr && mgr->dlg && (uint32_t)mgr->dlg > 0x10000) ? mgr->dlg : nullptr;
        // mgr->dlg 在施法初始化阶段为 NULL，改用 BUILD hook 传入的 battle_dlg_（更早可用）
        if (!dlg && battle_dlg_) dlg = battle_dlg_;
        // 最后备用：从活动对话框获取（可能包含战场对话框）
        if (!dlg) {
            _Dlg_* active = GetActiveDlg();
            if (active && (uint32_t)active > 0x10000) dlg = active;
        }
        _Dlg_* active_dlg = GetActiveDlg();
        _Pcx16_* screen = o_WndMgr ? o_WndMgr->screen_pcx16 : nullptr;
        if (!screen || !dlg) return;

        if (last_mgr_ != mgr) {
            last_mgr_ = mgr;
            active_ = false;
            manual_battle_started_ = false;
            suppressed_for_result_ = false;
            stack_checksum_ = 0;
            ResetText();
            MarkDirty("new battle manager");
        }

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
        if (suppressed_for_result_ && manual_battle_started_) {
            active_ = true;
            suppressed_for_result_ = false;
            stack_checksum_ = 0;
            ResetText();
            MarkDirty("re-enter manual battle");
        }

        if (!active_) return;
        if (suppressed_for_result_) return;

        // 面板尺寸
        int panel_w = cfg.ranged_panel_width;
        int panel_h = cfg.ranged_panel_height;

        // 坐标计算：面板居中于战场对话框顶部，背景紧贴边框，文字在框内
        int x, y;
        if (dlg && dlg->width > 0 && dlg->height > 0) {
            x = dlg->x + (dlg->width - panel_w) / 2;
            y = dlg->y - panel_h;  // 面板底部对齐到对话框顶部
        } else {
            int battle_x = (screen->width - 800) / 2;
            int battle_y = (screen->height - 600) / 2;
            if (battle_x < 0) battle_x = 0;
            if (battle_y < 0) battle_y = 0;
            x = battle_x + (800 - panel_w) / 2;
            y = battle_y - panel_h;
        }
        // 确保面板在屏幕可见范围内
        if (x < 0) x = 0;
        if (y + panel_h < 0) return;  // 面板完全在屏幕上方，不画
        if (y < 0) y = 0;  // 面板顶部超出屏幕，从屏幕顶开始画

        //WriteLog("[DrawImpl] panel_w=%d panel_h=%d dlg=%p x=%d y=%d bg_=%p",
        //    panel_w, panel_h, (void*)dlg, x, y, (void*)bg_);

        EnsureBackground();
        Recalculate(mgr);
        if (!bg_) return;

        // 1. 把背景图复制到临时合成图 bg_composite_
        if (!bg_composite_ || bg_composite_->width != panel_w || bg_composite_->height != panel_h) {
            if (bg_composite_) bg_composite_->Destroy();
            bg_composite_ = _Pcx16_::Create(panel_w, panel_h);
        }
        if (!bg_composite_ || !bg_composite_->buffer) return;

        // 复制背景图到合成图
        for (int yy = 0; yy < panel_h; ++yy) {
            _dword_* dst_row = (_dword_*)(bg_composite_->buffer + yy * bg_composite_->scanlineSize);
            _dword_* src_row = (_dword_*)(bg_->buffer + yy * bg_->scanlineSize);
            for (int xx = 0; xx < panel_w; ++xx) {
                dst_row[xx] = src_row[xx];
            }
        }

        // 2. 文字画到合成图
        // 面板宽度298，中线在149。左列右边界贴中线，右列左边界贴中线
        int left_col_x = 8;      // 左列起始 x
        int left_col_w = 141;    // 左列宽度（右边界=149=中线）
        int right_col_x = 154;    // 右列起始 x（左边界贴着中线）
        int right_col_w = 144;    // 右列宽度（到面板右边界298）
        int row_h = cfg.row_y[1] - cfg.row_y[0];  // 行高 = 第2行Y - 第1行Y

        _Fnt_* font = GetRangedPanelTextFont();
        if (font) {
            char tmp[64];
            // 左列数字：右对齐（数字末尾对齐到区域右边界，即中线）
            _snprintf(tmp, sizeof(tmp) - 1, "%d", ranged_value_[0]);
            font->TextDraw(bg_composite_, tmp, left_col_x, cfg.row_y[0], left_col_w, row_h, NH3Dlg::eTextColor::WHITE, NH3Dlg::eTextAlignment::MIDDLE_RIGHT);
            _snprintf(tmp, sizeof(tmp) - 1, "%d", spell_value_[0]);
            font->TextDraw(bg_composite_, tmp, left_col_x, cfg.row_y[1], left_col_w, row_h, NH3Dlg::eTextColor::WHITE, NH3Dlg::eTextAlignment::MIDDLE_RIGHT);
            _snprintf(tmp, sizeof(tmp) - 1, "%d", total_value_[0]);
            font->TextDraw(bg_composite_, tmp, left_col_x, cfg.row_y[2], left_col_w, row_h, NH3Dlg::eTextColor::WHITE, NH3Dlg::eTextAlignment::MIDDLE_RIGHT);

            // 右列数字：左对齐（数字从区域左侧开始）
            _snprintf(tmp, sizeof(tmp) - 1, "%d", ranged_value_[1]);
            font->TextDraw(bg_composite_, tmp, right_col_x, cfg.row_y[0], right_col_w, row_h, NH3Dlg::eTextColor::WHITE, NH3Dlg::eTextAlignment::MIDDLE_LEFT);
            _snprintf(tmp, sizeof(tmp) - 1, "%d", spell_value_[1]);
            font->TextDraw(bg_composite_, tmp, right_col_x, cfg.row_y[1], right_col_w, row_h, NH3Dlg::eTextColor::WHITE, NH3Dlg::eTextAlignment::MIDDLE_LEFT);
            _snprintf(tmp, sizeof(tmp) - 1, "%d", total_value_[1]);
            font->TextDraw(bg_composite_, tmp, right_col_x, cfg.row_y[2], right_col_w, row_h, NH3Dlg::eTextColor::WHITE, NH3Dlg::eTextAlignment::MIDDLE_LEFT);
        }

        // 3. 一次 blt 合成图到 backbuffer
        DrawPcx16ToBackBuffer(bg_composite_, x, y, FALSE);

        // 4. 强制刷新面板区域
        if (!redraw_in_progress_) {
            redraw_in_progress_ = true;
            o_WndMgr->H3Redraw(x, y, panel_w, panel_h);
            redraw_in_progress_ = false;
        }
    }

    bool Active() const { return active_; }

private:
    void ResetRuntimeState()
    {
        bg_ = nullptr;
        bg_composite_ = nullptr;
        text_mask_ = nullptr;
        bg_load_failed_ = false;
        last_dlg_ = nullptr;
        last_mgr_ = nullptr;
        battle_dlg_ = nullptr;
        active_ = false;
        manual_battle_started_ = false;
        suppressed_for_result_ = false;
        redraw_in_progress_ = false;
        dirty_ = true;
        stack_checksum_ = 0;
        ranged_value_[0] = 0;
        ranged_value_[1] = 0;
        spell_value_[0] = 0;
        spell_value_[1] = 0;
        total_value_[0] = 0;
        total_value_[1] = 0;
        ResetText();
    }

    void ResetText()
    {
        memset(text_, 0, sizeof(text_));
        dirty_ = true;
    }

    void EnsureBackground()
    {
        if (bg_ || bg_load_failed_) return;

        char modulePath[MAX_PATH];
        GetModuleFileNameA(g_hModule, modulePath, MAX_PATH);
        char* slash = strrchr(modulePath, '\\');
        if (slash) slash[1] = 0; else modulePath[0] = 0;
        char path[2048];
        _snprintf(path, sizeof(path) - 1, "%simg\\%s", modulePath, cfg.ranged_panel_image);
        path[sizeof(path) - 1] = 0;

        bg_ = CreatePcx16Background(path);
        bg_load_failed_ = (bg_ == nullptr);
    }

    void ReleaseBackground()
    {
        if (bg_) { bg_->Destroy(); bg_ = nullptr; }
        if (bg_composite_) { bg_composite_->Destroy(); bg_composite_ = nullptr; }
        if (text_mask_) { text_mask_->Destroy(); text_mask_ = nullptr; }
        bg_load_failed_ = false;
    }

    void SuppressForResult(const char* reason, _BattleMgr_* mgr, _Dlg_* active_dlg)
    {
        (void)reason;
        (void)mgr;
        (void)active_dlg;
        active_ = false;
        suppressed_for_result_ = true;
        ReleaseBackground();
        ResetText();
    }

    void Recalculate(_BattleMgr_* mgr)
    {
        if (!mgr) return;

        unsigned int checksum = 0;
        if (!SafeComputeStackChecksum(mgr, &checksum)) return;

        // 如果 checksum 没变且不是 dirty，跳过重算
        if (checksum == stack_checksum_ && !dirty_) {
            return;
        }

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

        ranged_value_[0] = ranged[0];
        ranged_value_[1] = ranged[1];
        spell_value_[0] = spell[0];
        spell_value_[1] = spell[1];
        total_value_[0] = total[0];
        total_value_[1] = total[1];

        _snprintf(text_[0], sizeof(text_[0]) - 1, "%d", ranged[0]);
        _snprintf(text_[1], sizeof(text_[1]) - 1, "%d", ranged[1]);
        _snprintf(text_[2], sizeof(text_[2]) - 1, "%d", spell[0]);
        _snprintf(text_[3], sizeof(text_[3]) - 1, "%d", spell[1]);
        _snprintf(text_[4], sizeof(text_[4]) - 1, "%d", total[0]);
        _snprintf(text_[5], sizeof(text_[5]) - 1, "%d", total[1]);
        for (int i = 0; i < 6; ++i) text_[i][sizeof(text_[i]) - 1] = 0;

        dirty_ = false;
    }

    void MaybeLogDraw(_BattleMgr_* mgr)
    {
        (void)mgr;
    }

    _Pcx16_* bg_;
    _Pcx16_* bg_composite_;  // 临时合成图：背景+文字，每帧重新合成
    _Pcx16_* text_mask_;
    bool bg_load_failed_;
    _Dlg_* last_dlg_;
    _BattleMgr_* last_mgr_;
    _Dlg_* battle_dlg_;  // 从 BUILD hook 传入的战斗对话框指针（比 mgr->dlg 更早可用）
    bool active_;
    bool manual_battle_started_;
    bool suppressed_for_result_;
    bool dirty_;
    bool redraw_in_progress_;  // 防止 H3Redraw 递归崩溃

    unsigned int stack_checksum_;
    char text_[6][64];
    int ranged_value_[2];
    int spell_value_[2];
    int total_value_[2];

    // 双缓冲架构：screen_pcx16 直接绘制（HD 版 OpenGL 不支持 DD Blt）
    // bg_ 是 _Pcx16_* 对象，直接用 DrawToPcx16 画到 screen

public:
    // 诊断用 getter
    int GetRangedValue(int side) const { return (side >= 0 && side <= 1) ? ranged_value_[side] : 0; }
    int GetSpellValue(int side) const { return (side >= 0 && side <= 1) ? spell_value_[side] : 0; }
    int GetTotalValue(int side) const { return (side >= 0 && side <= 1) ? total_value_[side] : 0; }
    bool IsActive() const { return active_; }
    bool IsDirty() const { return dirty_; }
    bool IsManualBattleStarted() const { return manual_battle_started_; }
};

// 静态实例和 getter 函数
static RangedOverlayPanel s_ranged_overlay_panel;

static RangedOverlayPanel* GetRangedOverlayPanelPtr()
{
    return &s_ranged_overlay_panel;
}

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

// ============================================================================
// 文档方案：DD 离屏 Surface 架构
// ============================================================================

// 创建空离屏 DD surface（与 backbuffer 同像素格式）
static LPDIRECTDRAWSURFACE CreateEmptyDDSurface(int w, int h)
{
    if (!o_DD || w <= 0 || h <= 0) return nullptr;
    DDSURFACEDESC desc = {};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
    desc.dwWidth = w;
    desc.dwHeight = h;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    LPDIRECTDRAWSURFACE surface = nullptr;
    HRESULT hr = o_DD->CreateSurface(&desc, &surface, nullptr);
    if (FAILED(hr) || !surface) return nullptr;
    return surface;
}

// 解码 PCX 并写入 DD surface（使用 backbuffer 的像素格式）
static bool CreateDDBackground(const char* pcx_path, LPDIRECTDRAWSURFACE surface, int* out_w, int* out_h)
{
    if (!surface || !pcx_path) return false;
    unsigned char* data = nullptr;
    int size = 0;
    if (!ReadWholeFile(pcx_path, &data, &size)) return false;
    if (size < 128) { free(data); return false; }

    int bpp = data[3];
    int xmin = *(short*)(data + 4);
    int ymin = *(short*)(data + 6);
    int xmax = *(short*)(data + 8);
    int ymax = *(short*)(data + 10);
    int nplanes = data[65];
    int bpl = *(short*)(data + 66);
    int w = xmax - xmin + 1;
    int h = ymax - ymin + 1;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) { free(data); return false; }
    if (!(nplanes == 1 && bpp == 8)) { free(data); return false; }

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
    if (rp < rawSize) { free(raw); free(data); return false; }

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
                palFound = true; break;
            }
        }
    }
    free(data);
    if (!palFound) { free(raw); return false; }

    // 写入 DD surface
    DDSURFACEDESC desc = {};
    desc.dwSize = sizeof(desc);
    HRESULT hr = surface->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, nullptr);
    if (FAILED(hr) || !desc.lpSurface) { free(raw); return false; }

    int surf_bpp = GetBackBufferBpp(o_DDSurfaceBackBuffer);
    if (surf_bpp == 32) {
        // DD surface 是 BGRA 格式
        for (int y = 0; y < h; ++y) {
            _byte_* dst = (_byte_*)desc.lpSurface + y * desc.lPitch;
            _byte_* src = raw + y * bpl;
            for (int x = 0; x < w; ++x) {
                unsigned char idx = src[x];
                dst[x * 4 + 0] = pal[idx * 3 + 2];     // B
                dst[x * 4 + 1] = pal[idx * 3 + 1];     // G
                dst[x * 4 + 2] = pal[idx * 3 + 0];     // R
                dst[x * 4 + 3] = 0;                     // A=0 (opaque)
            }
        }
    } else {
        // DD surface 是 RGB565
        for (int y = 0; y < h; ++y) {
            _word_* dst = (_word_*)((_byte_*)desc.lpSurface + y * desc.lPitch);
            _byte_* src = raw + y * bpl;
            for (int x = 0; x < w; ++x) {
                unsigned char idx = src[x];
                unsigned char r = pal[idx * 3];
                unsigned char g = pal[idx * 3 + 1];
                unsigned char b = pal[idx * 3 + 2];
                dst[x] = (_word_)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    }
    surface->Unlock(nullptr);
    free(raw);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return true;
}

// 清除 screen_pcx16 面板区域（旧文字残留）
static void ClearScreenRegion(_Pcx16_* screen, int x, int y, int w, int h)
{
    if (!screen || !screen->buffer || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > screen->width) w = screen->width - x;
    if (y + h > screen->height) h = screen->height - y;
    if (w <= 0 || h <= 0) return;

    // screen_pcx16 假设 16bpp RGB565
    for (int row = y; row < y + h; ++row) {
        _word_* p = (_word_*)(screen->buffer + row * screen->scanlineSize) + x;
        for (int col = 0; col < w; ++col) p[col] = 0;
    }
}

// 文字画到 screen_pcx16（RGB565）
static void SafeDrawTextToScreenPcx16(_Fnt_* font, _Pcx16_* screen, const char* text, int x, int y, int w, int h, int color, int align)
{
    if (!font || !screen || !screen->buffer || !text) return;
    __try {
        font->TextDraw(screen, text, x, y, w, h, (eTextColor)color, (eTextAlignment)align);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Blt DD surface 到 backbuffer
static bool BltDDSurfaceToBackBuffer(LPDIRECTDRAWSURFACE src_surface, int dst_x, int dst_y, int w, int h)
{
    if (!src_surface || !o_DDSurfaceBackBuffer || w <= 0 || h <= 0) return false;
    RECT src_rect = {0, 0, w, h};
    RECT dst_rect = {dst_x, dst_y, dst_x + w, dst_y + h};
    HRESULT hr = o_DDSurfaceBackBuffer->Blt(&dst_rect, src_surface, &src_rect, DDBLT_WAIT, nullptr);
    return SUCCEEDED(hr);
}

// 从 backbuffer 保存 underlay
static bool CopyBackBufferToSurface(LPDIRECTDRAWSURFACE dst_surface, int src_x, int src_y, int w, int h)
{
    if (!dst_surface || !o_DDSurfaceBackBuffer || w <= 0 || h <= 0) return false;
    RECT src_rect = {src_x, src_y, src_x + w, src_y + h};
    HRESULT hr = dst_surface->Blt(nullptr, o_DDSurfaceBackBuffer, &src_rect, DDBLT_WAIT, nullptr);
    return SUCCEEDED(hr);
}

// 恢复 underlay 到 backbuffer
static bool RestoreSurfaceToBackBuffer(LPDIRECTDRAWSURFACE src_surface, int dst_x, int dst_y, int w, int h)
{
    if (!src_surface || !o_DDSurfaceBackBuffer || w <= 0 || h <= 0) return false;
    RECT src_rect = {0, 0, w, h};
    RECT dst_rect = {dst_x, dst_y, dst_x + w, dst_y + h};
    HRESULT hr = o_DDSurfaceBackBuffer->Blt(&dst_rect, src_surface, &src_rect, DDBLT_WAIT, nullptr);
    return SUCCEEDED(hr);
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
    // 如果 battle_dlg_ 还未设置，尝试从 mgr 获取（某些战斗路径 mgr->dlg 在 BUILD 时为 NULL）
    if (!s_ranged_overlay_panel.HasBattleDlg() && mgr && mgr->dlg && (uint32_t)mgr->dlg > 0x10000) {
        s_ranged_overlay_panel.SetBattleDlg(mgr->dlg);
    }
    s_ranged_overlay_panel.Draw(mgr);
}

// Hook_AfterBlt @ 0x600430：backbuffer Blt 完成后的点，在这个位置画面板不会被覆盖。
int __stdcall Hook_AfterBlt(LoHook* h, HookContext* c)
{
    (void)h;
    (void)c;
    if (s_ranged_overlay_panel.Active() && o_BattleMgr) {
        UpdateRangedPanel(o_BattleMgr);
    }
    return EXEC_DEFAULT;
}

// Hook_CycleCombatScreen @ 0x495C50：战斗动画循环，每帧调用 Refresh()。
// 这是比 0x493FC0 更内层的渲染点，CombatAnimation 插件也使用这个点。
// 原函数调用后，backbuffer 已包含完整战场画面，在此处画面板不会被覆盖。
int __stdcall Hook_CycleCombatScreen(HiHook* h, _BattleMgr_* mgr)
{
    // 先调用原函数（渲染本帧）
    THISCALL_1(int, h->GetDefaultFunc(), mgr);
    // 在渲染完成后画面板
    if (s_ranged_overlay_panel.Active() && o_BattleMgr) {
        UpdateRangedPanel(o_BattleMgr);
    }
    return 0;
}

// Combat_StartBattle @ 0x4781C0：进入战斗时先置脏，首次绘制计算。
int __stdcall Hook_CombatStartBattle(HiHook* h, _BattleMgr_* mgr)
{
    int result = THISCALL_1(int, h->GetDefaultFunc(), mgr);
    BeginRangedPanelManualBattle(mgr, "combat start");
    return result;
}

// Hook_CombatCastSpell：战斗内施法后标记面板脏，等待下一帧重算。
int __stdcall Hook_CombatCastSpell(HiHook* h, _BattleMgr_* mgr, int x, int y)
{
    THISCALL_3(void, h->GetDefaultFunc(), mgr, x, y);
    MarkRangedPanelDirty("spell cast");
    return 0;
}
