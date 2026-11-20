#include "Editor.hpp"
#include "Commands.h"
#include <commdlg.h>
#include <vector>

namespace editor {
Editor::Editor(composia::Application& app)
    : Editor(app, std::make_shared<composia::TextBox>(composia::TextBox::Options{
        .name=L"Document", .multiline=true, .fontFamily=L"Consolas", .fontSize=16, .padding=16})) {}

Editor::Editor(composia::Application& app, std::shared_ptr<composia::TextBox> field)
    : ElementWindow(app, L"Untitled - Composia Editor", 760, 520, Content::native, field), field_(std::move(field)) {
    wil::unique_hmenu menu{LoadMenuW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(EDITOR_MENU))};
    THROW_LAST_ERROR_IF(!menu);
    THROW_IF_WIN32_BOOL_FALSE(SetMenu(hwnd(), menu.get()));
    menu.release();
    change_ = field_->on_text_changed([this] {
        if (dirty_ != field_->modified()) { dirty_ = field_->modified(); update_title(); }
    });
    focus_element(*field_);
}

void Editor::update_title() {
    const auto filename = path_.empty() ? std::wstring(L"Untitled") : path_.filename().wstring();
    SetWindowTextW(hwnd(), ((dirty_ ? L"* " : L"") + filename + L" - Composia Editor").c_str());
}

void Editor::replace(const std::filesystem::path& path, Document doc) {
    field_->set_text(doc.text);
    field_->select(0, 0);
    path_ = path;
    newline_ = std::move(doc.newline);
    bom_ = doc.bom;
    dirty_ = false;
    update_title();
    focus_element(*field_);
}

void Editor::open(const std::filesystem::path& path) {
    replace(path, read_document(path));
}

std::filesystem::path Editor::choose_file(bool saving) {
    std::vector<wchar_t> buffer(32768);
    auto initial = saving ? path_.wstring() : std::wstring{};
    if (initial.size() < buffer.size()) { initial.copy(buffer.data(), initial.size()); }
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd();
    dialog.lpstrFilter = L"Text documents (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrDefExt = L"txt";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
        (saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (saving ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)) { return buffer.data(); }
    if (CommDlgExtendedError()) { throw std::runtime_error("Windows could not open the file dialog."); }
    return {};
}

bool Editor::save(bool choosePath) {
    const auto path = choosePath || path_.empty() ? choose_file(true) : path_;
    if (path.empty()) { return false; }
    write_document(path, field_->text(), newline_, bom_);
    field_->mark_saved();
    path_ = path;
    dirty_ = false;
    update_title();
    return true;
}

bool Editor::may_discard() {
    if (!field_->modified()) { return true; }
    switch (MessageBoxW(hwnd(), L"Save changes to this document?", L"Composia Editor", MB_YESNOCANCEL | MB_ICONQUESTION)) {
    case IDYES: return save(false);
    case IDNO: return true;
    default: return false;
    }
}

void Editor::command(unsigned id) {
    switch (id) {
    case FILE_NEW: if (may_discard()) { replace({}, {}); } break;
    case FILE_OPEN: {
        const auto path = choose_file(false);
        if (!path.empty() && may_discard()) { open(path); }
        break;
    }
    case FILE_SAVE: save(false); break;
    case FILE_SAVE_AS: save(true); break;
    case FILE_EXIT: PostMessageW(hwnd(), WM_CLOSE, 0, 0); break;
    case EDIT_UNDO: field_->undo(); break;
    case EDIT_REDO: field_->redo(); break;
    case EDIT_CUT: field_->cut(); break;
    case EDIT_COPY: field_->copy(); break;
    case EDIT_PASTE: field_->paste(); break;
    case EDIT_SELECT_ALL: field_->select(0, -1); break;
    }
}

std::optional<LRESULT> Editor::on_message(UINT msg, WPARAM w, LPARAM l) {
    try {
        if (msg == WM_CLOSE && !may_discard()) { return 0; }
        if (msg == WM_COMMAND && !l) { command(LOWORD(w)); return 0; }
        if (msg == WM_CHAR && GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) >= 0) {
            switch (w) {
            case 14: command(FILE_NEW); return 0;
            case 15: command(FILE_OPEN); return 0;
            case 19: command(GetKeyState(VK_SHIFT) < 0 ? FILE_SAVE_AS : FILE_SAVE); return 0;
            }
        }
    } catch (const std::exception& ex) {
        MessageBoxA(hwnd(), ex.what(), "Composia Editor", MB_OK | MB_ICONERROR);
        return 0;
    }
    return ElementWindow::on_message(msg, w, l);
}
}
