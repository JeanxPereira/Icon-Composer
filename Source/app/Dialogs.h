#pragma once
// Os seletores de arquivo do sistema. So Windows por enquanto; nos outros o
// resultado e vazio COM motivo, porque vazio sem motivo e um cancelamento.
#include <filesystem>
#include <string>
#include <vector>

namespace icapp {

struct FileFilter {
    std::string label;                    // "Artwork (svg, png)"
    std::vector<std::string> extensions;  // minusculas, sem ponto: {"svg", "png"}
};

struct FolderDialogOptions {
    std::string title;             // vazio: o titulo do sistema
    std::filesystem::path startIn; // a pasta cujo CONTEUDO aparece primeiro
    // O balde do "onde estavamos": o Windows lembra a ultima pasta por GUID do
    // cliente, e sem um todo dialogo do processo divide o mesmo.
    std::string mruKey;
};

// Vazio: cancelado, ou `why` diz o que falhou antes de perguntar.
std::filesystem::path openFolderDialog(const FolderDialogOptions& options, std::string* why = nullptr);
std::filesystem::path openFileDialog(const std::vector<FileFilter>& filters, std::string* why = nullptr);
std::filesystem::path saveFileDialog(const std::string& defaultName, std::string* why = nullptr);

}  // namespace icapp
