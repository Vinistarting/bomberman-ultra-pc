/*
 * Bomberman ULTRA (NPEB00076) -- runner do port PC.
 *
 * Baseado em ps3recomp/templates/project/main.cpp. Mudanças do port:
 *   - resolução/tela cheia vindas do launcher (BMU_WIDTH, BMU_HEIGHT,
 *     BMU_FULLSCREEN) em vez de constantes;
 *   - defaults de VFS/saves/cache quando o exe é aberto sem o launcher;
 *   - EBOOT.elf padrão em game/PS3_GAME/USRDIR quando não há argumento;
 *   - bmu_register_hooks() (src/bomberman_hooks.cpp) logo após ppu_hle_init().
 *
 * Texto original do template abaixo.
 *
 * ps3recomp game project -- entry point template.
 *
 * This is the runner. It boots a lifted title through the toolkit's PPU
 * scaffold: allocate the flat guest VM, load the PPU ELF named on the command
 * line, register the lifted function table and the HLE NID handlers, start a
 * frame clock, and dispatch the entry OPD.
 *
 * The scaffold does the work; nothing below reimplements it. ppu_load_elf,
 * ppu_recomp_register, ppu_hle_init, ppu_sysprx_register, ppu_fs_register,
 * lv2_init_syscalls and ppu_run all come from the toolkit, compiled into this
 * project from PS3RECOMP_DIR (see CMakeLists.txt). What is left here is the
 * part a port owns: which backend to present through, how the frame clock is
 * paced, and whatever diagnostics the title turns out to need.
 *
 * runtime/ppu/tests/boot_main.cpp in the toolkit is the same boot in its
 * fully-instrumented form -- crash filter, hang watchdog, guest-PC sampling
 * profiler. Read it when a boot goes wrong; copy from it what the title
 * needs. It is deliberately not what a fresh project starts with.
 *
 * The one part that IS duplicated from it is the frame clock below, because a
 * port is expected to change its pacing and a shared one would be the wrong
 * shape for that. It was copied faithfully, comments and all. If a pacing bug
 * is fixed in one of the two, look at the other.
 *
 * This builds on Windows, macOS and Linux. CI builds and runs it against the
 * boot smoke title on the last two, and compile-checks it against clang-cl on
 * the first (tools/check_ppu_scaffold.py).
 */

/* The lifter's generated header, which declares ppu_context and the function
 * table. It comes first: everything below is written against that struct. */
#include "ppu_recomp.h"

/* The Win32 names -- Sleep, GetTickCount64, CreateThread, InterlockedIncrement,
 * VirtualAlloc, the scalar typedefs -- from one place. On Windows this is a
 * passthrough to <windows.h>; off Windows the toolkit's shim supplies the same
 * names over pthreads and mmap, so the call sites below are written once. */
#include "win32_compat.h"
#include "ps3emu/ps3_ftrace.h"   /* PS3_FTRACE: present spans */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/mman.h>   /* mmap: the guest arena, see alloc_guest_vm */
#include <unistd.h>     /* _exit ao fechar a janela */
#endif

#ifdef _WIN32
/* timeBeginPeriod: <windows.h> arrives with WIN32_LEAN_AND_MEAN set, which
 * excludes the multimedia timer API, so it is asked for by name -- and after
 * windows.h, since timeapi.h uses UINT. No shim exists off Windows because
 * there is nothing to shim: the 15.6 ms default timer granularity this works
 * around is a Windows problem. */
#include <timeapi.h>
#endif

/* ---------------------------------------------------------------------------
 * What the scaffold gives this file
 * -----------------------------------------------------------------------*/
extern "C" {
uint32_t ppu_load_elf(const char* path);      /* ELF -> guest RAM, returns entry OPD */
void     ppu_recomp_register(void);           /* generated: lifted table -> address map */
void     ppu_hle_init(void);                  /* firmware import NID -> HLE handler */
void     ppu_sysprx_register(void);           /* boot-critical CRT (sys_initialize_tls, ...) */
void     ppu_fs_register(void);               /* cellFs over the real game directory */
void     lv2_init_syscalls(void);             /* the lv2 syscall table */
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

/* This project's own: load whatever real system PRX modules the title needs
 * into guest RAM and register their exports. stubs.cpp has the empty version
 * a game with no lifted PRX wants. */
void     ps3_load_prx_modules(void);

extern const char* ppu_vfs_root;              /* host dir the PS3 mount points map into */

/* The status the guest handed to sys_process_exit. A title that exits that way
 * never returns through ppu_run, so this is only read on the path where the
 * entry function unwound instead. */
extern int     g_sys_process_exit_called;
extern int32_t g_sys_process_exit_code;

/* Host-provided symbols the runtime and the HLE libraries link against. */
uint8_t* vm_base = nullptr;
extern uint32_t ppu_vm_size;                  /* ppu_loader.cpp: the OOB guard */

/* Guest-callback dispatch. g_ps3_guest_caller is the hook the HLE runtime
 * calls back into recompiled code through -- cellSysutil events and the GCM
 * vblank/flip handlers. ppu_guest_call does the OPD -> dispatch. */
typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern ps3_guest_caller_fn g_ps3_guest_caller;
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);

/* The GCM half of the frame clock. */
void     cellGcmTickVBlank(void);
void     cellGcmTickFlip(void);
int      cellGcmTickFlipReport(void);
void     cellGcm_flip_debug(void);
void     cellGcm_set_precise_flip(int on);
void     cellPad_host_pump(void);
int      cellPad_quit_requested(void);
int      cellGcm_flip_outstanding(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);      /* drain get -> put */
unsigned cellGcm_flip_request_count(void);
}

/* ---------------------------------------------------------------------------
 * RSX present backend
 * -----------------------------------------------------------------------*/
/* D3D12 on Windows, Metal on Apple, and the null backend's headless software
 * path anywhere else -- the same selection runtime/host/host_posix.c and the
 * toolkit's boot harness make. All three expose the same three entry points,
 * so the frame clock below is backend-agnostic. */
#if defined(PS3RECOMP_RSX_VULKAN)
extern "C" int  rsx_vulkan_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_vulkan_backend_present(void);
extern "C" int  rsx_vulkan_backend_pump_messages(void);
#  define rsx_backend_init    rsx_vulkan_backend_init
#  define rsx_backend_present rsx_vulkan_backend_present
#  define rsx_backend_pump    rsx_vulkan_backend_pump_messages
#  define RSX_BACKEND_NAME    "Vulkan"
#elif defined(_WIN32)
extern "C" int  rsx_d3d12_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_d3d12_backend_present(void);
extern "C" int  rsx_d3d12_backend_pump_messages(void);
#  define rsx_backend_init    rsx_d3d12_backend_init
#  define rsx_backend_present rsx_d3d12_backend_present
#  define rsx_backend_pump    rsx_d3d12_backend_pump_messages
#  define RSX_BACKEND_NAME    "D3D12"
#elif defined(__APPLE__)
extern "C" int  rsx_metal_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_metal_backend_present(void);
extern "C" int  rsx_metal_backend_pump_messages(void);
#  define rsx_backend_init    rsx_metal_backend_init
#  define rsx_backend_present rsx_metal_backend_present
#  define rsx_backend_pump    rsx_metal_backend_pump_messages
#  define RSX_BACKEND_NAME    "Metal"
#else
extern "C" int  rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_null_backend_present(void);
extern "C" int  rsx_null_backend_pump_messages(void);
#  define rsx_backend_init    rsx_null_backend_init
#  define rsx_backend_present rsx_null_backend_present
#  define rsx_backend_pump    rsx_null_backend_pump_messages
#  define RSX_BACKEND_NAME    "null (headless software)"
#endif

/* ---------------------------------------------------------------------------
 * Configuration
 * -----------------------------------------------------------------------*/
/* The flat VM must span every region the PS3 memory map uses, not just the
 * game image: sys_ppu_thread_create puts thread stacks at 0xD0000000, so a
 * smaller arena makes every spawned thread's stack access out of bounds. */
#define VM_SIZE       0x100010000ull   /* the full 32-bit guest space + a 64K guard */
#define STACK_TOP     0x0FF00000u      /* main-thread stack, below the 0x10000000 segment */
/* Tamanho da janela/backbuffer: BMU_WIDTH/BMU_HEIGHT (launcher), 1280x720
 * por padrão -- a resolução nativa do jogo no PS3. */
static uint32_t g_win_w = 1280, g_win_h = 720;

static void read_window_config(void)
{
    const char* w = getenv("BMU_WIDTH");
    const char* h = getenv("BMU_HEIGHT");
    if (w && atoi(w) >= 320) g_win_w = (uint32_t)atoi(w);
    if (h && atoi(h) >= 240) g_win_h = (uint32_t)atoi(h);
}

#ifdef _WIN32
/* Tela cheia sem bordas: a janela do backend é criada nesta thread (o
 * frame_clock chama rsx_backend_init), então EnumThreadWindows a encontra sem
 * depender do título. O swapchain flip-model estica o backbuffer na janela. */
static BOOL CALLBACK bmu_find_wnd(HWND h, LPARAM out)
{
    if (IsWindowVisible(h)) { *(HWND*)out = h; return FALSE; }
    return TRUE;
}
static void apply_fullscreen(void)
{
    const char* f = getenv("BMU_FULLSCREEN");
    if (!f || *f != '1') return;
    HWND wnd = NULL;
    EnumThreadWindows(GetCurrentThreadId(), bmu_find_wnd, (LPARAM)&wnd);
    if (!wnd) return;
    MONITORINFO mi = { sizeof mi };
    GetMonitorInfo(MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowLongPtr(wnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(wnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    ShowCursor(FALSE);
}
#else
static void apply_fullscreen(void) {}
#endif

/* ---------------------------------------------------------------------------
 * Frame clock
 * -----------------------------------------------------------------------*/
/* On real hardware the RSX raises a vblank interrupt about 60 times a second
 * and that is what drives the game's frame loop. With no RSX it is synthesized
 * here: a host thread calls cellGcmTickVBlank/TickFlip, which invoke the
 * handlers the guest registered, and drains the GCM FIFO. Without this the
 * title initialises, registers its handlers and then waits forever.
 *
 * The ticks are driven off real elapsed time rather than off how long present()
 * took. A hidden or occluded window makes present block hard, and pacing the
 * ticks behind it paces the whole game behind it. */
static volatile LONG g_frames_presented = 0;

/* Frames handed to the backend at a guest FLIP boundary -- one per frame the
 * guest actually finished. The presents made before the guest's first flip, so
 * a fresh window is not left blank through a long boot, carry no guest frame
 * and are deliberately not counted. */
extern "C" unsigned ppu_boot_frames_presented(void)
{
    return (unsigned)g_frames_presented;
}

#if defined(PS3RECOMP_RSX_VULKAN)
extern "C" void rsx_vulkan_backend_present_begin(void);
extern "C" void rsx_vulkan_backend_present_finish(void);
#endif

static double pace_now_ms(void)
{
    LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

/* Vblank do RSX: no PS3 o flip só acontece no próximo vblank (60 Hz), e
 * enquanto isso o RSX não anda -- é isso que segura um jogo que não espera
 * o flip (os menus do Bomberman pediam ~87 flips/s e corriam rápido demais).
 * Antes o present FIFO fazia esse papel, mas bloqueando até o vblank mesmo
 * quando o frame já vinha atrasado. Aqui só espera quem está adiantado. */
static void pace_flip_60hz(void)
{
    static double next = 0.0;
    const double period = 1000.0 / 60.0;
    double now = pace_now_ms();
    if (next == 0.0 || now - next > period) { next = now + period; return; }   /* atrasado: sem espera */
    while (now < next) {
        const double left = next - now;
        if (left > 1.5) Sleep((DWORD)(left - 1.0));
        else SwitchToThread();
        now = pace_now_ms();
    }
    next += period;
}

static void present_guest_frame(void)
{
#if defined(PS3RECOMP_RSX_VULKAN)
    /* Metade 1: o frame vai para a GPU sem esperar; a metade 2 (esperar a
     * cópia e mostrar na janela) roda quando a thread fica ociosa ou no
     * próximo present -- a thread do RSX segue drenando o FIFO enquanto a
     * GPU desenha, em vez de ficar parada esperando por ela. */
    rsx_vulkan_backend_present_begin();
#else
    rsx_backend_present();
#endif
    /* This thread increments and guest threads read, so interlocked rather
     * than a volatile ++, which on arm64 is neither atomic nor a fence. */
    InterlockedIncrement(&g_frames_presented);
}

extern "C" void cellGcm_fifo_kick_wait(unsigned ms);

static double qpc_ms(void)
{
    LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

static DWORD WINAPI frame_clock(LPVOID)
{
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "Bomberman ULTRA";

    int rsx_ok = (rsx_backend_init(g_win_w, g_win_h, title) == 0);
#if !defined(PS3RECOMP_RSX_VULKAN)
    if (rsx_ok) apply_fullscreen();
#endif
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            rsx_ok ? "OK -- window open" : "FAILED");

    unsigned  last_flip = 0;
    /* vblank a 60 Hz de verdade: com GetTickCount e +16 ms eram 62,5 Hz (o
     * jogo rodava ~4% rápido e o contador mostrava 62-63). */
    double next_tick = qpc_ms();

    /* PS3RECOMP_VK_STATS: quanto desta thread (drain do FIFO + present) fica
     * ocupada -- é ela que escreve os labels/flips que o jogo espera. */
    const char* st_env = getenv("PS3RECOMP_VK_STATS");
    const int stats = st_env && *st_env == '1';
    double st_t0 = qpc_ms(), st_busy = 0.0, st_drain = 0.0, st_present = 0.0;

    for (;;) {
        /* Antes era Sleep(4): o drain só andava a cada ~4 ms, e cada vez que o
         * jogo esperava um label escrito por ele pagava esse atraso -- várias
         * vezes por frame. Agora espera o "kick" (um thread do jogo achou a
         * fila seca) com teto de 1 ms. */
        /* Logo depois de um flip não espera o kick: o fence que vem depois do
         * flip no FIFO só é drenado na próxima passada, e o jogo está parado
         * esperando por ele. */
        static int again = 0, begun = 0;
        /* BMU_FPS_CAP (launcher, "Limitar a 60 FPS"): 1 = trava em 60 como o
         * PS3, 0 = sem trava (o jogo corre mais rápido se o PC der mais de 60
         * -- a lógica é presa ao frame). */
        static int fps_cap = -1;
        if (fps_cap < 0) {
            const char* e = getenv("BMU_FPS_CAP");
            fps_cap = !(e && e[0] == '0');
            cellGcm_set_precise_flip(1);
            fprintf(stderr, "[boot] limite de 60 FPS: %s\n", fps_cap ? "ligado" : "desligado");
        }
        if (!again) cellGcm_fifo_kick_wait(1);
        again = 0;
        const double busy0 = stats ? qpc_ms() : 0.0;
        double now = qpc_ms();
#ifdef _WIN32
        /* F9: grava a linha do tempo (PS3_FTRACE) a partir de agora. Com
         * PS3_FTRACE=99999:<s>:<arquivo> o buffer fica alocado e parado; F9
         * desloca a janela para o presente. */
        if (PS3_FT_ON() && (GetAsyncKeyState(VK_F9) & 0x8000) && !g_ps3_ft.dumped) {
            static int armed = 0;
            if (!armed) {
                armed = 1;
                const uint64_t len = g_ps3_ft.end - g_ps3_ft.start, t = ps3_ft_now();
                g_ps3_ft.start = t; g_ps3_ft.end = t + len;
                fprintf(stderr, "[ftrace] F9: gravando %.1f s\n", (double)len / 1e9);
            }
        }
#endif

        /* vblank a 60 Hz (contador/handler de vblank do jogo). */
        int fired = 0;
        while (now - next_tick >= 0.0 && fired < 240) {
            cellGcmTickVBlank();
            next_tick += 1000.0 / 60.0;  /* 60 Hz */
            fired++;
        }
        if (fired >= 240) next_tick = now;   /* fell too far behind -- resync */

        /* Flips a 60 Hz sem quantizar no vblank (port Bomberman).
         *
         * O jogo usa SetFlipMode(HSYNC) e tem a lógica presa ao frame: cada
         * flip é 1/60 s de jogo. Completar o flip só no tick de vblank fazia
         * um frame que passasse 1 ms dos 16,7 esperar o tick seguinte -- o
         * jogo caía para ~30 flips/s e rodava em câmera lenta (metade da
         * velocidade), enquanto o contador mostrava ~55 (presents repetidos).
         * Agora o flip completa assim que o drain o alcança, desde que tenham
         * passado 16,7 ms do anterior: nunca acima de 60, sem perder meio
         * frame por atraso pequeno. */
        /* O flip completa ANTES de esperar a GPU (present_finish): antes ele
         * só completava depois do present_finish, 2-5 ms por frame em que o
         * drain ficava parado no fence pós-flip e o jogo esperando. */
        static double last_flip_done = 0.0;
        auto pace_flip = [&]() {
            const double period = 1000.0 / 60.0;
            const double t = qpc_ms();
            if ((!fps_cap || t - last_flip_done >= period - 0.25) && cellGcmTickFlipReport())
                last_flip_done = (t - last_flip_done > 2.0 * period) ? t : last_flip_done + period;
        };
        auto do_present = [&]() {
#if defined(PS3RECOMP_RSX_VULKAN)
            if (begun) rsx_vulkan_backend_present_finish();   /* um begin por vez */
#endif
            const double _p0 = stats ? qpc_ms() : 0.0; const uint64_t _f0 = PS3_FT_ON() ? ps3_ft_now() : 0;
            present_guest_frame();
            if (_f0) ps3_ft_span(PS3_FT_PRESENT, 0, 1, 0, _f0, ps3_ft_now());
            if (stats) st_present += qpc_ms() - _p0;
            last_flip = cellGcm_flip_request_count();
            begun = 1; again = 1;
            pace_flip();
        };
        if (rsx_ok) pace_flip();
        /* Os comandos depois de um flip só rodam quando ele completa (no
         * máximo 60 por segundo), como o WaitFlip do RSX: é isso que segura o
         * jogo -- lógica presa ao frame -- na velocidade certa quando a
         * máquina daria mais de 60. Sem isso a partida corria a ~70 flips/s.
         * Trava de segurança: um flip que não completa em 100 ms é largado. */
        static double gate_since = 0.0;
        int flip_wait = fps_cap && cellGcm_flip_outstanding();
        if (!flip_wait) gate_since = 0.0;
        else if (gate_since == 0.0) gate_since = qpc_ms();
        else if (qpc_ms() - gate_since > 100.0) {
            flip_wait = 0;
            static int nd = 0;
            if (nd++ < 5) cellGcm_flip_debug();
        }

        if (rsx_ok) {
            /* Present só quando o drain alcançou um flip novo (um frame
             * completo). Antes havia presents repetidos -- a cada vblank e ao
             * pedido de flip -- que inflavam o contador e gastavam tempo
             * desta thread. */
            if (cellGcm_take_flip_pending()) do_present();
            if (!flip_wait) { const double _d0 = stats ? qpc_ms() : 0.0; cellGcm_rsx_process_fifo(); if (stats) st_drain += qpc_ms() - _d0; }
            if (cellGcm_take_flip_pending()) do_present();

#if defined(PS3RECOMP_RSX_VULKAN)
            /* Sem flip novo nesta passada: agora sim espera a GPU e mostra. */
            if (!again && begun) { rsx_vulkan_backend_present_finish(); begun = 0; }
#endif
            const int pump_rc = rsx_backend_pump();
            cellPad_host_pump();   /* controles SDL: hotplug/leitura nesta thread */
            if (pump_rc != 0 || cellPad_quit_requested()) {
                /* Janela fechada: encerra o processo. */
                fprintf(stderr, "[boot] %s -- encerrando\n", pump_rc != 0 ? "janela fechada" : "SELECT+START no controle");
                fflush(stdout); fflush(stderr);
#ifdef _WIN32
                ExitProcess(0);
#else
                _exit(0);
#endif
            }
            /* Antes do primeiro flip a janela mostra o que houver (boot). */
            if (cellGcm_flip_request_count() == 0 && last_flip == 0) rsx_backend_present();
        }
        if (stats) {
            const double t = qpc_ms();
            st_busy += t - busy0;
            if (t - st_t0 >= 2000.0) {
                const double k = 1000.0 / (t - st_t0);
                static unsigned st_flips0 = 0;
                const unsigned fl = cellGcm_flip_request_count();
                fprintf(stderr, "[rsx] thread do RSX ocupada %.0f ms/s (drain do FIFO %.0f, present %.0f) | flips do jogo: %.1f/s\n",
                        st_busy * k, st_drain * k, st_present * k, (fl - st_flips0) * k);
                st_flips0 = fl;
                st_busy = st_drain = st_present = 0.0; st_t0 = t;
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * VFS root
 * -----------------------------------------------------------------------*/
/* Derive the directory holding PS3_GAME from the EBOOT path
 * <root>/PS3_GAME/USRDIR/EBOOT.elf -> <root>. $PS3_VFS_ROOT overrides. */
static char s_vfs_root[1024];

static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }

    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    /* strip EBOOT.elf, USRDIR and PS3_GAME */
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* ---------------------------------------------------------------------------
 * Guest VM
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
/* Demand-paging for the flat VM: reserve the whole 4 GB guest space, which
 * costs no commit, and commit each 64 KB page on first access. Every 32-bit
 * guest offset is then valid, so a garbage guest pointer reads zero instead of
 * killing the process. Faults outside the arena fall through untouched. */
static LONG WINAPI vm_commit_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + VM_SIZE) {
            void* page = (void*)(fault & ~(uintptr_t)0xFFFF);
            if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static bool alloc_guest_vm(void)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, vm_commit_veh);
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#else
    /* No vectored exception handlers off Windows, so the arena is mapped up
     * front and lazily backed by the OS: only pages the title touches cost
     * anything. It stops below the 0xE0000000 mark, so ppu_vm_size arms the
     * loader's bounds check for what is left.
     *
     * mmap, not calloc: vm_commit() hands vm_base + addr to mprotect, which
     * takes page-aligned addresses only. glibc serves an allocation this big
     * from its own mmap chunk and returns a pointer 16 bytes past the page
     * start, so every commit failed with EINVAL and sys_memory_allocate
     * answered ENOMEM to every call. mmap's base is page-aligned and zeroed. */
    void* p = mmap(NULL, 0xE0000000u, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    vm_base = (p == MAP_FAILED) ? nullptr : (uint8_t*)p;
    ppu_vm_size = 0xE0000000u;
#endif
    return vm_base != nullptr;
}

/* ---------------------------------------------------------------------------
 * Main
 * -----------------------------------------------------------------------*/
static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1,
                                 uint64_t a2, uint64_t a3, uint64_t a4,
                                 uint64_t a5, uint64_t a6, uint64_t a7)
{
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}

/* ---------------------------------------------------------------------------
 * Defaults do Bomberman ULTRA (quando aberto sem o launcher)
 * -----------------------------------------------------------------------*/
extern "C" void bmu_register_hooks(void);     /* src/bomberman_hooks.cpp */

static char s_default_elf[1024];

static void set_default_env(const char* key, const char* val)
{
    if (getenv(key)) return;
#ifdef _WIN32
    _putenv_s(key, val);
#else
    setenv(key, val, 0);
#endif
}

/* Procura a raiz do port (a pasta com game/PS3_GAME/USRDIR/EBOOT.elf) subindo a
 * partir do exe: bin/, build/Release/ e build/ ficam 1-2 níveis abaixo dela. */
static bool find_port_root(const char* exe, char* root, size_t n)
{
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", exe);
    for (char* p = buf; *p; p++) if (*p == '\\') *p = '/';
    for (int up = 0; up < 4; up++) {
        char* s = strrchr(buf, '/');
        if (!s) break;
        *s = 0;
        char probe[1100];
        snprintf(probe, sizeof probe, "%s/game/PS3_GAME/USRDIR/EBOOT.elf", buf);
        FILE* f = fopen(probe, "rb");
        if (f) { fclose(f); snprintf(root, n, "%s", buf); return true; }
    }
    return false;
}

static void apply_port_defaults(const char* exe)
{
#if defined(PS3RECOMP_RSX_VULKAN)
    /* Backend Vulkan: janela SDL2 + programas do guest no motor de desenho
     * compartilhado (o mesmo do Metal) -- é o caminho que renderiza este jogo
     * corretamente; o D3D12 desenha a tela de logos com geometria errada. */
    set_default_env("PS3RECOMP_VK_WINDOW", "1");
    set_default_env("PS3RECOMP_VK_GUEST_PROGRAMS", "1");
    { const char* f = getenv("BMU_FULLSCREEN");
      if (f && *f == '1') set_default_env("PS3RECOMP_VK_FULLSCREEN", "1"); }
    /* Contador de FPS no canto superior direito (desligável no launcher) e
     * uma linha de estatística a cada 2 s no log (fps, draws, esperas). */
    { const char* f = getenv("BMU_SHOW_FPS");
      set_default_env("PS3RECOMP_VK_FPS", (f && *f == '0') ? "0" : "1"); }
    set_default_env("PS3RECOMP_VK_STATS", "1");
    set_default_env("PS3_JC_STATS", "1");      /* tempo dos jobs SPU de render */
#endif
    char root[1024];
    if (!find_port_root(exe, root, sizeof root)) return;
    char v[1100];
    snprintf(v, sizeof v, "%s/game", root);           set_default_env("PS3_VFS_ROOT", v);
    snprintf(v, sizeof v, "%s/user/dev_hdd0", root);  set_default_env("PS3_HDD0_ROOT", v);
    snprintf(v, sizeof v, "%s/user/dev_hdd1", root);  set_default_env("PS3_HDD1_ROOT", v);
    snprintf(v, sizeof v, "%s/game/licenses", root);  set_default_env("BMU_LICENSE_DIR", v);
    /* O jogo não usa o firmware (flashMP3/flashATRAC.pic nunca são abertos);
     * uma pasta própria evita o caminho padrão do ps3recomp (D:/recomp/...). */
    snprintf(v, sizeof v, "%s/user/dev_flash", root); set_default_env("PS3_DEV_FLASH", v);
    set_default_env("PS3_TITLE", "Bomberman ULTRA");
#ifdef _WIN32
    snprintf(v, sizeof v, "%s/user", root);           CreateDirectoryA(v, NULL);
    snprintf(v, sizeof v, "%s/user/dev_hdd0", root);  CreateDirectoryA(v, NULL);
    snprintf(v, sizeof v, "%s/user/dev_hdd1", root);  CreateDirectoryA(v, NULL);
    snprintf(v, sizeof v, "%s/user/dev_flash", root); CreateDirectoryA(v, NULL);
#endif
    snprintf(s_default_elf, sizeof s_default_elf, "%s/game/PS3_GAME/USRDIR/EBOOT.elf", root);
}

/* ---------------------------------------------------------------------------
 * Licença: o port só roda com o RAP da versão europeia
 * -------------------------------------------------------------------------*/
/* O mesmo teste do launcher, aqui também para quem abrir o exe direto: o
 * arquivo <BMU_LICENSE_DIR>/EP0555-NPEB00076_00-BOMBERMANGAME001.rap precisa
 * ter os 16 bytes cujo SHA-256 está abaixo. Só o hash fica no código. */
static const uint8_t kRapSha256[32] = {
    0xBE,0x87,0x27,0x1B,0x92,0x47,0x68,0x8B,0xD6,0x80,0xCC,0xC9,0x67,0x58,0x65,0xE6,
    0x9C,0xEA,0x9E,0x01,0x72,0x19,0x67,0xC8,0x3E,0x10,0x58,0xB2,0x34,0xCB,0x79,0xA1,
};

static void sha256_16(const uint8_t msg[16], uint8_t out[32])
{
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
    uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    uint8_t blk[64] = { 0 };
    memcpy(blk, msg, 16);
    blk[16] = 0x80;
    blk[63] = 16 * 8;                         /* comprimento em bits */
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)blk[4 * i] << 24 | (uint32_t)blk[4 * i + 1] << 16 | (uint32_t)blk[4 * i + 2] << 8 | blk[4 * i + 3];
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
    for (int i = 16; i < 64; i++) {
        const uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        const uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        const uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
#undef ROR
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(h[i] >> 24); out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8); out[4 * i + 3] = (uint8_t)h[i];
    }
}

static bool license_present(void)
{
    const char* dir = getenv("BMU_LICENSE_DIR");
    char path[1200];
    snprintf(path, sizeof path, "%s/EP0555-NPEB00076_00-BOMBERMANGAME001.rap", dir && *dir ? dir : "game/licenses");
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t buf[17];
    const size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n != 16) return false;
    uint8_t hsh[32];
    sha256_16(buf, hsh);
    return memcmp(hsh, kRapSha256, 32) == 0;
}

int main(int argc, char** argv)
{
#ifdef _WIN32
    char exe_path[1024];
    GetModuleFileNameA(NULL, exe_path, sizeof exe_path);
    apply_port_defaults(exe_path);
#else
    apply_port_defaults(argv[0]);
#endif
    read_window_config();

    const char* elf_path = argc >= 2 ? argv[1] : (s_default_elf[0] ? s_default_elf : NULL);
    if (!elf_path) {
        printf("uso: %s <EBOOT.elf>  (ou rode pelo BombermanUltraLauncher.exe)\n", argv[0]);
        return 2;
    }

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    /* 1 ms timer resolution. The default granularity is about 15.6 ms, which
     * inflates every shorter wait the title makes and throttles the whole
     * thing. POSIX timers are already fine-grained. */
    timeBeginPeriod(1);
    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered: do not lose prints on a kill */
#endif

    printf("=== Bomberman ULTRA (NPEB00076) -- ps3recomp ===\n");

    if (!license_present()) {
        const char* msg = "Licença não encontrada.\n\nEste port só roda com a licença da versão europeia "
                          "do Bomberman ULTRA (EP0555-NPEB00076_00-BOMBERMANGAME001.rap). Abra o "
                          "BombermanUltraLauncher.exe e use \"Localizar licença\".";
        fprintf(stderr, "[boot] %s\n", msg);
#ifdef _WIN32
        MessageBoxA(NULL, msg, "Bomberman ULTRA", MB_ICONWARNING | MB_OK);
#endif
        return 3;
    }

    if (!alloc_guest_vm()) {
        fprintf(stderr, "ERROR: could not allocate the guest address space\n");
        return 1;
    }

    uint32_t entry = ppu_load_elf(elf_path);
    if (!entry) {
        fprintf(stderr, "ERROR: could not load %s\n", elf_path);
        return 1;
    }

    derive_vfs_root(elf_path);
    printf("[boot] VFS root: %s\n", ppu_vfs_root);

    ppu_recomp_register();   /* lifted function table -> address map */
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    bmu_register_hooks();    /* NIDs que faltam no ps3recomp + políticas do port */
    ppu_sysprx_register();   /* boot-critical CRT */
    ppu_fs_register();       /* cellFs over the game directory */
    lv2_init_syscalls();     /* the lv2 syscall table */

    /* Install the guest-callback hook, then start the frame clock. It no-ops
     * until the title registers its vblank and flip handlers during init. */
    g_ps3_guest_caller = harness_guest_caller;
    CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n",
           entry, STACK_TOP);

    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);

    /* A title that called sys_process_exit never reaches this line: that path
     * ends in the host exit(). Getting here means the entry function returned
     * instead, so hand back the status the guest published if it published
     * one. Returning a hardcoded 0 would report success for a run that never
     * got anywhere. */
    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}
