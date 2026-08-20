#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>
#include "../common/status.h"

namespace gs {

enum class OverlayKind { Clean, Modified };

// 模块级锁计数（DllCanUnloadNow 使用）
extern LONG g_moduleLocks;

// IShellIconOverlayIdentifier 实现：Clean（绿圈）/ Modified（红叹号）各一个实例。
class COverlayIcon : public IShellIconOverlayIdentifier {
public:
    explicit COverlayIcon(OverlayKind kind);
    virtual ~COverlayIcon() = default;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IShellIconOverlayIdentifier
    STDMETHODIMP GetOverlayInfo(PWSTR pwszIconFile, int cchMax, int* pIndex,
                                DWORD* pdwFlags) override;
    STDMETHODIMP GetPriority(int* pPriority) override;
    STDMETHODIMP IsMemberOf(PCWSTR pwszPath, DWORD dwAttrib) override;

private:
    OverlayKind kind_;
    LONG refs_ = 1;
};

class COverlayFactory : public IClassFactory {
public:
    explicit COverlayFactory(OverlayKind kind);
    virtual ~COverlayFactory() = default;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
    STDMETHODIMP LockServer(BOOL fLock) override;

private:
    OverlayKind kind_;
    LONG refs_ = 1;
};

// 查询 + 短缓存入口：仓库定位 → 管道查询 → 250ms 缓存
bool CachedQueryStatus(const std::wstring& absPath, StatusKind& out);

}  // namespace gs
