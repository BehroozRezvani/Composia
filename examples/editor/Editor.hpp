#pragma once

#include "Document.hpp"
#include <composia/ElementWindow.hpp>
#include <composia/TextBox.hpp>

namespace editor {
class Editor final : public composia::ElementWindow {
public:
    explicit Editor(composia::Application&);
    void open(const std::filesystem::path&);

private:
    Editor(composia::Application&, std::shared_ptr<composia::TextBox>);
    std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) override;
    std::filesystem::path choose_file(bool saving);
    bool save(bool choosePath);
    bool may_discard();
    void replace(const std::filesystem::path&, Document);
    void update_title();
    void command(unsigned);

    std::shared_ptr<composia::TextBox> field_;
    std::filesystem::path path_;
    std::wstring newline_{L"\r\n"};
    bool bom_{}, dirty_{};
    composia::Connection change_;
};
}
