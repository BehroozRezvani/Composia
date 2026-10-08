#include "MailModel.hpp"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <ranges>

namespace mail {

std::wstring_view folder_name(Folder folder) noexcept {
    switch (folder) {
    case Folder::inbox: return L"Inbox";
    case Folder::starred: return L"Starred";
    case Folder::sent: return L"Sent";
    case Folder::drafts: return L"Drafts";
    case Folder::archive: return L"Archive";
    case Folder::junk: return L"Junk";
    case Folder::trash: return L"Trash";
    }
    return L"";
}

void detail::append(std::wstring& out, std::wstring_view value) { out += value; }

unsigned Mailbox::add(Message message) {
    message.id = nextId_++;
    messages_.push_back(std::move(message));
    return messages_.back().id;
}

const Message* Mailbox::find(unsigned id) const noexcept {
    const auto it = std::ranges::find(messages_, id, &Message::id);
    return it == messages_.end() ? nullptr : &*it;
}

Message* Mailbox::find(unsigned id) noexcept {
    return const_cast<Message*>(std::as_const(*this).find(id));
}

bool Mailbox::in_folder(const Message& message, Folder folder) noexcept {
    if (folder == Folder::starred) {
        return message.starred && message.folder != Folder::junk && message.folder != Folder::trash;
    }
    return message.folder == folder;
}

std::vector<unsigned> Mailbox::list(Folder folder, std::wstring_view query) const {
    std::vector<const Message*> matches;
    for (const auto& message : messages_) {
        if (!in_folder(message, folder)) { continue; }
        if (!query.empty() && !contains_ignore_case(message.sender, query) && !contains_ignore_case(message.address, query) &&
            !contains_ignore_case(message.subject, query) && !contains_ignore_case(message.body, query) &&
            !contains_ignore_case(message.recipients, query)) {
            continue;
        }
        matches.push_back(&message);
    }
    std::ranges::stable_sort(matches, [](const Message* a, const Message* b) {
        return a->received != b->received ? a->received > b->received : a->id > b->id;
    });
    std::vector<unsigned> ids;
    ids.reserve(matches.size());
    for (const auto message : matches) { ids.push_back(message->id); }
    return ids;
}

unsigned Mailbox::count(Folder folder) const noexcept {
    return static_cast<unsigned>(std::ranges::count_if(messages_, [&](const Message& m) { return in_folder(m, folder); }));
}

unsigned Mailbox::unread_count(Folder folder) const noexcept {
    return static_cast<unsigned>(std::ranges::count_if(messages_, [&](const Message& m) { return in_folder(m, folder) && m.unread; }));
}

bool Mailbox::move(unsigned id, Folder destination) {
    if (destination == Folder::starred) { return false; }
    const auto message = find(id);
    if (!message) { return false; }
    if (destination == Folder::trash && message->folder == Folder::trash) { return remove(id); }
    message->folder = destination;
    return true;
}

bool Mailbox::remove(unsigned id) {
    return std::erase_if(messages_, [id](const Message& m) { return m.id == id; }) != 0;
}

bool Mailbox::set_unread(unsigned id, bool value) {
    const auto message = find(id);
    if (!message) { return false; }
    message->unread = value;
    return true;
}

bool Mailbox::set_starred(unsigned id, bool value) {
    const auto message = find(id);
    if (!message) { return false; }
    message->starred = value;
    return true;
}

bool contains_ignore_case(std::wstring_view text, std::wstring_view query) {
    if (query.empty()) { return true; }
    const auto lower = [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); };
    return !std::ranges::search(text, query, {}, lower, lower).empty();
}

std::wstring initials(std::wstring_view name) {
    std::wstring result;
    bool start = true;
    for (const wchar_t c : name) {
        if (std::iswspace(c)) { start = true; continue; }
        if (start && std::iswalnum(c) && result.size() < 2) { result.push_back(static_cast<wchar_t>(std::towupper(c))); }
        start = false;
    }
    return result.empty() ? L"?" : result;
}

std::wstring preview(std::wstring_view body, std::size_t limit) {
    std::wstring result;
    bool space = true;
    for (const wchar_t c : body) {
        if (result.size() >= limit) { break; }
        if (std::iswspace(c)) {
            if (!space) { result.push_back(L' '); }
            space = true;
        } else if (c != L'>' || !space) {
            result.push_back(c);
            space = false;
        }
    }
    while (!result.empty() && result.back() == L' ') { result.pop_back(); }
    return result;
}

std::wstring format_size(std::uint64_t bytes) {
    if (bytes < 1024) { return concat(bytes, L" B"); }
    if (bytes < 1024 * 1024) { return concat((bytes + 512) / 1024, L" KB"); }
    const auto tenths = (bytes * 10 + 524288) / 1048576;  // Rounded to one decimal place.
    return concat(tenths / 10, L".", tenths % 10, L" MB");
}

namespace {
constexpr const wchar_t* dayNames[]{L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday"};
constexpr const wchar_t* monthNames[]{L"January", L"February", L"March", L"April", L"May", L"June", L"July", L"August",
    L"September", L"October", L"November", L"December"};

// Local calendar time through Win32, avoiding the CRT's locale and time tables.
SYSTEMTIME local(Clock::time_point when) {
    using Ticks = std::chrono::duration<long long, std::ratio<1, 10000000>>;
    const auto ticks = std::chrono::duration_cast<Ticks>(when.time_since_epoch()).count() + 116444736000000000LL;
    const FILETIME fileTime{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    SYSTEMTIME utc{}, result{};
    FileTimeToSystemTime(&fileTime, &utc);
    SystemTimeToTzSpecificLocalTime(nullptr, &utc, &result);
    return result;
}

long day_number(const SYSTEMTIME& t) {
    return static_cast<long>(std::chrono::sys_days{std::chrono::year{t.wYear} / t.wMonth / t.wDay}.time_since_epoch().count());
}

std::wstring two_digits(unsigned value) { return concat(value < 10 ? L"0" : L"", value); }
std::wstring_view short_name(const wchar_t* name) { return {name, 3}; }
}

std::wstring format_time(Clock::time_point when, Clock::time_point now) {
    const auto then = local(when), today = local(now);
    const auto age = day_number(today) - day_number(then);
    if (age <= 0 && then.wYear == today.wYear) { return concat(two_digits(then.wHour), L":", two_digits(then.wMinute)); }
    if (age == 1) { return L"Yesterday"; }
    if (age < 7 && then.wYear == today.wYear) { return std::wstring{short_name(dayNames[then.wDayOfWeek])}; }
    if (then.wYear == today.wYear) { return concat(then.wDay, L" ", short_name(monthNames[then.wMonth - 1])); }
    return concat(then.wDay, L" ", short_name(monthNames[then.wMonth - 1]), L" ", then.wYear);
}

std::wstring format_date(Clock::time_point when) {
    const auto t = local(when);
    return concat(dayNames[t.wDayOfWeek], L", ", t.wDay, L" ", monthNames[t.wMonth - 1], L" ", t.wYear,
        L" at ", two_digits(t.wHour), L":", two_digits(t.wMinute));
}

namespace {
std::wstring prefixed(std::wstring_view prefix, std::wstring_view subject) {
    const auto lower = [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); };
    if (subject.size() >= prefix.size() && std::ranges::equal(subject.substr(0, prefix.size()), prefix, {}, lower, lower)) {
        return std::wstring{subject};
    }
    return concat(prefix, subject);
}
}

std::wstring reply_subject(std::wstring_view subject) { return prefixed(L"Re: ", subject); }
std::wstring forward_subject(std::wstring_view subject) { return prefixed(L"Fwd: ", subject); }

std::wstring quote_reply(const Message& message) {
    std::wstring quoted = concat(L"\n\nOn ", format_date(message.received), L", ", message.sender, L" <", message.address, L"> wrote:\n> ");
    for (const wchar_t c : message.body) {
        quoted.push_back(c);
        if (c == L'\n') { quoted += L"> "; }
    }
    return quoted;
}

std::wstring quote_forward(const Message& message) {
    return concat(L"\n\n---------- Forwarded message ----------\nFrom: ", message.sender, L" <", message.address, L">\nDate: ",
        format_date(message.received), L"\nSubject: ", message.subject, L"\nTo: ", message.recipients, L"\n\n", message.body);
}

}
