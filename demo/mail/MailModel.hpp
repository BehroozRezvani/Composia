#pragma once

#include <array>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// An in-memory mailbox for the demo. Nothing here talks to a server or account.
namespace mail {

enum class Folder { inbox, starred, sent, drafts, archive, junk, trash };
constexpr std::array<Folder, 7> folders{Folder::inbox, Folder::starred, Folder::sent, Folder::drafts,
    Folder::archive, Folder::junk, Folder::trash};
[[nodiscard]] std::wstring_view folder_name(Folder) noexcept;
using Clock = std::chrono::system_clock;

struct Attachment {
    std::wstring name;
    std::uint64_t bytes{};
};

struct Message {
    unsigned id{};
    Folder folder{Folder::inbox};
    std::wstring sender;      // Display name.
    std::wstring address;     // Sender mailbox.
    std::wstring recipients;  // The "To" line as typed.
    std::wstring subject;
    std::wstring body;
    std::vector<Attachment> attachments;
    Clock::time_point received{};
    bool unread{};
    bool starred{};
};

class Mailbox {
public:
    // Builds the demo content with timestamps relative to `now`.
    [[nodiscard]] static Mailbox sample(Clock::time_point now = Clock::now());

    unsigned add(Message);
    [[nodiscard]] const Message* find(unsigned id) const noexcept;
    [[nodiscard]] Message* find(unsigned id) noexcept;
    // Newest first. Starred is a virtual folder of starred messages outside Junk and Trash.
    [[nodiscard]] std::vector<unsigned> list(Folder, std::wstring_view query = {}) const;
    [[nodiscard]] unsigned count(Folder) const noexcept;
    [[nodiscard]] unsigned unread_count(Folder) const noexcept;
    // Moving to Trash from Trash deletes permanently; the virtual folder is not a destination.
    bool move(unsigned id, Folder destination);
    bool remove(unsigned id);
    bool set_unread(unsigned id, bool value);
    bool set_starred(unsigned id, bool value);
    [[nodiscard]] const std::vector<Message>& messages() const noexcept { return messages_; }
    [[nodiscard]] std::size_t size() const noexcept { return messages_.size(); }

private:
    [[nodiscard]] static bool in_folder(const Message&, Folder) noexcept;
    std::vector<Message> messages_;
    unsigned nextId_{1};
};

// Joins strings and integers. It replaces std::format, whose floating-point and Unicode tables
// would otherwise add well over 100 KB to the demo executable.
namespace detail {
void append(std::wstring& out, std::wstring_view value);
template<std::integral T> requires (!std::same_as<T, bool> && !std::same_as<T, wchar_t>)
void append(std::wstring& out, T value) { out += std::to_wstring(value); }
}
template<class... Parts>
[[nodiscard]] std::wstring concat(const Parts&... parts) {
    std::wstring out;
    (detail::append(out, parts), ...);
    return out;
}

[[nodiscard]] bool contains_ignore_case(std::wstring_view text, std::wstring_view query);
[[nodiscard]] std::wstring initials(std::wstring_view name);
[[nodiscard]] std::wstring preview(std::wstring_view body, std::size_t limit = 140);
[[nodiscard]] std::wstring format_size(std::uint64_t bytes);
// Short list form: "09:14", "Yesterday", "Mon", or "3 Oct".
[[nodiscard]] std::wstring format_time(Clock::time_point when, Clock::time_point now);
// Long header form: "Tuesday, 6 October 2026 at 09:14".
[[nodiscard]] std::wstring format_date(Clock::time_point when);
[[nodiscard]] std::wstring reply_subject(std::wstring_view subject);
[[nodiscard]] std::wstring forward_subject(std::wstring_view subject);
[[nodiscard]] std::wstring quote_reply(const Message&);
[[nodiscard]] std::wstring quote_forward(const Message&);

}
