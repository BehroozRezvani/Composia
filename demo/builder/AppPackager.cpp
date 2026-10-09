#include "AppPackager.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>

namespace builder {
namespace {
std::filesystem::path playerOverride;

bool fail(std::wstring* error, std::wstring message) {
    if (error) { *error = std::move(message); }
    return false;
}
}

std::optional<std::string> read_resource(HMODULE module, const wchar_t* name) {
    const auto found = FindResourceW(module, name, RT_RCDATA);
    if (!found) { return std::nullopt; }
    const auto loaded = LoadResource(module, found);
    const auto size = SizeofResource(module, found);
    if (!loaded || size == 0) { return std::nullopt; }
    const auto data = static_cast<const char*>(LockResource(loaded));
    if (!data) { return std::nullopt; }
    return std::string{data, size};
}

std::optional<std::string> embedded_design() { return read_resource(GetModuleHandleW(nullptr), design_resource); }

std::filesystem::path module_path() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) { return {}; }
        if (length < path.size() - 1) { path.resize(length); return path; }
        path.resize(path.size() * 2);
    }
}

void set_player_path(std::filesystem::path path) { playerOverride = std::move(path); }

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) { return std::nullopt; }
    std::string bytes{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    if (!file && !file.eof()) { return std::nullopt; }
    return bytes;
}

std::optional<std::string> player_bytes(std::wstring* error) {
    if (!playerOverride.empty()) {
        if (auto bytes = read_file(playerOverride)) { return bytes; }
        fail(error, L"Could not read the form player at " + playerOverride.wstring());
        return std::nullopt;
    }
    if (auto bytes = read_resource(GetModuleHandleW(nullptr), player_resource)) { return bytes; }
    const auto sibling = module_path().parent_path() / player_file_name;
    if (auto bytes = read_file(sibling)) { return bytes; }
    fail(error, std::wstring{L"This build of the UI builder has no form player embedded, and "} + player_file_name + L" is not beside it");
    return std::nullopt;
}

bool write_app(const std::string& player, const Document& document, const std::filesystem::path& output, std::wstring* error) {
    if (player.size() < 64 || player[0] != 'M' || player[1] != 'Z') { return fail(error, L"The form player is not an executable"); }
    {
        std::ofstream file{output, std::ios::binary | std::ios::trunc};
        file.write(player.data(), static_cast<std::streamsize>(player.size()));
        if (!file) { return fail(error, L"Could not write " + output.wstring()); }
    }
    const auto design = document.to_text();
    const auto update = BeginUpdateResourceW(output.c_str(), FALSE);
    if (!update) {
        const auto code = GetLastError();
        std::error_code ignored;
        std::filesystem::remove(output, ignored);
        return fail(error, L"Could not open " + output.filename().wstring() + L" for resource updates: " + describe_error(code));
    }
    // UpdateResource keeps a copy of the data, so the buffer only has to live until EndUpdateResource.
    std::string payload = design;
    if (!UpdateResourceW(update, RT_RCDATA, design_resource, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), payload.data(), static_cast<DWORD>(payload.size()))) {
        const auto code = GetLastError();
        EndUpdateResourceW(update, TRUE);
        std::error_code ignored;
        std::filesystem::remove(output, ignored);
        return fail(error, L"Could not embed the design: " + describe_error(code));
    }
    if (!EndUpdateResourceW(update, FALSE)) {
        const auto code = GetLastError();
        std::error_code ignored;
        std::filesystem::remove(output, ignored);
        return fail(error, L"Could not finish writing " + output.filename().wstring() + L": " + describe_error(code));
    }
    return true;
}

std::wstring app_file_name(std::wstring_view title) {
    std::wstring name;
    for (const wchar_t c : title) {
        const bool banned = c < 0x20 || c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*';
        name.push_back(banned ? L'_' : c);
    }
    while (!name.empty() && (name.back() == L' ' || name.back() == L'.')) { name.pop_back(); }
    if (name.empty()) { name = L"Form"; }
    return name + L".exe";
}

std::wstring describe_error(DWORD error) {
    wchar_t* buffer{};
    const auto length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = length && buffer ? std::wstring{buffer, length} : L"error " + std::to_wstring(error);
    if (buffer) { LocalFree(buffer); }
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ' || message.back() == L'.')) { message.pop_back(); }
    return message;
}

}
