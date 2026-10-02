#include "Source/app/ShellIntegration.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <cwchar>

namespace icapp {
namespace {

constexpr const wchar_t* kVerbKey = L"Software\\Classes\\Directory\\shell\\IconComposer";
constexpr const wchar_t* kCommandKey = L"Software\\Classes\\Directory\\shell\\IconComposer\\command";

std::wstring exePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);   // o caminho nao coube: dobra e tenta de novo
    }
}

std::wstring commandFor(const std::wstring& exe) { return L"\"" + exe + L"\" \"%1\""; }

std::string failure(const char* what, LSTATUS status) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s (error %ld)", what, static_cast<long>(status));
    return buf;
}

LSTATUS setString(HKEY key, const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

// O Explorer guarda os verbos em cache; isto o manda reler.
void tellTheShell() { SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); }

}  // namespace

bool contextMenuSupported() { return true; }

bool contextMenuRegistered() {
    const std::wstring exe = exePath();
    if (exe.empty()) return false;
    wchar_t value[2048];
    DWORD bytes = sizeof value;
    if (RegGetValueW(HKEY_CURRENT_USER, kCommandKey, nullptr, RRF_RT_REG_SZ, nullptr, value, &bytes) !=
        ERROR_SUCCESS)
        return false;
    // Sem diferenciar maiusculas: e um caminho do Windows.
    return _wcsicmp(value, commandFor(exe).c_str()) == 0;
}

std::string setContextMenu(bool enabled) {
    if (!enabled) {
        const LSTATUS gone = RegDeleteTreeW(HKEY_CURRENT_USER, kVerbKey);
        if (gone != ERROR_SUCCESS && gone != ERROR_FILE_NOT_FOUND)
            return failure("the context menu entry could not be removed", gone);
        tellTheShell();
        return {};
    }

    const std::wstring exe = exePath();
    if (exe.empty()) return "the path of this executable could not be read";

    HKEY verb = nullptr;
    LSTATUS s = RegCreateKeyExW(HKEY_CURRENT_USER, kVerbKey, 0, nullptr, 0, KEY_WRITE, nullptr, &verb, nullptr);
    if (s != ERROR_SUCCESS) return failure("the context menu entry could not be created", s);
    // O valor padrao e o rotulo de reserva, para um shell que nao resolva o
    // `MUIVerb`; o que o Explorer mostra e a string do idioma do sistema.
    s = setString(verb, nullptr, L"Open with Icon Composer");
    if (s == ERROR_SUCCESS)
        s = setString(verb, L"MUIVerb", L"@" + exe + L",-" + std::to_wstring(kContextMenuVerbString));
    // So nas pastas cujo nome termina em `.icon`: sem isto o item apareceria
    // em toda pasta do sistema.
    if (s == ERROR_SUCCESS) s = setString(verb, L"AppliesTo", L"System.FileName:\"*.icon\"");
    if (s == ERROR_SUCCESS) s = setString(verb, L"Icon", L"\"" + exe + L"\",0");
    RegCloseKey(verb);
    if (s != ERROR_SUCCESS) {
        RegDeleteTreeW(HKEY_CURRENT_USER, kVerbKey);
        return failure("the context menu entry could not be written", s);
    }

    HKEY command = nullptr;
    s = RegCreateKeyExW(HKEY_CURRENT_USER, kCommandKey, 0, nullptr, 0, KEY_WRITE, nullptr, &command, nullptr);
    if (s == ERROR_SUCCESS) {
        s = setString(command, nullptr, commandFor(exe));
        RegCloseKey(command);
    }
    if (s != ERROR_SUCCESS) {
        // Um verbo sem comando e um item de menu que nao faz nada: sai inteiro.
        RegDeleteTreeW(HKEY_CURRENT_USER, kVerbKey);
        return failure("the context menu command could not be written", s);
    }
    tellTheShell();
    return {};
}

}  // namespace icapp

#else

namespace icapp {

bool contextMenuSupported() { return false; }
bool contextMenuRegistered() { return false; }
std::string setContextMenu(bool) { return "the folder context menu is a Windows Explorer feature"; }

}  // namespace icapp

#endif
