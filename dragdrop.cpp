// dragdrop.cpp
//
// Compile this file as C++ (e.g. `cl /c /EHsc dragdrop.cpp`).
// It implements the plain-C API declared in dragdrop.h, wrapping the
// CDropTarget / CDropSource / CFileDataObject COM classes from the earlier
// example. Link the resulting .obj together with your C89 main.c
// (link.exe / your linker doesn't care that the .obj came from C++; you
// just need to link against ole32.lib, shell32.lib, and the C++ runtime).

#include "dragdrop.h"
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <atomic>
#include <vector>
#include <string>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

// --- ref-counting base, drop target/source/data object -------------------
// (identical to the previous example; trimmed comments for brevity)

template <typename Interface>
class RefCountedUnknown : public Interface {
public:
    RefCountedUnknown() : m_ref(1) {}
    STDMETHOD_(ULONG, AddRef)() override { return ++m_ref; }
    STDMETHOD_(ULONG, Release)() override {
        LONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
protected:
    virtual ~RefCountedUnknown() = default;
private:
    std::atomic<LONG> m_ref;
};

static DD_OnFilesDroppedFn g_callback = nullptr;
static void* g_callbackUserdata = nullptr;

class CDropTarget : public RefCountedUnknown<IDropTarget> {
public:
    explicit CDropTarget(HWND hwnd) : m_hwnd(hwnd), m_bAcceptFmt(false) {}

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHOD(DragEnter)(IDataObject* pDataObj, DWORD, POINTL, DWORD* pdwEffect) override {
        FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        m_bAcceptFmt = (pDataObj->QueryGetData(&fmt) == S_OK);
        *pdwEffect = m_bAcceptFmt ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }

    STDMETHOD(DragOver)(DWORD, POINTL, DWORD* pdwEffect) override {
        *pdwEffect = m_bAcceptFmt ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }

    STDMETHOD(DragLeave)() override {
        m_bAcceptFmt = false;
        return S_OK;
    }

    STDMETHOD(Drop)(IDataObject* pDataObj, DWORD, POINTL, DWORD* pdwEffect) override {
        *pdwEffect = DROPEFFECT_NONE;
        FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM stg;
        if (FAILED(pDataObj->GetData(&fmt, &stg)))
            return S_OK;

        HDROP hDrop = static_cast<HDROP>(GlobalLock(stg.hGlobal));
        if (hDrop) {
            UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::wstring> paths(count);
            std::vector<const wchar_t*> cpaths(count);
            for (UINT i = 0; i < count; ++i) {
                wchar_t path[MAX_PATH];
                DragQueryFileW(hDrop, i, path, MAX_PATH);
                paths[i] = path;
            }
            for (UINT i = 0; i < count; ++i) cpaths[i] = paths[i].c_str();
            GlobalUnlock(stg.hGlobal);

            // Hand the file list back to the C89 side instead of handling it here.
            if (g_callback) {
                g_callback(cpaths.data(), static_cast<int>(count), g_callbackUserdata);
            }
        }
        ReleaseStgMedium(&stg);
        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }

private:
    HWND m_hwnd;
    bool m_bAcceptFmt;
};

class CDropSource : public RefCountedUnknown<IDropSource> {
public:
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD(QueryContinueDrag)(BOOL fEscapePressed, DWORD grfKeyState) override {
        if (fEscapePressed) return DRAGDROP_S_CANCEL;
        if (!(grfKeyState & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    STDMETHOD(GiveFeedback)(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};

class CFileDataObject : public RefCountedUnknown<IDataObject> {
public:
    explicit CFileDataObject(const std::vector<std::wstring>& files) : m_files(files) {}

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHOD(GetData)(FORMATETC* pFormatEtc, STGMEDIUM* pMedium) override {
        if (pFormatEtc->cfFormat != CF_HDROP || pFormatEtc->tymed != TYMED_HGLOBAL)
            return DV_E_FORMATETC;

        size_t bufSize = sizeof(DROPFILES) + sizeof(wchar_t);
        for (auto& f : m_files) bufSize += (f.size() + 1) * sizeof(wchar_t);

        HGLOBAL hGlobal = GlobalAlloc(GHND, bufSize);
        if (!hGlobal) return E_OUTOFMEMORY;

        auto* df = static_cast<DROPFILES*>(GlobalLock(hGlobal));
        df->pFiles = sizeof(DROPFILES);
        df->fWide = TRUE;
        wchar_t* dst = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES));
        for (auto& f : m_files) {
            wcscpy_s(dst, f.size() + 1, f.c_str());
            dst += f.size() + 1;
        }
        *dst = L'\0';
        GlobalUnlock(hGlobal);

        pMedium->tymed = TYMED_HGLOBAL;
        pMedium->hGlobal = hGlobal;
        pMedium->pUnkForRelease = nullptr;
        return S_OK;
    }

    STDMETHOD(QueryGetData)(FORMATETC* pFormatEtc) override {
        return (pFormatEtc->cfFormat == CF_HDROP && pFormatEtc->tymed & TYMED_HGLOBAL) ? S_OK : DV_E_FORMATETC;
    }
    STDMETHOD(GetDataHere)(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    STDMETHOD(GetCanonicalFormatEtc)(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    STDMETHOD(SetData)(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    STDMETHOD(EnumFormatEtc)(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
    STDMETHOD(DAdvise)(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return E_NOTIMPL; }
    STDMETHOD(DUnadvise)(DWORD) override { return E_NOTIMPL; }
    STDMETHOD(EnumDAdvise)(IEnumSTATDATA**) override { return E_NOTIMPL; }

private:
    std::vector<std::wstring> m_files;
};

// --- extern "C" boundary: this is the only part your C89 code touches ----

static IDropTarget* g_pDropTarget = nullptr;

extern "C" int DD_Initialize(void) {
    return SUCCEEDED(OleInitialize(nullptr)) ? 0 : -1;
}

extern "C" void DD_Uninitialize(void) {
    OleUninitialize();
}

extern "C" int DD_EnableDropTarget(HWND hwnd) {
    g_pDropTarget = new CDropTarget(hwnd);
    return SUCCEEDED(RegisterDragDrop(hwnd, g_pDropTarget)) ? 0 : -1;
}

extern "C" void DD_DisableDropTarget(HWND hwnd) {
    RevokeDragDrop(hwnd);
    if (g_pDropTarget) { g_pDropTarget->Release(); g_pDropTarget = nullptr; }
}

extern "C" int DD_BeginFileDragOne(const wchar_t* path) {
    std::vector<std::wstring> files;
    files.emplace_back(path);

    CFileDataObject* pDataObj = new CFileDataObject(files);
    CDropSource* pDropSource = new CDropSource();

    DWORD dwEffect = 0;
    HRESULT hr = DoDragDrop(pDataObj, pDropSource, DROPEFFECT_COPY | DROPEFFECT_MOVE, &dwEffect);

    pDataObj->Release();
    pDropSource->Release();
    return SUCCEEDED(hr) ? 0 : -1;
}

extern "C" int DD_BeginFileDrag(const wchar_t** paths, int count) {
    std::vector<std::wstring> files;
    files.reserve(count);
    for (int i = 0; i < count; ++i) files.emplace_back(paths[i]);

    CFileDataObject* pDataObj = new CFileDataObject(files);
    CDropSource* pDropSource = new CDropSource();

    DWORD dwEffect = 0;
    HRESULT hr = DoDragDrop(pDataObj, pDropSource, DROPEFFECT_COPY | DROPEFFECT_MOVE, &dwEffect);

    pDataObj->Release();
    pDropSource->Release();
    return SUCCEEDED(hr) ? 0 : -1;
}

extern "C" void DD_SetDropCallback(DD_OnFilesDroppedFn callback, void* userdata) {
    g_callback = callback;
    g_callbackUserdata = userdata;
}

/* SHLWAPI replacements */

static LRESULT CALLBACK WorkerWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

extern "C" HWND MyCreateWorkerWindow(WNDPROC wndProc, HWND hWndParent, DWORD dwExStyle, DWORD dwStyle, HMENU hMenu, LPVOID wnd_data)
{
    static const wchar_t* kClassName = L"MyContextMenuWorkerClass";
    static BOOL registered = FALSE;

    if (!registered)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = wndProc ? wndProc : DefWindowProcW;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = kClassName;
        wc.cbWndExtra = sizeof(LONG_PTR);

        ATOM atom = RegisterClassExW(&wc);
        if (atom == 0)
        {
            DWORD err = GetLastError();
            if (err != ERROR_CLASS_ALREADY_EXISTS)
            {
                // real failure ¡X log err, bail out, don't set registered = TRUE
                return NULL;
            }
        }
        registered = TRUE;
    }
    HWND hWndpp, hWnd;
    // check if null dwStyle
    if (dwStyle == 0)
        dwStyle = WS_POPUP;
    // find most parent
    while (hWndpp = GetParent(hWndParent)) {
        if (hWndpp) hWndParent = hWndpp;
        else break;
    }
    hWnd = CreateWindowExW(
        dwExStyle, kClassName, L"", dwStyle,
        0, 0, 0, 0,
        hWndParent, NULL, GetModuleHandle(NULL), NULL);
    if (!hWnd) {
        DWORD err = GetLastError();
        return NULL;
    }
    if (hWnd && wnd_data)
        SetWindowLongPtr(hWnd, 0, (LONG_PTR)wnd_data);

    return hWnd;
}

extern "C" HRESULT MyForwardContextMenuMsg(
    IUnknown* pcm, UINT uMsg, WPARAM wParam, LPARAM lParam,
    LRESULT* plResult, BOOL fDefaultHandling)
{
    if (!pcm)
        return E_NOINTERFACE;

    HRESULT hr = E_NOTIMPL;

    IContextMenu3* pcm3 = NULL;
    if (SUCCEEDED(pcm->QueryInterface(IID_IContextMenu3, (void**)&pcm3)) && pcm3)
    {
        hr = pcm3->HandleMenuMsg2(uMsg, wParam, lParam, plResult);
        pcm3->Release();
    }

    if (hr == E_NOTIMPL)
    {
        IContextMenu2* pcm2 = NULL;
        if (SUCCEEDED(pcm->QueryInterface(IID_IContextMenu2, (void**)&pcm2)) && pcm2)
        {
            hr = pcm2->HandleMenuMsg(uMsg, wParam, lParam);
            if (SUCCEEDED(hr) && plResult)
                *plResult = 0; // HandleMenuMsg has no out-result; caller wanted 0
            pcm2->Release();
        }
    }

    if (hr == E_NOTIMPL && fDefaultHandling && plResult)
        *plResult = 0;

    return hr;
}
