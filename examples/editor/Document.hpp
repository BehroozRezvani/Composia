#pragma once

#include <filesystem>
#include <string>

namespace editor {
struct Document {
    std::wstring text;
    std::wstring newline{L"\r\n"};
    bool bom{};
};
Document read_document(const std::filesystem::path&);
void write_document(const std::filesystem::path&, std::wstring_view text, std::wstring_view newline, bool bom);
}
