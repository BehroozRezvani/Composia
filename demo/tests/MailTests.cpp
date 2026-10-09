#include "MailDemoWindow.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace std::chrono_literals;

namespace {
void require(bool value, const char* reason) { if (!value) { throw std::runtime_error(reason); } }

void model() {
    const auto now = mail::Clock::now();
    auto mailbox = mail::Mailbox::sample(now);
    require(mailbox.size() >= 40, "Sample mailbox is unexpectedly small");
    require(mailbox.count(mail::Folder::inbox) >= 20 && mailbox.unread_count(mail::Folder::inbox) >= 3, "Inbox sample lacks content");
    for (const auto folder : mail::folders) { require(mailbox.count(folder) > 0, "Every sample folder should have content"); }
    const auto inbox = mailbox.list(mail::Folder::inbox);
    require(inbox.size() == mailbox.count(mail::Folder::inbox), "Listing and count disagree");
    for (std::size_t index = 1; index < inbox.size(); ++index) {
        require(mailbox.find(inbox[index - 1])->received >= mailbox.find(inbox[index])->received, "Inbox is not newest first");
    }
    require(!mailbox.find(inbox.front())->unread, "The newest inbox message should start read so launch selection is quiet");
    const auto starred = mailbox.list(mail::Folder::starred);
    require(!starred.empty(), "Starred sample is empty");
    for (const auto id : starred) {
        const auto& message = *mailbox.find(id);
        require(message.starred && message.folder != mail::Folder::junk && message.folder != mail::Folder::trash, "Starred listing is wrong");
    }
    const auto lower = mailbox.list(mail::Folder::inbox, L"harborlight"), upper = mailbox.list(mail::Folder::inbox, L"HARBORLIGHT");
    require(!lower.empty() && lower == upper, "Search is not case-insensitive");
    for (const auto id : lower) {
        const auto& m = *mailbox.find(id);
        require(mail::contains_ignore_case(m.subject, L"harborlight") || mail::contains_ignore_case(m.body, L"harborlight") ||
            mail::contains_ignore_case(m.address, L"harborlight") || mail::contains_ignore_case(m.recipients, L"harborlight"), "Search matched an unrelated message");
    }
    require(mailbox.list(mail::Folder::inbox, L"zzzz-no-such-text").empty(), "Search returned a false match");

    const auto id = inbox[1];
    require(!mailbox.move(id, mail::Folder::starred), "The virtual folder accepted a move");
    require(mailbox.move(id, mail::Folder::trash) && mailbox.find(id)->folder == mail::Folder::trash, "Move to Trash failed");
    require(mailbox.move(id, mail::Folder::trash) && !mailbox.find(id), "Trashing from Trash did not delete permanently");
    require(!mailbox.move(id, mail::Folder::inbox) && !mailbox.remove(id) && !mailbox.set_unread(id, true), "Missing id was accepted");
    const auto other = inbox[2];
    require(mailbox.set_unread(other, true) && mailbox.find(other)->unread, "Unread flag did not change");
    require(mailbox.set_starred(other, true) && mailbox.find(other)->starred, "Star flag did not change");
    mail::Message added;
    added.folder = mail::Folder::sent;
    added.subject = L"Added";
    added.received = now + 1h;
    const auto newId = mailbox.add(std::move(added));
    require(newId != 0 && mailbox.list(mail::Folder::sent).front() == newId, "Added message is not the newest in its folder");

    require(mail::initials(L"Priya Natarajan") == L"PN" && mail::initials(L"Lumen CI") == L"LC" && mail::initials(L"  ") == L"?", "Initials are wrong");
    require(mail::preview(L"Hi Jordan,\n\n  Quick   recap\n> quoted", 100) == L"Hi Jordan, Quick recap quoted", "Preview did not collapse whitespace");
    require(mail::preview(L"abcdefghij", 4) == L"abcd", "Preview limit ignored");
    require(mail::format_size(512) == L"512 B" && mail::format_size(421888) == L"412 KB" && mail::format_size(1887436) == L"1.8 MB", "Size formatting is wrong");
    require(mail::reply_subject(L"Hello") == L"Re: Hello" && mail::reply_subject(L"re: Hello") == L"re: Hello", "Reply subject prefix is wrong");
    require(mail::forward_subject(L"Hello") == L"Fwd: Hello" && mail::forward_subject(L"Fwd: Hello") == L"Fwd: Hello", "Forward subject prefix is wrong");
    const auto today = mail::format_time(now - 1min, now);
    require(today.size() == 5 && today[2] == L':', "Same-day time should be HH:MM");
    require(mail::format_time(now - 36h, now) == L"Yesterday" || mail::format_time(now - 36h, now).size() == 3, "Day-old time should be Yesterday or a weekday");
    require(mail::format_date(now).find(L" at ") != std::wstring::npos, "Long date lacks a time");
    const auto& original = *mailbox.find(inbox.front());
    const auto quoted = mail::quote_reply(original);
    require(quoted.find(L"wrote:") != std::wstring::npos && quoted.find(L"\n> ") != std::wstring::npos, "Reply quote is malformed");
    require(mail::quote_forward(original).find(L"Forwarded message") != std::wstring::npos, "Forward quote is malformed");
}

void client(bool warp) {
    composia::Application app{warp};
    {
        MailDemoWindow window{app};
        window.show();
        const auto paint = [&] { UpdateWindow(window.hwnd()); };
        const auto click = [&](MailDemoWindow::Hit kind, unsigned id) {
            paint();
            const auto bounds = window.region(kind, id);
            require(bounds.has_value(), "Expected a hit region from the last draw");
            const auto scale = static_cast<float>(window.dpi()) / 96.0f;
            const auto x = static_cast<int>((bounds->x + bounds->width / 2) * scale), y = static_cast<int>((bounds->y + bounds->height / 2) * scale);
            SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            SendMessageW(window.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        };
        const auto key = [&](WPARAM virtualKey) { SendMessageW(window.hwnd(), WM_KEYDOWN, virtualKey, 0); SendMessageW(window.hwnd(), WM_KEYUP, virtualKey, 0); };
        const auto type = [&](TextField& field, std::wstring_view text) {
            for (const wchar_t c : text) { SendMessageW(field.hwnd(), WM_CHAR, c, 0); }
        };
        require(app.post([&] {
            auto& mailbox = window.mailbox();
            paint();
            require(window.draw_count() > 0 && window.folder() == mail::Folder::inbox, "Initial paint or folder is wrong");
            const auto inbox = window.listed();
            require(inbox.size() >= 10 && window.selected() == inbox.front(), "Launch should select the newest inbox message");
            require(mailbox.find(inbox[1])->unread, "Second inbox message should start unread");
            click(MailDemoWindow::Hit::message, inbox[1]);
            require(window.selected() == inbox[1] && !mailbox.find(inbox[1])->unread, "Clicking a row did not select and mark it read");
            key(VK_DOWN);
            require(window.selected() == inbox[2], "Down arrow did not move the selection");
            key(VK_UP);
            require(window.selected() == inbox[1], "Up arrow did not move the selection");
            const bool starred = mailbox.find(inbox[1])->starred;
            key('S');
            require(mailbox.find(inbox[1])->starred != starred, "S did not toggle the star");
            click(MailDemoWindow::Hit::message_star, inbox[1]);
            require(mailbox.find(inbox[1])->starred == starred, "Clicking the row star did not toggle it back");
            click(MailDemoWindow::Hit::header_star, inbox[1]);
            require(mailbox.find(inbox[1])->starred != starred, "Clicking the header star did not toggle it");
            key('S');
            key('U');
            require(mailbox.find(inbox[1])->unread && window.status() == L"Marked as unread", "U did not mark the message unread");

            key(VK_DELETE);
            require(mailbox.find(inbox[1])->folder == mail::Folder::trash && window.selected() == inbox[2], "Delete did not trash and advance");
            require(window.status() == L"Moved to Trash" && window.undo_button().enabled(), "Delete did not offer undo");
            paint();
            require(IsWindowVisible(window.undo_button().hwnd()), "Undo button is not visible after a move");
            window.undo_button().invoke();
            require(mailbox.find(inbox[1])->folder == mail::Folder::inbox && window.selected() == inbox[1], "Undo did not restore the message");
            paint();
            require(!IsWindowVisible(window.undo_button().hwnd()), "Undo button stayed visible after undo");
            window.archive_button().invoke();
            require(mailbox.find(inbox[1])->folder == mail::Folder::archive, "Archive button did not archive");
            window.undo();
            require(mailbox.find(inbox[1])->folder == mail::Folder::inbox, "Undo after archive failed");

            const auto sentIndex = static_cast<unsigned>(std::ranges::find(mail::folders, mail::Folder::sent) - mail::folders.begin());
            click(MailDemoWindow::Hit::folder, sentIndex);
            require(window.folder() == mail::Folder::sent && window.selected() == window.listed().front(), "Folder click did not switch folders");
            window.select_folder(mail::Folder::inbox);

            window.scroll_list(400);
            paint();
            require(window.list_scroll() == 400, "List scroll was not applied");
            window.scroll_list(-10000);
            paint();
            require(window.list_scroll() == 0, "List scroll did not clamp");
            window.scroll_body(10000);
            paint();
            require(window.body_scroll() >= 0, "Body scroll did not clamp");

            window.search_field().set_text(L"harborlight");
            require(window.listed() == mailbox.list(mail::Folder::inbox, L"harborlight") && !window.listed().empty(), "Search did not filter the list");
            SendMessageW(window.hwnd(), WM_COMMAND, IDCANCEL, 0);
            require(window.search_field().text().empty() && window.listed().size() == inbox.size(), "Escape did not clear the search");

            const auto sentBefore = mailbox.count(mail::Folder::sent);
            window.compose_button().invoke();
            paint();
            require(window.composing() && IsWindowVisible(window.to_field().hwnd()) && IsWindowVisible(window.send_button().hwnd()), "Compose did not show its form");
            require(!IsWindowVisible(window.reply_button().hwnd()) && !window.compose_button().enabled(), "Reading controls stayed active while composing");
            require(GetFocus() == window.to_field().hwnd(), "Compose did not focus the recipient field");
            window.send();
            require(window.composing() && window.status() == L"Add a recipient before sending", "Sending without a recipient was accepted");
            type(window.to_field(), L"mira@harborlightbooks.example");
            SendMessageW(window.to_field().hwnd(), WM_KEYDOWN, VK_RETURN, 0);
            require(GetFocus() == window.subject_field().hwnd(), "Enter in the recipient field did not advance focus");
            type(window.subject_field(), L"Estimate review");
            type(window.body_field(), L"Hi Mira,");
            SendMessageW(window.body_field().hwnd(), WM_KEYDOWN, VK_RETURN, 0);
            type(window.body_field(), L"Tuesday works.");
            require(window.body_field().text() == L"Hi Mira,\nTuesday works.", "Typing into the body produced the wrong text");
            SendMessageW(window.body_field().hwnd(), WM_KEYDOWN, VK_BACK, 0);
            SendMessageW(window.body_field().hwnd(), WM_KEYDOWN, VK_HOME, 0);
            require(window.body_field().text() == L"Hi Mira,\nTuesday works" && window.body_field().caret() == 9, "Backspace or Home misbehaved");
            window.send_button().invoke();
            require(!window.composing() && mailbox.count(mail::Folder::sent) == sentBefore + 1, "Send did not add a sent message");
            const auto sent = mailbox.list(mail::Folder::sent).front();
            require(mailbox.find(sent)->recipients == L"mira@harborlightbooks.example" && mailbox.find(sent)->subject == L"Estimate review", "Sent message content is wrong");
            require(window.status().starts_with(L"Sent to mira@harborlightbooks.example") && window.selected() == inbox.front(), "Send did not report or restore selection");

            window.select_message(inbox[3]);
            window.reply_button().invoke();
            const auto& original = *mailbox.find(inbox[3]);
            require(window.composing() && window.to_field().text().find(original.address) != std::wstring::npos, "Reply did not address the sender");
            require(window.subject_field().text() == mail::reply_subject(original.subject) && window.body_field().text().find(L"wrote:") != std::wstring::npos, "Reply did not quote");
            require(window.body_field().caret() == 0 && GetFocus() == window.body_field().hwnd(), "Reply did not place the caret before the quote");
            window.discard();
            require(!window.composing() && window.status() == L"Message discarded" && window.selected() == inbox[3], "Discard did not close compose");

            const auto draftsBefore = mailbox.count(mail::Folder::drafts);
            window.forward();
            require(window.composing() && window.subject_field().text() == mail::forward_subject(original.subject), "Forward did not prefill the subject");
            SendMessageW(window.hwnd(), WM_COMMAND, IDCANCEL, 0);
            require(!window.composing() && mailbox.count(mail::Folder::drafts) == draftsBefore + 1 && window.status() == L"Draft saved", "Escape did not save the forward as a draft");
            window.compose();
            SendMessageW(window.hwnd(), WM_COMMAND, IDCANCEL, 0);
            require(!window.composing() && mailbox.count(mail::Folder::drafts) == draftsBefore + 1, "Escape saved an empty draft");

            window.select_folder(mail::Folder::drafts);
            const auto drafts = window.listed();
            require(window.selected() == drafts.front() && !window.composing(), "Selecting a draft opened compose implicitly");
            key(VK_RETURN);
            require(window.composing() && window.subject_field().text() == mailbox.find(drafts.front())->subject, "Enter did not open the draft");
            type(window.subject_field(), L" (edited)");
            window.save_draft();
            require(!window.composing() && mailbox.count(mail::Folder::drafts) == draftsBefore + 1, "Saving an edited draft duplicated it");
            require(mailbox.find(drafts.front())->subject.ends_with(L" (edited)") && window.selected() == drafts.front(), "Edited draft was not updated");
            click(MailDemoWindow::Hit::message, drafts.front());
            require(window.composing(), "Clicking a draft did not open it");
            window.discard();
            require(!window.composing() && !mailbox.find(drafts.front()) && window.status() == L"Draft deleted", "Discarding an open draft did not delete it");

            window.select_folder(mail::Folder::trash);
            const auto trash = window.listed();
            key(VK_DELETE);
            require(!mailbox.find(trash.front()) && window.status() == L"Deleted permanently" && !window.undo_button().enabled(), "Trash delete was not permanent");

            THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(window.hwnd(), nullptr, 0, 0, 1000, 620, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
            const auto draws = window.draw_count();
            paint();
            require(window.draw_count() > draws, "Resize did not repaint");
            app.graphics().recreate();
            paint();
            require(window.draw_count() > draws + 1, "Device replacement did not repaint");
            window.select_folder(mail::Folder::inbox);
            paint();
            require(window.selected() == window.listed().front() && window.region(MailDemoWindow::Hit::message, window.listed().front()), "Final state is wrong");
            PostMessageW(window.hwnd(), WM_CLOSE, 0, 0);
        }), "Could not schedule the client checks");
        require(app.run() == 0, "Client loop failed");
    }
    app.close();
}
}

int main(int argc, char** argv) {
    try {
        require(argc >= 2, "Expected a test name");
        const std::string_view name{argv[1]};
        if (name == "model") { model(); }
        else if (name == "client") { client(argc > 2 && std::string_view{argv[2]} == "--warp"); }
        else { throw std::runtime_error("Unknown test"); }
        return 0;
    } catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
