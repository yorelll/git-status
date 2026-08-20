#include "overlay.h"
#include "server.h"
#include "repo_lookup.h"
#include "../common/ipc.h"
#include "../common/pathutil.h"
#include "../common/pipe_client.h"
#include <mutex>
#include <unordered_map>

namespace gs {

LONG g_moduleLocks = 0;

// ── 查询短缓存（250ms）──────────────────────────────
struct QueryEntry { StatusKind st; DWORD tick; };
static std::mutex g_cacheMtx;
static std::unordered_map<std::wstring, QueryEntry> g_queryCache;
static constexpr DWORD kQueryTtlMs = 250;

// 尽力向缓存注册仓库（30s 节流，防多进程重复刷）
static void RegisterRepoIfNeeded(const std::wstring& root) {
    static std::mutex m;
    static std::unordered_map<std::wstring, DWORD> reg;
    std::wstring key = ToLowerW(Backslash(root));
    DWORD now = GetTickCount();
    {
        std::lock_guard g(m);
        auto it = reg.find(key);
        if (it != reg.end() && now - it->second < 30000) return;
        reg[key] = now;
    }
    StatusKind ignored;
    PipeQuery(root, kIpcRegisterRepo, ignored, true);
}

bool CachedQueryStatus(const std::wstring& absPath, StatusKind& out) {
    std::wstring norm = ToLowerW(Backslash(absPath));

    // 1) 仓库定位（带 TTL 缓存）
    std::wstring root = RepoFinder::Instance().Find(absPath);
    if (root.empty()) { out = StatusKind::NotRepo; return true; }

    // 跳过 .git 内部
    if (norm == root || norm.size() > root.size() + 1) {
        std::wstring rel = norm.substr(root.size());
        while (!rel.empty() && rel[0] == L'\\') rel.erase(0, 1);
        if (rel == L".git" || rel.rfind(L".git\\", 0) == 0) {
            out = StatusKind::NotRepo;
            return true;
        }
    }

    // 2) 查询缓存命中
    {
        std::lock_guard g(g_cacheMtx);
        auto it = g_queryCache.find(norm);
        if (it != g_queryCache.end() && GetTickCount() - it->second.tick < kQueryTtlMs) {
            out = it->second.st;
            return true;
        }
    }

    // 3) 确保缓存进程知道该仓库
    RegisterRepoIfNeeded(root);

    // 4) 管道查询
    StatusKind st = StatusKind::Unknown;
    if (!PipeQuery(absPath, kIpcQueryStatus, st, true)) return false;  // 缓存未就绪 → 无图标

    {
        std::lock_guard g(g_cacheMtx);
        if (g_queryCache.size() > 8192) g_queryCache.clear();
        g_queryCache[norm] = { st, GetTickCount() };
    }
    out = st;
    return true;
}

// ── COverlayIcon ─────────────────────────────────────
COverlayIcon::COverlayIcon(OverlayKind kind) : kind_(kind) {}

STDMETHODIMP COverlayIcon::QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IShellIconOverlayIdentifier) {
        *ppv = static_cast<IShellIconOverlayIdentifier*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) COverlayIcon::AddRef() { return InterlockedIncrement(&refs_); }
STDMETHODIMP_(ULONG) COverlayIcon::Release() {
    LONG r = InterlockedDecrement(&refs_);
    if (r == 0) delete this;
    return (ULONG)r;
}

STDMETHODIMP COverlayIcon::GetOverlayInfo(PWSTR pwszIconFile, int cchMax, int* pIndex,
                                          DWORD* pdwFlags) {
    wchar_t dllPath[MAX_PATH * 2] = {0};
    DWORD n = GetModuleFileNameW(g_hInst, dllPath, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2 || (int)n >= cchMax) return E_FAIL;
    lstrcpynW(pwszIconFile, dllPath, cchMax);
    // 图标索引是 0 基的（shell 用 ExtractIconEx 按索引取图标，资源 ID 取不到 → 白图）。
    // 顺序：.rc 中 IDI_CLEAN(0) 在前、IDI_MODIFIED(1) 在后。
    *pIndex = (kind_ == OverlayKind::Clean) ? 0 : 1;
    *pdwFlags = ISIOI_ICONFILE | ISIOI_ICONINDEX;
    return S_OK;
}

STDMETHODIMP COverlayIcon::GetPriority(int* pPriority) {
    *pPriority = 0;  // 最高优先，抢在 OneDrive/Dropbox 前
    return S_OK;
}

STDMETHODIMP COverlayIcon::IsMemberOf(PCWSTR pwszPath, DWORD /*dwAttrib*/) {
    if (!pwszPath || !pwszPath[0]) return S_FALSE;
    std::wstring path(pwszPath);
    if (path.size() < 3 || path[1] != L':') return S_FALSE;  // 只处理盘符路径

    StatusKind st = StatusKind::Unknown;
    if (!CachedQueryStatus(path, st)) return S_FALSE;
    if (st == StatusKind::Clean) return (kind_ == OverlayKind::Clean) ? S_OK : S_FALSE;
    if (st == StatusKind::Modified) return (kind_ == OverlayKind::Modified) ? S_OK : S_FALSE;
    return S_FALSE;  // NotRepo / Unknown
}

// ── COverlayFactory ──────────────────────────────────
COverlayFactory::COverlayFactory(OverlayKind kind) : kind_(kind) {}

STDMETHODIMP COverlayFactory::QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) COverlayFactory::AddRef() { return InterlockedIncrement(&refs_); }
STDMETHODIMP_(ULONG) COverlayFactory::Release() {
    LONG r = InterlockedDecrement(&refs_);
    if (r == 0) delete this;
    return (ULONG)r;
}

STDMETHODIMP COverlayFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) {
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    COverlayIcon* obj = new COverlayIcon(kind_);
    HRESULT hr = obj->QueryInterface(riid, ppv);
    obj->Release();  // 释放初始引用，交给调用方
    return hr;
}

STDMETHODIMP COverlayFactory::LockServer(BOOL fLock) {
    if (fLock) InterlockedIncrement(&g_moduleLocks);
    else InterlockedDecrement(&g_moduleLocks);
    return S_OK;
}

}  // namespace gs
