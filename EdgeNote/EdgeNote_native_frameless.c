// EdgeNote - native Win32 frameless sticky note for Windows 10/11 x64
// Standalone PE executable, no external runtime.

// Full source is maintained from the generated build used for EdgeNote_Frameless_v3.exe.
// This repository copy corresponds to the frameless Win32 build with four-edge auto-hide.

#include <windows.h>

#define ID_EDIT 1001
#define IDM_TOPMOST 2001
#define IDM_AUTOHIDE 2002
#define IDM_FONT_PLUS 2003
#define IDM_FONT_MINUS 2004
#define IDM_UNDOCK 2005
#define IDM_CLEAR 2006
#define IDM_EXIT 2007

#define TIMER_EDGE 1
#define TIMER_SAVE 2
#define EDGE_NONE 0
#define EDGE_LEFT 1
#define EDGE_RIGHT 2
#define EDGE_TOP 3
#define EDGE_BOTTOM 4
#define SNAP_DISTANCE 32
#define HIDDEN_SLIVER 4
#define EDGE_TRIGGER 3
#define HIDE_TICKS 7
#define MIN_W 260
#define MIN_H 180
#define TITLE_H 34
#define RESIZE_BORDER 7
#define TITLE_BTN_W 38

static HWND g_hwnd, g_edit;
static HBRUSH g_bodyBrush, g_titleBrush;
static HFONT g_editFont, g_titleFont;
static RECT g_visibleRect;
static int g_dockEdge = EDGE_NONE;
static int g_hidden = 0;
static int g_hideCounter = 0;
static int g_alwaysOnTop = 1;
static int g_autoHide = 1;
static int g_fontSize = 13;
static int g_inMoveSize = 0;
static int g_menuOpen = 0;

static const COLORREF COL_BODY = RGB(247,229,142);
static const COLORREF COL_TITLE = RGB(228,201,85);
static const COLORREF COL_TEXT = RGB(47,44,30);
static const COLORREF COL_TITLETEXT = RGB(81,71,17);

static HMONITOR monitor_for_visible_rect(void) {
    POINT p = {(g_visibleRect.left + g_visibleRect.right)/2, (g_visibleRect.top + g_visibleRect.bottom)/2};
    return MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
}

static void get_monitor_info(HMONITOR mon, MONITORINFO *mi) {
    ZeroMemory(mi, sizeof(*mi)); mi->cbSize = sizeof(*mi); GetMonitorInfo(mon, mi);
}

static HWND z_after(void) { return g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST; }

static void place_visible_for_dock(void) {
    MONITORINFO mi; get_monitor_info(monitor_for_visible_rect(), &mi);
    int w = g_visibleRect.right - g_visibleRect.left;
    int h = g_visibleRect.bottom - g_visibleRect.top;
    int x = g_visibleRect.left, y = g_visibleRect.top;
    if (g_dockEdge == EDGE_LEFT) x = mi.rcWork.left;
    else if (g_dockEdge == EDGE_RIGHT) x = mi.rcWork.right - w;
    else if (g_dockEdge == EDGE_TOP) y = mi.rcWork.top;
    else if (g_dockEdge == EDGE_BOTTOM) y = mi.rcWork.bottom - h;
    SetWindowPos(g_hwnd, z_after(), x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    GetWindowRect(g_hwnd, &g_visibleRect);
}

static void show_from_edge(void) {
    if (!g_hidden) return;
    place_visible_for_dock();
    g_hidden = 0; g_hideCounter = 0;
}

static void hide_to_edge(void) {
    if (g_dockEdge == EDGE_NONE || !g_autoHide) return;
    MONITORINFO mi; get_monitor_info(monitor_for_visible_rect(), &mi);
    int w = g_visibleRect.right - g_visibleRect.left;
    int h = g_visibleRect.bottom - g_visibleRect.top;
    int x = g_visibleRect.left, y = g_visibleRect.top;
    if (g_dockEdge == EDGE_LEFT) x = mi.rcMonitor.left - w + HIDDEN_SLIVER;
    else if (g_dockEdge == EDGE_RIGHT) x = mi.rcMonitor.right - HIDDEN_SLIVER;
    else if (g_dockEdge == EDGE_TOP) y = mi.rcMonitor.top - h + HIDDEN_SLIVER;
    else if (g_dockEdge == EDGE_BOTTOM) y = mi.rcMonitor.bottom - HIDDEN_SLIVER;
    SetWindowPos(g_hwnd, z_after(), x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_hidden = 1; g_hideCounter = 0;
}

static int point_in_rect_margin(POINT p, RECT r, int margin) {
    return p.x >= r.left-margin && p.x <= r.right+margin && p.y >= r.top-margin && p.y <= r.bottom+margin;
}

static void edge_timer(void) {
    if (g_dockEdge == EDGE_NONE || !g_autoHide || g_inMoveSize || g_menuOpen) return;
    POINT p; if (!GetCursorPos(&p)) return;
    MONITORINFO mi; get_monitor_info(monitor_for_visible_rect(), &mi);
    if (g_hidden) {
        int hit = 0;
        if (g_dockEdge == EDGE_LEFT && p.x <= mi.rcMonitor.left + EDGE_TRIGGER && p.y >= g_visibleRect.top-20 && p.y <= g_visibleRect.bottom+20) hit = 1;
        else if (g_dockEdge == EDGE_RIGHT && p.x >= mi.rcMonitor.right-1-EDGE_TRIGGER && p.y >= g_visibleRect.top-20 && p.y <= g_visibleRect.bottom+20) hit = 1;
        else if (g_dockEdge == EDGE_TOP && p.y <= mi.rcMonitor.top + EDGE_TRIGGER && p.x >= g_visibleRect.left-20 && p.x <= g_visibleRect.right+20) hit = 1;
        else if (g_dockEdge == EDGE_BOTTOM && p.y >= mi.rcMonitor.bottom-1-EDGE_TRIGGER && p.x >= g_visibleRect.left-20 && p.x <= g_visibleRect.right+20) hit = 1;
        if (hit) show_from_edge();
    } else {
        RECT r; GetWindowRect(g_hwnd, &r);
        if (!point_in_rect_margin(p, r, 8)) {
            if (++g_hideCounter >= HIDE_TICKS) hide_to_edge();
        } else g_hideCounter = 0;
    }
}

static void detect_and_snap(void) {
    RECT r; GetWindowRect(g_hwnd, &r);
    POINT center = {(r.left+r.right)/2, (r.top+r.bottom)/2};
    MONITORINFO mi; get_monitor_info(MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST), &mi);
    int dl = abs(r.left-mi.rcWork.left), dr = abs(mi.rcWork.right-r.right);
    int dt = abs(r.top-mi.rcWork.top), db = abs(mi.rcWork.bottom-r.bottom);
    int best = SNAP_DISTANCE+1, edge = EDGE_NONE;
    if (dl < best) {best=dl; edge=EDGE_LEFT;} if (dr < best) {best=dr; edge=EDGE_RIGHT;}
    if (dt < best) {best=dt; edge=EDGE_TOP;} if (db < best) {best=db; edge=EDGE_BOTTOM;}
    g_dockEdge = edge; g_hidden = 0; g_visibleRect = r;
    if (edge != EDGE_NONE) place_visible_for_dock();
}

static void layout_edit(void) {
    if (!g_edit) return;
    RECT cr; GetClientRect(g_hwnd, &cr);
    int pad=7;
    MoveWindow(g_edit, pad, TITLE_H+2, cr.right-pad*2, cr.bottom-TITLE_H-pad-2, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,
            7,TITLE_H+2,300,300,hwnd,(HMENU)ID_EDIT,GetModuleHandleW(NULL),NULL);
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        SetTimer(hwnd, TIMER_EDGE, 100, NULL);
        return 0;
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;
    case WM_NCHITTEST: {
        RECT wr; GetWindowRect(hwnd,&wr);
        int x=GET_X_LPARAM(lParam), y=GET_Y_LPARAM(lParam);
        int lx=x-wr.left, ly=y-wr.top, w=wr.right-wr.left, h=wr.bottom-wr.top;
        int L=lx<RESIZE_BORDER, R=lx>=w-RESIZE_BORDER, T=ly<RESIZE_BORDER, B=ly>=h-RESIZE_BORDER;
        if(T&&L)return HTTOPLEFT; if(T&&R)return HTTOPRIGHT; if(B&&L)return HTBOTTOMLEFT; if(B&&R)return HTBOTTOMRIGHT;
        if(L)return HTLEFT; if(R)return HTRIGHT; if(T)return HTTOP; if(B)return HTBOTTOM;
        if(ly<TITLE_H && lx<w-TITLE_BTN_W*2) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps); RECT cr; GetClientRect(hwnd,&cr);
        FillRect(dc,&cr,g_bodyBrush); RECT tr={0,0,cr.right,TITLE_H}; FillRect(dc,&tr,g_titleBrush);
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,COL_TITLETEXT);
        RECT title={12,0,cr.right-TITLE_BTN_W*2-4,TITLE_H}; DrawTextW(dc,L"便签",-1,&title,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT menuR={cr.right-TITLE_BTN_W*2,0,cr.right-TITLE_BTN_W,TITLE_H}; DrawTextW(dc,L"⋯",-1,&menuR,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT closeR={cr.right-TITLE_BTN_W,0,cr.right,TITLE_H}; DrawTextW(dc,L"×",-1,&closeR,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        EndPaint(hwnd,&ps); return 0;
    }
    case WM_SIZE: layout_edit(); InvalidateRect(hwnd,NULL,FALSE); return 0;
    case WM_TIMER: if(wParam==TIMER_EDGE) edge_timer(); return 0;
    case WM_ENTERSIZEMOVE: g_inMoveSize=1; return 0;
    case WM_EXITSIZEMOVE: g_inMoveSize=0; detect_and_snap(); return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm=(MINMAXINFO*)lParam; mm->ptMinTrackSize.x=MIN_W; mm->ptMinTrackSize.y=MIN_H; return 0;
    }
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wParam,COL_BODY); SetTextColor((HDC)wParam,COL_TEXT); return (LRESULT)g_bodyBrush;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}

int WINAPI wWinMain(HINSTANCE hInst,HINSTANCE hPrev,LPWSTR cmd,int show) {
    (void)hPrev; (void)cmd;
    g_bodyBrush=CreateSolidBrush(COL_BODY); g_titleBrush=CreateSolidBrush(COL_TITLE);
    WNDCLASSEXW wc={sizeof(wc)}; wc.lpfnWndProc=WndProc; wc.hInstance=hInst; wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=g_bodyBrush; wc.lpszClassName=L"EdgeNoteFramelessClass"; RegisterClassExW(&wc);
    g_hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"EdgeNote",
        WS_POPUP|WS_THICKFRAME|WS_CLIPCHILDREN,1100,180,360,430,NULL,NULL,hInst,NULL);
    GetWindowRect(g_hwnd,&g_visibleRect); ShowWindow(g_hwnd,show); UpdateWindow(g_hwnd);
    MSG m; while(GetMessageW(&m,NULL,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);} return 0;
}
