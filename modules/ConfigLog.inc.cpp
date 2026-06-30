// ========== 配置 ==========

static struct Config {
    char  label_fight_value[64];
    char  ranged_panel_image[256];
    char  ranged_panel_text_font[64];
    int   ranged_panel_text_color;
    int   ranged_panel_width;
    int   ranged_panel_height;
    int   ranged_panel_y;
    int   row_y[3];
    int   fight_value_y_offset;
} cfg;

static char g_ini_path[MAX_PATH];
static char g_log_path[MAX_PATH];
static wchar_t g_log_path_w[MAX_PATH * 2];
static HMODULE g_hModule = nullptr;

static const int MAX_LOG_FILES_TO_KEEP = 30;
static const int MAX_LOG_FILES_TO_SCAN = 1024;

struct LogFileEntryW {
    wchar_t path[MAX_PATH * 2];
    FILETIME last_write;
};

static int __cdecl CompareLogFileEntryW(const void* a, const void* b)
{
    const LogFileEntryW* la = (const LogFileEntryW*)a;
    const LogFileEntryW* lb = (const LogFileEntryW*)b;
    int cmp = CompareFileTime(&la->last_write, &lb->last_write);
    if (cmp != 0) return cmp;
    return _wcsicmp(la->path, lb->path);
}

static void CleanupOldLogFilesW(const wchar_t* log_dir, const wchar_t* log_base, const wchar_t* current_log_path)
{
    if (!log_dir || !log_dir[0] || !log_base || !log_base[0]) return;

    wchar_t pattern[MAX_PATH * 2];
    _snwprintf_s(pattern, _countof(pattern), _TRUNCATE, L"%s\\%s_*.log", log_dir, log_base);

    LogFileEntryW* entries = (LogFileEntryW*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_LOG_FILES_TO_SCAN * sizeof(LogFileEntryW));
    if (!entries) return;
    int count = 0;
    bool current_found = false;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (count >= MAX_LOG_FILES_TO_SCAN) break;
        _snwprintf_s(entries[count].path, _countof(entries[count].path), _TRUNCATE, L"%s\\%s", log_dir, fd.cFileName);
        entries[count].last_write = fd.ftLastWriteTime;
        if (current_log_path && _wcsicmp(entries[count].path, current_log_path) == 0) current_found = true;
        ++count;
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    int keep_existing = current_found ? MAX_LOG_FILES_TO_KEEP : (MAX_LOG_FILES_TO_KEEP - 1);
    if (keep_existing < 0) keep_existing = 0;
    if (count <= keep_existing) { HeapFree(GetProcessHeap(), 0, entries); return; }

    qsort(entries, count, sizeof(entries[0]), CompareLogFileEntryW);
    int delete_count = count - keep_existing;
    for (int i = 0; i < delete_count; ++i) DeleteFileW(entries[i].path);
    HeapFree(GetProcessHeap(), 0, entries);
}

static void SetupDatedLogPathAndCleanup(HMODULE hModule)
{
    wchar_t module_path[MAX_PATH * 2] = { 0 };
    GetModuleFileNameW(hModule, module_path, _countof(module_path));

    wchar_t dir[MAX_PATH * 2] = { 0 };
    wchar_t base[MAX_PATH * 2] = { 0 };
    const wchar_t* slash1 = wcsrchr(module_path, L'\\');
    const wchar_t* slash2 = wcsrchr(module_path, L'/');
    const wchar_t* slash = slash1 > slash2 ? slash1 : slash2;
    const wchar_t* name = slash ? slash + 1 : module_path;
    if (slash) {
        int len = (int)(slash - module_path);
        if (len >= (int)_countof(dir)) len = (int)_countof(dir) - 1;
        memcpy(dir, module_path, len * sizeof(wchar_t));
        dir[len] = 0;
    } else {
        wcscpy_s(dir, L".");
    }
    wcsncpy_s(base, name, _TRUNCATE);
    wchar_t* dot = wcsrchr(base, L'.');
    if (dot) *dot = 0;

    SYSTEMTIME st;
    GetLocalTime(&st);
    _snwprintf_s(g_log_path_w, _countof(g_log_path_w), _TRUNCATE,
        L"%s\\%s_%04u%02u%02u_%02u%02u%02u.log",
        dir, base, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    WideCharToMultiByte(CP_UTF8, 0, g_log_path_w, -1, g_log_path, sizeof(g_log_path), nullptr, nullptr);
    // 旧日志清理不在 DllMain 中执行（Loader Lock）；改由 DoLogCleanupOnce() 在首次 hook 时触发。
}

static bool g_log_cleaned = false;
static void DoLogCleanupOnce()
{
    if (g_log_cleaned) return;
    g_log_cleaned = true;
    if (!g_log_path_w[0]) return;
    wchar_t dir[MAX_PATH * 2] = { 0 };
    wchar_t base[MAX_PATH * 2] = { 0 };
    const wchar_t* s1 = wcsrchr(g_log_path_w, L'\\');
    const wchar_t* s2 = wcsrchr(g_log_path_w, L'/');
    const wchar_t* sl = s1 > s2 ? s1 : s2;
    if (sl) {
        int dlen = (int)(sl - g_log_path_w);
        if (dlen >= (int)_countof(dir)) dlen = (int)_countof(dir) - 1;
        memcpy(dir, g_log_path_w, dlen * sizeof(wchar_t));
        dir[dlen] = 0;
        wcsncpy_s(base, sl + 1, _TRUNCATE);
        wchar_t* dot = wcsrchr(base, L'.');
        if (dot) *dot = 0;
    }
    CleanupOldLogFilesW(dir, base, g_log_path_w);
    // 直接追加日志行（WriteLog 此时尚不可见，用原始文件 API）
    HANDLE hf = CreateFileW(g_log_path_w, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER fpos; fpos.QuadPart = 0;
        if (SetFilePointerEx(hf, fpos, &fpos, FILE_END) && fpos.QuadPart == 0) {
            DWORD wr; WriteFile(hf, "\xEF\xBB\xBF", 3, &wr, nullptr);
        }
        SYSTEMTIME st; GetLocalTime(&st);
        char line[128];
        int n = _snprintf(line, sizeof(line)-1, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] 旧日志清理完成。\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        if (n > 0) { DWORD wr; WriteFile(hf, line, (DWORD)n, &wr, nullptr); }
        CloseHandle(hf);
    }
}


static void ReadConfig()
{
    const char* f = g_ini_path;
    GetPrivateProfileStringA("Format", "LabelFightValue", "Fight Value", cfg.label_fight_value, sizeof(cfg.label_fight_value), f);
    GetPrivateProfileStringA("RangedPanel", "BackgroundImage", "rp_bg.pcx", cfg.ranged_panel_image, sizeof(cfg.ranged_panel_image), f);
    GetPrivateProfileStringA("RangedPanel", "TextFont", "smalfont.fnt", cfg.ranged_panel_text_font, sizeof(cfg.ranged_panel_text_font), f);
    cfg.ranged_panel_text_color = GetPrivateProfileIntA("RangedPanel", "TextColor", 4, f);
    if (cfg.ranged_panel_text_color < 0) cfg.ranged_panel_text_color = 0;
    if (cfg.ranged_panel_text_color > 255) cfg.ranged_panel_text_color = 255;
    cfg.ranged_panel_width      = GetPrivateProfileIntA("RangedPanel", "Width", 298, f);
    cfg.ranged_panel_height     = GetPrivateProfileIntA("RangedPanel", "Height", 93, f);
    cfg.ranged_panel_y          = GetPrivateProfileIntA("RangedPanel", "Y", 0, f);
    cfg.row_y[0] = GetPrivateProfileIntA("RangedPanel", "Row1Y", 24, f);
    cfg.row_y[1] = GetPrivateProfileIntA("RangedPanel", "Row2Y", 42, f);
    cfg.row_y[2] = GetPrivateProfileIntA("RangedPanel", "Row3Y", 60, f);
    cfg.fight_value_y_offset = GetPrivateProfileIntA("Layout", "FightValueYOffset", 8, f);
}
