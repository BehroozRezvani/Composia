#pragma once

#include "BuilderModel.hpp"
#include <composia/Native.hpp>
#include <filesystem>
#include <optional>
#include <string>

// Turns a design into a standalone executable: a copy of the form player with the design embedded
// as a resource. No compiler is involved at build time; the player is already compiled code.
namespace builder {

constexpr wchar_t design_resource[] = L"COMPOSIA_FORM";    // RCDATA in a built app.
constexpr wchar_t player_resource[] = L"COMPOSIA_PLAYER";  // RCDATA in the builder: the player executable.
constexpr wchar_t player_file_name[] = L"composia-form-player.exe";

// Reads an RCDATA resource from a module (the running executable, or one loaded as a data file).
[[nodiscard]] std::optional<std::string> read_resource(HMODULE module, const wchar_t* name);
// The design embedded in the running executable, if any.
[[nodiscard]] std::optional<std::string> embedded_design();
[[nodiscard]] std::filesystem::path module_path();

// Overrides where player_bytes looks first; tests point it at the freshly built player.
void set_player_path(std::filesystem::path);
// The player executable: the override, the copy embedded in this executable, or a sibling file.
[[nodiscard]] std::optional<std::string> player_bytes(std::wstring* error = nullptr);

// Writes `output` as a copy of `player` carrying the design. Removes a partial file on failure.
[[nodiscard]] bool write_app(const std::string& player, const Document&, const std::filesystem::path& output, std::wstring* error = nullptr);

[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path&);
// "Sign in" becomes "Sign in.exe"; characters Windows rejects in names are replaced.
[[nodiscard]] std::wstring app_file_name(std::wstring_view title);
[[nodiscard]] std::wstring describe_error(DWORD error);

}
