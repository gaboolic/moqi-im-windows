// Standalone, non-installing test: fake registry only, no Windows API calls.
// cl /std:c++17 /EHsc Test-MachineRegistrationCleanup.cpp /Fe:<temporary exe>
#include "MachineRegistrationCleanup.h"

#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

struct FakeRegistry {
  std::set<std::wstring> keys;
  std::wstring denied_delete;
  bool deny_enumeration = false;
  std::vector<std::wstring> deleted;

  bool Children(const std::wstring& path, std::vector<std::wstring>& names) {
    if (deny_enumeration) return false;
    std::set<std::wstring> children;
    for (const auto& key : keys) {
      const auto prefix = path + L"\\";
      if (key.compare(0, prefix.size(), prefix) == 0) {
        auto suffix = key.substr(prefix.size());
        children.insert(suffix.substr(0, suffix.find(L'\\')));
      }
    }
    names.assign(children.begin(), children.end());
    return true;
  }
  bool Delete(const std::wstring& path) {
    if (path == denied_delete) return false;
    deleted.push_back(path);
    for (auto it = keys.begin(); it != keys.end();) {
      if (*it == path || it->compare(0, path.size() + 1, path + L"\\") == 0)
        it = keys.erase(it);
      else
        ++it;
    }
    return true;
  }
};

void Check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

int main() {
  try {
    const std::wstring id(moqi_setup::kServiceClsid), ctf(moqi_setup::kCtfRoot);
    const auto tip = ctf + L"TIP\\" + id;
    const auto com = L"Software\\Classes\\CLSID\\" + id;
    const auto reverse = ctf + L"Category\\{category}\\" + id;
    FakeRegistry registry;
    registry.keys = {tip + L"\\LanguageProfile\\{profile}",
      tip + L"\\Category\\Category\\{category}\\" + id,
      tip + L"\\Category\\Item\\" + id + L"\\{category}",
      com + L"\\InprocServer32", reverse,
      ctf + L"CategoryItem\\" + id + L"\\{category}",
      ctf + L"Category\\{category}\\{other-service}",
      ctf + L"TIP\\{other-service}\\LanguageProfile",
      L"Software\\Classes\\CLSID\\{other-service}\\InprocServer32"};
    Check(moqi_setup::CleanupMachineRegistration(registry), "cleanup rejected");
    Check(registry.keys.size() == 3, "owned keys/reverse references remain or sibling deleted");
    const auto remaining = registry.keys;
    Check(moqi_setup::CleanupMachineRegistration(registry), "idempotent cleanup failed");
    Check(registry.keys == remaining, "second cleanup changed siblings");
    std::cout << "PASS owned COM/TIP, category indexes, siblings and idempotency\n";

    // The TIP may already be gone; independently discover global references.
    FakeRegistry orphan;
    orphan.keys = {reverse};
    Check(moqi_setup::CleanupMachineRegistration(orphan) && orphan.keys.empty(), "orphan remains");
    std::cout << "PASS orphan reverse reference without TIP\n";

    FakeRegistry denied;
    denied.keys = {tip, com, reverse};
    denied.denied_delete = reverse;
    Check(!moqi_setup::CleanupMachineRegistration(denied), "delete error became success");
    Check(denied.keys.count(tip) && denied.keys.count(com), "continued after failed reverse cleanup");
    denied.denied_delete.clear();
    denied.deny_enumeration = true;
    Check(!moqi_setup::CleanupMachineRegistration(denied), "enumeration error became success");
    std::cout << "PASS checked delete/enumeration failure propagation\n";

    // Production constructs separate adapters for each view, without sharing
    // mutable fake state. A failed view must not appear as successful cleanup.
    FakeRegistry view32, view64;
    view32.keys = {com};
    view64.keys = {com};
    view64.denied_delete = com;
    Check(moqi_setup::CleanupMachineRegistration(view32), "32-bit view failed");
    Check(!moqi_setup::CleanupMachineRegistration(view64), "64-bit error became success");
    Check(view32.keys.empty() && view64.keys.count(com), "view isolation broken");
    std::cout << "PASS independent views\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
