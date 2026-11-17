#pragma once

#include <string>
#include <vector>

namespace moqi_setup {

inline constexpr wchar_t kServiceClsid[] = L"{8F204C91-2D7A-4B3E-9E1F-6A5C0D8B2E7F}";
inline constexpr wchar_t kCtfRoot[] = L"Software\\Microsoft\\CTF\\";

// Registry is deliberately supplied by the caller: production binds this to
// HKLM and one explicit WOW64 view; tests use an in-memory fake, never HKCU/HKCR.
// Delete must check the result and verify absence. Missing keys are successful.
// Children must distinguish missing keys (empty/success) from access/API errors.
template <typename Registry>
bool CleanupMachineRegistration(Registry& registry) {
  const std::wstring clsid(kServiceClsid);
  const std::wstring ctf(kCtfRoot);

  // Older TSF layouts can retain global category reverse indexes even if the
  // owning TIP tree has already disappeared. Never delete a category itself or
  // a sibling service: only the exact Moqi item beneath each category.
  std::vector<std::wstring> categories;
  if (!registry.Children(ctf + L"Category", categories)) return false;
  for (const auto& category : categories) {
    if (!registry.Delete(ctf + L"Category\\" + category + L"\\" + clsid)) return false;
  }
  if (!registry.Delete(ctf + L"CategoryItem\\" + clsid)) return false;

  // Current Windows stores BOTH Category\\Category and Category\\Item
  // indexes below the owning TIP. Delete that whole owned subtree, not just
  // LanguageProfile, so both forward and reverse references are removed.
  if (!registry.Delete(ctf + L"TIP\\" + clsid)) return false;
  return registry.Delete(L"Software\\Classes\\CLSID\\" + clsid);
}

}  // namespace moqi_setup
