#include "TsfLog.h"

#include <Windows.h>
#include <Objbase.h>

namespace MoqiIME {

void tsfLog(const std::wstring& message) {
    (void)message;
}

std::wstring tsfGuidToString(REFGUID guid) {
    LPOLESTR buffer = nullptr;
    if (FAILED(::StringFromCLSID(guid, &buffer))) {
        return L"<guid-convert-failed>";
    }

    std::wstring result{buffer};
    ::CoTaskMemFree(buffer);
    return result;
}

} // namespace MoqiIME
