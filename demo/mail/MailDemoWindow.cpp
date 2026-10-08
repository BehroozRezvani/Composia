#include "MailDemoWindow.hpp"
#include <composia/ScopedSurfaceDraw.hpp>
#include <windowsx.h>
#include <algorithm>
#include <cwctype>
#include <initializer_list>

using namespace composia;
using mail::Folder;

namespace {
constexpr UINT32 chrome = 0x0B121B, listBg = 0x101923, readBg = 0x131D28, divider = 0x1F2C38, ink = 0xEAF2F4, inkSoft = 0xC3D0D8,
    inkMuted = 0x93A9B5, inkFaint = 0x5F7380, accent = 0x6FE6C8, gold = 0xF2C14E, selectedBg = 0x1C2B38, hoverBg = 0x16212C,
    badgeBg = 0x22313D, chipBg = 0x1C2733, thumb = 0x33424E;
constexpr std::array<UINT32, 8> avatarColors{0x5B8DEF, 0xB36FE6, 0xE66F8F, 0xE6A26F, 0x6FE6C8, 0x8FD36F, 0x6FB8E6, 0xD9C46F};
constexpr float topBar = 56, statusBar = 28, sidebarWidth = 216, rowHeight = 76, folderRow = 36;
// Segoe MDL2 Assets glyphs.
constexpr wchar_t iconMail = 0xE715, iconStar = 0xE734, iconStarFill = 0xE735, iconSend = 0xE724, iconEdit = 0xE70F,
    iconArchive = 0xE7B8, iconJunk = 0xE7BA, iconTrash = 0xE74D, iconAttach = 0xE8A1;
constexpr auto ownerName = L"Jordan Reyes", ownerAddress = L"jordan@lumen.example";

wchar_t folder_icon(Folder folder) noexcept {
    switch (folder) {
    case Folder::inbox: return iconMail;
    case Folder::starred: return iconStarFill;
    case Folder::sent: return iconSend;
    case Folder::drafts: return iconEdit;
    case Folder::archive: return iconArchive;
    case Folder::junk: return iconJunk;
    case Folder::trash: return iconTrash;
    }
    return iconMail;
}

unsigned folder_index(Folder folder) noexcept {
    return static_cast<unsigned>(std::ranges::find(mail::folders, folder) - mail::folders.begin());
}

bool blank(std::wstring_view text) noexcept {
    return std::ranges::all_of(text, [](wchar_t c) { return std::iswspace(c) != 0; });
}

struct Style {
    float size = 14;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    bool wrap = false;
    DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
    bool middle = false;
    float lineHeight = 0;
};
}

// Drawing helpers for one surface update. Text layouts are rebuilt per draw; icons use cached formats.
class MailPainter {
public:
    MailPainter(ID2D1DeviceContext6* dc, IDWriteFactory7* factory, ID2D1SolidColorBrush* brush,
        std::map<int, wil::com_ptr<IDWriteTextFormat>>& formats) : dc_(dc), factory_(factory), brush_(brush), formats_(formats) {}

    [[nodiscard]] TextLayout make(std::wstring_view value, float width, float height, const Style& style) const {
        TextLayout label{factory_, value, style.size, style.weight};
        label.resize(std::max(1.0f, width), std::max(1.0f, height));
        const auto layout = label.layout().get();
        THROW_IF_FAILED(layout->SetTextAlignment(style.align));
        THROW_IF_FAILED(layout->SetParagraphAlignment(style.middle ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR));
        if (!style.wrap) {
            THROW_IF_FAILED(layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
            wil::com_ptr<IDWriteInlineObject> ellipsis;
            THROW_IF_FAILED(factory_->CreateEllipsisTrimmingSign(layout, ellipsis.put()));
            const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            THROW_IF_FAILED(layout->SetTrimming(&trimming, ellipsis.get()));
        }
        if (style.lineHeight > 0) {
            THROW_IF_FAILED(layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, style.lineHeight, style.lineHeight * 0.8f));
        }
        return label;
    }

    void draw(const TextLayout& label, float x, float y, UINT32 color) const {
        brush_->SetColor(D2D1::ColorF(color));
        dc_->DrawTextLayout({x, y}, label.layout().get(), brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    [[nodiscard]] static float height(const TextLayout& label) {
        DWRITE_TEXT_METRICS metrics{};
        THROW_IF_FAILED(label.layout()->GetMetrics(&metrics));
        return metrics.height;
    }

    float text(std::wstring_view value, layout::Rect bounds, UINT32 color, const Style& style = {}) const {
        if (value.empty() || bounds.width <= 0 || bounds.height <= 0) { return 0; }
        const auto label = make(value, bounds.width, bounds.height, style);
        draw(label, bounds.x, bounds.y, color);
        return height(label);
    }

    [[nodiscard]] float measure(std::wstring_view value, const Style& style = {}) const {
        if (value.empty()) { return 0; }
        DWRITE_TEXT_METRICS metrics{};
        THROW_IF_FAILED(make(value, 100000, 1000, style).layout()->GetMetrics(&metrics));
        return metrics.widthIncludingTrailingWhitespace;
    }

    void fill(layout::Rect r, UINT32 color, float radius = 0) const {
        if (r.width <= 0 || r.height <= 0) { return; }
        brush_->SetColor(D2D1::ColorF(color));
        const D2D1_RECT_F rect{r.x, r.y, r.x + r.width, r.y + r.height};
        if (radius > 0) { dc_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_); }
        else { dc_->FillRectangle(rect, brush_); }
    }

    void icon(wchar_t glyph, float x, float y, float size, UINT32 color) const {
        auto& format = formats_[static_cast<int>(size * 4)];
        if (!format) {
            THROW_IF_FAILED(factory_->CreateTextFormat(L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, size, L"en-US", format.put()));
        }
        brush_->SetColor(D2D1::ColorF(color));
        dc_->DrawText(&glyph, 1, format.get(), {x, y, x + size * 2, y + size * 2}, brush_);
    }

    void avatar(std::wstring_view name, float cx, float cy, float radius) const {
        brush_->SetColor(D2D1::ColorF(avatarColors[std::hash<std::wstring_view>{}(name) % avatarColors.size()]));
        dc_->FillEllipse(D2D1::Ellipse({cx, cy}, radius, radius), brush_);
        text(mail::initials(name), {cx - radius, cy - radius, radius * 2, radius * 2}, chrome,
            {.size = radius * 0.72f, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD, .align = DWRITE_TEXT_ALIGNMENT_CENTER, .middle = true});
    }

    void clip(layout::Rect r) const { dc_->PushAxisAlignedClip({r.x, r.y, r.x + r.width, r.y + r.height}, D2D1_ANTIALIAS_MODE_ALIASED); }
    void unclip() const { dc_->PopAxisAlignedClip(); }

    void scrollbar(layout::Rect view, float extent, float offset) const {
        if (extent <= view.height) { return; }
        const float length = std::max(24.0f, view.height * view.height / extent);
        const float top = view.y + (view.height - length) * offset / (extent - view.height);
        fill({view.x + view.width - 6, top, 3, length}, thumb, 1.5f);
    }

private:
    ID2D1DeviceContext6* dc_;
    IDWriteFactory7* factory_;
    ID2D1SolidColorBrush* brush_;
    std::map<int, wil::com_ptr<IDWriteTextFormat>>& formats_;
};

MailDemoWindow::MailDemoWindow(Application& app)
    : Window(app, L"Lumen Mail", 1240, 800), app_(app), target_(app.compositor(), app.graphics(), hwnd()),
      mailbox_(mail::Mailbox::sample(mail::Clock::now())), composeButton_(*this, L"Compose"), replyButton_(*this, L"Reply"),
      forwardButton_(*this, L"Forward"), archiveButton_(*this, L"Archive"), deleteButton_(*this, L"Delete"),
      sendButton_(*this, L"Send"), draftButton_(*this, L"Save draft"), discardButton_(*this, L"Discard"), undoButton_(*this, L"Undo"),
      search_(*this, L"Search mail"), to_(*this, L"Recipients"), subject_(*this, L"Subject"), body_(*this, L"Write your message", true) {
    connections_[0] = composeButton_.on_click([this] { compose(); });
    connections_[1] = replyButton_.on_click([this] { reply(); });
    connections_[2] = forwardButton_.on_click([this] { forward(); });
    connections_[3] = archiveButton_.on_click([this] { archive(); });
    connections_[4] = deleteButton_.on_click([this] { remove(); });
    connections_[5] = sendButton_.on_click([this] { send(); });
    connections_[6] = draftButton_.on_click([this] { save_draft(); });
    connections_[7] = discardButton_.on_click([this] { discard(); });
    connections_[8] = undoButton_.on_click([this] { undo(); });
    connections_[9] = search_.on_change([this] { search(search_.text()); });
    connections_[10] = to_.on_submit([this] { subject_.focus(); });
    connections_[11] = subject_.on_submit([this] { body_.focus(); });
    for (Window* hidden : std::initializer_list<Window*>{&sendButton_, &draftButton_, &discardButton_, &undoButton_, &to_, &subject_, &body_}) {
        hidden->show(SW_HIDE);
    }
    refresh();
    select_message(ids_.empty() ? std::nullopt : std::optional{ids_.front()});
    update_controls();
    invalidate();
}

MailDemoWindow::~MailDemoWindow() = default;

std::optional<layout::Rect> MailDemoWindow::region(Hit kind, unsigned id) const noexcept {
    for (const auto& region : regions_) {
        if (region.kind == kind && region.id == id) { return region.bounds; }
    }
    return std::nullopt;
}

std::optional<MailDemoWindow::Region> MailDemoWindow::hit_test(float x, float y) const noexcept {
    for (const auto& region : regions_) {
        if (region.bounds.contains(x, y)) { return region; }
    }
    return std::nullopt;
}

numerics::float2 MailDemoWindow::pointer(LPARAM lparam) const noexcept {
    const auto scale = static_cast<float>(dpi() ? dpi() : 96) / 96.0f;
    return {GET_X_LPARAM(lparam) / scale, GET_Y_LPARAM(lparam) / scale};
}

void MailDemoWindow::refresh() {
    ids_ = mailbox_.list(folder_, query_);
    if (selected_ && !mailbox_.find(*selected_)) { selected_.reset(); }
}

void MailDemoWindow::notify(std::wstring text, std::optional<Undo> undo) {
    status_ = std::move(text);
    undo_ = undo;
    update_controls();
    invalidate();
}

void MailDemoWindow::update_controls() {
    const auto message = selected_ ? mailbox_.find(*selected_) : nullptr;
    const bool reading = message && !composing_;
    composeButton_.enabled(!composing_);
    replyButton_.enabled(reading && message->folder != Folder::drafts);
    forwardButton_.enabled(reading && message->folder != Folder::drafts);
    archiveButton_.enabled(reading && message->folder != Folder::archive && message->folder != Folder::drafts);
    deleteButton_.enabled(reading);
    sendButton_.enabled(composing_);
    draftButton_.enabled(composing_);
    discardButton_.enabled(composing_);
    undoButton_.enabled(undo_.has_value());
}

void MailDemoWindow::select_folder(Folder folder) {
    if (composing_) { dismiss(); }
    folder_ = folder;
    listScroll_ = 0;
    status_.clear();
    refresh();
    select_message(ids_.empty() ? std::nullopt : std::optional{ids_.front()});
}

void MailDemoWindow::select_message(std::optional<unsigned> id) {
    if (composing_) { dismiss(); }
    if (id && !mailbox_.find(*id)) { id.reset(); }
    selected_ = id;
    bodyScroll_ = 0;
    if (id) { mailbox_.set_unread(*id, false); }
    update_controls();
    invalidate();
}

void MailDemoWindow::select_adjacent(int delta) {
    if (ids_.empty()) { return; }
    const auto current = selected_ ? std::ranges::find(ids_, *selected_) : ids_.end();
    const auto index = current == ids_.end() ? (delta > 0 ? 0 : static_cast<int>(ids_.size()) - 1)
        : std::clamp(static_cast<int>(current - ids_.begin()) + delta, 0, static_cast<int>(ids_.size()) - 1);
    select_message(ids_[static_cast<std::size_t>(index)]);
    const float top = index * rowHeight;
    if (top < listScroll_) { listScroll_ = top; }
    else if (top + rowHeight > listScroll_ + listView_.height) { listScroll_ = top + rowHeight - listView_.height; }
}

void MailDemoWindow::search(std::wstring_view query) {
    query_ = query;
    listScroll_ = 0;
    refresh();
    invalidate();
}

void MailDemoWindow::open_compose(std::wstring_view title, std::wstring to, std::wstring subject, std::wstring body, std::optional<unsigned> draft) {
    if (composing_) { return; }
    composing_ = true;
    resumeId_ = selected_;
    draftId_ = draft;
    composeTitle_ = title;
    to_.set_text(std::move(to));
    subject_.set_text(std::move(subject));
    body_.set_text(std::move(body));
    update_controls();
    arrange();
    invalidate();
    if (to_.text().empty()) { to_.focus(); }
    else { body_.set_caret(0); body_.focus(); }
}

void MailDemoWindow::close_compose() {
    composing_ = false;
    draftId_.reset();
    composeTitle_.clear();
    to_.set_text({});
    subject_.set_text({});
    body_.set_text({});
    refresh();
    selected_ = resumeId_ && mailbox_.find(*resumeId_) ? resumeId_ : std::nullopt;
    resumeId_.reset();
    if (!selected_ && !ids_.empty()) { selected_ = ids_.front(); }
    bodyScroll_ = 0;
    update_controls();
    arrange();
    SetFocus(hwnd());
    invalidate();
}

bool MailDemoWindow::compose_blank() const {
    return blank(to_.text()) && blank(subject_.text()) && blank(body_.text());
}

void MailDemoWindow::compose() { open_compose(L"New message", {}, {}, {}, std::nullopt); }

void MailDemoWindow::reply() {
    const auto message = selected_ ? mailbox_.find(*selected_) : nullptr;
    if (!message || composing_ || message->folder == Folder::drafts) { return; }
    const auto to = message->folder == Folder::sent ? message->recipients : mail::concat(message->sender, L" <", message->address, L">");
    open_compose(L"Reply", to, mail::reply_subject(message->subject), mail::quote_reply(*message), std::nullopt);
}

void MailDemoWindow::forward() {
    const auto message = selected_ ? mailbox_.find(*selected_) : nullptr;
    if (!message || composing_ || message->folder == Folder::drafts) { return; }
    open_compose(L"Forward", {}, mail::forward_subject(message->subject), mail::quote_forward(*message), std::nullopt);
}

void MailDemoWindow::open_draft() {
    const auto message = selected_ ? mailbox_.find(*selected_) : nullptr;
    if (!message || composing_ || message->folder != Folder::drafts) { return; }
    open_compose(L"Edit draft", message->recipients, message->subject, message->body, message->id);
}

void MailDemoWindow::send() {
    if (!composing_) { return; }
    if (blank(to_.text())) {
        notify(L"Add a recipient before sending");
        to_.focus();
        return;
    }
    mail::Message message;
    message.folder = Folder::sent;
    message.sender = ownerName;
    message.address = ownerAddress;
    message.recipients = to_.text();
    message.subject = blank(subject_.text()) ? L"(no subject)" : subject_.text();
    message.body = body_.text();
    message.received = mail::Clock::now();
    if (draftId_) { mailbox_.remove(*draftId_); }
    const auto id = mailbox_.add(std::move(message));
    const auto recipients = to_.text();
    close_compose();
    if (folder_ == Folder::sent) { selected_ = id; }
    notify(mail::concat(L"Sent to ", recipients, L"  (demo only, nothing left this PC)"));
}

void MailDemoWindow::save_draft() {
    if (!composing_) { return; }
    if (compose_blank()) {
        notify(L"Nothing to save");
        return;
    }
    const auto now = mail::Clock::now();
    if (auto draft = draftId_ ? mailbox_.find(*draftId_) : nullptr) {
        draft->recipients = to_.text();
        draft->subject = subject_.text();
        draft->body = body_.text();
        draft->received = now;
    } else {
        mail::Message message;
        message.folder = Folder::drafts;
        message.sender = ownerName;
        message.address = ownerAddress;
        message.recipients = to_.text();
        message.subject = subject_.text();
        message.body = body_.text();
        message.received = now;
        draftId_ = mailbox_.add(std::move(message));
    }
    const auto saved = *draftId_;
    close_compose();
    if (folder_ == Folder::drafts) { selected_ = saved; }
    notify(L"Draft saved");
}

void MailDemoWindow::discard() {
    if (!composing_) { return; }
    const bool existing = draftId_ && mailbox_.find(*draftId_);
    if (existing) { mailbox_.remove(*draftId_); }
    close_compose();
    notify(existing ? L"Draft deleted" : L"Message discarded");
}

void MailDemoWindow::dismiss() {
    if (!composing_) { return; }
    if (compose_blank()) { close_compose(); notify(L"Empty message closed"); }
    else { save_draft(); }
}

void MailDemoWindow::move_selected(Folder destination, std::wstring_view verb) {
    const auto message = selected_ ? mailbox_.find(*selected_) : nullptr;
    if (!message || composing_ || message->folder == destination) { return; }
    const auto id = message->id;
    const auto from = message->folder;
    std::optional<unsigned> next;
    if (const auto it = std::ranges::find(ids_, id); it != ids_.end()) {
        if (it + 1 != ids_.end()) { next = *(it + 1); }
        else if (it != ids_.begin()) { next = *(it - 1); }
    }
    mailbox_.move(id, destination);
    refresh();
    select_message(next);
    notify(std::wstring{verb}, Undo{id, from});
}

void MailDemoWindow::archive() { move_selected(Folder::archive, L"Archived"); }

void MailDemoWindow::remove() {
    const auto message = selected_ ? mailbox_.find(*selected_) : nullptr;
    if (!message || composing_) { return; }
    if (message->folder != Folder::trash) { move_selected(Folder::trash, L"Moved to Trash"); return; }
    const auto id = message->id;
    std::optional<unsigned> next;
    if (const auto it = std::ranges::find(ids_, id); it != ids_.end()) {
        if (it + 1 != ids_.end()) { next = *(it + 1); }
        else if (it != ids_.begin()) { next = *(it - 1); }
    }
    mailbox_.remove(id);
    refresh();
    select_message(next);
    notify(L"Deleted permanently");
}

void MailDemoWindow::toggle_star(std::optional<unsigned> id) {
    const auto message = mailbox_.find(id ? *id : selected_.value_or(0));
    if (!message) { return; }
    message->starred = !message->starred;
    refresh();
    invalidate();
}

void MailDemoWindow::mark_unread() {
    if (!selected_ || composing_) { return; }
    mailbox_.set_unread(*selected_, true);
    notify(L"Marked as unread");
}

void MailDemoWindow::undo() {
    if (!undo_) { return; }
    const auto restore = *undo_;
    if (!mailbox_.move(restore.id, restore.from)) { notify(L"Nothing to undo"); return; }
    refresh();
    if (const auto it = std::ranges::find(ids_, restore.id); it != ids_.end()) {
        select_message(restore.id);
        const float top = static_cast<float>(it - ids_.begin()) * rowHeight;
        listScroll_ = std::clamp(listScroll_, std::min(top, top + rowHeight - listView_.height), top);
    }
    notify(mail::concat(L"Restored to ", mail::folder_name(restore.from)));
}

void MailDemoWindow::scroll_list(float delta) { listScroll_ += delta; invalidate(); }
void MailDemoWindow::scroll_body(float delta) { bodyScroll_ += delta; invalidate(); }

void MailDemoWindow::arrange() {
    const auto size = target_.logical_size();
    if (size.x <= 0 || size.y <= 0) { return; }
    const auto visible = [](Window& window, bool shown) {
        if ((IsWindowVisible(window.hwnd()) != FALSE) != shown) { window.show(shown ? SW_SHOWNA : SW_HIDE); }
    };
    const float paneHeight = std::max(1.0f, size.y - topBar - statusBar);
    sidebar_ = {0, topBar, sidebarWidth, paneHeight};
    const float listWidth = std::clamp(size.x * 0.3f, 300.0f, 400.0f);
    list_ = {sidebarWidth, topBar, listWidth, paneHeight};
    reading_ = {sidebarWidth + listWidth, topBar, std::max(1.0f, size.x - sidebarWidth - listWidth), paneHeight};
    composeButton_.set_bounds({16, topBar + 12, sidebarWidth - 32, 40});
    search_.set_bounds({list_.x + 16, topBar + 50, std::max(1.0f, list_.width - 32), 36});
    listView_ = {list_.x, topBar + 100, list_.width, std::max(1.0f, list_.height - 100)};
    const float x = reading_.x + 32, right = reading_.x + reading_.width - 32, width = std::max(1.0f, right - x);
    const auto tools = layout::stack({x, topBar + 12, width, 36}, std::array{84.0f, 96.0f, 92.0f, 84.0f}, 8, layout::Axis::horizontal);
    Button* toolButtons[]{&replyButton_, &forwardButton_, &archiveButton_, &deleteButton_};
    for (std::size_t index = 0; index != tools.size(); ++index) {
        toolButtons[index]->set_bounds(tools[index]);
        visible(*toolButtons[index], !composing_);
    }
    const float formTop = topBar + 64;
    to_.set_bounds({x + 72, formTop, std::max(1.0f, width - 72), 36});
    subject_.set_bounds({x + 72, formTop + 44, std::max(1.0f, width - 72), 36});
    const float bodyTop = formTop + 92, bodyBottom = reading_.y + reading_.height - 64;
    body_.set_bounds({x, bodyTop, width, std::max(40.0f, bodyBottom - bodyTop)});
    const auto actions = layout::stack({x, reading_.y + reading_.height - 52, width, 40}, std::array{96.0f, 112.0f, 96.0f}, 8, layout::Axis::horizontal);
    sendButton_.set_bounds(actions[0]);
    draftButton_.set_bounds(actions[1]);
    discardButton_.set_bounds(actions[2]);
    for (Window* control : std::initializer_list<Window*>{&sendButton_, &draftButton_, &discardButton_, &to_, &subject_, &body_}) {
        visible(*control, composing_);
    }
    visible(undoButton_, undo_.has_value());
}

void MailDemoWindow::on_paint() {
    const auto pixels = client_pixels();
    if (pixels.cx <= 0 || pixels.cy <= 0 || IsIconic(hwnd())) { return; }
    app_.render([&] {
        target_.resize(pixels, dpi());
        arrange();
        draw();
    });
}

void MailDemoWindow::draw() {
    ScopedSurfaceDraw draw{target_.surface(), app_.graphics(), dpi()};
    const auto dc = draw.context().get();
    const auto size = target_.logical_size();
    dc->Clear(D2D1::ColorF(chrome));
    if (!brush_) { THROW_IF_FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(ink), brush_.put())); }
    regions_.clear();
    MailPainter p{dc, draw.text_factory().get(), brush_.get(), iconFormats_};
    p.icon(iconMail, 20, 17, 20, accent);
    p.text(L"Lumen Mail", {52, 15, 200, 28}, ink, {.size = 18, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
    p.text(ownerName, {size.x - 276, 11, 260, 20}, ink, {.size = 13, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD, .align = DWRITE_TEXT_ALIGNMENT_TRAILING});
    p.text(ownerAddress, {size.x - 276, 30, 260, 18}, inkMuted, {.size = 12, .align = DWRITE_TEXT_ALIGNMENT_TRAILING});
    draw_sidebar(p);
    draw_list(p);
    if (composing_) { draw_compose(p); } else { draw_reading(p); }
    draw_status(p);
    draw.finish();
    ++drawCount_;
}

void MailDemoWindow::draw_sidebar(MailPainter& p) {
    p.fill(sidebar_, chrome);
    float y = sidebar_.y + 68;
    for (const auto folder : mail::folders) {
        const layout::Rect row{8, y, sidebarWidth - 16, folderRow};
        const bool current = folder == folder_;
        const auto index = folder_index(folder);
        const bool hovered = hover_ && hover_->kind == Hit::folder && hover_->id == index;
        if (current) { p.fill(row, selectedBg, 6); } else if (hovered) { p.fill(row, hoverBg, 6); }
        p.icon(folder_icon(folder), row.x + 12, y + 10, 15, current ? accent : inkMuted);
        p.text(mail::folder_name(folder), {row.x + 42, y + 8, row.width - 110, 20}, current ? ink : inkSoft,
            {.size = 14, .weight = current ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL});
        const unsigned count = folder == Folder::drafts ? mailbox_.count(folder) : mailbox_.unread_count(folder);
        if (count > 0) {
            const auto label = std::to_wstring(count);
            const float width = 18.0f + 7.0f * static_cast<float>(label.size());
            const layout::Rect badge{row.x + row.width - 12 - width, y + 9, width, 18};
            const bool loud = folder == Folder::inbox;
            p.fill(badge, loud ? accent : badgeBg, 9);
            p.text(label, badge, loud ? chrome : inkSoft,
                {.size = 11, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD, .align = DWRITE_TEXT_ALIGNMENT_CENTER, .middle = true});
        }
        regions_.push_back({Hit::folder, index, row});
        y += folderRow + 2;
    }
    const float noteY = sidebar_.y + sidebar_.height - 58;
    p.fill({24, noteY - 12, sidebarWidth - 48, 1}, divider);
    p.text(L"Demo mailbox", {24, noteY, sidebarWidth - 48, 18}, inkMuted, {.size = 12, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
    p.text(mail::concat(mailbox_.size(), L" messages, all in memory"), {24, noteY + 20, sidebarWidth - 48, 16}, inkFaint, {.size = 11});
}

void MailDemoWindow::draw_list(MailPainter& p) {
    p.fill(list_, listBg);
    p.fill({list_.x, list_.y, 1, list_.height}, divider);
    const float x = list_.x + 16, width = list_.width - 32;
    p.text(mail::folder_name(folder_), {x, topBar + 12, width * 0.5f, 26}, ink, {.size = 18, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
    const auto summary = query_.empty()
        ? mail::concat(mailbox_.count(folder_), L" messages, ", mailbox_.unread_count(folder_), L" unread")
        : mail::concat(ids_.size(), L" match “", query_, L"”");
    p.text(summary, {x + width * 0.5f, topBar + 18, width * 0.5f, 18}, inkMuted, {.size = 12, .align = DWRITE_TEXT_ALIGNMENT_TRAILING});

    listExtent_ = static_cast<float>(ids_.size()) * rowHeight;
    listScroll_ = std::clamp(listScroll_, 0.0f, std::max(0.0f, listExtent_ - listView_.height));
    p.clip(listView_);
    if (ids_.empty()) {
        p.text(query_.empty() ? L"Nothing here yet" : L"No messages match your search", {x, listView_.y + 28, width, 22}, inkMuted,
            {.size = 14, .align = DWRITE_TEXT_ALIGNMENT_CENTER});
    }
    float y = listView_.y - listScroll_;
    const float viewBottom = listView_.y + listView_.height;
    for (const auto id : ids_) {
        if (y + rowHeight > listView_.y && y < viewBottom) {
            const auto& message = *mailbox_.find(id);
            const layout::Rect row{list_.x, y, list_.width, rowHeight};
            const bool selected = selected_ == id;
            const bool hovered = hover_ && (hover_->kind == Hit::message || hover_->kind == Hit::message_star) && hover_->id == id;
            if (selected) { p.fill(row, selectedBg); } else if (hovered) { p.fill(row, hoverBg); }
            if (selected) { p.fill({row.x, y, 3, rowHeight}, accent); }
            if (message.unread) { p.fill({row.x + 11, y + 35, 6, 6}, accent, 3); }
            p.avatar(message.sender, row.x + 42, y + 38, 18);
            const float textX = row.x + 72, textRight = row.x + row.width - 16, timeWidth = 76;
            const auto weight = message.unread ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
            p.text(message.sender, {textX, y + 11, std::max(1.0f, textRight - textX - timeWidth - 4), 20}, ink, {.size = 15, .weight = weight});
            p.text(mail::format_time(message.received, now_), {textRight - timeWidth, y + 13, timeWidth, 18},
                message.unread ? accent : inkMuted, {.size = 12, .weight = weight, .align = DWRITE_TEXT_ALIGNMENT_TRAILING});
            float subjectRight = textRight;
            if (!message.attachments.empty()) {
                p.icon(iconAttach, textRight - 14, y + 33, 13, inkMuted);
                subjectRight -= 20;
            }
            p.text(message.subject, {textX, y + 31, std::max(1.0f, subjectRight - textX), 20}, message.unread ? ink : inkSoft, {.size = 14, .weight = weight});
            const layout::Rect star{textRight - 20, y + 50, 24, 24};
            const bool starHover = hover_ && hover_->kind == Hit::message_star && hover_->id == id;
            p.icon(message.starred ? iconStarFill : iconStar, star.x + 4, star.y + 4, 14, message.starred ? gold : starHover ? inkSoft : thumb);
            p.text(mail::preview(message.body), {textX, y + 51, std::max(1.0f, star.x - textX - 6), 18}, inkMuted, {.size = 13});
            p.fill({textX, y + rowHeight - 1, textRight - textX, 1}, divider);
            const auto clipped = [&](layout::Rect r) {
                const float top = std::max(r.y, listView_.y), bottom = std::min(r.y + r.height, viewBottom);
                return layout::Rect{r.x, top, r.width, std::max(0.0f, bottom - top)};
            };
            regions_.push_back({Hit::message_star, id, clipped(star)});
            regions_.push_back({Hit::message, id, clipped(row)});
        }
        y += rowHeight;
    }
    p.unclip();
    p.scrollbar(listView_, listExtent_, listScroll_);
}

void MailDemoWindow::draw_reading(MailPainter& p) {
    p.fill(reading_, readBg);
    p.fill({reading_.x, reading_.y, 1, reading_.height}, divider);
    const float x = reading_.x + 32, right = reading_.x + reading_.width - 32, width = std::max(1.0f, right - x);
    const auto message = selected_ ? mailbox_.find(*selected_) : nullptr;
    if (!message) {
        const float middle = reading_.y + reading_.height * 0.42f;
        p.icon(iconMail, reading_.x + reading_.width * 0.5f - 22, middle - 60, 44, thumb);
        p.text(ids_.empty() ? L"This folder is empty" : L"Select a message to read it", {x, middle, width, 26}, inkSoft,
            {.size = 16, .align = DWRITE_TEXT_ALIGNMENT_CENTER});
        p.text(L"C compose   R reply   E archive   S star   U unread   Delete trash   Up/Down move", {x, middle + 32, width, 20}, inkFaint,
            {.size = 12, .align = DWRITE_TEXT_ALIGNMENT_CENTER});
        return;
    }
    float y = topBar + 66;
    const layout::Rect star{right - 30, y - 2, 30, 30};
    p.icon(message->starred ? iconStarFill : iconStar, star.x + 5, star.y + 4, 20, message->starred ? gold : inkMuted);
    regions_.push_back({Hit::header_star, message->id, star});
    p.text(message->subject, {x, y, std::max(1.0f, width - 44), 30}, ink, {.size = 20, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
    y += 46;
    p.avatar(message->sender, x + 20, y + 20, 20);
    const auto date = mail::format_date(message->received);
    const float dateWidth = std::min(std::max(p.measure(date, {.size = 13}) + 6, 150.0f), width * 0.45f);
    p.text(message->sender, {x + 56, y + 1, std::max(1.0f, width - 56 - dateWidth - 8), 20}, ink, {.size = 15, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
    p.text(mail::concat(message->address, L"   to ", message->recipients.empty() ? std::wstring_view{L"(no recipients)"} : std::wstring_view{message->recipients}),
        {x + 56, y + 22, std::max(1.0f, width - 56 - dateWidth - 8), 18}, inkMuted, {.size = 13});
    p.text(date, {right - dateWidth, y + 2, dateWidth, 18}, inkMuted, {.size = 13, .align = DWRITE_TEXT_ALIGNMENT_TRAILING});
    if (message->folder == Folder::drafts) {
        p.text(L"Press Enter to edit this draft", {right - dateWidth, y + 23, dateWidth, 18}, accent, {.size = 12, .align = DWRITE_TEXT_ALIGNMENT_TRAILING});
    } else {
        const auto label = L"Mark as unread";
        const float labelWidth = p.measure(label, {.size = 12}) + 4;
        const layout::Rect link{right - labelWidth, y + 23, labelWidth, 18};
        const bool hovered = hover_ && hover_->kind == Hit::mark_unread;
        p.text(label, link, hovered ? ink : accent, {.size = 12, .align = DWRITE_TEXT_ALIGNMENT_TRAILING});
        regions_.push_back({Hit::mark_unread, message->id, link});
    }
    y += 58;
    if (!message->attachments.empty()) {
        float cx = x;
        for (std::size_t index = 0; index != message->attachments.size(); ++index) {
            const auto& attachment = message->attachments[index];
            const auto label = mail::concat(attachment.name, L"   ", mail::format_size(attachment.bytes));
            const float chipWidth = std::min(std::min(280.0f, width), p.measure(label, {.size = 13}) + 46);
            if (cx + chipWidth > right && cx > x) { break; }
            const layout::Rect chip{cx, y, chipWidth, 30};
            p.fill(chip, chipBg, 8);
            p.icon(iconAttach, chip.x + 10, chip.y + 7, 14, accent);
            p.text(label, {chip.x + 34, chip.y + 6, chipWidth - 44, 20}, inkSoft, {.size = 13});
            regions_.push_back({Hit::attachment, static_cast<unsigned>(index), chip});
            cx += chipWidth + 8;
        }
        y += 44;
    }
    p.fill({x, y, width, 1}, divider);
    y += 18;
    bodyView_ = {x, y, width, std::max(1.0f, reading_.y + reading_.height - y - 16)};
    const auto body = p.make(message->body, width, 1000000, {.size = 14, .wrap = true, .lineHeight = 22});
    bodyExtent_ = MailPainter::height(body);
    bodyScroll_ = std::clamp(bodyScroll_, 0.0f, std::max(0.0f, bodyExtent_ - bodyView_.height));
    p.clip(bodyView_);
    p.draw(body, x, y - bodyScroll_, inkSoft);
    p.unclip();
    p.scrollbar(bodyView_, bodyExtent_, bodyScroll_);
}

void MailDemoWindow::draw_compose(MailPainter& p) {
    p.fill(reading_, readBg);
    p.fill({reading_.x, reading_.y, 1, reading_.height}, divider);
    const float x = reading_.x + 32, width = std::max(1.0f, reading_.width - 64);
    p.text(composeTitle_, {x, topBar + 16, width, 30}, ink, {.size = 20, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
    const float formTop = topBar + 64;
    p.text(L"To", {x, formTop + 9, 64, 20}, inkMuted, {.size = 13});
    p.text(L"Subject", {x, formTop + 53, 64, 20}, inkMuted, {.size = 13});
    bodyView_ = {};
}

void MailDemoWindow::draw_status(MailPainter& p) {
    const auto size = target_.logical_size();
    const layout::Rect bar{0, size.y - statusBar, size.x, statusBar};
    p.fill(bar, chrome);
    p.fill({0, bar.y, size.x, 1}, divider);
    const auto summary = status_.empty()
        ? mail::concat(mail::folder_name(folder_), L"   ", mailbox_.count(folder_), L" messages   ", mailbox_.unread_count(folder_), L" unread")
        : status_;
    const float textWidth = std::min(size.x - 320, p.measure(summary, {.size = 12}) + 4);
    p.text(summary, {16, bar.y + 6, std::max(1.0f, textWidth), 18}, status_.empty() ? inkMuted : ink, {.size = 12});
    undoButton_.set_bounds({16 + textWidth + 10, bar.y + 3, 64, 22});
    p.text(L"Offline demo, nothing is sent or received", {size.x - 300, bar.y + 6, 284, 18}, inkFaint, {.size = 12, .align = DWRITE_TEXT_ALIGNMENT_TRAILING});
}

std::optional<LRESULT> MailDemoWindow::on_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_GETMINMAXINFO: {
        const auto info = reinterpret_cast<MINMAXINFO*>(lparam);
        const auto windowDpi = hwnd() ? dpi() : GetDpiForSystem();
        info->ptMinTrackSize = {MulDiv(980, windowDpi, 96), MulDiv(600, windowDpi, 96)};
        return 0;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | (wparam == VK_RETURN ? DLGC_WANTMESSAGE : 0);
    case WM_COMMAND:
        if (LOWORD(wparam) == IDCANCEL) {
            if (composing_) { dismiss(); }
            else if (!query_.empty()) { search_.set_text({}); SetFocus(hwnd()); }
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT && hover_) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
        break;
    case WM_LBUTTONDOWN: {
        const auto point = pointer(lparam);
        SetFocus(hwnd());
        if (const auto hit = hit_test(point.x, point.y)) {
            switch (hit->kind) {
            case Hit::folder: select_folder(mail::folders[hit->id]); break;
            case Hit::message:
                select_message(hit->id);
                open_draft();
                break;
            case Hit::message_star:
            case Hit::header_star: toggle_star(hit->id); break;
            case Hit::mark_unread: mark_unread(); break;
            case Hit::attachment: notify(L"Attachments are placeholders in this demo"); break;
            }
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        const auto point = pointer(lparam);
        const auto hit = hit_test(point.x, point.y);
        const bool changed = hit.has_value() != hover_.has_value() || (hit && (hit->kind != hover_->kind || hit->id != hover_->id));
        if (changed) { hover_ = hit; invalidate(); }
        if (!tracking_) {
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd(), 0};
            THROW_IF_WIN32_BOOL_FALSE(TrackMouseEvent(&tracking));
            tracking_ = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        if (hover_) { hover_.reset(); invalidate(); }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd(), &point);
        const auto position = pointer(MAKELPARAM(point.x, point.y));
        const auto steps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
        if (list_.contains(position.x, position.y)) { scroll_list(-steps * rowHeight); }
        else if (reading_.contains(position.x, position.y)) { scroll_body(-steps * 66); }
        return 0;
    }
    case WM_KEYDOWN: {
        const bool control = GetKeyState(VK_CONTROL) < 0;
        switch (wparam) {
        case VK_UP: select_adjacent(-1); return 0;
        case VK_DOWN: select_adjacent(1); return 0;
        case VK_HOME: select_adjacent(-static_cast<int>(ids_.size())); return 0;
        case VK_END: select_adjacent(static_cast<int>(ids_.size())); return 0;
        case VK_PRIOR: scroll_list(-listView_.height); return 0;
        case VK_NEXT: scroll_list(listView_.height); return 0;
        case VK_DELETE: remove(); return 0;
        case VK_RETURN: open_draft(); return 0;
        case 'C': compose(); return 0;
        case 'N': if (control) { compose(); return 0; } break;
        case 'R': reply(); return 0;
        case 'F': if (control) { search_.focus(); } else { forward(); } return 0;
        case 'E': archive(); return 0;
        case 'S': toggle_star(); return 0;
        case 'U': mark_unread(); return 0;
        case 'Z': if (control) { undo(); return 0; } break;
        }
        break;
    }
    }
    return std::nullopt;
}
