// EdgeNote - native Win32 sticky note for Windows 10/11 x64
// True borderless window, manual resize, four-edge auto-hide, multi-instance.

#include <windows.h>
#include <windowsx.h>
#include <stdlib.h>

#define ID_EDIT 1001
#define IDM_NEWNOTE 2000
#define IDM_TOPMOST 2001
#define IDM_AUTOHIDE 2002
#define IDM_FONT_PLUS 2003
#define IDM_FONT_MINUS 2004
#define IDM_UNDOCK 2005
#define IDM_CLEAR 2006
#define IDM_EXIT 2007

#define TIMER_EDGE 1
#define EDGE_NONE 0
#define EDGE_LEFT 1
#define EDGE_RIGHT 2
#define EDGE_TOP 3
#define EDGE_BOTTOM 4
#define SNAP_DISTANCE 32
#define HIDDEN_SLIVER 4
#define EDGE_TRIGGER 3
#define HIDE_TICKS 7
#define MIN_W 220
#define MIN_H 150
#define TITLE_H 34
#define RESIZE_BORDER 7
#define TITLE_BTN_W 38

#define DRAG_NONE 0
#define DRAG_MOVE 1
#define DRAG_SIZE 2

static HWND g_hwnd = NULL;
static HWND g_edit = NULL;
static HBRUSH g_bodyBrush = NULL;
static HBRUSH g_titleBrush = NULL;
static HFONT g_editFont = NULL;
static RECT g_visibleRect = {0};
static int g_dockEdge = EDGE_NONE;
static int g_hidden = 0;
static int g_hideCounter = 0;
static int g_alwaysOnTop = 1;
static int g_autoHide = 1;
static int g_fontSize = 13;
static int g_inMoveSize = 0;
static int g_menuOpen = 0;

static int g_dragMode = DRAG_NONE;
static int g_dragHit = HTCLIENT;
static POINT g_dragStart = {0};
static RECT g_dragRect = {0};

static const COLORREF COL_BODY = RGB(247, 229, 142);
static const COLORREF COL_TITLE = RGB(228, 201, 85);
static const COLORREF COL_TEXT = RGB(47, 44, 30);
static const COLORREF COL_TITLETEXT = RGB(81, 71, 17);

static void get_monitor_info(HMONITOR mon, MONITORINFO *mi) {
    ZeroMemory(mi, sizeof(*mi));
    mi->cbSize = sizeof(*mi);
    GetMonitorInfoW(mon, mi);
}

static HMONITOR monitor_for_visible_rect(void) {
    POINT p;
    p.x = (g_visibleRect.left + g_visibleRect.right) / 2;
    p.y = (g_visibleRect.top + g_visibleRect.bottom) / 2;
    return MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
}

static HWND z_after(void) {
    return g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST;
}

static void update_font(void) {
    HDC dc = GetDC(g_hwnd);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(g_hwnd, dc);

    HFONT f = CreateFontW(
        -MulDiv(g_fontSize, dpi, 72), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei UI"
    );
    if (f) {
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)f, TRUE);
        if (g_editFont) DeleteObject(g_editFont);
        g_editFont = f;
    }
}

static void layout_edit(void) {
    if (!g_edit) return;
    RECT cr;
    GetClientRect(g_hwnd, &cr);
    /* Leave only a tiny invisible resize hit-zone around the content. */
    int pad = RESIZE_BORDER;
    int ew = cr.right - pad * 2;
    int eh = cr.bottom - TITLE_H - pad;
    if (ew < 1) ew = 1;
    if (eh < 1) eh = 1;
    MoveWindow(g_edit, pad, TITLE_H, ew, eh, TRUE);
}

static void place_visible_for_dock(void) {
    MONITORINFO mi;
    get_monitor_info(monitor_for_visible_rect(), &mi);

    int w = g_visibleRect.right - g_visibleRect.left;
    int h = g_visibleRect.bottom - g_visibleRect.top;
    int x = g_visibleRect.left;
    int y = g_visibleRect.top;

    if (g_dockEdge == EDGE_LEFT) x = mi.rcWork.left;
    else if (g_dockEdge == EDGE_RIGHT) x = mi.rcWork.right - w;
    else if (g_dockEdge == EDGE_TOP) y = mi.rcWork.top;
    else if (g_dockEdge == EDGE_BOTTOM) y = mi.rcWork.bottom - h;

    SetWindowPos(g_hwnd, z_after(), x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    GetWindowRect(g_hwnd, &g_visibleRect);
}

static void show_from_edge(void) {
    if (!g_hidden) return;
    place_visible_for_dock();
    g_hidden = 0;
    g_hideCounter = 0;
}

static void hide_to_edge(void) {
    if (g_dockEdge == EDGE_NONE || !g_autoHide) return;

    MONITORINFO mi;
    get_monitor_info(monitor_for_visible_rect(), &mi);

    int w = g_visibleRect.right - g_visibleRect.left;
    int h = g_visibleRect.bottom - g_visibleRect.top;
    int x = g_visibleRect.left;
    int y = g_visibleRect.top;

    if (g_dockEdge == EDGE_LEFT)
        x = mi.rcMonitor.left - w + HIDDEN_SLIVER;
    else if (g_dockEdge == EDGE_RIGHT)
        x = mi.rcMonitor.right - HIDDEN_SLIVER;
    else if (g_dockEdge == EDGE_TOP)
        y = mi.rcMonitor.top - h + HIDDEN_SLIVER;
    else if (g_dockEdge == EDGE_BOTTOM)
        y = mi.rcMonitor.bottom - HIDDEN_SLIVER;

    SetWindowPos(g_hwnd, z_after(), x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_hidden = 1;
    g_hideCounter = 0;
}

static int point_in_rect_margin(POINT p, RECT r, int margin) {
    return p.x >= r.left - margin && p.x <= r.right + margin &&
           p.y >= r.top - margin && p.y <= r.bottom + margin;
}

static void edge_timer(void) {
    if (g_dockEdge == EDGE_NONE || !g_autoHide || g_inMoveSize || g_menuOpen)
        return;

    POINT p;
    if (!GetCursorPos(&p)) return;

    MONITORINFO mi;
    get_monitor_info(monitor_for_visible_rect(), &mi);

    if (g_hidden) {
        int hit = 0;
        if (g_dockEdge == EDGE_LEFT &&
            p.x <= mi.rcMonitor.left + EDGE_TRIGGER &&
            p.y >= g_visibleRect.top - 20 && p.y <= g_visibleRect.bottom + 20)
            hit = 1;
        else if (g_dockEdge == EDGE_RIGHT &&
                 p.x >= mi.rcMonitor.right - 1 - EDGE_TRIGGER &&
                 p.y >= g_visibleRect.top - 20 && p.y <= g_visibleRect.bottom + 20)
            hit = 1;
        else if (g_dockEdge == EDGE_TOP &&
                 p.y <= mi.rcMonitor.top + EDGE_TRIGGER &&
                 p.x >= g_visibleRect.left - 20 && p.x <= g_visibleRect.right + 20)
            hit = 1;
        else if (g_dockEdge == EDGE_BOTTOM &&
                 p.y >= mi.rcMonitor.bottom - 1 - EDGE_TRIGGER &&
                 p.x >= g_visibleRect.left - 20 && p.x <= g_visibleRect.right + 20)
            hit = 1;

        if (hit) show_from_edge();
    } else {
        RECT r;
        GetWindowRect(g_hwnd, &r);
        if (!point_in_rect_margin(p, r, 8)) {
            if (++g_hideCounter >= HIDE_TICKS) hide_to_edge();
        } else {
            g_hideCounter = 0;
        }
    }
}

static void detect_and_snap(void) {
    RECT r;
    GetWindowRect(g_hwnd, &r);

    POINT center;
    center.x = (r.left + r.right) / 2;
    center.y = (r.top + r.bottom) / 2;

    MONITORINFO mi;
    get_monitor_info(MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST), &mi);

    int dl = abs(r.left - mi.rcWork.left);
    int dr = abs(mi.rcWork.right - r.right);
    int dt = abs(r.top - mi.rcWork.top);
    int db = abs(mi.rcWork.bottom - r.bottom);
    int best = SNAP_DISTANCE + 1;
    int edge = EDGE_NONE;

    if (dl < best) { best = dl; edge = EDGE_LEFT; }
    if (dr < best) { best = dr; edge = EDGE_RIGHT; }
    if (dt < best) { best = dt; edge = EDGE_TOP; }
    if (db < best) { best = db; edge = EDGE_BOTTOM; }

    g_dockEdge = edge;
    g_hidden = 0;
    g_visibleRect = r;
    if (edge != EDGE_NONE) place_visible_for_dock();
}

static int resize_hit_test(int x, int y, int w, int h) {
    int L = x < RESIZE_BORDER;
    int R = x >= w - RESIZE_BORDER;
    int T = y < RESIZE_BORDER;
    int B = y >= h - RESIZE_BORDER;

    if (T && L) return HTTOPLEFT;
    if (T && R) return HTTOPRIGHT;
    if (B && L) return HTBOTTOMLEFT;
    if (B && R) return HTBOTTOMRIGHT;
    if (L) return HTLEFT;
    if (R) return HTRIGHT;
    if (T) return HTTOP;
    if (B) return HTBOTTOM;
    return HTCLIENT;
}

static void begin_drag(int mode, int hit) {
    g_dragMode = mode;
    g_dragHit = hit;
    g_inMoveSize = 1;
    g_hidden = 0;
    g_dockEdge = EDGE_NONE;
    GetCursorPos(&g_dragStart);
    GetWindowRect(g_hwnd, &g_dragRect);
    SetCapture(g_hwnd);
}

static void apply_drag(void) {
    if (g_dragMode == DRAG_NONE) return;

    POINT p;
    GetCursorPos(&p);
    int dx = p.x - g_dragStart.x;
    int dy = p.y - g_dragStart.y;
    RECT r = g_dragRect;

    if (g_dragMode == DRAG_MOVE) {
        OffsetRect(&r, dx, dy);
    } else {
        if (g_dragHit == HTLEFT || g_dragHit == HTTOPLEFT || g_dragHit == HTBOTTOMLEFT)
            r.left += dx;
        if (g_dragHit == HTRIGHT || g_dragHit == HTTOPRIGHT || g_dragHit == HTBOTTOMRIGHT)
            r.right += dx;
        if (g_dragHit == HTTOP || g_dragHit == HTTOPLEFT || g_dragHit == HTTOPRIGHT)
            r.top += dy;
        if (g_dragHit == HTBOTTOM || g_dragHit == HTBOTTOMLEFT || g_dragHit == HTBOTTOMRIGHT)
            r.bottom += dy;

        if (r.right - r.left < MIN_W) {
            if (g_dragHit == HTLEFT || g_dragHit == HTTOPLEFT || g_dragHit == HTBOTTOMLEFT)
                r.left = r.right - MIN_W;
            else
                r.right = r.left + MIN_W;
        }
        if (r.bottom - r.top < MIN_H) {
            if (g_dragHit == HTTOP || g_dragHit == HTTOPLEFT || g_dragHit == HTTOPRIGHT)
                r.top = r.bottom - MIN_H;
            else
                r.bottom = r.top + MIN_H;
        }
    }

    SetWindowPos(g_hwnd, z_after(), r.left, r.top,
                 r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void end_drag(void) {
    if (g_dragMode == DRAG_NONE) return;
    ReleaseCapture();
    g_dragMode = DRAG_NONE;
    g_dragHit = HTCLIENT;
    g_inMoveSize = 0;
    GetWindowRect(g_hwnd, &g_visibleRect);
    detect_and_snap();
}

static void create_new_note(void) {
    WCHAR exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return;

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    if (CreateProcessW(exe, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

static void show_menu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, IDM_NEWNOTE, L"新建便签");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (g_alwaysOnTop ? MF_CHECKED : 0), IDM_TOPMOST, L"总在最前");
    AppendMenuW(menu, MF_STRING | (g_autoHide ? MF_CHECKED : 0), IDM_AUTOHIDE, L"贴边自动隐藏");
    AppendMenuW(menu, MF_STRING, IDM_UNDOCK, L"取消贴边");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_FONT_PLUS, L"字体放大");
    AppendMenuW(menu, MF_STRING, IDM_FONT_MINUS, L"字体缩小");
    AppendMenuW(menu, MF_STRING, IDM_CLEAR, L"清空");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出此便签");

    RECT wr;
    GetWindowRect(hwnd, &wr);
    g_menuOpen = 1;
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                             wr.right - TITLE_BTN_W * 2,
                             wr.top + TITLE_H,
                             0, hwnd, NULL);
    g_menuOpen = 0;
    DestroyMenu(menu);

    if (cmd) SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_edit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            RESIZE_BORDER, TITLE_H, 300, 300,
            hwnd, (HMENU)ID_EDIT, GetModuleHandleW(NULL), NULL
        );
        update_font();
        SetTimer(hwnd, TIMER_EDGE, 100, NULL);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT cr;
        GetClientRect(hwnd, &cr);

        FillRect(dc, &cr, g_bodyBrush);
        RECT tr = {0, 0, cr.right, TITLE_H};
        FillRect(dc, &tr, g_titleBrush);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, COL_TITLETEXT);

        RECT title = {12, 0, cr.right - TITLE_BTN_W * 2 - 4, TITLE_H};
        DrawTextW(dc, L"便签", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT menuR = {cr.right - TITLE_BTN_W * 2, 0, cr.right - TITLE_BTN_W, TITLE_H};
        DrawTextW(dc, L"⋯", -1, &menuR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT closeR = {cr.right - TITLE_BTN_W, 0, cr.right, TITLE_H};
        DrawTextW(dc, L"×", -1, &closeR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        layout_edit();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_EDGE) edge_timer();
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        RECT cr;
        GetClientRect(hwnd, &cr);

        /* Buttons win over resize zones so the top-right stays easy to click. */
        if (y < TITLE_H && x >= cr.right - TITLE_BTN_W * 2)
            return 0;

        int hit = resize_hit_test(x, y, cr.right, cr.bottom);
        if (hit != HTCLIENT) {
            begin_drag(DRAG_SIZE, hit);
            return 0;
        }

        if (y < TITLE_H) {
            begin_drag(DRAG_MOVE, HTCAPTION);
            return 0;
        }
        return 0;
    }

    case WM_MOUSEMOVE:
        if (g_dragMode != DRAG_NONE) {
            apply_drag();
            return 0;
        }
        return 0;

    case WM_LBUTTONUP: {
        if (g_dragMode != DRAG_NONE) {
            end_drag();
            return 0;
        }

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        RECT cr;
        GetClientRect(hwnd, &cr);

        if (y >= 0 && y < TITLE_H) {
            if (x >= cr.right - TITLE_BTN_W) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (x >= cr.right - TITLE_BTN_W * 2) {
                show_menu(hwnd);
                return 0;
            }
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (g_dragMode != DRAG_NONE) {
            g_dragMode = DRAG_NONE;
            g_dragHit = HTCLIENT;
            g_inMoveSize = 0;
            GetWindowRect(g_hwnd, &g_visibleRect);
        }
        return 0;

    case WM_SETCURSOR: {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(hwnd, &p);
        RECT cr;
        GetClientRect(hwnd, &cr);
        int hit = resize_hit_test(p.x, p.y, cr.right, cr.bottom);
        LPCTSTR cur = IDC_ARROW;
        if (hit == HTLEFT || hit == HTRIGHT) cur = IDC_SIZEWE;
        else if (hit == HTTOP || hit == HTBOTTOM) cur = IDC_SIZENS;
        else if (hit == HTTOPLEFT || hit == HTBOTTOMRIGHT) cur = IDC_SIZENWSE;
        else if (hit == HTTOPRIGHT || hit == HTBOTTOMLEFT) cur = IDC_SIZENESW;
        SetCursor(LoadCursor(NULL, cur));
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_NEWNOTE:
            create_new_note();
            return 0;
        case IDM_TOPMOST:
            g_alwaysOnTop = !g_alwaysOnTop;
            SetWindowPos(hwnd, z_after(), 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            return 0;
        case IDM_AUTOHIDE:
            g_autoHide = !g_autoHide;
            if (!g_autoHide && g_hidden) show_from_edge();
            return 0;
        case IDM_FONT_PLUS:
            if (g_fontSize < 36) ++g_fontSize;
            update_font();
            return 0;
        case IDM_FONT_MINUS:
            if (g_fontSize > 8) --g_fontSize;
            update_font();
            return 0;
        case IDM_UNDOCK:
            if (g_hidden) show_from_edge();
            g_dockEdge = EDGE_NONE;
            g_hidden = 0;
            return 0;
        case IDM_CLEAR:
            SetWindowTextW(g_edit, L"");
            return 0;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wParam, COL_BODY);
        SetTextColor((HDC)wParam, COL_TEXT);
        return (LRESULT)g_bodyBrush;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_EDGE);
        if (g_editFont) {
            DeleteObject(g_editFont);
            g_editFont = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmd, int show) {
    (void)hPrev;
    (void)cmd;

    /* No mutex / single-instance gate: every launch creates another note. */
    g_bodyBrush = CreateSolidBrush(COL_BODY);
    g_titleBrush = CreateSolidBrush(COL_TITLE);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_bodyBrush;
    wc.lpszClassName = L"EdgeNoteBorderlessClass";
    RegisterClassExW(&wc);

    /* WS_POPUP only: no WS_THICKFRAME, so Windows draws no visible frame. */
    g_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        wc.lpszClassName,
        L"EdgeNote",
        WS_POPUP | WS_CLIPCHILDREN,
        1100, 180, 360, 430,
        NULL, NULL, hInst, NULL
    );

    if (!g_hwnd) return 1;

    GetWindowRect(g_hwnd, &g_visibleRect);
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    if (g_bodyBrush) DeleteObject(g_bodyBrush);
    if (g_titleBrush) DeleteObject(g_titleBrush);
    return 0;
}
