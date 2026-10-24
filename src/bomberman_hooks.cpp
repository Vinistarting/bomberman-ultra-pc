/*
 * Bomberman ULTRA (NPEB00076) -- hooks específicos do port.
 *
 * O EBOOT importa 213 funções de 16 módulos (ver analysis/imports.json). O
 * ps3recomp já responde 182 delas; as 31 abaixo não existem no runtime e, sem
 * handler, cairiam no "UNRESOLVED NID -> faking CELL_OK" genérico, que não
 * escreve os parâmetros de saída. Aqui elas ganham respostas coerentes com um
 * console offline, seguindo o comportamento do RPCS3 (rpcs3/Emu/Cell/Modules).
 *
 * Política do port:
 *   - Voice chat (cellSysutilAvc2): no-op com sucesso, câmera desconectada.
 *   - PSN (sceNp, sceNp2/Matching2): offline. O multiplayer local (4 controles) não
 *     depende disso; o modo "Live" do menu deve falhar com elegância.
 *   - DRM de upgrade (trial -> completo): sceNpDrmVerifyUpgradeLicense só
 *     responde OK se existir <BMU_LICENSE_DIR>/<content_id>.rap, ou seja, se o
 *     usuário tiver a licença daquele conteúdo -- mesma regra do RPCS3.
 *
 * Os NIDs são SHA1(nome + sufixo PS3)[0..3] little-endian e batem com os
 * resolvidos em analysis/imports.json.
 */
#include "ppu_recomp.h"     /* ppu_context (gerado pelo lifter) */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern "C" {
extern uint8_t* vm_base;   /* main.cpp */
typedef void (*hle_ctx_fn)(ppu_context*);
void ps3_hle_register_ctx(uint32_t nid, const char* name, hle_ctx_fn fn);
}

/* ---------------------------------------------------------------------------
 * Acesso à memória do guest (big-endian)
 * -----------------------------------------------------------------------*/
static inline void g_w8(uint32_t a, uint8_t v)   { if (a) vm_base[a] = v; }
static inline void g_w32(uint32_t a, uint32_t v)
{
    if (!a) return;
    vm_base[a] = (uint8_t)(v >> 24); vm_base[a + 1] = (uint8_t)(v >> 16);
    vm_base[a + 2] = (uint8_t)(v >> 8); vm_base[a + 3] = (uint8_t)v;
}
static void g_str(uint32_t a, char* out, size_t n)
{
    size_t i = 0;
    if (a) for (; i + 1 < n && vm_base[a + i]; i++) out[i] = (char)vm_base[a + i];
    out[i] = 0;
}

#define ARG(i)  ((uint32_t)ctx->gpr[3 + (i)])
#define RET(v)  (ctx->gpr[3] = (uint64_t)(uint32_t)(v))

/* Códigos de erro (valores do RPCS3) */
enum : uint32_t {
    CELL_OK                                   = 0,
    SCE_NP_ERROR_OFFLINE                      = 0x8002aa0c,
    SCE_NP_BASIC_ERROR_NOT_CONNECTED          = 0x8002a678,
    SCE_NP_DRM_ERROR_LICENSE_NOT_FOUND        = 0x80029521,
    SCE_NP_DRM_ERROR_INVALID_PARAM            = 0x80029502,
    SCE_NP_MATCHING2_ERROR_CONTEXT_NOT_STARTED = 0x80022308,
    CELL_AVC2_ERROR_INVALID_ARGUMENT          = 0x8002b705,
};

static int g_log = -1;
static void hook_log(const char* name, const char* extra = "")
{
    if (g_log < 0) g_log = getenv("BMU_HOOK_LOG") ? 1 : 0;
    static int once_budget = 64;
    if (g_log || once_budget > 0) {
        if (!g_log) once_budget--;
        fprintf(stderr, "[bmu] %s %s\n", name, extra);
    }
}

/* ---------------------------------------------------------------------------
 * cellSysutilAvc2 -- voice/video chat do modo online
 * -----------------------------------------------------------------------*/
static void avc2_ok(ppu_context* ctx) { RET(CELL_OK); }

static void avc2_is_camera_attached(ppu_context* ctx)
{
    if (!ARG(0)) { RET(CELL_AVC2_ERROR_INVALID_ARGUMENT); return; }
    g_w8(ARG(0), 0);                      /* CELL_AVC2_CAMERA_STATUS_DETACHED */
    RET(CELL_OK);
}

static void avc2_estimate_container(ppu_context* ctx)
{
    if (!ARG(0) || !ARG(1)) { RET(CELL_AVC2_ERROR_INVALID_ARGUMENT); return; }
    g_w32(ARG(1), 0x300000);              /* voice chat, v110+ (RPCS3) */
    RET(CELL_OK);
}

/* ---------------------------------------------------------------------------
 * sceNp / sceNp2 -- PSN offline
 * -----------------------------------------------------------------------*/
static void np_basic_offline(ppu_context* ctx) { hook_log("sceNpBasic* (offline)"); RET(SCE_NP_BASIC_ERROR_NOT_CONNECTED); }
static void np_account_region(ppu_context* ctx) { RET(SCE_NP_ERROR_OFFLINE); }
static void np_matching2_offline(ppu_context* ctx) { RET(SCE_NP_MATCHING2_ERROR_CONTEXT_NOT_STARTED); }

static void np_drm_execute_purchase(ppu_context* ctx)
{
    hook_log("sceNpDrmExecuteGamePurchase", "(loja PSN indisponível -- ignorado)");
    RET(CELL_OK);
}

static void np_drm_verify_upgrade(ppu_context* ctx)
{
    if (!ARG(0)) { RET(SCE_NP_DRM_ERROR_INVALID_PARAM); return; }
    char cid[0x30];
    g_str(ARG(0), cid, sizeof cid);
    const char* dir = getenv("BMU_LICENSE_DIR");
    char path[1200];
    snprintf(path, sizeof path, "%s/%s.rap", dir ? dir : "game/licenses", cid);
    struct stat st;
    const bool ok = cid[0] && stat(path, &st) == 0 && st.st_size == 16;
    char msg[200];
    snprintf(msg, sizeof msg, "content_id=%s -> %s", cid, ok ? "licenciado" : "sem licença (modo trial)");
    hook_log("sceNpDrmVerifyUpgradeLicense", msg);
    RET(ok ? CELL_OK : SCE_NP_DRM_ERROR_LICENSE_NOT_FOUND);
}

/* ---------------------------------------------------------------------------
 * Entrada / sistema
 * -----------------------------------------------------------------------*/
static void ok_stub(ppu_context* ctx) { RET(CELL_OK); }

/* cellKbRead(port, CellKbData*): { u32 led; u32 mkey; s32 len; u16 keycode[62]; }
 * Sem teclas pendentes: len = 0. */
static void kb_read(ppu_context* ctx)
{
    const uint32_t data = ARG(1);
    if (data) { g_w32(data + 8, 0); }
    RET(CELL_OK);
}

/* cellKey2CharGetChar(handle, kdata, charCode**, charNum*, processed*) */
static void key2char_getchar(ppu_context* ctx)
{
    g_w32(ARG(3), 0);
    g_w8(ARG(4), 0);
    RET(CELL_OK);
}

/* _cellGcmFunc15(CellGcmContextData*): no RPCS3 é um TODO vazio. */
static void gcm_func15(ppu_context* ctx) { (void)ctx; }

/* ---------------------------------------------------------------------------
 * Registro
 * -----------------------------------------------------------------------*/
struct HookEntry { uint32_t nid; const char* name; hle_ctx_fn fn; };

static const HookEntry kHooks[] = {
    /* sceNp */
    { 0x01FBBC9B, "sceNpBasicSendMessageGui",             np_basic_offline },
    { 0x64A704CC, "sceNpBasicRecvMessageAttachmentLoad",  np_basic_offline },
    { 0x806960AB, "sceNpBasicRecvMessageCustom",          np_basic_offline },
    { 0xB1E0718B, "sceNpManagerGetAccountRegion",         np_account_region },
    { 0x2ECD48ED, "sceNpDrmVerifyUpgradeLicense",         np_drm_verify_upgrade },
    { 0xF283C143, "sceNpDrmExecuteGamePurchase",          np_drm_execute_purchase },
    /* sceNp2 */
    { 0x26AE9FF8, "sceNpMatching2SignalingGetConnectionInfo", np_matching2_offline },
    { 0x8B209CA2, "sceNpMatching2GetRoomDataInternal",    np_matching2_offline },
    { 0xE4AC32CA, "sceNpMatching2GetRoomDataExternalList", np_matching2_offline },
    /* sys_io */
    { 0xBE5BE3BA, "cellPadSetSensorMode",                 ok_stub },
    { 0xFF0A21B7, "cellKbRead",                           kb_read },
    /* cellSysutilAvc2 */
    { 0x02DC41EE, "cellSysutilAvc2JoinChat",              avc2_ok },
    { 0x11F071CB, "cellSysutilAvc2StopStreaming2",        avc2_ok },
    { 0x2F280883, "cellSysutilAvc2EstimateMemoryContainerSize", avc2_estimate_container },
    { 0x2FC0AB58, "cellSysutilAvc2SetVideoMuting",        avc2_ok },
    { 0x38F33624, "cellSysutilAvc2Unload",                avc2_ok },
    { 0x3A37E7F1, "cellSysutilAvc2DestroyWindow",         avc2_ok },
    { 0x3C8C827C, "cellSysutilAvc2SetWindowPosition",     avc2_ok },
    { 0x4C4B9665, "cellSysutilAvc2IsCameraAttached",      avc2_is_camera_attached },
    { 0xA8DC0EFA, "cellSysutilAvc2CreateWindow",          avc2_ok },
    { 0xAC170042, "cellSysutilAvc2ShowWindow",            avc2_ok },
    { 0xAEE75751, "cellSysutilAvc2SetWindowSize",         avc2_ok },
    { 0xBF022284, "cellSysutilAvc2LeaveChat",             avc2_ok },
    { 0xC6C93D62, "cellSysutilAvc2Load",                  avc2_ok },
    { 0xCCF0AEFF, "cellSysutilAvc2SetAttribute",          avc2_ok },
    { 0xD42657DD, "cellSysutilAvc2StartStreaming2",       avc2_ok },
    { 0xDE6AFC37, "cellSysutilAvc2HideWindow",            avc2_ok },
    /* outros */
    { 0x48BD97C7, "sceNpTrophyAbortHandle",               ok_stub },
    { 0x3A33C1FD, "_cellGcmFunc15",                       gcm_func15 },
    { 0x56776C0D, "cellKey2CharGetChar",                  key2char_getchar },
    { 0x42B23552, "sys_prx_register_library",             ok_stub },
};

extern "C" void bmu_register_hooks(void)
{
    for (const HookEntry& h : kHooks)
        ps3_hle_register_ctx(h.nid, h.name, h.fn);
    fprintf(stderr, "[bmu] %u hooks do Bomberman ULTRA registrados\n",
            (unsigned)(sizeof kHooks / sizeof kHooks[0]));
}

/* main.cpp chama isto; o Bomberman não carrega PRX de sistema reais (libsre
 * etc.) -- cellSpurs/MultiStream ficam com o HLE do ps3recomp. */
extern "C" void ps3_load_prx_modules(void)
{
}
