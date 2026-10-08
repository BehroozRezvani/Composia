#pragma once

#include "MailModel.hpp"
#include "TextField.hpp"
#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <array>
#include <map>
#include <optional>
#include <vector>

class MailPainter;

// A three-pane mail client over an in-memory mailbox. It has no account, network, or storage.
class MailDemoWindow final : public composia::Window {
public:
    enum class Hit { folder, message, message_star, header_star, mark_unread, attachment };
    struct Region {
        Hit kind;
        unsigned id;
        composia::layout::Rect bounds;
    };

    explicit MailDemoWindow(composia::Application&);
    ~MailDemoWindow() override;

    [[nodiscard]] mail::Mailbox& mailbox() noexcept { return mailbox_; }
    [[nodiscard]] mail::Folder folder() const noexcept { return folder_; }
    [[nodiscard]] const std::vector<unsigned>& listed() const noexcept { return ids_; }
    [[nodiscard]] std::optional<unsigned> selected() const noexcept { return selected_; }
    [[nodiscard]] bool composing() const noexcept { return composing_; }
    [[nodiscard]] const std::wstring& status() const noexcept { return status_; }
    [[nodiscard]] float list_scroll() const noexcept { return listScroll_; }
    [[nodiscard]] float body_scroll() const noexcept { return bodyScroll_; }
    [[nodiscard]] unsigned draw_count() const noexcept { return drawCount_; }
    // Bounds in DIPs from the most recent draw, for pointer input and tests.
    [[nodiscard]] std::optional<composia::layout::Rect> region(Hit, unsigned id = 0) const noexcept;
    [[nodiscard]] composia::CompositionWindowTarget& composition_target() noexcept { return target_; }
    [[nodiscard]] TextField& search_field() noexcept { return search_; }
    [[nodiscard]] TextField& to_field() noexcept { return to_; }
    [[nodiscard]] TextField& subject_field() noexcept { return subject_; }
    [[nodiscard]] TextField& body_field() noexcept { return body_; }
    [[nodiscard]] composia::Button& compose_button() noexcept { return composeButton_; }
    [[nodiscard]] composia::Button& reply_button() noexcept { return replyButton_; }
    [[nodiscard]] composia::Button& archive_button() noexcept { return archiveButton_; }
    [[nodiscard]] composia::Button& delete_button() noexcept { return deleteButton_; }
    [[nodiscard]] composia::Button& send_button() noexcept { return sendButton_; }
    [[nodiscard]] composia::Button& undo_button() noexcept { return undoButton_; }

    void select_folder(mail::Folder);
    void select_message(std::optional<unsigned> id);
    void select_adjacent(int delta);
    void search(std::wstring_view query);
    void compose();
    void reply();
    void forward();
    void send();
    void save_draft();
    void discard();
    void dismiss();
    void open_draft();
    void archive();
    void remove();
    void toggle_star(std::optional<unsigned> id = std::nullopt);
    void mark_unread();
    void undo();
    void scroll_list(float delta);
    void scroll_body(float delta);

private:
    struct Undo {
        unsigned id;
        mail::Folder from;
    };
    void on_resize() override { invalidate(); }
    void on_paint() override;
    void on_graphics_recreated() override { brush_.reset(); }
    std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) override;
    void arrange();
    void draw();
    void draw_sidebar(MailPainter&);
    void draw_list(MailPainter&);
    void draw_reading(MailPainter&);
    void draw_compose(MailPainter&);
    void draw_status(MailPainter&);
    void refresh();
    void update_controls();
    void notify(std::wstring text, std::optional<Undo> undo = std::nullopt);
    void open_compose(std::wstring_view title, std::wstring to, std::wstring subject, std::wstring body, std::optional<unsigned> draft);
    void close_compose();
    [[nodiscard]] bool compose_blank() const;
    void move_selected(mail::Folder destination, std::wstring_view verb);
    [[nodiscard]] std::optional<Region> hit_test(float x, float y) const noexcept;
    [[nodiscard]] composia::numerics::float2 pointer(LPARAM) const noexcept;

    composia::Application& app_;
    composia::CompositionWindowTarget target_;
    wil::com_ptr<ID2D1SolidColorBrush> brush_;
    std::map<int, wil::com_ptr<IDWriteTextFormat>> iconFormats_;
    mail::Mailbox mailbox_;
    mail::Folder folder_{mail::Folder::inbox};
    std::vector<unsigned> ids_;
    std::optional<unsigned> selected_, draftId_, resumeId_;
    std::wstring query_, status_, composeTitle_;
    std::optional<Undo> undo_;
    std::vector<Region> regions_;
    std::optional<Region> hover_;
    float listScroll_{}, bodyScroll_{}, listExtent_{}, bodyExtent_{};
    composia::layout::Rect sidebar_{}, list_{}, reading_{}, listView_{}, bodyView_{};
    composia::Button composeButton_, replyButton_, forwardButton_, archiveButton_, deleteButton_;
    composia::Button sendButton_, draftButton_, discardButton_, undoButton_;
    TextField search_, to_, subject_, body_;
    std::array<composia::Connection, 12> connections_;
    bool composing_{}, tracking_{};
    unsigned drawCount_{};
    mail::Clock::time_point now_ = mail::Clock::now();
};
