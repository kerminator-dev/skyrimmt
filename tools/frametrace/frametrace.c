/* DXMT display/frame-time proxy. Optional borderless stretching preserves the
 * game's logical viewport. FRAMETRACE_OUT enables Present timing CSV output.
 * All D3D11 calls are forwarded to DXMT's builtin d3d11.dll in system32.
 *
 * Install: copy build/frametrace/d3d11.dll next to SkyrimSE.exe and launch with WINEDLLOVERRIDES="d3d11=n,b"
 * (per launch, no registry change). Without the override the file is ignored (builtin d3d11 wins).
 * Output: $FRAMETRACE_OUT (Windows or unix path), columns:
 *   frame, t_us (QPC at Present entry since first frame), dt_us (interval to previous Present entry),
 *   present_us (time spent inside Present: vsync/back-pressure wait + DXMT submit), sync (SyncInterval argument)
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>

typedef HRESULT (WINAPI *PFN_CREATE_DEV_SC)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
    UINT, UINT, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (WINAPI *PFN_CREATE_DEV)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *,
    UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (WINAPI *PFN_PRESENT)(IDXGISwapChain *, UINT, UINT);

static HMODULE real;
static PFN_PRESENT real_present;
static FILE *out;
static LARGE_INTEGER freq, first, prev;
static LONGLONG frame_period;
static LONGLONG frame_deadline;
static unsigned long long frames;
static CRITICAL_SECTION lock;
static HWND stretch_window;
static int stretch_enabled;
static UINT render_width, render_height;
typedef HRESULT (WINAPI *PFN_RESIZE)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
static PFN_RESIZE real_resize;
typedef BOOL (WINAPI *PFN_CLIENT_RECT)(HWND, LPRECT);
static PFN_CLIENT_RECT real_client_rect;

/* Skyrim queries its client area when sizing the 3D viewport. Only enlarging
 * the OS window makes that viewport larger than the low-resolution backbuffer:
 * at 50% just a quarter of the scene survives, while the separately sized HUD
 * still looks correct. Give the game logical render dimensions, but leave
 * USER32/Wine/DXMT's real client area alone so presentation still fills the screen.
 * Patch this process's main EXE import only, never the system API or a game file. */
static BOOL WINAPI game_client_rect(HWND window, LPRECT rect)
{
    if (window == stretch_window && rect && render_width && render_height) {
        SetRect(rect, 0, 0, render_width, render_height);
        return TRUE;
    }
    return real_client_rect(window, rect);
}

static int install_client_rect_hook(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return 0;
    IMAGE_IMPORT_DESCRIPTOR *imports = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva);
    for (; imports->Name; ++imports) {
        if (!imports->OriginalFirstThunk || !imports->FirstThunk) continue;
        IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)(base + imports->OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *iat = (IMAGE_THUNK_DATA64 *)(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *name = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((const char *)name->Name, "GetClientRect")) continue;
            if (iat->u1.Function == (ULONG_PTR)game_client_rect) return 1;
            DWORD old;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) return 0;
            real_client_rect = (PFN_CLIENT_RECT)(ULONG_PTR)iat->u1.Function;
            InterlockedExchangePointer((PVOID volatile *)&iat->u1.Function, (PVOID)game_client_rect);
            VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            fprintf(stderr, "SkyrimMT display: game viewport fixed at %ux%u\n", render_width, render_height);
            return 1;
        }
    }
    return 0;
}

/* Keep the swapchain at the game's resolution; DXMT's Metal layer scales it to
 * the borderless window. No display-mode switch, SKSE, or game DLL patch needed. */
static void stretch_to_monitor(void)
{
    MONITORINFO monitor = { .cbSize = sizeof(MONITORINFO) };
    RECT rect;
    if (!stretch_window || !GetMonitorInfoA(MonitorFromWindow(stretch_window, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    GetWindowRect(stretch_window, &rect);
    RECT target = monitor.rcMonitor;
    if (EqualRect(&rect, &target)) return;
    LONG_PTR style = GetWindowLongPtrA(stretch_window, GWL_STYLE);
    SetWindowLongPtrA(stretch_window, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
    SetWindowPos(stretch_window, NULL, target.left, target.top,
                 target.right - target.left, target.bottom - target.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    fprintf(stderr, "SkyrimMT display: render %ux%u -> window %ldx%ld\n",
            render_width, render_height, target.right - target.left, target.bottom - target.top);
}

static HRESULT WINAPI hook_resize(IDXGISwapChain *sc, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    DXGI_SWAP_CHAIN_DESC desc;
    if (stretch_enabled && SUCCEEDED(IDXGISwapChain_GetDesc(sc, &desc)) && desc.OutputWindow == stretch_window) {
        width = render_width; height = render_height;
    }
    return real_resize(sc, count, width, height, format, flags);
}

static HMODULE load_real(void)
{
    if (!real) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, sizeof(path));
        lstrcatA(path, "\\d3d11.dll");
        real = LoadLibraryA(path);
    }
    return real;
}

static void open_out(void)
{
    const char *p = getenv("FRAMETRACE_OUT");
    out = fopen(p && *p ? p : "frametrace.csv", "w");
    if (out) { setvbuf(out, NULL, _IOFBF, 1 << 16); fprintf(out, "frame,t_us,dt_us,present_us,sync\n"); }
}

static HRESULT WINAPI hook_present(IDXGISwapChain *sc, UINT sync, UINT flags)
{
    if (flags & DXGI_PRESENT_TEST) return real_present(sc, sync, flags);
    if (stretch_enabled && !(frames % 120)) stretch_to_monitor();
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    HRESULT hr = real_present(sc, sync, flags);
    QueryPerformanceCounter(&b);
    EnterCriticalSection(&lock);
    if (!frames) first = prev = a;
    if (out) {
        fprintf(out, "%llu,%lld,%lld,%lld,%u\n", frames,
                (a.QuadPart - first.QuadPart) * 1000000 / freq.QuadPart,
                (a.QuadPart - prev.QuadPart) * 1000000 / freq.QuadPart,
                (b.QuadPart - a.QuadPart) * 1000000 / freq.QuadPart, sync);
        if ((frames & 255) == 255) fflush(out);
    }
    prev = a; frames++;
    LeaveCriticalSection(&lock);
    /* Pace the game before it starts the next frame. Metal still presents on
     * the normal VSync schedule, without an extra minimum display interval. */
    if (frame_period && hr == S_OK && !(flags & DXGI_PRESENT_DO_NOT_WAIT)) {
        /* Advance from the previous deadline, so work before and inside
         * Present consumes part of this frame's budget. Resync after stalls
         * rather than running several frames without a delay to catch up. */
        frame_deadline = frame_deadline ? frame_deadline + frame_period : a.QuadPart + frame_period;
        if (frame_deadline < b.QuadPart) frame_deadline = b.QuadPart;
        LONGLONG remaining;
        while ((remaining = frame_deadline - b.QuadPart) > 0) {
            DWORD milliseconds = (DWORD)(remaining * 1000 / freq.QuadPart);
            if (milliseconds > 1) Sleep(milliseconds - 1);
            else SwitchToThread();
            QueryPerformanceCounter(&b);
        }
    } else if (frame_period) {
        frame_deadline = 0;
    }
    return hr;
}

static void hook_swapchain(IDXGISwapChain *sc)
{
    void **vt = *(void ***)sc;
    if (vt[8] == (void *)hook_present) return;       /* IDXGISwapChain::Present = slot 8 */
    DWORD old;
    real_present = (PFN_PRESENT)vt[8];
    if (VirtualProtect(&vt[8], sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) {
        vt[8] = (void *)hook_present;
        VirtualProtect(&vt[8], sizeof(void *), old, &old);
    }
    if (stretch_enabled && VirtualProtect(&vt[13], sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) {
        real_resize = (PFN_RESIZE)vt[13];
        vt[13] = (void *)hook_resize;
        VirtualProtect(&vt[13], sizeof(void *), old, &old);
    }
}

__declspec(dllexport) HRESULT WINAPI D3D11CreateDeviceAndSwapChain(IDXGIAdapter *ad, D3D_DRIVER_TYPE dt, HMODULE sw,
    UINT fl, const D3D_FEATURE_LEVEL *lv, UINT nlv, UINT sdk, const DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **sc,
    ID3D11Device **dev, D3D_FEATURE_LEVEL *got, ID3D11DeviceContext **ctx)
{
    PFN_CREATE_DEV_SC f = load_real() ? (PFN_CREATE_DEV_SC)GetProcAddress(real, "D3D11CreateDeviceAndSwapChain") : NULL;
    if (!f) return E_FAIL;
    HRESULT hr = f(ad, dt, sw, fl, lv, nlv, sdk, desc, sc, dev, got, ctx);
    if (SUCCEEDED(hr) && sc && *sc) {
        DXGI_SWAP_CHAIN_DESC actual;
        if (stretch_enabled && !stretch_window && SUCCEEDED(IDXGISwapChain_GetDesc(*sc, &actual)) && actual.Windowed) {
            stretch_window = actual.OutputWindow;
            render_width = actual.BufferDesc.Width; render_height = actual.BufferDesc.Height;
            if (!install_client_rect_hook()) {
                fprintf(stderr, "SkyrimMT display: cannot preserve game viewport; stretching disabled to avoid cropping\n");
                stretch_enabled = 0;
            }
        }
        hook_swapchain(*sc);
    }
    return hr;
}

__declspec(dllexport) HRESULT WINAPI D3D11CreateDevice(IDXGIAdapter *ad, D3D_DRIVER_TYPE dt, HMODULE sw, UINT fl,
    const D3D_FEATURE_LEVEL *lv, UINT nlv, UINT sdk, ID3D11Device **dev, D3D_FEATURE_LEVEL *got, ID3D11DeviceContext **ctx)
{
    PFN_CREATE_DEV f = load_real() ? (PFN_CREATE_DEV)GetProcAddress(real, "D3D11CreateDevice") : NULL;
    return f ? f(ad, dt, sw, fl, lv, nlv, sdk, dev, got, ctx) : E_FAIL;
}

/* rarely used entry points: forwarded with a generic trampoline (same signature in the real DLL) */
#define FWD(name) \
    __declspec(dllexport) void *name(void *a, void *b, void *c, void *d, void *e, void *f, void *g, void *h, void *i, void *j) { \
        typedef void *(*fn)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *); \
        fn p = load_real() ? (fn)GetProcAddress(real, #name) : NULL; \
        return p ? p(a, b, c, d, e, f, g, h, i, j) : (void *)(LONG_PTR)E_FAIL; }
FWD(D3D11CoreCreateDevice)
FWD(D3D11On12CreateDevice)

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&lock);
        QueryPerformanceFrequency(&freq);
        const char *limit = getenv("SKYRIMMT_FRAME_LIMIT");
        if (limit && (!strcmp(limit, "30") || !strcmp(limit, "45")))
            frame_period = freq.QuadPart / atoi(limit);
        stretch_enabled = getenv("SKYRIMMT_STRETCH") && !strcmp(getenv("SKYRIMMT_STRETCH"), "1");
        if (getenv("FRAMETRACE_OUT")) open_out();
    } else if (reason == DLL_PROCESS_DETACH && out) {
        fflush(out); fclose(out);
    }
    return TRUE;
}
