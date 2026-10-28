/*
 * Bomberman ULTRA - Launcher do port PC (recompilação estática via ps3recomp)
 *
 * Win32 puro (sem STL), um único arquivo. Responsável por:
 *   - verificar os arquivos do jogo (EBOOT.elf, media.zip, Bman.bnk, licença
 *     e executável recompilado);
 *   - unir arquivos divididos em partes (*.part000, *.part001...);
 *   - "Preparar jogo": importar os arquivos do jogo do usuário (pasta do
 *     RPCS3) e compilar o port neste PC (tools\build_windows.py, com o
 *     Python "embeddable" oficial baixado para toolchain\python);
 *   - guardar as configurações em launcher.ini;
 *   - montar o ambiente (PS3_VFS_ROOT, PS3_HDD0_ROOT, PS3_LANG, BMU_WIDTH...)
 *     que o runtime do ps3recomp e o src/main.cpp leem, e iniciar o jogo.
 *
 * Layout esperado (relativo à pasta do launcher):
 *   game\PS3_GAME\USRDIR\EBOOT.elf     binário PPU decriptado
 *   game\PS3_GAME\USRDIR\media.zip     assets (o jogo abre /dev_bdvd/PS3_GAME/USRDIR/)
 *   game\PS3_GAME\USRDIR\Bman.bnk
 *   game\licenses\*.rap                licença do usuário
 *   bin\BombermanUltra.exe             executável recompilado (ou build\...)
 *   user\dev_hdd0, user\dev_hdd1       saves e cache (criados sozinhos)
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   /* TaskDialogIndirect e afins */
#endif
#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>
#include <stdint.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objidl.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <wchar.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <commdlg.h>
#include <bcrypt.h>
#include <shobjidl.h>
#include <urlmon.h>
#include <wininet.h>

#include "resource.h"

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uxtheme.lib")
#endif

/* ------------------------------------------------------------------------ */
/* Constantes                                                               */
/* ------------------------------------------------------------------------ */
#define APP_TITLE   L"Bomberman ULTRA — Launcher"
#define WIN_W       720
#define WIN_H       580
#define BANNER_H    150

enum {
    IDC_RES = 100, IDC_FULLSCREEN, IDC_LANG, IDC_AUDIO,
    IDC_CONSOLE, IDC_PLAY, IDC_PREPARE, IDC_SAVES, IDC_LOG, IDC_STATUS, IDC_QUIT, IDC_FPS, IDC_CAP, IDC_LICENSE, IDC_STEAM
};
#define WM_APP_GAME_EXIT (WM_APP + 1)
#define WM_APP_PREPARE   (WM_APP + 2)
#define WM_APP_JOGAR     (WM_APP + 3)
#define WM_APP_BUILD_DONE (WM_APP + 4)

struct Resolution { int w, h; const wchar_t* label; };
static const Resolution kRes[] = {
    {1280, 720,  L"1280 × 720 (nativa do PS3)"},
    {1600, 900,  L"1600 × 900"},
    {1920, 1080, L"1920 × 1080"},
    {2560, 1440, L"2560 × 1440"},
    {3840, 2160, L"3840 × 2160"},
};
struct Lang { const wchar_t* code; const wchar_t* label; };
/* Idiomas com strings_xx.xus no media.zip. "auto" = idioma do Windows.
 * "ptbr" é a tradução deste port: o jogo roda em inglês e o runtime abre
 * media_ptbr.zip (o media.zip com strings_en.xus trocado pela tradução,
 * montado pelo launcher a partir de traducao\strings_ptbr.xus). */
static const Lang kLang[] = {
    {L"ptbr", L"Português (Brasil)"}, {L"en", L"English"}, {L"fr", L"Français"}, {L"de", L"Deutsch"},
    {L"it", L"Italiano"}, {L"es", L"Español"}, {L"ja", L"日本語"},
    {L"ko", L"한국어"}, {L"auto", L"Automático (Windows)"},
};
static const wchar_t* kAudio[] = { L"Ligado", L"Mudo (PS3_MUTE)", L"Desativado (PS3_NO_AUDIO)" };

/* ------------------------------------------------------------------------ */
/* Estado                                                                   */
/* ------------------------------------------------------------------------ */
static HINSTANCE g_inst;
static HWND g_wnd, g_res, g_full, g_lang, g_audio, g_console, g_fps, g_cap, g_play, g_status;
static HFONT g_font, g_font_bold, g_font_title;
static HBRUSH g_bg_brush, g_panel_brush;
static Gdiplus::Image* g_banner = nullptr;
static ULONG_PTR g_gdiplus = 0;
static wchar_t g_root[MAX_PATH];      /* pasta do launcher */
static wchar_t g_ini[MAX_PATH];
static wchar_t g_game_exe[MAX_PATH];
static HANDLE g_game_proc = nullptr;
static bool   g_game_killed = false;   /* encerrado pelo botão: sem aviso de erro */

static const COLORREF kBg = RGB(24, 26, 33), kPanel = RGB(34, 37, 46), kText = RGB(230, 232, 238),
                      kDim = RGB(150, 156, 170);

/* ------------------------------------------------------------------------ */
/* Utilidades de caminho                                                    */
/* ------------------------------------------------------------------------ */
static void path_join(wchar_t* out, const wchar_t* a, const wchar_t* b)
{
    _snwprintf(out, MAX_PATH, L"%s\\%s", a, b);
    out[MAX_PATH - 1] = 0;
}
static bool file_exists(const wchar_t* p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static void game_path(wchar_t* out, const wchar_t* rel)
{
    wchar_t g[MAX_PATH];
    path_join(g, g_root, L"game");
    path_join(out, g, rel);
}
static bool license_ok(void);
static bool g_jogar_mode = false;   /* --jogar: abre o jogo direto (atalho da Steam) */
static bool any_file(const wchar_t* dir, const wchar_t* pattern)
{
    wchar_t q[MAX_PATH];
    path_join(q, dir, pattern);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(q, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    FindClose(h);
    return true;
}

#ifndef PF_AVX2_INSTRUCTIONS_AVAILABLE
#define PF_AVX2_INSTRUCTIONS_AVAILABLE 40
#endif
/* bin\BombermanUltra.exe é compilado para x86-64-v3 (AVX2/FMA: os jobs SPU e o
 * áudio rodam ~2x mais rápido); CPUs sem AVX2 usam o _compat (SSE4.2). */
static bool cpu_has_avx2(void)
{
    return IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) != FALSE;
}

/* Executável recompilado: bin\ primeiro, depois as saídas comuns do CMake. */
static bool find_game_exe(void)
{
    if (!cpu_has_avx2()) {
        path_join(g_game_exe, g_root, L"bin\\BombermanUltra_compat.exe");
        if (file_exists(g_game_exe)) return true;
    }
    static const wchar_t* cands[] = {
        L"bin\\BombermanUltra.exe",
        L"build\\Release\\BombermanUltra.exe",
        L"build\\RelWithDebInfo\\BombermanUltra.exe",
        L"build\\BombermanUltra.exe",
        L"build\\Debug\\BombermanUltra.exe",
    };
    for (size_t i = 0; i < sizeof cands / sizeof cands[0]; i++) {
        path_join(g_game_exe, g_root, cands[i]);
        if (file_exists(g_game_exe)) return true;
    }
    path_join(g_game_exe, g_root, cands[0]);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Arquivos divididos: <nome>.part000, .part001 ... -> <nome>               */
/* ------------------------------------------------------------------------ */
static bool has_parts(const wchar_t* base)
{
    wchar_t p[MAX_PATH];
    _snwprintf(p, MAX_PATH, L"%s.part000", base);
    return file_exists(p);
}
/* Une <src>.part000, .part001... em <dst> e apaga as partes. */
static bool join_parts(const wchar_t* src, const wchar_t* dst, wchar_t* err, size_t errn)
{
    wchar_t tmp[MAX_PATH];
    _snwprintf(tmp, MAX_PATH, L"%s.joining", dst);
    HANDLE out = CreateFileW(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) { _snwprintf(err, errn, L"Não consegui criar %s", tmp); return false; }
    static BYTE buf[1 << 20];
    int n = 0;
    for (;; n++) {
        wchar_t part[MAX_PATH];
        _snwprintf(part, MAX_PATH, L"%s.part%03d", src, n);
        if (!file_exists(part)) break;
        HANDLE in = CreateFileW(part, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (in == INVALID_HANDLE_VALUE) { CloseHandle(out); _snwprintf(err, errn, L"Falha ao abrir %s", part); return false; }
        DWORD rd = 0, wr = 0;
        while (ReadFile(in, buf, sizeof buf, &rd, nullptr) && rd) {
            if (!WriteFile(out, buf, rd, &wr, nullptr) || wr != rd) {
                CloseHandle(in); CloseHandle(out);
                _snwprintf(err, errn, L"Falha ao gravar (disco cheio?)");
                return false;
            }
        }
        CloseHandle(in);
    }
    CloseHandle(out);
    if (n == 0) { DeleteFileW(tmp); return true; }
    if (!MoveFileExW(tmp, dst, MOVEFILE_REPLACE_EXISTING)) { _snwprintf(err, errn, L"Falha ao renomear %s", tmp); return false; }
    for (int i = 0; i < n; i++) {
        wchar_t part[MAX_PATH];
        _snwprintf(part, MAX_PATH, L"%s.part%03d", src, i);
        DeleteFileW(part);
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Configurações (launcher.ini)                                             */
/* ------------------------------------------------------------------------ */
static int ini_int(const wchar_t* key, int def)
{
    return (int)GetPrivateProfileIntW(L"launcher", key, def, g_ini);
}
static void ini_put_int(const wchar_t* key, int v)
{
    wchar_t s[32];
    _snwprintf(s, 32, L"%d", v);
    WritePrivateProfileStringW(L"launcher", key, s, g_ini);
}

static void load_settings(void)
{
    int r = ini_int(L"resolucao", 2);
    if (r < 0 || r >= (int)(sizeof kRes / sizeof kRes[0])) r = 0;
    SendMessageW(g_res, CB_SETCURSEL, r, 0);
    SendMessageW(g_full, BM_SETCHECK, ini_int(L"tela_cheia", 0) ? BST_CHECKED : BST_UNCHECKED, 0);
    /* Idioma salvo pelo código (o índice mudou quando entrou o PT-BR). */
    int l = 0;
    {
        wchar_t code[16] = L"";
        GetPrivateProfileStringW(L"launcher", L"idioma_codigo", L"", code, 16, g_ini);
        bool found = false;
        for (int i = 0; i < (int)(sizeof kLang / sizeof kLang[0]); i++)
            if (code[0] && !wcscmp(code, kLang[i].code)) { l = i; found = true; }
        if (!found) {   /* launcher.ini antigo: índice sem o PT-BR na frente */
            int old = ini_int(L"idioma", -1);
            l = (old >= 0 && old + 1 < (int)(sizeof kLang / sizeof kLang[0])) ? old + 1 : 0;
        }
    }
    SendMessageW(g_lang, CB_SETCURSEL, l, 0);
    int a = ini_int(L"audio", 0);
    if (a < 0 || a > 2) a = 0;
    SendMessageW(g_audio, CB_SETCURSEL, a, 0);
    SendMessageW(g_console, BM_SETCHECK, ini_int(L"console", 0) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_fps, BM_SETCHECK, ini_int(L"mostrar_fps", 1) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_cap, BM_SETCHECK, ini_int(L"limitar_60fps", 1) ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void save_settings(void)
{
    ini_put_int(L"resolucao", (int)SendMessageW(g_res, CB_GETCURSEL, 0, 0));
    ini_put_int(L"tela_cheia", SendMessageW(g_full, BM_GETCHECK, 0, 0) == BST_CHECKED);
    {
        int l = (int)SendMessageW(g_lang, CB_GETCURSEL, 0, 0);
        if (l < 0) l = 0;
        WritePrivateProfileStringW(L"launcher", L"idioma_codigo", kLang[l].code, g_ini);
        WritePrivateProfileStringW(L"launcher", L"idioma", nullptr, g_ini);
    }
    ini_put_int(L"audio", (int)SendMessageW(g_audio, CB_GETCURSEL, 0, 0));
    ini_put_int(L"console", SendMessageW(g_console, BM_GETCHECK, 0, 0) == BST_CHECKED);
    ini_put_int(L"mostrar_fps", SendMessageW(g_fps, BM_GETCHECK, 0, 0) == BST_CHECKED);
    ini_put_int(L"limitar_60fps", SendMessageW(g_cap, BM_GETCHECK, 0, 0) == BST_CHECKED);
}


/* ------------------------------------------------------------------------ */
/* Licença                                                                  */
/* ------------------------------------------------------------------------ */
/* O port foi compilado a partir do Bomberman ULTRA europeu (NPEB00076). O
 * launcher só abre o jogo com a licença dessa versão: o arquivo RAP de
 * EP0555-NPEB00076_00-BOMBERMANGAME001, que o próprio usuário obtém do seu
 * PS3/conta. O repositório não traz nenhuma licença: só o SHA-256 dela, que
 * serve para reconhecer o arquivo certo e não permite recriá-lo. */
static const wchar_t kContentId[] = L"EP0555-NPEB00076_00-BOMBERMANGAME001";
static const unsigned char kRapSha256[32] = {
    0xBE,0x87,0x27,0x1B,0x92,0x47,0x68,0x8B,0xD6,0x80,0xCC,0xC9,0x67,0x58,0x65,0xE6,
    0x9C,0xEA,0x9E,0x01,0x72,0x19,0x67,0xC8,0x3E,0x10,0x58,0xB2,0x34,0xCB,0x79,0xA1,
};

static bool bcrypt_hash(LPCWSTR alg_id, const unsigned char* d, size_t n, unsigned char* out, ULONG outn)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, alg_id, nullptr, 0) != 0) return false;
    BCRYPT_HASH_HANDLE hh = nullptr;
    bool ok = BCryptCreateHash(alg, &hh, nullptr, 0, nullptr, 0, 0) == 0;
    for (size_t off = 0; ok && off < n; off += 1u << 30) {
        const ULONG len = (ULONG)std::min<size_t>(n - off, 1u << 30);
        ok = BCryptHashData(hh, (PUCHAR)d + off, len, 0) == 0;
    }
    if (ok) ok = BCryptFinishHash(hh, out, outn, 0) == 0;
    if (hh) BCryptDestroyHash(hh);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}
static bool sha256(const unsigned char* d, size_t n, unsigned char out[32])
{
    return bcrypt_hash(BCRYPT_SHA256_ALGORITHM, d, n, out, 32);
}

/* Um RAP é um arquivo de 16 bytes; só o da versão europeia confere. */
static bool rap_valid(const wchar_t* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    unsigned char buf[17]; DWORD got = 0;
    const BOOL rd = ReadFile(h, buf, sizeof buf, &got, nullptr);
    CloseHandle(h);
    if (!rd || got != 16) return false;
    unsigned char hsh[32];
    return sha256(buf, 16, hsh) && !memcmp(hsh, kRapSha256, 32);
}

static void license_path(wchar_t* out)
{
    wchar_t rel[96];
    _snwprintf(rel, 96, L"licenses\\%s.rap", kContentId);
    game_path(out, rel);
}

static bool license_ok(void)
{
    wchar_t p[MAX_PATH];
    license_path(p);
    return rap_valid(p);
}

/* Copia um RAP válido para game\licenses\<content_id>.rap (o nome que o
 * runtime procura). */
static bool license_install(const wchar_t* src)
{
    if (!rap_valid(src)) return false;
    wchar_t dir[MAX_PATH], dst[MAX_PATH];
    game_path(dir, L"licenses");
    SHCreateDirectoryExW(nullptr, dir, nullptr);
    license_path(dst);
    if (!_wcsicmp(src, dst)) return true;
    return CopyFileW(src, dst, FALSE) != 0;
}

/* Procura um RAP válido (qualquer nome) numa pasta. */
static bool license_search_dir(const wchar_t* dir)
{
    wchar_t q[MAX_PATH];
    path_join(q, dir, L"*.rap");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(q, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wchar_t f[MAX_PATH];
        path_join(f, dir, fd.cFileName);
        if (license_install(f)) { found = true; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

/* Busca automática: game\licenses (qualquer nome), a pasta do launcher, as
 * duas pastas acima dela (onde costumam ficar o PKG e o RAP), Downloads,
 * Desktop e Documentos. */
static bool license_autofind(void)
{
    if (license_ok()) return true;
    wchar_t d[MAX_PATH];
    game_path(d, L"licenses");
    if (license_search_dir(d)) return true;
    wcscpy(d, g_root);
    for (int up = 0; up < 3; up++) {
        if (license_search_dir(d)) return true;
        wchar_t* sl = wcsrchr(d, L'\\');
        if (!sl || sl == d + 2) break;
        *sl = 0;
    }
    static const KNOWNFOLDERID* kf[] = { &FOLDERID_Downloads, &FOLDERID_Desktop, &FOLDERID_Documents };
    for (const KNOWNFOLDERID* id : kf) {
        PWSTR k = nullptr;
        if (SHGetKnownFolderPath(*id, 0, nullptr, &k) == S_OK) {
            const bool f = license_search_dir(k);
            CoTaskMemFree(k);
            if (f) return true;
        }
    }
    return false;
}

/* Botão "Localizar licença": o usuário aponta o arquivo .rap. */
static void license_pick(void)
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn; ZeroMemory(&ofn, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_wnd;
    ofn.lpstrFilter = L"Licença PS3 (*.rap)\0*.rap\0Todos os arquivos\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Localizar a licença EP0555-NPEB00076_00-BOMBERMANGAME001.rap";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    if (license_install(file)) {
        MessageBoxW(g_wnd, L"Licença da versão europeia reconhecida e instalada.", APP_TITLE, MB_ICONINFORMATION);
    } else {
        MessageBoxW(g_wnd, L"Este arquivo não é a licença do Bomberman ULTRA europeu "
                           L"(EP0555-NPEB00076_00-BOMBERMANGAME001.rap).\n\n"
                           L"O port foi compilado a partir dessa versão e só roda com a licença dela.",
                    APP_TITLE, MB_ICONWARNING);
    }
}

/* ------------------------------------------------------------------------ */
/* Verificação                                                              */
/* ------------------------------------------------------------------------ */
struct Check { bool ok; bool fatal; wchar_t text[200]; };

static int run_checks(Check* c, int max)
{
    int n = 0;
    wchar_t p[MAX_PATH];
    (void)max;

    game_path(p, L"PS3_GAME\\USRDIR\\EBOOT.elf");
    c[n].ok = file_exists(p); c[n].fatal = true;
    wcscpy(c[n].text, c[n].ok ? L"EBOOT.elf decriptado" : L"EBOOT.elf ausente — use “Preparar jogo”");
    n++;

    static const wchar_t* assets[] = { L"media.zip", L"Bman.bnk" };
    for (int i = 0; i < 2; i++) {
        wchar_t rel[64];
        _snwprintf(rel, 64, L"PS3_GAME\\USRDIR\\%s", assets[i]);
        game_path(p, rel);
        bool ok = file_exists(p), parts = has_parts(p);
        c[n].ok = ok; c[n].fatal = true;
        if (ok) _snwprintf(c[n].text, 200, L"%s", assets[i]);
        else if (parts) _snwprintf(c[n].text, 200, L"%s em partes — use \u201cPreparar jogo\u201d", assets[i]);
        else _snwprintf(c[n].text, 200, L"%s ausente — use \u201cPreparar jogo\u201d", assets[i]);
        n++;
    }

    c[n].ok = license_ok(); c[n].fatal = true;
    wcscpy(c[n].text, c[n].ok ? L"Licença Europa (EP0555-NPEB00076)"
                              : L"Falta a licença .rap da versão Europa — use \u201cLocalizar licença\u201d");
    n++;

    c[n].ok = find_game_exe(); c[n].fatal = true;
    if (c[n].ok) _snwprintf(c[n].text, 200, L"Jogo: %s", g_game_exe + wcslen(g_root) + 1);
    else wcscpy(c[n].text, L"Jogo ainda não compilado neste PC — use “Preparar jogo”");
    n++;

    /* O jogo desenha via Vulkan: precisa do vulkan-1.dll do driver de vídeo. */
    {
        HMODULE vk = LoadLibraryW(L"vulkan-1.dll");
        c[n].ok = vk != nullptr; c[n].fatal = true;
        if (vk) FreeLibrary(vk);
        wcscpy(c[n].text, c[n].ok ? L"Vulkan (driver de vídeo)"
                                  : L"vulkan-1.dll não encontrado — atualize o driver da placa de vídeo");
        n++;
    }

    return n;
}

static bool g_building = false;   /* "Preparar jogo" em andamento */
static void refresh_status(void)
{
    if (g_building) {
        SetWindowTextW(g_status, L"Compilando o jogo neste PC…\r\n\r\n"
                                 L"Acompanhe na janela do console. Na primeira vez leva de 10 a 40 minutos "
                                 L"(baixa as ferramentas e converte o jogo).\r\n\r\n"
                                 L"Pode usar o PC enquanto isso; não feche a janela do console.");
        EnableWindow(g_play, FALSE);
        return;
    }
    Check c[8];
    int n = run_checks(c, 8);
    wchar_t buf[1600];
    buf[0] = 0;
    bool can_play = true;
    for (int i = 0; i < n; i++) {
        wchar_t line[260];
        _snwprintf(line, 260, L"%s  %s\r\n", c[i].ok ? L"✔" : (c[i].fatal ? L"✖" : L"⚠"), c[i].text);
        wcscat(buf, line);
        if (!c[i].ok && c[i].fatal) can_play = false;
    }
    wcscat(buf, L"\r\nTeclado: setas, Z = ✕, X = ○, A = □, S = △, Enter = START\r\n"
                L"Controles: DualShock 4, DualSense e Xbox (pode conectar com o jogo aberto)\r\n");
    SetWindowTextW(g_status, buf);
    EnableWindow(g_play, can_play || g_game_proc);
}

/* ------------------------------------------------------------------------ */
/* Iniciar o jogo                                                           */
/* ------------------------------------------------------------------------ */
static void env_set(const wchar_t* k, const wchar_t* v) { SetEnvironmentVariableW(k, v); }

static void ensure_dir(const wchar_t* p) { SHCreateDirectoryExW(nullptr, p, nullptr); }

static DWORD WINAPI wait_game(LPVOID)
{
    WaitForSingleObject(g_game_proc, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(g_game_proc, &code);
    PostMessageW(g_wnd, WM_APP_GAME_EXIT, code, 0);
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Tradução PT-BR                                                           */
/* ------------------------------------------------------------------------ */
/* Monta game\PS3_GAME\USRDIR\media_ptbr.zip: cópia do media.zip com
 * strings_en.xus trocado por traducao\strings_ptbr.xus (entrada STORED). As
 * outras entradas são copiadas byte a byte. Só refaz quando o zip não existe
 * ou é mais velho que alguma das fontes. */
static bool read_whole(const wchar_t* path, std::vector<unsigned char>& out)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0; size_t off = 0; bool ok = true;
    while (off < out.size()) {
        const DWORD want = (DWORD)std::min<size_t>(out.size() - off, 1u << 26);
        if (!ReadFile(h, out.data() + off, want, &got, nullptr) || !got) { ok = false; break; }
        off += got;
    }
    CloseHandle(h);
    return ok;
}
static ULONGLONG file_mtime(const wchar_t* p)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &a)) return 0;
    return ((ULONGLONG)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
}
static uint32_t crc32_buf(const unsigned char* d, size_t n)
{
    static uint32_t t[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; } init = true; }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = t[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static inline uint16_t rd16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd32(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline void put16(std::vector<unsigned char>& v, uint16_t x) { v.push_back((unsigned char)x); v.push_back((unsigned char)(x >> 8)); }
static inline void put32(std::vector<unsigned char>& v, uint32_t x) { put16(v, (uint16_t)x); put16(v, (uint16_t)(x >> 16)); }

/* Parte pura (testável fora do Windows): z = media.zip, nw = strings
 * traduzidas, out = zip novo. */
static bool ptbr_zip_rebuild(const std::vector<unsigned char>& z, const std::vector<unsigned char>& nw,
                             std::vector<unsigned char>& out)
{
    if (z.size() < 22) return false;
    size_t eocd = z.size() - 22;
    while (eocd > 0 && rd32(&z[eocd]) != 0x06054b50u) eocd--;
    if (rd32(&z[eocd]) != 0x06054b50u) return false;
    const unsigned n = rd16(&z[eocd + 10]);
    size_t p = rd32(&z[eocd + 16]);
    std::vector<unsigned char> cd;
    out.clear();
    out.reserve(z.size() + nw.size());
    const uint32_t nw_crc = crc32_buf(nw.data(), nw.size());
    for (unsigned i = 0; i < n; i++) {
        if (p + 46 > z.size() || rd32(&z[p]) != 0x02014b50u) return false;
        const unsigned char* c = &z[p];
        uint16_t vm = rd16(c + 4), vn = rd16(c + 6), fl = rd16(c + 8), meth = rd16(c + 10), mt2 = rd16(c + 12), mdt = rd16(c + 14);
        uint32_t crc = rd32(c + 16), cs = rd32(c + 20), us = rd32(c + 24);
        const uint16_t nl = rd16(c + 28), el = rd16(c + 30), cl = rd16(c + 32);
        const uint16_t ia = rd16(c + 36); const uint32_t ea = rd32(c + 38), lho = rd32(c + 42);
        const unsigned char* name = c + 46;
        p += 46 + nl + el + cl;
        if (lho + 30 > z.size()) return false;
        const size_t data_off = lho + 30 + rd16(&z[lho + 26]) + rd16(&z[lho + 28]);
        const unsigned char* data = &z[0] + data_off;
        size_t dlen = cs;
        if (nl == 14 && !memcmp(name, "strings_en.xus", 14)) {
            meth = 0; data = nw.data(); dlen = nw.size(); cs = us = (uint32_t)nw.size(); crc = nw_crc;
        } else if (data_off + cs > z.size()) return false;
        fl &= (uint16_t)~8u;
        const uint32_t off = (uint32_t)out.size();
        put32(out, 0x04034b50u); put16(out, vn); put16(out, fl); put16(out, meth); put16(out, mt2); put16(out, mdt);
        put32(out, crc); put32(out, cs); put32(out, us); put16(out, nl); put16(out, 0);
        out.insert(out.end(), name, name + nl);
        out.insert(out.end(), data, data + dlen);
        put32(cd, 0x02014b50u); put16(cd, vm); put16(cd, vn); put16(cd, fl); put16(cd, meth); put16(cd, mt2); put16(cd, mdt);
        put32(cd, crc); put32(cd, cs); put32(cd, us); put16(cd, nl); put16(cd, 0); put16(cd, 0); put16(cd, 0);
        put16(cd, ia); put32(cd, ea); put32(cd, off);
        cd.insert(cd.end(), name, name + nl);
    }
    const uint32_t cdoff = (uint32_t)out.size();
    out.insert(out.end(), cd.begin(), cd.end());
    put32(out, 0x06054b50u); put16(out, 0); put16(out, 0); put16(out, (uint16_t)n); put16(out, (uint16_t)n);
    put32(out, (uint32_t)cd.size()); put32(out, cdoff); put16(out, 0);

    return true;
}

static bool build_ptbr_zip(void)
{
    wchar_t src[MAX_PATH], dst[MAX_PATH], tr[MAX_PATH];
    game_path(src, L"PS3_GAME\\USRDIR\\media.zip");
    game_path(dst, L"PS3_GAME\\USRDIR\\media_ptbr.zip");
    path_join(tr, g_root, L"traducao\\strings_ptbr.xus");
    const ULONGLONG ms = file_mtime(src), mt = file_mtime(tr), md = file_mtime(dst);
    if (!ms || !mt) return false;
    if (md && md >= ms && md >= mt) return true;

    std::vector<unsigned char> z, nw, out;
    if (!read_whole(src, z) || !read_whole(tr, nw) || !ptbr_zip_rebuild(z, nw, out)) return false;
    wchar_t tmp[MAX_PATH];
    _snwprintf(tmp, MAX_PATH, L"%s.tmp", dst);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t off = 0; DWORD wr = 0; bool ok = true;
    while (off < out.size()) {
        const DWORD want = (DWORD)std::min<size_t>(out.size() - off, 1u << 26);
        if (!WriteFile(h, out.data() + off, want, &wr, nullptr) || !wr) { ok = false; break; }
        off += wr;
    }
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp, dst, MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp); return false; }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Atalho na Steam                                                          */
/* ------------------------------------------------------------------------ */
/* "Adicionar à Steam" grava um jogo não-Steam em
 * <Steam>\userdata\<usuário>\config\shortcuts.vdf (VDF binário: 0x00 abre
 * objeto, 0x01 string, 0x02 int32, 0x08 fecha), apontando para este launcher
 * com "--jogar": abre o jogo direto com as opções salvas e fecha junto com
 * ele, então dá para jogar do Big Picture só com o controle (SELECT+START
 * segurados fecham o jogo). A Steam reescreve esse arquivo ao sair, por isso
 * ela precisa estar fechada enquanto o atalho é gravado. */
static std::string to_utf8(const wchar_t* w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

struct VdfScan { int entries = 0; bool already = false; std::string want_exe; };
static bool vdf_skip_cstr(const std::vector<unsigned char>& v, size_t& i, std::string* out)
{
    const size_t s0 = i;
    while (i < v.size() && v[i]) i++;
    if (i >= v.size()) return false;
    if (out) out->assign((const char*)&v[s0], i - s0);
    i++;
    return true;
}
/* Pula o corpo de um objeto aninhado (ex.: "tags"); i termina no 0x08 dele. */
static bool vdf_walk(const std::vector<unsigned char>& v, size_t& i, int depth, VdfScan*)
{
    if (depth > 16) return false;
    while (i < v.size()) {
        const unsigned char t = v[i];
        if (t == 0x08) return true;
        i++;
        if (!vdf_skip_cstr(v, i, nullptr)) return false;
        if (t == 0x00) {
            if (!vdf_walk(v, i, depth + 1, nullptr)) return false;
            i++;
        } else if (t == 0x01) {
            if (!vdf_skip_cstr(v, i, nullptr)) return false;
        } else if (t == 0x02) {
            if (i + 4 > v.size()) return false;
            i += 4;
        } else if (t == 0x07) {
            if (i + 8 > v.size()) return false;
            i += 8;
        } else {
            return false;
        }
    }
    return false;
}
/* Conta os atalhos, vê se o nosso Exe já está lá e acha o 0x08 que fecha
 * "shortcuts" (onde o atalho novo entra). */
static bool vdf_scan_shortcuts(const std::vector<unsigned char>& v, VdfScan* sc, size_t* close_pos)
{
    /* raiz: 0x00 "shortcuts" 0x00 <filhos> 0x08 0x08 */
    if (v.size() < 13 || v[0] != 0x00) return false;
    size_t i = 1;
    std::string key;
    if (!vdf_skip_cstr(v, i, &key) || _stricmp(key.c_str(), "shortcuts")) return false;
    while (i < v.size() && v[i] != 0x08) {
        if (v[i] != 0x00) return false;          /* cada atalho é um objeto */
        i++;
        if (!vdf_skip_cstr(v, i, nullptr)) return false;
        sc->entries++;
        /* campos do atalho */
        std::string exe;
        while (i < v.size() && v[i] != 0x08) {
            const unsigned char t = v[i++];
            std::string k;
            if (!vdf_skip_cstr(v, i, &k)) return false;
            if (t == 0x00) {
                if (!vdf_walk(v, i, 3, nullptr)) return false;
                i++;
            } else if (t == 0x01) {
                std::string val;
                if (!vdf_skip_cstr(v, i, &val)) return false;
                if (!_stricmp(k.c_str(), "exe")) exe = val;
            } else if (t == 0x02) { if (i + 4 > v.size()) return false; i += 4; }
            else if (t == 0x07) { if (i + 8 > v.size()) return false; i += 8; }
            else return false;
        }
        if (i >= v.size()) return false;
        i++;                                     /* 0x08 do atalho */
        if (!exe.empty() && !_stricmp(exe.c_str(), sc->want_exe.c_str())) sc->already = true;
    }
    if (i >= v.size()) return false;
    *close_pos = i;                              /* 0x08 que fecha "shortcuts" */
    return true;
}

static void vdf_str(std::vector<unsigned char>& o, const char* k, const std::string& val)
{
    o.push_back(0x01); o.insert(o.end(), k, k + strlen(k) + 1);
    o.insert(o.end(), val.begin(), val.end()); o.push_back(0);
}
static void vdf_int(std::vector<unsigned char>& o, const char* k, uint32_t val)
{
    o.push_back(0x02); o.insert(o.end(), k, k + strlen(k) + 1);
    for (int b = 0; b < 4; b++) o.push_back((unsigned char)(val >> (8 * b)));
}

static bool process_running(const wchar_t* exe)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe; pe.dwSize = sizeof pe;
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
        if (!_wcsicmp(pe.szExeFile, exe)) found = true;
    CloseHandle(snap);
    return found;
}

static bool write_whole(const wchar_t* path, const std::vector<unsigned char>& data)
{
    wchar_t tmp[MAX_PATH];
    _snwprintf(tmp, MAX_PATH, L"%s.tmp", path);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    const BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr) && wr == data.size();
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp); return false; }
    return true;
}

/* Parte pura (testável fora do Windows): acrescenta o atalho em v.
 * 1 = acrescentado, 0 = já existia, -1 = arquivo malformado. */
static int vdf_add_shortcut(std::vector<unsigned char>& v, const std::string& exe_q, const std::string& start_q,
                            const std::string& icon, const std::string& name, uint32_t appid)
{
    if (v.empty()) {
        const char hdr[] = "\0shortcuts";
        v.assign(hdr, hdr + sizeof hdr);          /* 0x00 "shortcuts" 0x00 */
        v.push_back(0x08); v.push_back(0x08);
    }
    VdfScan sc; sc.want_exe = exe_q;
    size_t close_pos = 0;
    if (!vdf_scan_shortcuts(v, &sc, &close_pos)) return -1;
    if (sc.already) return 0;

    std::vector<unsigned char> e;
    e.push_back(0x00);
    const std::string idx = std::to_string(sc.entries);
    e.insert(e.end(), idx.begin(), idx.end()); e.push_back(0);
    vdf_int(e, "appid", appid);
    vdf_str(e, "AppName", name);
    vdf_str(e, "Exe", exe_q);
    vdf_str(e, "StartDir", start_q);
    vdf_str(e, "icon", icon);
    vdf_str(e, "ShortcutPath", "");
    vdf_str(e, "LaunchOptions", "--jogar");
    vdf_int(e, "IsHidden", 0);
    vdf_int(e, "AllowDesktopConfig", 1);
    vdf_int(e, "AllowOverlay", 1);
    vdf_int(e, "OpenVR", 0);
    vdf_int(e, "Devkit", 0);
    vdf_str(e, "DevkitGameID", "");
    vdf_int(e, "DevkitOverrideAppID", 0);
    vdf_int(e, "LastPlayTime", 0);
    vdf_str(e, "FlatpakAppID", "");
    e.push_back(0x00); { const char t[] = "tags"; e.insert(e.end(), t, t + sizeof t); }
    e.push_back(0x08);                            /* fim de tags */
    e.push_back(0x08);                            /* fim do atalho */
    v.insert(v.begin() + close_pos, e.begin(), e.end());
    return 1;
}

/* Grava o atalho para um usuário. 1 = gravado, 0 = já existia, -1 = erro. */
static int steam_add_for_user(const wchar_t* cfg_dir, const std::string& exe_q, const std::string& start_q,
                              const std::string& icon, const std::string& name, uint32_t appid)
{
    wchar_t vdf_path[MAX_PATH];
    path_join(vdf_path, cfg_dir, L"shortcuts.vdf");
    std::vector<unsigned char> v;
    if (file_exists(vdf_path) && !read_whole(vdf_path, v)) return -1;
    const int r = vdf_add_shortcut(v, exe_q, start_q, icon, name, appid);
    if (r <= 0) return r;
    /* Cópia de segurança do original antes de regravar. */
    if (file_exists(vdf_path)) {
        wchar_t bak[MAX_PATH];
        _snwprintf(bak, MAX_PATH, L"%s.bak_bomberman", vdf_path);
        CopyFileW(vdf_path, bak, FALSE);
    }
    return write_whole(vdf_path, v) ? 1 : -1;
}

static void steam_add(void)
{
    wchar_t steam[MAX_PATH]; DWORD sz = sizeof steam;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ,
                     nullptr, steam, &sz) != ERROR_SUCCESS) {
        MessageBoxW(g_wnd, L"Não encontrei a Steam instalada neste usuário do Windows.", APP_TITLE, MB_ICONWARNING);
        return;
    }
    for (wchar_t* c = steam; *c; c++) if (*c == L'/') *c = L'\\';

    bool reopen = false;
    if (process_running(L"steam.exe")) {
        if (MessageBoxW(g_wnd, L"A Steam está aberta. Ela regrava a lista de jogos ao fechar, então "
                               L"precisa estar fechada para o atalho ser salvo.\n\n"
                               L"Fechar a Steam agora, adicionar o atalho e abrir a Steam de novo?",
                        APP_TITLE, MB_ICONQUESTION | MB_YESNO) != IDYES) return;
        ShellExecuteW(nullptr, L"open", L"steam://exit", nullptr, nullptr, SW_SHOWNORMAL);
        SetCursor(LoadCursor(nullptr, IDC_WAIT));
        for (int i = 0; i < 300 && process_running(L"steam.exe"); i++) Sleep(100);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        if (process_running(L"steam.exe")) {
            MessageBoxW(g_wnd, L"A Steam não fechou a tempo. Feche-a e tente de novo.", APP_TITLE, MB_ICONWARNING);
            return;
        }
        Sleep(1000);                              /* deixa ela terminar de gravar */
        reopen = true;
    }

    wchar_t me[MAX_PATH], icon_w[MAX_PATH], start_w[MAX_PATH + 4];
    GetModuleFileNameW(nullptr, me, MAX_PATH);
    _snwprintf(start_w, MAX_PATH + 4, L"%s\\", g_root);
    game_path(icon_w, L"PS3_GAME\\ICON0.PNG");
    const std::string exe_q = "\"" + to_utf8(me) + "\"";
    const std::string start_q = "\"" + to_utf8(start_w) + "\"";
    const std::string icon = file_exists(icon_w) ? to_utf8(icon_w) : to_utf8(me);
    const std::string name = "Bomberman ULTRA";
    const std::string key = exe_q + name;
    const uint32_t appid = crc32_buf((const unsigned char*)key.data(), key.size()) | 0x80000000u;

    wchar_t ud[MAX_PATH], q[MAX_PATH];
    path_join(ud, steam, L"userdata");
    path_join(q, ud, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(q, &fd);
    int added = 0, existed = 0, failed = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (!iswdigit(fd.cFileName[0]) || !wcscmp(fd.cFileName, L"0")) continue;
            wchar_t user[MAX_PATH], cfg[MAX_PATH];
            path_join(user, ud, fd.cFileName);
            path_join(cfg, user, L"config");
            SHCreateDirectoryExW(nullptr, cfg, nullptr);
            const int r = steam_add_for_user(cfg, exe_q, start_q, icon, name, appid);
            if (r > 0) added++; else if (r == 0) existed++; else failed++;
            /* Capa na grade da biblioteca: o ícone do jogo. */
            if (r >= 0 && file_exists(icon_w)) {
                wchar_t grid[MAX_PATH], art[MAX_PATH], fn[32];
                path_join(grid, cfg, L"grid");
                SHCreateDirectoryExW(nullptr, grid, nullptr);
                _snwprintf(fn, 32, L"%u.png", appid);
                path_join(art, grid, fn);
                if (!file_exists(art)) CopyFileW(icon_w, art, TRUE);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (reopen) {
        wchar_t exe[MAX_PATH];
        path_join(exe, steam, L"steam.exe");
        ShellExecuteW(nullptr, L"open", exe, nullptr, nullptr, SW_SHOWNORMAL);
    }
    wchar_t msg[400];
    if (!added && !existed && !failed)
        wcscpy(msg, L"Não achei nenhum usuário da Steam neste PC (pasta userdata vazia). Entre na Steam uma vez e tente de novo.");
    else if (failed && !added && !existed)
        wcscpy(msg, L"Não consegui gravar o atalho (shortcuts.vdf ilegível ou sem permissão).");
    else
        _snwprintf(msg, 400, L"Bomberman ULTRA %s na Steam (%d usuário(s)).\n\n"
                   L"Ele abre direto no jogo com as opções deste launcher. Para fechar o jogo pelo "
                   L"controle, segure SELECT + START (Share + Options no DualShock 4) por meio segundo.",
                   added ? L"adicionado" : L"já estava", added ? added : existed);
    MessageBoxW(g_wnd, msg, APP_TITLE, failed && !added && !existed ? MB_ICONWARNING : MB_ICONINFORMATION);
}

static void launch_game(void)
{
    save_settings();
    if (!license_autofind()) {
        refresh_status();
        MessageBoxW(g_wnd, L"Para jogar é preciso a licença da versão europeia do Bomberman ULTRA:\n"
                           L"EP0555-NPEB00076_00-BOMBERMANGAME001.rap\n\n"
                           L"Use \u201cLocalizar licença\u201d e aponte o arquivo .rap.",
                    APP_TITLE, MB_ICONWARNING);
        return;
    }
    if (!find_game_exe()) { refresh_status(); return; }

    wchar_t game[MAX_PATH], usr[MAX_PATH], hdd0[MAX_PATH], hdd1[MAX_PATH], lic[MAX_PATH],
            logs[MAX_PATH], eboot[MAX_PATH], tmp[64];
    path_join(game, g_root, L"game");
    path_join(usr, g_root, L"user");
    path_join(hdd0, usr, L"dev_hdd0");
    path_join(hdd1, usr, L"dev_hdd1");
    path_join(lic, game, L"licenses");
    path_join(logs, g_root, L"logs");
    game_path(eboot, L"PS3_GAME\\USRDIR\\EBOOT.elf");
    ensure_dir(hdd0); ensure_dir(hdd1); ensure_dir(logs);

    /* Ambiente lido pelo runtime do ps3recomp e pelo src/main.cpp */
    env_set(L"PS3_VFS_ROOT", game);          /* /dev_bdvd/ -> game\ (PS3_GAME\USRDIR) */
    env_set(L"PS3_HDD0_ROOT", hdd0);         /* saves (cellSaveData) e /dev_hdd0 */
    env_set(L"PS3_HDD1_ROOT", hdd1);         /* cache.dat/cache.idx (cellSysCacheMount) */
    env_set(L"PS3_TITLE", L"Bomberman ULTRA");
    env_set(L"BMU_LICENSE_DIR", lic);

    /* O jogo não usa nada do firmware: as strings flashMP3/flashATRAC.pic
     * existem no EBOOT, mas nenhum código as referencia (todo o áudio do
     * Bman.bnk é ADPCM do SCREAM). Uma pasta vazia própria evita que o
     * runtime procure o dev_flash padrão dele. */
    wchar_t flash[MAX_PATH];
    path_join(flash, usr, L"dev_flash");
    ensure_dir(flash);
    env_set(L"PS3_DEV_FLASH", flash);

    int r = (int)SendMessageW(g_res, CB_GETCURSEL, 0, 0);
    _snwprintf(tmp, 64, L"%d", kRes[r].w); env_set(L"BMU_WIDTH", tmp);
    _snwprintf(tmp, 64, L"%d", kRes[r].h); env_set(L"BMU_HEIGHT", tmp);
    env_set(L"BMU_FULLSCREEN", SendMessageW(g_full, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : nullptr);
    env_set(L"BMU_SHOW_FPS", SendMessageW(g_fps, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");
    env_set(L"BMU_FPS_CAP", SendMessageW(g_cap, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");

    int l = (int)SendMessageW(g_lang, CB_GETCURSEL, 0, 0);
    if (l < 0) l = 0;
    env_set(L"PS3_VFS_REDIRECT", nullptr);
    if (!wcscmp(kLang[l].code, L"ptbr")) {
        if (build_ptbr_zip()) {
            env_set(L"PS3_LANG", L"en");
            env_set(L"PS3_VFS_REDIRECT", L"media.zip=media_ptbr.zip");
        } else {
            MessageBoxW(g_wnd, L"Não foi possível montar a tradução (traducao\\strings_ptbr.xus ou media.zip ausente). "
                               L"O jogo vai abrir em inglês.", APP_TITLE, MB_ICONWARNING);
            env_set(L"PS3_LANG", L"en");
        }
    } else {
        env_set(L"PS3_LANG", wcscmp(kLang[l].code, L"auto") ? kLang[l].code : nullptr);
    }

    int a = (int)SendMessageW(g_audio, CB_GETCURSEL, 0, 0);
    env_set(L"PS3_MUTE", a == 1 ? L"1" : nullptr);
    env_set(L"PS3_NO_AUDIO", a == 2 ? L"1" : nullptr);

    bool console = SendMessageW(g_console, BM_GETCHECK, 0, 0) == BST_CHECKED;

    /* Log detalhado do runtime (cada wait/sinal) derruba o FPS: no Windows o
     * lock do FILE não é justo e as threads do jogo ficam esperando o log.
     * Desligado por padrão; "log_detalhado=1" no launcher.ini liga. */
    env_set(L"PS3_VERBOSE", ini_int(L"log_detalhado", 0) ? L"1" : L"0");

    /* [env] do launcher.ini: variáveis extras repassadas ao jogo, para
     * depuração (ex.: VP_NOFIXPROJ=1, SPU_JOBPROF=5). */
    {
        static wchar_t sect[8192];
        DWORD n = GetPrivateProfileSectionW(L"env", sect, 8192, g_ini);
        for (wchar_t* kv = sect; n && *kv; kv += wcslen(kv) + 1) {
            wchar_t* eq = wcschr(kv, L'=');
            if (!eq || eq == kv) continue;
            *eq = 0;
            env_set(kv, eq[1] ? eq + 1 : nullptr);
            *eq = L'=';
        }
    }

    wchar_t cmd[2 * MAX_PATH + 16];
    _snwprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%s\" \"%s\"", g_game_exe, eboot);

    STARTUPINFOW si; ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof pi);
    HANDLE logf = INVALID_HANDLE_VALUE;
    DWORD flags = 0;
    if (console) {
        flags = CREATE_NEW_CONSOLE;
    } else {
        wchar_t lp[MAX_PATH];
        path_join(lp, logs, L"bomberman.log");
        SECURITY_ATTRIBUTES sa = { sizeof sa, nullptr, TRUE };
        logf = CreateFileW(lp, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (logf != INVALID_HANDLE_VALUE) {
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = logf; si.hStdError = logf; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        }
        flags = CREATE_NO_WINDOW;
    }
    BOOL ok = CreateProcessW(g_game_exe, cmd, nullptr, nullptr, logf != INVALID_HANDLE_VALUE, flags,
                             nullptr, g_root, &si, &pi);
    if (logf != INVALID_HANDLE_VALUE) CloseHandle(logf);
    if (!ok) {
        wchar_t m[300];
        _snwprintf(m, 300, L"Não foi possível iniciar o jogo (erro %lu).", GetLastError());
        MessageBoxW(g_wnd, m, APP_TITLE, MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread);
    g_game_proc = pi.hProcess;
    g_game_killed = false;
    EnableWindow(g_play, TRUE);
    SetWindowTextW(g_play, L"■  ENCERRAR JOGO");
    if (!g_jogar_mode) ShowWindow(g_wnd, SW_MINIMIZE);
    CloseHandle(CreateThread(nullptr, 0, wait_game, nullptr, 0, nullptr));
}

/* Arquivos que podem chegar divididos (limite de transferência de 30 MB).
 * "legacy" é onde a primeira entrega colocou as partes dos assets. */
struct SplitFile { const wchar_t* dst; const wchar_t* legacy; };
static const SplitFile kSplitFiles[] = {
    { L"game\\PS3_GAME\\USRDIR\\media.zip", L"game\\media.zip" },
    { L"game\\PS3_GAME\\USRDIR\\Bman.bnk",  L"game\\Bman.bnk" },
    { L"bin\\BombermanUltra.exe",            nullptr },
    { L"bin\\BombermanUltra_compat.exe",     nullptr },
};

/* Onde estão as partes de um arquivo (no destino ou no local antigo). */
static bool split_source(const SplitFile& f, wchar_t* dst, wchar_t* src)
{
    path_join(dst, g_root, f.dst);
    /* Partes novas ao lado do destino substituem o arquivo atual (atualização
     * do executável); depois de unidas elas são apagadas. */
    if (has_parts(dst)) { wcscpy(src, dst); return true; }
    if (file_exists(dst)) return false;
    if (f.legacy) {
        path_join(src, g_root, f.legacy);
        if (has_parts(src)) return true;
    }
    return false;
}

static bool any_parts_pending(void)
{
    for (size_t i = 0; i < sizeof kSplitFiles / sizeof kSplitFiles[0]; i++) {
        wchar_t d[MAX_PATH], s[MAX_PATH];
        if (split_source(kSplitFiles[i], d, s)) return true;
    }
    return false;
}

/* Um jogo que já está rodando (aberto por outra instância do launcher, ou
 * que sobrou de antes) passa a ser controlado por esta: o botão vira
 * ENCERRAR JOGO e a união de partes espera ele fechar (o exe fica travado). */
static DWORD WINAPI wait_game(LPVOID);
static void adopt_running_game(void)
{
    if (g_game_proc) return;
    wchar_t mine[2][MAX_PATH];
    path_join(mine[0], g_root, L"bin\\BombermanUltra.exe");
    path_join(mine[1], g_root, L"bin\\BombermanUltra_compat.exe");
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe; pe.dwSize = sizeof pe;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"BombermanUltra.exe") && _wcsicmp(pe.szExeFile, L"BombermanUltra_compat.exe"))
            continue;
        HANDLE p = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!p) continue;
        wchar_t path[MAX_PATH]; DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(p, 0, path, &n) &&
            (!_wcsicmp(path, mine[0]) || !_wcsicmp(path, mine[1]))) {
            g_game_proc = p;
            g_game_killed = false;
            SetWindowTextW(g_play, L"■  ENCERRAR JOGO");
            EnableWindow(g_play, TRUE);
            CloseHandle(CreateThread(nullptr, 0, wait_game, nullptr, 0, nullptr));
            break;
        }
        CloseHandle(p);
    }
    CloseHandle(snap);
}

static void prepare_files(void)
{
    if (g_game_proc) return;     /* exe em uso: une quando o jogo fechar */
    SetWindowTextW(g_status, L"Preparando arquivos (unindo partes)\u2026");
    UpdateWindow(g_status);
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    for (size_t i = 0; i < sizeof kSplitFiles / sizeof kSplitFiles[0]; i++) {
        wchar_t d[MAX_PATH], src[MAX_PATH], err[300];
        if (split_source(kSplitFiles[i], d, src) && !join_parts(src, d, err, 300)) {
            MessageBoxW(g_wnd, err, APP_TITLE, MB_ICONERROR);
            break;
        }
    }
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    refresh_status();
}

/* ------------------------------------------------------------------------ */
/* Preparar jogo: importa os arquivos do usuário e compila o port neste PC   */
/* ------------------------------------------------------------------------ */
/* Nada do jogo vem com o port: o usuário aponta o jogo que instalou no RPCS3
 * (PKG europeu + EBOOT decriptado pelo próprio RPCS3), o launcher copia para
 * game\PS3_GAME e roda tools\build_windows.py, que baixa a toolchain e
 * converte esse EBOOT em bin\BombermanUltra.exe. O Python é o pacote
 * "embeddable" oficial (python.org), extraído em toolchain\python. */
static const wchar_t kPyZip[] = L"python-3.12.10-embed-amd64.zip";
static const wchar_t kPyUrl[] = L"https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip";
static const unsigned char kPyMd5[16] = {   /* publicado em python.org/downloads/release/python-31210 */
    0xfe,0x8e,0xf2,0x05,0xf2,0xe9,0xc3,0xba,0x44,0xd0,0xcf,0x99,0x54,0xe1,0xab,0xd3,
};
/* SHA-256 dos segmentos PT_LOAD do EBOOT.elf europeu (o mesmo de build_windows.py). */
static const unsigned char kEbootSegSha256[32] = {
    0x7e,0xb0,0xea,0xfa,0xf4,0x5f,0x4a,0x23,0x8d,0x0f,0x05,0xf2,0xad,0x40,0x45,0x06,
    0xc7,0x0e,0x86,0x42,0x1f,0x37,0xee,0x11,0x7e,0x13,0x01,0x00,0x4d,0x6f,0x49,0x35,
};
static const wchar_t* kGameFiles[] = {   /* relativos a game\PS3_GAME */
    L"USRDIR\\EBOOT.elf", L"USRDIR\\media.zip", L"USRDIR\\Bman.bnk", L"PARAM.SFO",
};

static inline uint16_t be16(const unsigned char* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t be32(const unsigned char* p) { return ((uint32_t)be16(p) << 16) | be16(p + 2); }
static inline uint64_t be64(const unsigned char* p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }

static bool game_files_ready(void)
{
    for (size_t i = 0; i < sizeof kGameFiles / sizeof kGameFiles[0]; i++) {
        wchar_t rel[MAX_PATH], p[MAX_PATH];
        _snwprintf(rel, MAX_PATH, L"PS3_GAME\\%s", kGameFiles[i]);
        game_path(p, rel);
        if (!file_exists(p)) return false;
    }
    return true;
}

/* TITLE_ID do PARAM.SFO (formato PSF, little-endian). */
static bool sfo_title_id(const wchar_t* path, char* out, size_t outn)
{
    std::vector<unsigned char> v;
    if (!read_whole(path, v) || v.size() < 20 || memcmp(v.data(), "\0PSF", 4)) return false;
    const uint32_t kt = rd32(&v[8]), dt = rd32(&v[12]), n = rd32(&v[16]);
    for (uint32_t i = 0; i < n && i < 256; i++) {
        const size_t e = 20 + 16 * (size_t)i;
        if (e + 16 > v.size()) return false;
        const size_t kp = (size_t)kt + rd16(&v[e]);
        const uint32_t len = rd32(&v[e + 4]);
        const size_t dp = (size_t)dt + rd32(&v[e + 12]);
        if (kp >= v.size() || !memchr(&v[kp], 0, v.size() - kp)) return false;
        if (strcmp((const char*)&v[kp], "TITLE_ID")) continue;
        if (!len || dp + len > v.size()) return false;
        size_t m = 0;
        while (m < len && v[dp + m]) m++;
        m = std::min<size_t>(m, outn - 1);
        memcpy(out, &v[dp], m);
        out[m] = 0;
        return true;
    }
    return false;
}

/* O EBOOT.elf é o europeu decriptado? Mesmo hash que build_windows.py confere. */
static bool eboot_is_europe(const wchar_t* path)
{
    std::vector<unsigned char> d;
    if (!read_whole(path, d) || d.size() < 64 || memcmp(d.data(), "\x7f" "ELF", 4) || d[4] != 2) return false;
    const uint64_t phoff = be64(&d[0x20]);
    const unsigned n = be16(&d[0x38]);
    std::vector<unsigned char> buf;
    for (unsigned i = 0; i < n; i++) {
        const uint64_t e = phoff + 56ull * i;
        if (e + 56 > d.size()) return false;
        const uint64_t off = be64(&d[e + 8]), va = be64(&d[e + 16]), fs = be64(&d[e + 32]);
        if (be32(&d[e]) != 1 || !fs) continue;
        if (off > d.size() || fs > d.size() - off) return false;
        for (int k = 7; k >= 0; k--) buf.push_back((unsigned char)(va >> (8 * k)));
        for (int k = 7; k >= 0; k--) buf.push_back((unsigned char)(fs >> (8 * k)));
        buf.insert(buf.end(), d.begin() + (size_t)off, d.begin() + (size_t)(off + fs));
    }
    unsigned char h[32];
    return sha256(buf.data(), buf.size(), h) && !memcmp(h, kEbootSegSha256, 32);
}

static bool pick_folder(const wchar_t* title, wchar_t* out)
{
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return false;
    DWORD o = 0;
    dlg->GetOptions(&o);
    dlg->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(title);
    bool ok = false;
    if (SUCCEEDED(dlg->Show(g_wnd))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(dlg->GetResult(&it))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                wcsncpy(out, p, MAX_PATH - 1);
                out[MAX_PATH - 1] = 0;
                CoTaskMemFree(p);
                ok = true;
            }
            it->Release();
        }
    }
    dlg->Release();
    return ok;
}

/* Aceita a pasta do jogo (a que tem PARAM.SFO), a do RPCS3, dev_hdd0 ou dev_hdd0\game. */
static bool resolve_game_dir(const wchar_t* picked, wchar_t* out)
{
    static const wchar_t* subs[] = { L"", L"NPEB00076", L"game\\NPEB00076", L"dev_hdd0\\game\\NPEB00076", L"PS3_GAME" };
    for (size_t i = 0; i < sizeof subs / sizeof subs[0]; i++) {
        wchar_t d[MAX_PATH], sfo[MAX_PATH];
        if (subs[i][0]) path_join(d, picked, subs[i]); else wcscpy(d, picked);
        path_join(sfo, d, L"PARAM.SFO");
        if (file_exists(sfo)) { wcscpy(out, d); return true; }
    }
    return false;
}

static void reload_banner(bool release_only)
{
    delete g_banner;          /* o GDI+ mantém o PNG aberto */
    g_banner = nullptr;
    if (release_only) return;
    wchar_t p[MAX_PATH];
    game_path(p, L"PS3_GAME\\ICON0.PNG");
    g_banner = new Gdiplus::Image(p);
    InvalidateRect(g_wnd, nullptr, FALSE);
}

/* Copia src para game\PS3_GAME\dst_rel (nada a fazer se já for o mesmo arquivo). */
static bool copy_game_file(const wchar_t* src, const wchar_t* dst_rel)
{
    wchar_t rel[MAX_PATH], dst[MAX_PATH], fs[MAX_PATH], fd[MAX_PATH];
    _snwprintf(rel, MAX_PATH, L"PS3_GAME\\%s", dst_rel);
    game_path(dst, rel);
    if (GetFullPathNameW(src, MAX_PATH, fs, nullptr) && GetFullPathNameW(dst, MAX_PATH, fd, nullptr) && !_wcsicmp(fs, fd))
        return true;
    wchar_t dir[MAX_PATH];
    wcscpy(dir, dst);
    if (wchar_t* s = wcsrchr(dir, L'\\')) *s = 0;
    ensure_dir(dir);
    return CopyFileW(src, dst, FALSE) != FALSE;
}

static bool import_game(void)
{
    if (MessageBoxW(g_wnd,
            L"Primeiro, aponte o SEU Bomberman ULTRA europeu (NPEB00076).\n\n"
            L"Instale a licença e o PKG no RPCS3 (File \u2192 Install Packages/Raps/Edats) e escolha, na próxima janela, "
            L"a pasta do RPCS3 ou a pasta dev_hdd0\\game\\NPEB00076.\n\n"
            L"Os arquivos são copiados para a pasta game deste port; nada sai do seu PC.",
            APP_TITLE, MB_ICONINFORMATION | MB_OKCANCEL) != IDOK)
        return false;
    wchar_t picked[MAX_PATH], gd[MAX_PATH], p[MAX_PATH];
    if (!pick_folder(L"Pasta do RPCS3 ou dev_hdd0\\game\\NPEB00076", picked)) return false;
    if (!resolve_game_dir(picked, gd)) {
        MessageBoxW(g_wnd, L"Não achei o jogo nessa pasta (falta PARAM.SFO).\n\n"
                           L"Escolha a pasta do RPCS3 ou dev_hdd0\\game\\NPEB00076.", APP_TITLE, MB_ICONWARNING);
        return false;
    }
    char tid[32] = "";
    path_join(p, gd, L"PARAM.SFO");
    if (!sfo_title_id(p, tid, sizeof tid) || strcmp(tid, "NPEB00076")) {
        wchar_t msg[400];
        _snwprintf(msg, 400, L"Esse é outro jogo ou outra região (TITLE_ID %S).\n\n"
                             L"O port só funciona com o Bomberman ULTRA europeu, NPEB00076.", tid[0] ? tid : "?");
        MessageBoxW(g_wnd, msg, APP_TITLE, MB_ICONWARNING);
        return false;
    }
    wchar_t usr[MAX_PATH], media[MAX_PATH], bnk[MAX_PATH], eboot[MAX_PATH];
    path_join(usr, gd, L"USRDIR");
    path_join(media, usr, L"media.zip");
    path_join(bnk, usr, L"Bman.bnk");
    if (!file_exists(media) || !file_exists(bnk)) {
        MessageBoxW(g_wnd, L"Faltam media.zip e/ou Bman.bnk em USRDIR. O PKG foi instalado por completo?",
                    APP_TITLE, MB_ICONWARNING);
        return false;
    }
    path_join(eboot, usr, L"EBOOT.elf");
    if (!file_exists(eboot)) {
        if (MessageBoxW(g_wnd,
                L"Falta o EBOOT decriptado (EBOOT.elf).\n\n"
                L"No RPCS3: Utilities \u2192 Decrypt PS3 Binaries "
                L"e escolha o EBOOT.BIN da pasta USRDIR do jogo. O RPCS3 usa a sua licença .rap instalada nele "
                L"e grava o EBOOT.elf ao lado do EBOOT.BIN.\n\n"
                L"Depois clique em OK e selecione o EBOOT.elf.",
                APP_TITLE, MB_ICONINFORMATION | MB_OKCANCEL) != IDOK)
            return false;
        wchar_t file[MAX_PATH] = L"";
        OPENFILENAMEW ofn; ZeroMemory(&ofn, sizeof ofn);
        ofn.lStructSize = sizeof ofn;
        ofn.hwndOwner = g_wnd;
        ofn.lpstrFilter = L"EBOOT decriptado (*.elf)\0*.elf\0Todos os arquivos\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrInitialDir = usr;
        ofn.lpstrTitle = L"Selecione o EBOOT.elf decriptado pelo RPCS3";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn)) return false;
        wcscpy(eboot, file);
    }
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    const bool eu = eboot_is_europe(eboot);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    if (!eu) {
        MessageBoxW(g_wnd, L"Esse EBOOT.elf não é o do Bomberman ULTRA europeu (NPEB00076, versão 01.00) "
                           L"decriptado. Decripte o EBOOT.BIN dessa versão pelo RPCS3 e tente de novo.",
                    APP_TITLE, MB_ICONWARNING);
        return false;
    }
    SetWindowTextW(g_status, L"Copiando os arquivos do jogo\u2026");
    UpdateWindow(g_status);
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    reload_banner(true);
    wchar_t sfo[MAX_PATH], icon[MAX_PATH];
    path_join(sfo, gd, L"PARAM.SFO");
    path_join(icon, gd, L"ICON0.PNG");
    bool ok = copy_game_file(sfo, L"PARAM.SFO") && copy_game_file(media, L"USRDIR\\media.zip") &&
              copy_game_file(bnk, L"USRDIR\\Bman.bnk") && copy_game_file(eboot, L"USRDIR\\EBOOT.elf");
    if (ok && file_exists(icon)) copy_game_file(icon, L"ICON0.PNG");   /* opcional: banner e ícone */
    if (ok && !license_ok()) {
        /* O RPCS3 guarda as licenças em dev_hdd0\home\<usuário>\exdata. */
        wchar_t home[MAX_PATH], q[MAX_PATH];
        path_join(home, gd, L"..\\..\\home");
        path_join(q, home, L"*");
        WIN32_FIND_DATAW fd;
        HANDLE fh = FindFirstFileW(q, &fd);
        if (fh != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
                wchar_t u[MAX_PATH], ex[MAX_PATH];
                path_join(u, home, fd.cFileName);
                path_join(ex, u, L"exdata");
                if (license_search_dir(ex)) break;
            } while (FindNextFileW(fh, &fd));
            FindClose(fh);
        }
    }
    reload_banner(false);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    if (!ok) {
        MessageBoxW(g_wnd, L"Não consegui copiar os arquivos para a pasta game (disco cheio ou sem permissão?).",
                    APP_TITLE, MB_ICONERROR);
        return false;
    }
    return true;
}

/* ---- PKG do usuário: o build_windows.py extrai e decripta (sem RPCS3) ---- */
static wchar_t g_pkg[MAX_PATH];   /* PKG escolhido para este "Preparar jogo" ("" = nenhum) */

/* Lê o content ID do cabeçalho do PKG (texto puro em 0x30). */
static bool pkg_content_id(const wchar_t* path, char* cid, size_t n)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    unsigned char hd[0x80]; DWORD got = 0;
    const BOOL rd = ReadFile(h, hd, sizeof hd, &got, nullptr);
    CloseHandle(h);
    if (!rd || got != sizeof hd || memcmp(hd, "\x7fPKG", 4)) return false;
    size_t m = 0;
    while (m < 0x30 && hd[0x30 + m] >= 0x20 && hd[0x30 + m] < 0x7f) m++;
    m = std::min(m, n - 1);
    memcpy(cid, hd + 0x30, m);
    cid[m] = 0;
    return true;
}
static bool pkg_is_ours(const wchar_t* path)
{
    char cid[64];
    return pkg_content_id(path, cid, sizeof cid) && !strcmp(cid, "EP0555-NPEB00076_00-BOMBERMANGAME001");
}
static bool pkg_search_dir(const wchar_t* dir, wchar_t* out)
{
    wchar_t q[MAX_PATH];
    path_join(q, dir, L"*.pkg");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(q, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        path_join(out, dir, fd.cFileName);
        if (pkg_is_ours(out)) { found = true; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}
/* Mesmos lugares da licença: pasta do launcher e até 3 acima, Downloads,
 * Área de Trabalho e Documentos. */
static bool pkg_autofind(wchar_t* out)
{
    wchar_t d[MAX_PATH];
    wcscpy(d, g_root);
    for (int up = 0; up < 4; up++) {
        if (pkg_search_dir(d, out)) return true;
        wchar_t* sl = wcsrchr(d, L'\\');
        if (!sl || sl == d + 2) break;
        *sl = 0;
    }
    static const KNOWNFOLDERID* kf[] = { &FOLDERID_Downloads, &FOLDERID_Desktop, &FOLDERID_Documents };
    for (const KNOWNFOLDERID* id : kf) {
        PWSTR k = nullptr;
        if (SHGetKnownFolderPath(*id, 0, nullptr, &k) == S_OK) {
            const bool f = pkg_search_dir(k, out);
            CoTaskMemFree(k);
            if (f) return true;
        }
    }
    return false;
}

/* Localiza (ou deixa o usuário selecionar) o PKG europeu; grava em g_pkg. */
static bool pick_pkg(void)
{
    wchar_t found[MAX_PATH];
    if (pkg_autofind(found)) {
        std::wstring msg = L"Encontrei o PKG do Bomberman ULTRA europeu:\n\n";
        msg += found;
        msg += L"\n\nUsar este arquivo? (Não = escolher outro)";
        const int r = MessageBoxW(g_wnd, msg.c_str(), APP_TITLE, MB_ICONQUESTION | MB_YESNOCANCEL);
        if (r == IDCANCEL) return false;
        if (r == IDYES) { wcscpy(g_pkg, found); return true; }
    }
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn; ZeroMemory(&ofn, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_wnd;
    ofn.lpstrFilter = L"PKG do PS3 (*.pkg)\0*.pkg\0Todos os arquivos\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Selecione o PKG do Bomberman ULTRA europeu (NPEB00076)";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;
    char cid[64] = "";
    if (!pkg_content_id(file, cid, sizeof cid)) {
        MessageBoxW(g_wnd, L"Esse arquivo não é um PKG de PS3.", APP_TITLE, MB_ICONWARNING);
        return false;
    }
    if (strcmp(cid, "EP0555-NPEB00076_00-BOMBERMANGAME001")) {
        wchar_t msg[400];
        _snwprintf(msg, 400, L"Esse PKG é de outro jogo ou outra região (%S).\n\n"
                             L"O port só funciona com o Bomberman ULTRA europeu:\nEP0555-NPEB00076_00-BOMBERMANGAME001",
                   cid[0] ? cid : "?");
        MessageBoxW(g_wnd, msg, APP_TITLE, MB_ICONWARNING);
        return false;
    }
    wcscpy(g_pkg, file);
    return true;
}

/* De onde vêm os arquivos do jogo: 1 = PKG, 2 = RPCS3, 0 = cancelado. */
static int choose_import_method(void)
{
    enum { BTN_PKG = 101, BTN_RPCS3 = 102 };
    TASKDIALOG_BUTTON btns[] = {
        { BTN_PKG,   L"Selecionar o PKG do jogo (recomendado)\n"
                     L"O launcher extrai e decripta o PKG com a sua licença .rap. Não precisa do RPCS3." },
        { BTN_RPCS3, L"Usar o jogo instalado no RPCS3\n"
                     L"Pasta dev_hdd0\\game\\NPEB00076, com o EBOOT.elf decriptado pelo RPCS3." },
    };
    TASKDIALOGCONFIG tc; ZeroMemory(&tc, sizeof tc);
    tc.cbSize = sizeof tc;
    tc.hwndParent = g_wnd;
    tc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
    tc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    tc.pszWindowTitle = APP_TITLE;
    tc.pszMainIcon = TD_INFORMATION_ICON;
    tc.pszMainInstruction = L"De onde vêm os arquivos do jogo?";
    tc.pszContent = L"Você precisa do Bomberman ULTRA europeu (NPEB00076) e da licença "
                    L"EP0555-NPEB00076_00-BOMBERMANGAME001.rap. Nada sai do seu PC.";
    tc.pButtons = btns;
    tc.cButtons = 2;
    tc.nDefaultButton = BTN_PKG;
    int pressed = 0;
    if (SUCCEEDED(TaskDialogIndirect(&tc, &pressed, nullptr, nullptr)))
        return pressed == BTN_PKG ? 1 : pressed == BTN_RPCS3 ? 2 : 0;
    /* Sem TaskDialog (não deveria acontecer no Windows 10/11): caixa simples. */
    const int r = MessageBoxW(g_wnd, L"Selecionar o PKG do jogo?\n\nSim = PKG (sem RPCS3)\nNão = jogo instalado no RPCS3",
                              APP_TITLE, MB_ICONQUESTION | MB_YESNOCANCEL);
    return r == IDYES ? 1 : r == IDNO ? 2 : 0;
}

static bool file_md5_is(const wchar_t* path, const unsigned char want[16])
{
    std::vector<unsigned char> d;
    unsigned char h[16];
    return read_whole(path, d) && bcrypt_hash(BCRYPT_MD5_ALGORITHM, d.data(), d.size(), h, 16) && !memcmp(h, want, 16);
}

static DWORD run_wait(wchar_t* cmdline, const wchar_t* cwd, DWORD flags, const wchar_t* title = nullptr)
{
    STARTUPINFOW si; ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    si.lpTitle = (LPWSTR)title;
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(nullptr, cmdline, nullptr, nullptr, FALSE, flags, nullptr, cwd, &si, &pi)) return (DWORD)-1;
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return code;
}

/* Thread do "Preparar jogo": Python embutido + build_windows.py num console. */
static DWORD WINAPI build_thread(LPVOID)
{
    wchar_t tc[MAX_PATH], dl[MAX_PATH], py[MAX_PATH], py_exe[MAX_PATH], zip[MAX_PATH];
    path_join(tc, g_root, L"toolchain");
    path_join(dl, tc, L"downloads");
    path_join(py, tc, L"python");
    path_join(py_exe, py, L"python.exe");
    path_join(zip, dl, kPyZip);
    ensure_dir(dl);
    if (!file_exists(py_exe)) {
        SetWindowTextW(g_status, L"Baixando o Python embutido (11 MB, python.org)\u2026");
        if (!file_exists(zip) || !file_md5_is(zip, kPyMd5)) {
            DeleteFileW(zip);
            DeleteUrlCacheEntryW(kPyUrl);
            if (FAILED(URLDownloadToFileW(nullptr, kPyUrl, zip, 0, nullptr))) {
                PostMessageW(g_wnd, WM_APP_BUILD_DONE, 1, (LPARAM)L"Não consegui baixar o Python de python.org. "
                                                                    L"Confira a conexão (ou o antivírus/firewall) e tente de novo.");
                return 0;
            }
        }
        if (!file_md5_is(zip, kPyMd5)) {
            DeleteFileW(zip);
            PostMessageW(g_wnd, WM_APP_BUILD_DONE, 1, (LPARAM)L"O Python baixado não confere (MD5). Tente de novo.");
            return 0;
        }
        ensure_dir(py);
        wchar_t sys32[MAX_PATH], cmd[3 * MAX_PATH + 64];
        GetSystemDirectoryW(sys32, MAX_PATH);
        _snwprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%s\\tar.exe\" -xf \"%s\" -C \"%s\"", sys32, zip, py);
        if (run_wait(cmd, py, CREATE_NO_WINDOW) != 0 || !file_exists(py_exe)) {
            PostMessageW(g_wnd, WM_APP_BUILD_DONE, 1, (LPARAM)L"Não consegui extrair o Python (o tar.exe do Windows 10/11 "
                                                                L"não está disponível?).");
            return 0;
        }
    }
    /* O Python embutido ignora PYTHONPATH: os caminhos de import vêm do ._pth. */
    wchar_t pth[MAX_PATH];
    path_join(pth, py, L"python312._pth");
    std::wstring t = L"python312.zip\r\n.\r\n";
    t += std::wstring(g_root) + L"\\tools\r\n";
    t += std::wstring(tc) + L"\\src\\ps3recomp\\tools\r\n";
    const std::string u = to_utf8(t.c_str());
    write_whole(pth, std::vector<unsigned char>(u.begin(), u.end()));

    SetWindowTextW(g_status, L"Compilando o jogo neste PC\u2026 acompanhe na janela do console.");
    wchar_t cmd[5 * MAX_PATH];
    _snwprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%s\" -X utf8 \"%s\\tools\\build_windows.py\" --root \"%s\" --pause%s%s%s",
               py_exe, g_root, g_root, g_pkg[0] ? L" --pkg \"" : L"", g_pkg, g_pkg[0] ? L"\"" : L"");
    const DWORD code = run_wait(cmd, g_root, CREATE_NEW_CONSOLE, L"Bomberman ULTRA \u2014 Preparar jogo");
    if (code == (DWORD)-1)
        PostMessageW(g_wnd, WM_APP_BUILD_DONE, 1, (LPARAM)L"Não consegui iniciar o Python embutido.");
    else
        PostMessageW(g_wnd, WM_APP_BUILD_DONE, code, 0);
    return 0;
}

static void prepare_game(void)
{
    if (any_parts_pending()) { prepare_files(); return; }
    if (g_building) return;
    if (g_game_proc) {
        MessageBoxW(g_wnd, L"Feche o jogo antes de compilar.", APP_TITLE, MB_ICONINFORMATION);
        return;
    }
    wchar_t p[MAX_PATH];
    path_join(p, g_root, L"tools\\build_windows.py");
    if (!file_exists(p)) {
        MessageBoxW(g_wnd, L"Falta tools\\build_windows.py. Extraia o pacote do port inteiro, não só o launcher.",
                    APP_TITLE, MB_ICONERROR);
        return;
    }
    g_pkg[0] = 0;
    bool need_import = !game_files_ready();
    if (!need_import) {
        game_path(p, L"PS3_GAME\\USRDIR\\EBOOT.elf");
        if (!eboot_is_europe(p)) {
            MessageBoxW(g_wnd, L"O EBOOT.elf na pasta game não é o europeu decriptado. Aponte o jogo de novo.",
                        APP_TITLE, MB_ICONWARNING);
            need_import = true;
        }
    }
    if (need_import) {
        const int how = choose_import_method();
        if (how == 0) return;
        if (how == 1 ? !pick_pkg() : !import_game()) { refresh_status(); return; }
    }
    refresh_status();
    if (!license_ok() && !license_autofind()) {
        MessageBoxW(g_wnd, L"Para compilar e jogar é preciso a licença da versão europeia:\n"
                           L"EP0555-NPEB00076_00-BOMBERMANGAME001.rap\n\nSelecione o arquivo na próxima janela.",
                    APP_TITLE, MB_ICONINFORMATION);
        license_pick();
        refresh_status();
        if (!license_ok()) return;
    }

    std::wstring msg;
    if (g_pkg[0])
        msg += L"O PKG será extraído e decriptado com a sua licença para a pasta game (as chaves públicas do PS3 "
               L"vêm do código-fonte do RPCS3 no GitHub, conferidas por hash). Depois o jogo é compilado.\n\n";
    else if (find_game_exe())
        msg += L"O jogo já está compilado. Compilar de novo (por exemplo, depois de atualizar o port)?\n\n";
    msg += L"O launcher vai baixar as ferramentas de compilação (Python, CMake, Ninja, llvm-mingw e os códigos do "
           L"ps3recomp, SDL2 e glslang: cerca de 350 MB, só na primeira vez) e converter o SEU jogo em um "
           L"executável para este PC.\n\n"
           L"\u2022 Leva de 10 a 40 minutos, conforme o processador.\n"
           L"\u2022 Tudo fica na pasta toolchain; nada é instalado no Windows.\n";
    ULARGE_INTEGER freeb;
    if (GetDiskFreeSpaceExW(g_root, &freeb, nullptr, nullptr) && freeb.QuadPart < (6ull << 30))
        msg += L"\u2022 Atenção: há menos de 6 GB livres neste disco; o build pode não caber.\n";
    if (wcslen(g_root) > 80)
        msg += L"\u2022 Atenção: a pasta está num caminho longo. Se o build falhar, mova-a para algo como C:\\Jogos\\BombermanUltra.\n";
    msg += L"\nContinuar?";
    if (MessageBoxW(g_wnd, msg.c_str(), APP_TITLE, MB_ICONQUESTION | MB_YESNO) != IDYES) return;

    if (g_pkg[0]) reload_banner(true);   /* o ICON0.PNG vai ser gravado de novo */
    g_building = true;
    EnableWindow(GetDlgItem(g_wnd, IDC_PREPARE), FALSE);
    refresh_status();
    CloseHandle(CreateThread(nullptr, 0, build_thread, nullptr, 0, nullptr));
}

static void open_in_explorer(const wchar_t* rel, bool is_file)
{
    wchar_t p[MAX_PATH];
    path_join(p, g_root, rel);
    if (!is_file) ensure_dir(p);
    else if (!file_exists(p)) { MessageBoxW(g_wnd, L"Ainda não há log. Jogue uma vez.", APP_TITLE, MB_ICONINFORMATION); return; }
    ShellExecuteW(g_wnd, L"open", p, nullptr, nullptr, SW_SHOWNORMAL);
}

/* ------------------------------------------------------------------------ */
/* Interface                                                                */
/* ------------------------------------------------------------------------ */
static HWND mk(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT f)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_wnd,
                             (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)(f ? f : g_font), TRUE);
    return c;
}

static void build_ui(void)
{
    const int L = 24, top = BANNER_H + 16;
    mk(L"STATIC", L"VÍDEO", 0, L, top, 200, 18, -1, g_font_bold);
    mk(L"STATIC", L"Resolução de renderização", 0, L, top + 26, 200, 18, -1, nullptr);
    g_res = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, L, top + 46, 300, 200, IDC_RES, nullptr);
    for (size_t i = 0; i < sizeof kRes / sizeof kRes[0]; i++) SendMessageW(g_res, CB_ADDSTRING, 0, (LPARAM)kRes[i].label);
    g_full = mk(L"BUTTON", L"Tela cheia (janela sem bordas)", BS_AUTOCHECKBOX | WS_TABSTOP, L, top + 80, 300, 22, IDC_FULLSCREEN, nullptr);

    mk(L"STATIC", L"JOGO", 0, L, top + 120, 200, 18, -1, g_font_bold);
    mk(L"STATIC", L"Idioma", 0, L, top + 146, 140, 18, -1, nullptr);
    g_lang = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, L, top + 166, 145, 200, IDC_LANG, nullptr);
    for (size_t i = 0; i < sizeof kLang / sizeof kLang[0]; i++) SendMessageW(g_lang, CB_ADDSTRING, 0, (LPARAM)kLang[i].label);
    mk(L"STATIC", L"Áudio", 0, L + 155, top + 146, 140, 18, -1, nullptr);
    g_audio = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, L + 155, top + 166, 145, 200, IDC_AUDIO, nullptr);
    for (int i = 0; i < 3; i++) SendMessageW(g_audio, CB_ADDSTRING, 0, (LPARAM)kAudio[i]);

    g_console = mk(L"BUTTON", L"Mostrar console de depuração",
                   BS_AUTOCHECKBOX | WS_TABSTOP, L, top + 212, 330, 22, IDC_CONSOLE, nullptr);
    g_fps = mk(L"BUTTON", L"Mostrar contador de FPS",
               BS_AUTOCHECKBOX | WS_TABSTOP, L, top + 238, 330, 22, IDC_FPS, nullptr);
    g_cap = mk(L"BUTTON", L"Limitar a 60 FPS (velocidade original do PS3)",
               BS_AUTOCHECKBOX | WS_TABSTOP, L, top + 264, 330, 22, IDC_CAP, nullptr);

    const int R = 360;
    mk(L"STATIC", L"VERIFICAÇÃO", 0, R, top, 200, 18, -1, g_font_bold);
    g_status = mk(L"STATIC", L"", SS_LEFT, R, top + 26, WIN_W - R - 36, 170, IDC_STATUS, nullptr);
    mk(L"BUTTON", L"Preparar jogo", WS_TABSTOP, R, top + 204, 160, 30, IDC_PREPARE, nullptr);
    mk(L"BUTTON", L"Localizar licença", WS_TABSTOP, R + 168, top + 204, 160, 30, IDC_LICENSE, nullptr);
    mk(L"BUTTON", L"Pasta de saves", WS_TABSTOP, R, top + 240, 160, 30, IDC_SAVES, nullptr);
    mk(L"BUTTON", L"Adicionar à Steam", WS_TABSTOP, R + 168, top + 240, 160, 30, IDC_STEAM, nullptr);
    mk(L"BUTTON", L"Abrir log", WS_TABSTOP, R, top + 276, 160, 30, IDC_LOG, nullptr);
    mk(L"BUTTON", L"Sair", WS_TABSTOP, R + 168, top + 276, 160, 30, IDC_QUIT, nullptr);

    /* Com visual styles o texto de checkbox ignora WM_CTLCOLORSTATIC e fica
     * preto sobre o fundo escuro; sem tema ele respeita a cor. */
    SetWindowTheme(g_full, L"", L"");
    SetWindowTheme(g_console, L"", L"");
    SetWindowTheme(g_fps, L"", L"");
    SetWindowTheme(g_cap, L"", L"");

    g_play = mk(L"BUTTON", L"▶  JOGAR", BS_DEFPUSHBUTTON | WS_TABSTOP, R, top + 322, 328, 46, IDC_PLAY, g_font_title);
}

static void paint_banner(HDC hdc, const RECT& rc)
{
    using namespace Gdiplus;
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    LinearGradientBrush grad(Point(0, 0), Point(rc.right, BANNER_H), Color(255, 40, 24, 80), Color(255, 12, 70, 140));
    g.FillRectangle(&grad, 0, 0, rc.right, BANNER_H);
    int x = 24;
    if (g_banner && g_banner->GetLastStatus() == Ok) {
        int ih = BANNER_H - 32, iw = (int)(g_banner->GetWidth() * (double)ih / g_banner->GetHeight());
        g.DrawImage(g_banner, 24, 16, iw, ih);
        x = 24 + iw + 24;
    }
    FontFamily ff(L"Segoe UI");
    Font big(&ff, 26, FontStyleBold, UnitPixel), small(&ff, 14, FontStyleRegular, UnitPixel);
    SolidBrush yellow(Color(255, 255, 196, 0)), white(Color(255, 235, 238, 245)), dim(Color(200, 200, 210, 230));
    g.DrawString(L"Bomberman ULTRA", -1, &big, PointF((REAL)x, 34), &yellow);
    g.DrawString(L"Port PC — recompilação estática do PS3 (NPEB00076 · ps3recomp)", -1, &small, PointF((REAL)x, 74), &white);
    g.DrawString(L"Hudson Soft · 2009 · até 4 jogadores locais", -1, &small, PointF((REAL)x, 96), &dim);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE:
        g_wnd = h;
        build_ui();
        load_settings();
        license_autofind();
        adopt_running_game();
        refresh_status();
        if (g_jogar_mode) { PostMessageW(h, WM_APP_JOGAR, 0, 0); return 0; }
        /* Primeira execução: une as partes sozinho, sem precisar de clique. */
        if (any_parts_pending()) PostMessageW(h, WM_APP_PREPARE, 0, 0);
        return 0;
    case WM_APP_JOGAR:
        /* --jogar (atalho da Steam): sem janela; abre o jogo e sai junto. */
        if (!g_game_proc) {
            if (any_parts_pending()) prepare_files();
            launch_game();
        }
        if (!g_game_proc) DestroyWindow(h);
        return 0;
    case WM_APP_PREPARE:
        prepare_files();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        FillRect(dc, &rc, g_bg_brush);
        paint_banner(dc, rc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)wp;
        SetBkColor(dc, kBg);
        SetTextColor(dc, (HWND)lp == g_status ? kText : kDim);
        return (LRESULT)g_bg_brush;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wp;
        SetBkColor(dc, kPanel);
        SetTextColor(dc, kText);
        return (LRESULT)g_panel_brush;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_PLAY:
            if (g_game_proc) {   /* em execução: o botão encerra o jogo */
                if (MessageBoxW(h, L"Encerrar o jogo agora? O progresso não salvo será perdido.",
                                APP_TITLE, MB_ICONQUESTION | MB_YESNO) == IDYES) {
                    g_game_killed = true;
                    TerminateProcess(g_game_proc, 0);
                }
            } else {
                launch_game();
            }
            break;
        case IDC_PREPARE:      prepare_game(); break;
        case IDC_LICENSE:      license_pick(); refresh_status(); break;
        case IDC_STEAM:        steam_add(); break;
        case IDC_SAVES:        open_in_explorer(L"user\\dev_hdd0", false); break;
        case IDC_LOG:          open_in_explorer(L"logs\\bomberman.log", true); break;
        case IDC_QUIT:         DestroyWindow(h); break;
        default:               if (HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == BN_CLICKED) save_settings(); break;
        }
        return 0;
    case WM_APP_GAME_EXIT: {
        CloseHandle(g_game_proc);
        g_game_proc = nullptr;
        if (g_jogar_mode) {          /* atalho da Steam: fecha junto com o jogo */
            if (any_parts_pending()) prepare_files();
            DestroyWindow(h);
            return 0;
        }
        if (any_parts_pending()) PostMessageW(h, WM_APP_PREPARE, 0, 0);
        SetWindowTextW(g_play, L"▶  JOGAR");
        ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
        refresh_status();
        if ((int)wp != 0 && !g_game_killed) {
            wchar_t msg[300];
            _snwprintf(msg, 300, L"O jogo terminou com código %d (0x%08X).\nAbrir o log?", (int)wp, (unsigned)wp);
            if (MessageBoxW(h, msg, APP_TITLE, MB_ICONWARNING | MB_YESNO) == IDYES)
                open_in_explorer(L"logs\\bomberman.log", true);
        }
        return 0;
    }
    case WM_APP_BUILD_DONE: {
        g_building = false;
        reload_banner(false);   /* o ICON0.PNG pode ter vindo do PKG agora */
        EnableWindow(GetDlgItem(h, IDC_PREPARE), TRUE);
        refresh_status();
        ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
        if (lp) {
            MessageBoxW(h, (const wchar_t*)lp, APP_TITLE, MB_ICONERROR);
        } else if (wp == 0 && find_game_exe()) {
            MessageBoxW(h, L"Pronto! O jogo foi compilado neste PC a partir dos seus arquivos.\n\nClique em JOGAR.",
                        APP_TITLE, MB_ICONINFORMATION);
        } else if ((int)wp != 130) {
            wchar_t msg[300];
            _snwprintf(msg, 300, L"A compilação parou (código %d).\n\nAbrir o log (toolchain\\build.log)? "
                                 L"Clicar em “Preparar jogo” de novo continua de onde parou.", (int)wp);
            if (MessageBoxW(h, msg, APP_TITLE, MB_ICONWARNING | MB_YESNO) == IDYES)
                open_in_explorer(L"toolchain\\build.log", true);
        }
        return 0;
    }
    case WM_CLOSE:
        if (g_building && MessageBoxW(h, L"A compilação continua na janela do console mesmo com o launcher fechado.\n\n"
                                         L"Fechar o launcher?", APP_TITLE, MB_ICONQUESTION | MB_YESNO) != IDYES)
            return 0;
        save_settings();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show)
{
    g_inst = inst;
    g_jogar_mode = wcsstr(GetCommandLineW(), L"--jogar") != nullptr;
    /* Instância única: abrir de novo traz a janela que já existe. */
    if (HWND other = g_jogar_mode ? nullptr : FindWindowW(L"BombermanUltraLauncher", nullptr)) {
        ShowWindow(other, SW_RESTORE);
        SetForegroundWindow(other);
        return 0;
    }
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&g_gdiplus, &gsi, nullptr);

    GetModuleFileNameW(nullptr, g_root, MAX_PATH);
    wchar_t* slash = wcsrchr(g_root, L'\\');
    if (slash) *slash = 0;
    path_join(g_ini, g_root, L"launcher.ini");

    wchar_t icon_png[MAX_PATH];
    game_path(icon_png, L"PS3_GAME\\ICON0.PNG");
    g_banner = new Gdiplus::Image(icon_png);

    g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_bold = CreateFontW(-13, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_title = CreateFontW(-20, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_bg_brush = CreateSolidBrush(kBg);
    g_panel_brush = CreateSolidBrush(kPanel);

    WNDCLASSEXW wc; ZeroMemory(&wc, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = g_bg_brush;
    wc.lpszClassName = L"BombermanUltraLauncher";
    RegisterClassExW(&wc);

    RECT r = { 0, 0, WIN_W, WIN_H };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    int w = r.right - r.left, hgt = r.bottom - r.top;
    HWND h = CreateWindowExW(0, wc.lpszClassName, APP_TITLE, style,
                             (GetSystemMetrics(SM_CXSCREEN) - w) / 2, (GetSystemMetrics(SM_CYSCREEN) - hgt) / 2,
                             w, hgt, nullptr, nullptr, inst, nullptr);
    if (!g_jogar_mode) {
        ShowWindow(h, show);
        UpdateWindow(h);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    delete g_banner;
    Gdiplus::GdiplusShutdown(g_gdiplus);
    CoUninitialize();
    return 0;
}
