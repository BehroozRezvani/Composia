#include "Document.hpp"
#include <composia/Native.hpp>
#include <stdexcept>

namespace editor {
namespace {
constexpr DWORD limit = 16 * 1024 * 1024;
constexpr std::string_view signature = "\xef\xbb\xbf";

std::wstring line_endings(std::wstring_view text, std::wstring_view newline) {
    std::wstring result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r' || text[i] == L'\n') {
            if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') { ++i; }
            result.append(newline);
        } else {
            result.push_back(text[i]);
        }
    }
    return result;
}
}

Document read_document(const std::filesystem::path& path) {
    wil::unique_hfile file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    THROW_LAST_ERROR_IF(!file);
    LARGE_INTEGER size{};
    THROW_IF_WIN32_BOOL_FALSE(GetFileSizeEx(file.get(), &size));
    if (size.QuadPart > limit) { throw std::runtime_error("The editor supports documents up to 16 MiB."); }
    std::string data(static_cast<size_t>(size.QuadPart), '\0');
    DWORD count{};
    THROW_IF_WIN32_BOOL_FALSE(ReadFile(file.get(), data.data(), static_cast<DWORD>(data.size()), &count, nullptr));
    if (count != data.size()) { throw std::runtime_error("Incomplete file read."); }
    Document doc;
    std::string_view bytes{data};
    doc.bom = bytes.starts_with(signature);
    if (doc.bom) { bytes.remove_prefix(signature.size()); }
    if (bytes.empty()) { return doc; }
    if (bytes.find('\0') != bytes.npos) { throw std::runtime_error("The file contains embedded NUL characters."); }
    const int length = static_cast<int>(bytes.size());
    std::wstring text(bytes.size(), L'\0');
    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), length, text.data(), length);
    if (!chars) { throw std::runtime_error("The file is not valid UTF-8."); }
    text.resize(chars);
    auto first = text.find_first_of(L"\r\n");
    if (first != text.npos) { doc.newline = text.substr(first, text.compare(first, 2, L"\r\n") == 0 ? 2 : 1); }
    doc.text = line_endings(text, L"\r");
    return doc;
}

void write_document(const std::filesystem::path& path, std::wstring_view text, std::wstring_view newline, bool bom) {
    if (text.size() > limit) { throw std::runtime_error("The editor supports documents up to 16 MiB."); }
    const auto normalized = line_endings(text, newline);
    std::string data{bom ? signature : std::string_view{}};
    if (!normalized.empty()) {
        const int count = static_cast<int>(normalized.size());
        int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, normalized.data(), count, nullptr, 0, nullptr, nullptr);
        THROW_LAST_ERROR_IF(!bytes);
        if (data.size() + bytes > limit) { throw std::runtime_error("The UTF-8 file would exceed 16 MiB."); }
        const auto prefix = data.size();
        data.resize(prefix + bytes);
        THROW_LAST_ERROR_IF(!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, normalized.data(), count,
            data.data() + prefix, bytes, nullptr, nullptr));
    }
    GUID id{};
    THROW_IF_FAILED(CoCreateGuid(&id));
    wchar_t name[40]{};
    StringFromGUID2(id, name, 40);
    const auto temp = path.parent_path() / (std::wstring(name) + L".tmp");
    wil::unique_hfile file{CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    THROW_LAST_ERROR_IF(!file);
    auto cleanup = wil::scope_exit([&] { file.reset(); DeleteFileW(temp.c_str()); });
    DWORD written{};
    THROW_IF_WIN32_BOOL_FALSE(WriteFile(file.get(), data.data(), static_cast<DWORD>(data.size()), &written, nullptr));
    if (written != data.size()) { throw std::runtime_error("Incomplete file write."); }
    THROW_IF_WIN32_BOOL_FALSE(FlushFileBuffers(file.get()));
    file.reset();
    if (!ReplaceFileW(path.c_str(), temp.c_str(), nullptr, 0, nullptr, nullptr)) {
        THROW_LAST_ERROR_IF(GetLastError() != ERROR_FILE_NOT_FOUND);
        THROW_IF_WIN32_BOOL_FALSE(MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH));
    }
    cleanup.release();
}
}
