#include "Source/app/Dialogs.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>

#include <cstdint>
#include <cstdio>
#include <functional>

namespace icapp {
namespace {

namespace fs = std::filesystem;

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

void explain(std::string* why, const char* what, HRESULT hr) {
    if (!why) return;
    char buf[128];
    std::snprintf(buf, sizeof buf, "%s (HRESULT 0x%08lX)", what, static_cast<unsigned long>(hr));
    *why = buf;
}

// Um GUID estavel por chave, so para separar os baldes de MRU.
GUID guidFor(const std::string& key) {
    const std::uint64_t h = std::hash<std::string>{}(key);
    GUID g{};
    g.Data1 = static_cast<unsigned long>(h & 0xFFFFFFFFu);
    g.Data2 = static_cast<unsigned short>((h >> 32) & 0xFFFFu);
    g.Data3 = static_cast<unsigned short>((h >> 48) & 0xFFFFu);
    const unsigned char tail[8] = {'I', 'c', 'o', 'n', 'C', 'm', 'p', '0'};
    for (int i = 0; i < 8; ++i) g.Data4[i] = tail[i];
    return g;
}

// O caminho comum aos tres: cria o dialogo, deixa `setup` configurar, mostra
// e le o resultado. A GLFW ja pos esta thread num apartamento COM, entao o
// `CoInitializeEx` costuma voltar S_FALSE -- e ainda assim tem de ser pareado.
template <class Dialog>
fs::path show(REFCLSID clsid, REFIID iid, std::string* why, const std::function<void(Dialog*)>& setup) {
    if (why) why->clear();
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool balance = SUCCEEDED(init);
    fs::path out;
    Dialog* dialog = nullptr;
    const HRESULT made = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, iid,
                                          reinterpret_cast<void**>(&dialog));
    if (FAILED(made)) {
        explain(why, "the file dialog could not be created", made);
    } else {
        setup(dialog);
        const HRESULT shown = dialog->Show(GetActiveWindow());
        if (SUCCEEDED(shown)) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)) && item) {
                PWSTR wide = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wide)) && wide) {
                    out = fs::path(wide);
                    CoTaskMemFree(wide);
                }
                item->Release();
            }
        } else if (shown != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            explain(why, "the file dialog failed to open", shown);
        }
        dialog->Release();
    }
    if (balance) CoUninitialize();
    return out;
}

void startIn(IFileDialog* d, const fs::path& dir) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return;
    IShellItem* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(dir.wstring().c_str(), nullptr, IID_IShellItem,
                                              reinterpret_cast<void**>(&item))) && item) {
        // `SetFolder` e nao `SetDefaultFolder`: o default so vale com o balde
        // vazio, e quem passa `startIn` quer passar por cima do balde.
        d->SetFolder(item);
        item->Release();
    }
}

}  // namespace

fs::path openFolderDialog(const FolderDialogOptions& o, std::string* why) {
    return show<IFileOpenDialog>(CLSID_FileOpenDialog, IID_IFileOpenDialog, why, [&](IFileOpenDialog* d) {
        DWORD flags = 0;
        d->GetOptions(&flags);
        // FORCEFILESYSTEM: This PC, Bibliotecas e afins nao tem caminho.
        d->SetOptions(flags | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
        if (!o.title.empty()) d->SetTitle(widen(o.title).c_str());
        if (!o.mruKey.empty()) d->SetClientGuid(guidFor(o.mruKey));
        startIn(d, o.startIn);
    });
}

fs::path openFileDialog(const std::vector<FileFilter>& filters, std::string* why) {
    // As strings tem de viver ate o `Show`, entao moram aqui fora.
    std::vector<std::wstring> labels, specs;
    for (const auto& f : filters) {
        std::wstring spec;
        for (const auto& e : f.extensions) spec += (spec.empty() ? L"*." : L";*.") + widen(e);
        labels.push_back(widen(f.label));
        specs.push_back(spec);
    }
    labels.push_back(L"All Files");
    specs.push_back(L"*.*");
    std::vector<COMDLG_FILTERSPEC> table;
    for (std::size_t i = 0; i < labels.size(); ++i) table.push_back({labels[i].c_str(), specs[i].c_str()});
    return show<IFileOpenDialog>(CLSID_FileOpenDialog, IID_IFileOpenDialog, why, [&](IFileOpenDialog* d) {
        DWORD flags = 0;
        d->GetOptions(&flags);
        d->SetOptions(flags | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
        d->SetFileTypes(static_cast<UINT>(table.size()), table.data());
    });
}

fs::path saveFileDialog(const std::string& defaultName, std::string* why) {
    return show<IFileSaveDialog>(CLSID_FileSaveDialog, IID_IFileSaveDialog, why, [&](IFileSaveDialog* d) {
        DWORD flags = 0;
        d->GetOptions(&flags);
        d->SetOptions(flags | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
        if (!defaultName.empty()) d->SetFileName(widen(defaultName).c_str());
    });
}

}  // namespace icapp

#else

namespace icapp {
namespace {
std::filesystem::path refuse(std::string* why) {
    if (why) *why = "file dialogs are not implemented on this platform yet";
    return {};
}
}  // namespace
std::filesystem::path openFolderDialog(const FolderDialogOptions&, std::string* why) { return refuse(why); }
std::filesystem::path openFileDialog(const std::vector<FileFilter>&, std::string* why) { return refuse(why); }
std::filesystem::path saveFileDialog(const std::string&, std::string* why) { return refuse(why); }
}  // namespace icapp

#endif
