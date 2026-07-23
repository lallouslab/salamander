// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_SHELL_SELECTION_STANDALONE
#include "precomp.h"
#endif

#include "clipboard/ShellSelectionDataObject.h"

#include <shlobj.h>

namespace sally
{
namespace clipboard
{
namespace
{
class PidlOwner
{
public:
    explicit PidlOwner(PIDLIST_ABSOLUTE pidl = nullptr)
        : Pidl(pidl)
    {
    }

    ~PidlOwner()
    {
        CoTaskMemFree(Pidl);
    }

    PidlOwner(const PidlOwner&) = delete;
    PidlOwner& operator=(const PidlOwner&) = delete;

    PIDLIST_ABSOLUTE Get() const { return Pidl; }

private:
    PIDLIST_ABSOLUTE Pidl;
};
} // namespace

HRESULT CreateShellSelectionDataObject(const std::wstring& parentPath,
                                       const std::vector<std::wstring>& itemPaths,
                                       IDataObject** dataObject)
{
    if (dataObject == nullptr)
        return E_POINTER;

    *dataObject = nullptr;
    if (parentPath.empty() || itemPaths.empty())
        return E_INVALIDARG;

    PIDLIST_ABSOLUTE parentPidl = nullptr;
    HRESULT result = SHParseDisplayName(parentPath.c_str(), nullptr, &parentPidl, 0, nullptr);
    if (FAILED(result))
        return result;
    PidlOwner parentOwner(parentPidl);

    std::vector<LPCITEMIDLIST> childPidls;
    childPidls.reserve(itemPaths.size());
    for (const std::wstring& path : itemPaths)
    {
        PIDLIST_ABSOLUTE absolutePidl = nullptr;
        result = SHParseDisplayName(path.c_str(), nullptr, &absolutePidl, 0, nullptr);
        if (FAILED(result))
            break;
        PidlOwner absoluteOwner(absolutePidl);

        PIDLIST_RELATIVE childPidl = ILClone(ILFindLastID(absolutePidl));
        if (childPidl == nullptr)
        {
            result = E_OUTOFMEMORY;
            break;
        }
        childPidls.push_back(childPidl);
    }

    if (SUCCEEDED(result))
    {
        result = SHCreateDataObject(parentPidl, static_cast<UINT>(childPidls.size()),
                                    childPidls.data(),
                                    nullptr, IID_IDataObject, reinterpret_cast<void**>(dataObject));
    }

    for (LPCITEMIDLIST childPidl : childPidls)
        CoTaskMemFree(const_cast<LPITEMIDLIST>(childPidl));
    return result;
}
} // namespace clipboard
} // namespace sally
