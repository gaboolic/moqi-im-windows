#ifndef _Moqi_LAUNCHER_UTILS_H_
#define _Moqi_LAUNCHER_UTILS_H_

#include <json/json.h>
#include <string>

namespace Moqi {

bool loadJsonFile(const std::wstring filename, Json::Value& result);

bool saveJsonFile(const std::wstring filename, Json::Value& data);

std::wstring getCurrentExecutableDir();

std::wstring getAppLocalDir();

bool makeDirs(const std::wstring& path);

std::string generateUuidStr();

} // namespace Moqi

#endif // _Moqi_LAUNCHER_UTILS_H_
