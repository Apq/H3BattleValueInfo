// H3API 迁移兼容层：只保留本插件实际使用的旧 homm3.h 名称/布局。
// 目标是移除旧 deps 依赖，同时保留现有裸偏移逻辑。

using _char_   = char;
using _bool8_  = char;
using _int_    = int;

#define CALL_1(return_type, calling_convention, address, arg1) THISCALL_1(return_type, address, arg1)
#define CALL_2(return_type, calling_convention, address, arg1, arg2) THISCALL_2(return_type, address, arg1, arg2)
#define CALL_3(return_type, calling_convention, address, arg1, arg2, arg3) THISCALL_3(return_type, address, arg1, arg2, arg3)
#define CALL_7(return_type, calling_convention, address, arg1, arg2, arg3, arg4, arg5, arg6, arg7) THISCALL_7(return_type, address, arg1, arg2, arg3, arg4, arg5, arg6, arg7)

static const int HLEFT  = 0;
static const int HRIGHT = 2;
static const int HSS_SORCERY = 25;
static const int AS_SPELL_BOOK = 17; // eArtifactSlots::SPELLBOOK
static const int AID_RECANTERS_CLOAK = 83;
static const int AID_ORB_OF_THE_FIRMAMENT = 125;
static const int AID_ORB_OF_SILT = 126;
static const int AID_ORB_OF_TEMPESTUOUS_FIRE = 127;
static const int AID_ORB_OF_DRIVING_RAIN = 128;
static const unsigned SSF_AIR = 1;
static const unsigned SSF_FIRE = 2;
static const unsigned SSF_WATER = 4;
static const unsigned SSF_EARTH = 8;
static const int SPL_MAGIC_ARROW = 15;
static const int SPL_ICE_BOLT = 16;
static const int SPL_LIGHTNING_BOLT = 17;
static const int SPL_IMPLOSION = 18;
static const int SPL_FROST_RING = 20;
static const int SPL_FIREBALL = 21;
static const int SPL_METEOR_SHOWER = 23;
static const int SPL_TITANS_LIGHTNING_BOLT = 57;

using _Pcx8_ = H3LoadedPcx;
using _Pcx16_ = H3LoadedPcx16;
using _Fnt_ = H3Font;
using _DlgStaticText_ = H3DlgText;
using _EventMsg_ = H3Msg;

// 兼容旧成员名/函数名，尽量不动主体算法。
#define type command
#define subtype subtype
#define item_id itemId
#define DrawTextToPcx16 TextDraw
#define o_Smalfont_Fnt (H3Font::Load("smalfont.fnt"))
#define o_Tiny_Fnt     (H3Font::Load("tiny.fnt"))
#define o_Medfont_Fnt  (H3Font::Load("medfont.fnt"))
#define o_Bigfont_Fnt  (H3Font::Load("bigfont.fnt"))
#define o_Calli10R_Fnt (H3Font::Load("calli10r.fnt"))

static inline UINT16 RGB565_fromR8G8B8(UINT8 r, UINT8 g, UINT8 b)
{
    return (UINT16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static inline H3RGB565& palette565_at(H3LoadedPcx* pcx, int i)
{
    return pcx->palette565[i];
}

struct _Dlg_ {
    char _pad0[0x18];
    int x;
    int y;
    int width;
    int height;
    char _pad28[0x30 - 0x28];
    H3Vector<H3DlgItem*> items;
    H3DlgItem* AddItemToOwnArrayList(H3DlgItem* item) { return reinterpret_cast<H3BaseDlg*>(this)->AddItem(item); }
};

struct _CreatureInfoCompat {
    // 对应 H3CreatureInformation，从 info 结构体起点算
    // hitPoints 在 H3CreatureInformation 偏移 0x4C
    char _pad0[0x4C];
    int hit_points;     // 0x4C
    int speed;          // 0x50
    int attack;         // 0x54
    int defence;        // 0x58
    int damage_min;     // 0x5C (damageLow)
    int damage_max;     // 0x60 (damageHigh)
    int shots;          // 0x64 (numberShots)
};

struct _BattleStack_ {
    char _pad0[0x34];
    int creature_id;
    int hex_ix;
    int def_group_ix;
    int def_frame_ix;
    char _pad44[0x4C - 0x44];
    int count_current;
    int count_before_attack;
    char _pad54[0x58 - 0x54];
    int lost_hp;
    int army_slot_ix;
    int count_at_start;
    char _pad64[0x74 - 0x64];
    _CreatureInfoCompat creature;
    char _padDC[0x548 - 0xDC];
    bool CanShoot(_BattleStack_* target) { return THISCALL_2(bool, 0x442610, this, target); }
    int Calc_Damage_Bonuses(_BattleStack_* target, int base_damage, int a4, int a5, int a6, int* fireshield_damage) { return THISCALL_7(int, 0x443C60, this, target, base_damage, a4, a5, a6, fireshield_damage); }
};

typedef char _BattleStack_size_check[(sizeof(_BattleStack_) == 0x548) ? 1 : -1];

// H3Artifact 在原版是 8 字节：{INT32 id; INT32 subtype}
// 之前误用 4 字节导致 _Hero_ 后续字段全部偏移错误
struct _Artifact_ { int id; int mod; };

struct _Hero_ {
    char _pad0[0x18];
    short spell_points;
    int id;
    int id_wtf;
    char _pad22[0x55 - 0x22];
    short level;
    char _pad57[0xC9 - 0x57];
    unsigned char second_skill[28];
    char _padE5[0x12D - 0xE5];
    _Artifact_ doll_art[19];
    char _pad1C5[0x3EA - 0x1C5];
    unsigned char spell[70];
    unsigned char spell_level[70];
    char _pad478[0x47A - 0x478];
    unsigned char power;
    unsigned char knowledge;
    int GetLandModifierUnder() { return THISCALL_1(int, 0x4E5210, this); }
    bool DoesWearArtifact(int art_id) { return THISCALL_2(bool, 0x4E2C90, this, art_id); }
    int GetSpell_Specialisation_Bonuses(int spell_id, int skill_level, int damage) { return THISCALL_4(int, 0x4E6260, this, spell_id, skill_level, damage); }
};

// H3Spell 原版结构为 0x88 字节；只用前 0x44 的字段，但必须按 0x88 步进
struct _Spell_ {
    int type;            // 0x00
    const char* wav_name;// 0x04
    int animation_ix;    // 0x08
    unsigned flags;      // 0x0C
    const char* name;    // 0x10
    const char* short_name; // 0x14
    int level;           // 0x18
    unsigned school_flags; // 0x1C
    int mana_cost[4];    // 0x20
    int eff_power;       // 0x30
    int effect[4];       // 0x34
    char _pad44[0x88 - 0x44]; // 填充到原版 0x88
};

struct _WndMgr_ {
    char _pad0[0x40];
    _Pcx16_* screen_pcx16;
};

struct _BattleMgr_ {
    char _pad0[0x53C0];
    int spec_terr_type;
    char _pad53C4[0x53CC - 0x53C4];
    _Hero_* hero[2];
    char _pad53D4[0x54BC - 0x53D4];
    int stacks_count[2];
    char _pad54C4[0x54CC - 0x54C4];
    _BattleStack_ stack[2][21];
    char _pad1329C[0x132FC - 0x1329C];
    _Dlg_* dlg;
    void RedrawBattlefield(_bool8_ flip, _bool8_ set_battle_redraws, _bool8_ use_battle_redraws, int waiting_time, _bool8_ redraw_background, _bool8_ wait) {
        THISCALL_7(void, 0x493FC0, this, flip, set_battle_redraws, use_battle_redraws, waiting_time, redraw_background, wait);
    }
};

#define o_BattleMgr (*reinterpret_cast<_BattleMgr_**>(0x699420))
#define o_WndMgr (*reinterpret_cast<_WndMgr_**>(0x6992D0))
#define o_CurrentDlg (*reinterpret_cast<_Dlg_**>(0x698AC8))
#define o_Spell (*reinterpret_cast<_Spell_**>(0x687FA8))
#define o_DD (*reinterpret_cast<LPDIRECTDRAW*>(0x6AAD20))
#define o_DDSurfaceBackBuffer (*reinterpret_cast<LPDIRECTDRAWSURFACE*>(0x6AAD28))
