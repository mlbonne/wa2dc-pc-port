// version.dll proxy for WA2DC.exe: forwards every version.dll export to the
// real system DLL, and replaces the Unity configuration dialog's Input tab with
// a Saves tab (slot list + WP left + New save). The hook only reads saves.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <string>
#include <vector>
#include <algorithm>

using std::wstring;
using std::vector;

static HINSTANCE g_inst;
static HHOOK g_cbt;
static HWND g_dlg, g_tab, g_list, g_play, g_inputPage;
static int g_savesIndex = 1;
static int g_playId = IDOK;
static const UINT_PTR kTimer = 0x5741;
static const int kListId = 2001;

// ======================================================================= real version.dll
static HMODULE RealVersion()
{
    static HMODULE real = NULL;
    if (!real) {
        wchar_t dir[MAX_PATH];
        UINT n = GetSystemDirectoryW(dir, MAX_PATH);
        if (n && n < MAX_PATH) {
            wstring path = wstring(dir, n) + L"\\version.dll";
            real = LoadLibraryW(path.c_str());
        }
    }
    return real;
}

static FARPROC Real(const char* name)
{
    HMODULE h = RealVersion();
    return h ? GetProcAddress(h, name) : NULL;
}

extern "C" BOOL WINAPI fwd_GetFileVersionInfoA(LPCSTR a, DWORD b, DWORD c, LPVOID d)
{ typedef BOOL (WINAPI* F)(LPCSTR,DWORD,DWORD,LPVOID); F f=(F)Real("GetFileVersionInfoA"); return f?f(a,b,c,d):FALSE; }
extern "C" BOOL WINAPI fwd_GetFileVersionInfoW(LPCWSTR a, DWORD b, DWORD c, LPVOID d)
{ typedef BOOL (WINAPI* F)(LPCWSTR,DWORD,DWORD,LPVOID); F f=(F)Real("GetFileVersionInfoW"); return f?f(a,b,c,d):FALSE; }
extern "C" BOOL WINAPI fwd_GetFileVersionInfoByHandle(HANDLE a, DWORD b, LPVOID c, LPDWORD d)
{ typedef BOOL (WINAPI* F)(HANDLE,DWORD,LPVOID,LPDWORD); F f=(F)Real("GetFileVersionInfoByHandle"); return f?f(a,b,c,d):FALSE; }
extern "C" BOOL WINAPI fwd_GetFileVersionInfoExA(DWORD flags, LPCSTR file, DWORD handle, DWORD len, LPVOID data)
{ typedef BOOL (WINAPI* F)(DWORD,LPCSTR,DWORD,DWORD,LPVOID); F f=(F)Real("GetFileVersionInfoExA"); return f?f(flags,file,handle,len,data):FALSE; }
extern "C" BOOL WINAPI fwd_GetFileVersionInfoExW(DWORD flags, LPCWSTR file, DWORD handle, DWORD len, LPVOID data)
{ typedef BOOL (WINAPI* F)(DWORD,LPCWSTR,DWORD,DWORD,LPVOID); F f=(F)Real("GetFileVersionInfoExW"); return f?f(flags,file,handle,len,data):FALSE; }
extern "C" DWORD WINAPI fwd_GetFileVersionInfoSizeA(LPCSTR a, LPDWORD b)
{ typedef DWORD (WINAPI* F)(LPCSTR,LPDWORD); F f=(F)Real("GetFileVersionInfoSizeA"); return f?f(a,b):0; }
extern "C" DWORD WINAPI fwd_GetFileVersionInfoSizeW(LPCWSTR a, LPDWORD b)
{ typedef DWORD (WINAPI* F)(LPCWSTR,LPDWORD); F f=(F)Real("GetFileVersionInfoSizeW"); return f?f(a,b):0; }
extern "C" DWORD WINAPI fwd_GetFileVersionInfoSizeExA(DWORD flags, LPCSTR file, LPDWORD handle)
{ typedef DWORD (WINAPI* F)(DWORD,LPCSTR,LPDWORD); F f=(F)Real("GetFileVersionInfoSizeExA"); return f?f(flags,file,handle):0; }
extern "C" DWORD WINAPI fwd_GetFileVersionInfoSizeExW(DWORD flags, LPCWSTR file, LPDWORD handle)
{ typedef DWORD (WINAPI* F)(DWORD,LPCWSTR,LPDWORD); F f=(F)Real("GetFileVersionInfoSizeExW"); return f?f(flags,file,handle):0; }

extern "C" DWORD WINAPI fwd_VerFindFileA(DWORD flags, LPCSTR file, LPCSTR winDir, LPCSTR appDir, LPSTR curDir, PUINT curLen, LPSTR destDir, PUINT destLen)
{ typedef DWORD (WINAPI* F)(DWORD,LPCSTR,LPCSTR,LPCSTR,LPSTR,PUINT,LPSTR,PUINT); F f=(F)Real("VerFindFileA"); return f?f(flags,file,winDir,appDir,curDir,curLen,destDir,destLen):0; }
extern "C" DWORD WINAPI fwd_VerFindFileW(DWORD flags, LPCWSTR file, LPCWSTR winDir, LPCWSTR appDir, LPWSTR curDir, PUINT curLen, LPWSTR destDir, PUINT destLen)
{ typedef DWORD (WINAPI* F)(DWORD,LPCWSTR,LPCWSTR,LPCWSTR,LPWSTR,PUINT,LPWSTR,PUINT); F f=(F)Real("VerFindFileW"); return f?f(flags,file,winDir,appDir,curDir,curLen,destDir,destLen):0; }
extern "C" DWORD WINAPI fwd_VerInstallFileA(DWORD flags, LPCSTR src, LPCSTR dest, LPCSTR srcDir, LPCSTR destDir, LPSTR tmpFile, PUINT tmpLen)
{ typedef DWORD (WINAPI* F)(DWORD,LPCSTR,LPCSTR,LPCSTR,LPCSTR,LPSTR,PUINT); F f=(F)Real("VerInstallFileA"); return f?f(flags,src,dest,srcDir,destDir,tmpFile,tmpLen):0; }
extern "C" DWORD WINAPI fwd_VerInstallFileW(DWORD flags, LPCWSTR src, LPCWSTR dest, LPCWSTR srcDir, LPCWSTR destDir, LPWSTR tmpFile, PUINT tmpLen)
{ typedef DWORD (WINAPI* F)(DWORD,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,LPWSTR,PUINT); F f=(F)Real("VerInstallFileW"); return f?f(flags,src,dest,srcDir,destDir,tmpFile,tmpLen):0; }
extern "C" BOOL WINAPI fwd_VerQueryValueA(LPCVOID block, LPCSTR sub, LPVOID* buf, PUINT len)
{ typedef BOOL (WINAPI* F)(LPCVOID,LPCSTR,LPVOID*,PUINT); F f=(F)Real("VerQueryValueA"); return f?f(block,sub,buf,len):FALSE; }
extern "C" BOOL WINAPI fwd_VerQueryValueW(LPCVOID block, LPCWSTR sub, LPVOID* buf, PUINT len)
{ typedef BOOL (WINAPI* F)(LPCVOID,LPCWSTR,LPVOID*,PUINT); F f=(F)Real("VerQueryValueW"); return f?f(block,sub,buf,len):FALSE; }

// ======================================================================= Mono 2.0 Random + WP
struct MonoRandom {
    int seed[56]; int inext = 0, inextp = 31;
    explicit MonoRandom(int s) {
        int num = 161803398 - (s < 0 ? -s : s);
        seed[55] = num; int num2 = 1;
        for (int i = 1; i < 55; i++) {
            int n3 = 21 * i % 55;
            seed[n3] = num2;
            num2 = num - num2;
            if (num2 < 0) num2 += 2147483647;
            num = seed[n3];
        }
        for (int j = 1; j < 5; j++)
            for (int k = 1; k < 56; k++) {
                seed[k] -= seed[1 + (k + 30) % 55];
                if (seed[k] < 0) seed[k] += 2147483647;
            }
    }
    double Sample() {
        if (++inext >= 56) inext = 1;
        if (++inextp >= 56) inextp = 1;
        int n = seed[inext] - seed[inextp];
        if (n < 0) n += 2147483647;
        seed[inext] = n;
        return (double)n * 4.656612875245797E-10;
    }
    int Next(int mn, int mx) {
        unsigned int range = (unsigned int)(mx - mn);
        if (range <= 1) return mn;
        return (int)((unsigned int)(Sample() * (double)range) + mn);
    }
};

static int32_t ReadI32(const uint8_t* p)
{ return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }

static void XorFixed(uint8_t* d, size_t n)
{ static const uint8_t k[4] = { 236,119,165,130 }; for (size_t i=0;i<n;i++) d[i]^=k[i&3]; }

static void XorSeed(uint8_t* d, size_t n, int seed)
{ MonoRandom r(seed); for (size_t i=0;i<n;i++) d[i]^=(uint8_t)r.Next(1,127); }

static bool IsItem(int bit, const uint8_t* d) { return (d[bit/8] & (1 << (bit%8))) != 0; }

static int CalcWP(const uint8_t* p) {
    struct F { int off,size,bits,value; };
    static const F f[] = {
        {4,8,64,500},{12,8,64,500},{20,8,64,300},{28,8,64,300},
        {36,4,32,300},{40,4,32,300},{44,8,64,300},{52,4,32,500},
        {56,8,64,200},{64,4,32,700},{68,4,32,700},{72,8,64,200},{80,2,16,200}
    };
    int total=0;
    for (const F& x : f) for (int b=0;b<x.bits;b++) if (IsItem(b,p+x.off)) total += x.value;
    return total;
}

static bool ReadAll(const wstring& path, vector<uint8_t>& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(f,&sz);
    out.resize((size_t)sz.QuadPart);
    DWORD got=0;
    bool ok = out.empty() || (ReadFile(f,out.data(),(DWORD)out.size(),&got,NULL) && got==out.size());
    CloseHandle(f); return ok;
}

static wstring ExeDir() {
    wchar_t self[MAX_PATH]; GetModuleFileNameW(NULL,self,MAX_PATH);
    wstring s = self; size_t p = s.rfind(L'\\'); return p==wstring::npos?s:s.substr(0,p);
}
static wstring Join(const wstring& a, const wstring& b) {
    if (a.empty()) return b;
    wchar_t c=a[a.size()-1]; return (c==L'\\'||c==L'/')?a+b:a+L"\\"+b;
}
static wstring DocsDir() { return Join(ExeDir(), L"Documents"); }

struct Slot { wstring name, arg; bool root=false, isNew=false, hasSave=false; int wp=0; FILETIME time{}; bool hasTime=false; };

static bool DecodeWP(const wstring& savePath, const wstring& sharePath, int& wp) {
    vector<uint8_t> save;
    if (!ReadAll(savePath,save) || save.size()<600) return false;
    const uint8_t* p = save.data()+save.size()-600;
    uint8_t sb[4]; memcpy(sb,p+4,4); XorFixed(sb,4); int seed=ReadI32(sb);
    uint8_t plain[64]; memcpy(plain,p+8,64); XorSeed(plain,64,seed);
    int payload=ReadI32(plain+12);
    int store=0;
    vector<uint8_t> sh;
    if (ReadAll(sharePath,sh) && sh.size()>=636) {
        uint8_t ss[4]; memcpy(ss,sh.data()+4,4); XorFixed(ss,4); int sseed=ReadI32(ss);
        uint8_t sp[628]; memcpy(sp,sh.data()+8,628); XorSeed(sp,628,sseed);
        store=CalcWP(sp);
    }
    int total=payload+store; if (total<0) total=0; if (total>999999) total=999999;
    wp=total; return true;
}

static bool SaveTime(const wstring& path, FILETIME& ft) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if (f==INVALID_HANDLE_VALUE) return false;
    bool ok=GetFileTime(f,NULL,NULL,&ft)!=0; CloseHandle(f); return ok;
}

static void ScanSlots(vector<Slot>& slots) {
    slots.clear();
    wstring docs=DocsDir();
    Slot root; root.name=L"Save 1"; root.root=true;
    wstring rs=Join(docs,L"save.bin");
    root.hasSave=GetFileAttributesW(rs.c_str())!=INVALID_FILE_ATTRIBUTES;
    if (root.hasSave) { DecodeWP(rs,Join(docs,L"share.bin"),root.wp); root.hasTime=SaveTime(rs,root.time); }
    slots.push_back(root);
    wstring saves=Join(docs,L"Saves");
    WIN32_FIND_DATAW fd; HANDLE h=FindFirstFileW((saves+L"\\*").c_str(),&fd);
    if (h!=INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) continue;
            wstring n=fd.cFileName;
            if (n==L"."||n==L".."||n.find(L'\\')!=wstring::npos||n.find(L'/')!=wstring::npos) continue;
            wstring folder=Join(saves,n), save=Join(folder,L"save.bin");
            if (GetFileAttributesW(save.c_str())==INVALID_FILE_ATTRIBUTES &&
                GetFileAttributesW(Join(folder,L"share.bin").c_str())==INVALID_FILE_ATTRIBUTES) continue;
            Slot s; s.name=n; s.arg=L"Saves\\"+n;
            s.hasSave=GetFileAttributesW(save.c_str())!=INVALID_FILE_ATTRIBUTES;
            if (s.hasSave) { DecodeWP(save,Join(folder,L"share.bin"),s.wp); s.hasTime=SaveTime(save,s.time); }
            slots.push_back(s);
        } while (FindNextFileW(h,&fd));
        FindClose(h);
    }
    if (slots.size()>2) std::sort(slots.begin()+1,slots.end(),[](const Slot&a,const Slot&b){
        int na=0, nb=0;
        if (swscanf_s(a.name.c_str(),L"Save %d",&na)==1 &&
            swscanf_s(b.name.c_str(),L"Save %d",&nb)==1) return na<nb;
        return a.name<b.name;
    });
    int next=2; wchar_t name[32];
    while (true) { swprintf_s(name,L"Save %d",next); if (GetFileAttributesW(Join(saves,name).c_str())==INVALID_FILE_ATTRIBUTES) break; next++; }
    Slot fresh; fresh.name=name; fresh.arg=L"Saves\\"+fresh.name; fresh.isNew=true; slots.push_back(fresh);
}

// ======================================================================= dialog
static void FormatTime(const FILETIME& ft, wchar_t* out, size_t n) {
    FILETIME l; SYSTEMTIME st; FileTimeToLocalFileTime(&ft,&l); FileTimeToSystemTime(&l,&st);
    swprintf_s(out,n,L"%04d-%02d-%02d %02d:%02d",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute);
}

static void RenameTab() {
    if (!g_tab) return;
    int count=TabCtrl_GetItemCount(g_tab);
    for (int i=0;i<count;i++) {
        wchar_t text[64]; TCITEMW it={}; it.mask=TCIF_TEXT; it.pszText=text; it.cchTextMax=64;
        TabCtrl_GetItem(g_tab,i,&it);
        if (_wcsicmp(text,L"Input")==0 || (i==g_savesIndex && _wcsicmp(text,L"Saves")!=0)) {
            g_savesIndex=i; break;
        }
    }
    TCITEMW it={}; it.mask=TCIF_TEXT; it.pszText=(LPWSTR)L"Saves";
    TabCtrl_SetItem(g_tab,g_savesIndex,&it);
}

// Unity's Input page is a caption-less child dialog inside the tab; its axis
// ListView has control id 1017. The Graphics page is also a caption-less child
// dialog but has no 1017 ListView.
static BOOL CALLBACK FindIdProc(HWND h, LPARAM lp) {
    if (GetDlgCtrlID(h)==1017) { *(HWND*)lp=h; return FALSE; }
    return TRUE;
}

static HWND FindInputPage(HWND dlg) {
    HWND page=NULL;
    while ((page=FindWindowExW(dlg,page,L"#32770",NULL))!=NULL) {
        LONG_PTR style=GetWindowLongPtrW(page,GWL_STYLE);
        if (style & WS_CAPTION) continue;
        HWND found=NULL;
        EnumChildWindows(page,FindIdProc,(LPARAM)&found);
        if (found) return page;
    }
    return NULL;
}

static void UpdatePage() {
    if (!g_tab || !g_list) return;
    int sel=TabCtrl_GetCurSel(g_tab);
    if (sel==g_savesIndex) {
        // Unity's Input page is a sibling of our ListView; hide it after
        // Unity's own tab handler has run, then put our list on top.
        if (g_inputPage) ShowWindow(g_inputPage,SW_HIDE);
        ShowWindow(g_list,SW_SHOW);
        SetWindowPos(g_list,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        InvalidateRect(g_list,NULL,TRUE);
        RedrawWindow(g_dlg,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
        SetFocus(g_list);
    } else {
        ShowWindow(g_list,SW_HIDE);
    }
}

static void FillList() {
    vector<Slot> slots; ScanSlots(slots);
    ListView_DeleteAllItems(g_list);
    for (size_t i=0;i<slots.size();i++) {
        wchar_t wp[64]; if (slots[i].isNew) wcscpy_s(wp,L"New save"); else swprintf_s(wp,L"%d",slots[i].wp);
        wchar_t when[64]=L""; if (slots[i].hasTime) FormatTime(slots[i].time,when,64); else wcscpy_s(when,L"not played yet");
        LVITEMW it={}; it.mask=LVIF_TEXT; it.iItem=(int)i; it.pszText=(LPWSTR)slots[i].name.c_str();
        int row=ListView_InsertItem(g_list,&it);
        ListView_SetItemText(g_list,row,1,wp);
        ListView_SetItemText(g_list,row,2,when);
    }
    if (!slots.empty()) {
        LVITEMW it={}; it.mask=LVIF_STATE; it.iItem=0; it.state=LVIS_SELECTED|LVIS_FOCUSED; it.stateMask=LVIS_SELECTED|LVIS_FOCUSED;
        SendMessageW(g_list, LVM_SETITEMSTATE, 0, (LPARAM)&it);
    }
}

static bool CreateList(HWND dlg) {
    RECT page; GetClientRect(g_tab,&page);
    TabCtrl_AdjustRect(g_tab,FALSE,&page);
    MapWindowPoints(g_tab,dlg,(POINT*)&page,2);
    g_list=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",
        WS_CHILD|WS_CLIPSIBLINGS|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
        page.left,page.top,page.right-page.left,page.bottom-page.top,
        dlg,(HMENU)(INT_PTR)kListId,NULL,NULL);
    if (!g_list) return false;
    SetWindowPos(g_list,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    ListView_SetExtendedListViewStyle(g_list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW col={}; col.mask=LVCF_TEXT|LVCF_WIDTH;
    col.pszText=(LPWSTR)L"Save"; col.cx=120; ListView_InsertColumn(g_list,0,&col);
    col.pszText=(LPWSTR)L"WP left"; col.cx=90; ListView_InsertColumn(g_list,1,&col);
    col.pszText=(LPWSTR)L"Last played"; col.cx=150; ListView_InsertColumn(g_list,2,&col);
    HFONT font=(HFONT)SendMessageW(dlg,WM_GETFONT,0,0);
    if (!font) font=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessageW(g_list,WM_SETFONT,(WPARAM)font,TRUE);
    FillList();
    ShowWindow(g_list,SW_HIDE);
    return true;
}

static void SetSlotEnv() {
    int row=ListView_GetNextItem(g_list,-1,LVNI_SELECTED);
    vector<Slot> slots; ScanSlots(slots);
    if (row<0 || row>=(int)slots.size()) row=0;
    if (row<0 || row>=(int)slots.size()) return;
    Slot& s=slots[row];
    if (s.isNew) {
        wstring saves=Join(DocsDir(),L"Saves");
        CreateDirectoryW(DocsDir().c_str(),NULL);
        CreateDirectoryW(saves.c_str(),NULL);
        CreateDirectoryW(Join(saves,s.name).c_str(),NULL);
    }
    SetEnvironmentVariableW(L"WA2DC_SAVE_SLOT", s.root ? L"" : s.arg.c_str());
}

static bool SetupDialog(HWND dlg) {
    g_dlg=dlg;
    g_tab=FindWindowExW(dlg,NULL,L"SysTabControl32",NULL);
    if (!g_tab) return false;
    int count=TabCtrl_GetItemCount(g_tab);
    g_savesIndex=1;
    for (int i=0;i<count;i++) {
        wchar_t text[64]; TCITEMW it={}; it.mask=TCIF_TEXT; it.pszText=text; it.cchTextMax=64;
        TabCtrl_GetItem(g_tab,i,&it);
        if (_wcsicmp(text,L"Input")==0) { g_savesIndex=i; break; }
    }
    if (g_savesIndex<0 || g_savesIndex>=count) g_savesIndex=1;
    RenameTab();
    g_play=FindWindowExW(dlg,NULL,L"Button",L"Play!");
    if (g_play) g_playId=GetDlgCtrlID(g_play);
    g_inputPage=FindInputPage(dlg);
    if (!CreateList(dlg)) return false;
    UpdatePage();
    return true;
}

static int g_setupTries;

static LRESULT CALLBACK DialogProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                  UINT_PTR, DWORD_PTR)
{
    switch (msg) {
    case WM_TIMER:
        if (wp==kTimer) {
            if (SetupDialog(h)) {
                KillTimer(h,kTimer);
                if (g_cbt) { UnhookWindowsHookEx(g_cbt); g_cbt=NULL; }
            } else if (++g_setupTries > 30) {
                KillTimer(h,kTimer);
                RemoveWindowSubclass(h,DialogProc,1);
            }
            return 0;
        }
        break;
    case WM_NOTIFY: {
        NMHDR* hdr=(NMHDR*)lp;
        bool tabChanged = hdr && hdr->hwndFrom==g_tab && hdr->code==TCN_SELCHANGE;
        // Let Unity's own tab handler show/hide the pages first, then hide the
        // Input page again and bring our list to the front.
        LRESULT result = DefSubclassProc(h,msg,wp,lp);
        if (tabChanged) {
            RenameTab();
            UpdatePage();
        }
        return result;
    }
    case WM_COMMAND:
        if (LOWORD(wp)==g_playId) {
            SetSlotEnv();
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(h,DialogProc,1);
        g_dlg=g_tab=g_list=g_play=g_inputPage=NULL;
        break;
    }
    return DefSubclassProc(h,msg,wp,lp);
}

static LRESULT CALLBACK CbtProc(int code, WPARAM wp, LPARAM lp)
{
    if (code==HCBT_CREATEWND) {
        HWND h=(HWND)wp;
        wchar_t cls[64]={0};
        GetClassNameW(h,cls,64);
        if (_wcsicmp(cls,L"#32770")==0) {
            LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
            if (style & WS_CAPTION) {
                g_setupTries = 0;
                SetWindowSubclass(h,DialogProc,1,0);
                SetTimer(h,kTimer,100,NULL);
            }
        }
    }
    return CallNextHookEx(g_cbt,code,wp,lp);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason==DLL_PROCESS_ATTACH) {
        g_inst=inst;
        DisableThreadLibraryCalls(inst);
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES };
        InitCommonControlsEx(&icc);
        g_cbt=SetWindowsHookExW(WH_CBT,CbtProc,inst,GetCurrentThreadId());
    } else if (reason==DLL_PROCESS_DETACH) {
        if (g_cbt) { UnhookWindowsHookEx(g_cbt); g_cbt=NULL; }
    }
    return TRUE;
}
