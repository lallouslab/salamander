// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "HDropWideBuilder.h"

#include <objidl.h>
#include <vector>

namespace sally
{
namespace clipboard
{
class HDropWideEnumFormatEtc : public IEnumFORMATETC
{
private:
    volatile LONG RefCount;
    BOOL Consumed;

public:
    explicit HDropWideEnumFormatEtc(BOOL consumed = FALSE)
        : RefCount(1), Consumed(consumed)
    {
    }

    STDMETHOD(QueryInterface)
    (REFIID iid, void** ppv) override
    {
        if (ppv == NULL)
            return E_POINTER;

        if (iid == IID_IUnknown || iid == IID_IEnumFORMATETC)
        {
            *ppv = this;
            AddRef();
            return S_OK;
        }

        *ppv = NULL;
        return E_NOINTERFACE;
    }

    STDMETHOD_(ULONG, AddRef)
    (void) override
    {
        return static_cast<ULONG>(InterlockedIncrement(&RefCount));
    }

    STDMETHOD_(ULONG, Release)
    (void) override
    {
        ULONG refs = static_cast<ULONG>(InterlockedDecrement(&RefCount));
        if (refs == 0)
            delete this;
        return refs;
    }

    STDMETHOD(Next)
    (ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched) override
    {
        if (rgelt == NULL)
            return E_POINTER;
        if (celt > 1 && pceltFetched == NULL)
            return E_INVALIDARG;
        if (pceltFetched != NULL)
            *pceltFetched = 0;
        if (celt == 0 || Consumed)
            return S_FALSE;

        rgelt[0].cfFormat = CF_HDROP;
        rgelt[0].ptd = NULL;
        rgelt[0].dwAspect = DVASPECT_CONTENT;
        rgelt[0].lindex = -1;
        rgelt[0].tymed = TYMED_HGLOBAL;
        Consumed = TRUE;

        if (pceltFetched != NULL)
            *pceltFetched = 1;
        return celt == 1 ? S_OK : S_FALSE;
    }

    STDMETHOD(Skip)
    (ULONG celt) override
    {
        if (Consumed || celt == 0)
            return S_FALSE;
        Consumed = TRUE;
        return celt == 1 ? S_OK : S_FALSE;
    }

    STDMETHOD(Reset)
    (void) override
    {
        Consumed = FALSE;
        return S_OK;
    }

    STDMETHOD(Clone)
    (IEnumFORMATETC** ppenum) override
    {
        if (ppenum == NULL)
            return E_POINTER;

        *ppenum = new HDropWideEnumFormatEtc(Consumed);
        return *ppenum != NULL ? S_OK : E_OUTOFMEMORY;
    }
};

class HDropWideDataObject : public IDataObject
{
private:
    volatile LONG RefCount;
    std::vector<BYTE> Payload;

    HRESULT ValidateFormat(FORMATETC* formatEtc) const
    {
        if (formatEtc == NULL)
            return E_INVALIDARG;
        if ((formatEtc->tymed & TYMED_HGLOBAL) == 0)
            return DV_E_TYMED;
        if (formatEtc->cfFormat != CF_HDROP || Payload.empty())
            return DV_E_FORMATETC;
        if (formatEtc->dwAspect != DVASPECT_CONTENT)
            return DV_E_DVASPECT;
        return S_OK;
    }

public:
    explicit HDropWideDataObject(const std::vector<std::wstring>& paths)
        : RefCount(1)
    {
        BuildHDropWidePayload(paths, Payload);
    }

    bool IsValid() const { return !Payload.empty(); }

    STDMETHOD(QueryInterface)
    (REFIID iid, void** ppv) override
    {
        if (ppv == NULL)
            return E_POINTER;

        if (iid == IID_IUnknown || iid == IID_IDataObject)
        {
            *ppv = this;
            AddRef();
            return S_OK;
        }

        *ppv = NULL;
        return E_NOINTERFACE;
    }

    STDMETHOD_(ULONG, AddRef)
    (void) override
    {
        return static_cast<ULONG>(InterlockedIncrement(&RefCount));
    }

    STDMETHOD_(ULONG, Release)
    (void) override
    {
        ULONG refs = static_cast<ULONG>(InterlockedDecrement(&RefCount));
        if (refs == 0)
            delete this;
        return refs;
    }

    STDMETHOD(GetData)
    (FORMATETC* formatEtc, STGMEDIUM* medium) override
    {
        if (medium == NULL)
            return E_INVALIDARG;

        HRESULT hr = ValidateFormat(formatEtc);
        if (hr != S_OK)
            return hr;

        HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, Payload.size());
        if (data == NULL)
            return E_OUTOFMEMORY;

        void* out = GlobalLock(data);
        if (out == NULL)
        {
            GlobalFree(data);
            return E_UNEXPECTED;
        }

        memcpy(out, Payload.data(), Payload.size());
        GlobalUnlock(data);

        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = data;
        medium->pUnkForRelease = NULL;
        return S_OK;
    }

    STDMETHOD(GetDataHere)
    (FORMATETC*, STGMEDIUM*) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(QueryGetData)
    (FORMATETC* formatEtc) override
    {
        return ValidateFormat(formatEtc);
    }

    STDMETHOD(GetCanonicalFormatEtc)
    (FORMATETC*, FORMATETC*) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(SetData)
    (FORMATETC*, STGMEDIUM*, BOOL) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(EnumFormatEtc)
    (DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc) override
    {
        if (ppenumFormatEtc == NULL)
            return E_POINTER;

        *ppenumFormatEtc = NULL;
        if (dwDirection != DATADIR_GET || Payload.empty())
            return E_NOTIMPL;

        *ppenumFormatEtc = new HDropWideEnumFormatEtc;
        return *ppenumFormatEtc != NULL ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHOD(DAdvise)
    (FORMATETC*, DWORD, IAdviseSink*, DWORD*) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    STDMETHOD(DUnadvise)
    (DWORD) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    STDMETHOD(EnumDAdvise)
    (IEnumSTATDATA**) override
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }
};
} // namespace clipboard
} // namespace sally
