#ifndef MOQI_TSF_LOG_H
#define MOQI_TSF_LOG_H

#include <string>
#include <guiddef.h>

namespace MoqiIME {

void tsfLog(const std::wstring& message);
std::wstring tsfGuidToString(REFGUID guid);

} // namespace MoqiIME

#endif // MOQI_TSF_LOG_H
