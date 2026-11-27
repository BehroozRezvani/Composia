#include <composia/ElementWindow.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/TextBox.hpp>
#include <vector>
#include "support/TestSupport.hpp"
#include <UIAutomation.h>
#include <msctf.h>
#include <thread>

using namespace composia;
using testing::require;

namespace {
void flush(ElementWindow& window) {
    MSG msg{};
    while (PeekMessageW(&msg, window.hwnd(), 0xC000, 0xFFFF, PM_REMOVE)) {
        DispatchMessageW(&msg);
    }
    UpdateWindow(window.hwnd());
    window.rethrow_callback_error();
}

int cursor(const testing::Options& options) {
    Application app{options.warp};
    {
        auto root = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::canvas});
        auto edit = std::make_shared<TextBox>(TextBox::Options{.name = L"Message"}, L"Document text");
        edit->set_preferred_size({110, 60});
        root->add(edit);
        ElementWindow window{app, L"Text cursor", 260, 100, root};
        window.show(SW_SHOWNOACTIVATE);
        flush(window);
        const auto hwnd = window.hwnd();
        const auto arrow = LoadCursorW(nullptr, IDC_ARROW);
        const auto ibeam = LoadCursorW(nullptr, IDC_IBEAM);
        const auto query = [&](int hit) {
            SendMessageW(hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(hit, WM_MOUSEMOVE));
        };
        const auto move = [&](int x, HCURSOR expected) {
            // Place the fixture under the pointer. Never move the user's cursor.
            POINT mouse{};
            if (!GetCursorPos(&mouse)) {
                const auto pos = GetMessagePos();
                mouse = {static_cast<short>(LOWORD(pos)), static_cast<short>(HIWORD(pos))};
            }
            POINT client{};
            RECT frame{};
            THROW_IF_WIN32_BOOL_FALSE(ClientToScreen(hwnd, &client));
            THROW_IF_WIN32_BOOL_FALSE(GetWindowRect(hwnd, &frame));
            const int px = static_cast<int>(x * window.scale()), py = static_cast<int>(20 * window.scale());
            THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(hwnd, nullptr, mouse.x - px - (client.x - frame.left),
                mouse.y - py - (client.y - frame.top), 0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE));
            query(HTCLIENT);
            require(GetCursor() == expected, "Wrong cursor between WM_SETCURSOR and WM_MOUSEMOVE");
            SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(px, py));
            require(GetCursor() == expected, "Pointer movement changed the resolved cursor");
        };
        SetCursor(arrow);
        for (int x = 20; x < 64; x += 4) { move(x, ibeam); }
        edit->select(0, -1);
        move(50, ibeam);
        move(180, arrow);
        edit->set_read_only(true);
        move(40, ibeam);
        edit->set_enabled(false);
        move(40, arrow);
        edit->set_enabled(true);
        move(40, ibeam);
        query(HTMENU);
        require(GetCursor() == arrow, "The I-beam leaked into the native menu bar");
    }
    app.close();
    return 0;
}

int editing(const testing::Options& options) {
    Application app{options.warp};
    {
        const std::wstring sample = L"مرحبا e\u0301 日本語 \U0001F642";
        auto edit = std::make_shared<TextBox>(TextBox::Options{.name = L"Message", .multiline = true}, sample);
        require(edit->text() == sample, "Unicode document did not round trip");
        ElementWindow window{app, L"Compose", 220, 110, edit};
        window.show(SW_SHOWNOACTIVATE);
        flush(window);
        window.focus();
        require(edit->focused(), "Editor did not take focus");
        std::vector<bool> focusHeard;
        auto focusWatch = edit->on_focus([&](bool value) { focusHeard.push_back(value); });
        (void)window.elements().focus(nullptr);
        (void)window.elements().focus(edit.get());
        require(focusHeard == std::vector<bool>{false, true}, "The editor's focus changes were not raised once each");
        unsigned changes{};
        auto changed = edit->on_text_changed([&] { ++changes; });
        edit->select(0, -1);
        require(edit->selection().end == static_cast<long>(sample.size()), "Wrong UTF-16 selection range");
        edit->replace_selection(L"Word");
        require(edit->text() == L"Word" && edit->undo() && edit->text() == sample, "Undo lost Unicode text");
        require(edit->redo() && edit->text() == L"Word", "Redo failed");
        require(edit->can_undo() && !edit->can_redo(), "can_undo or can_redo after an edit");
        require(edit->send(EM_GETLINECOUNT) == 1, "A message straight to the text services");
        require(edit->text_services() != nullptr, "No text services");
        SendMessageW(window.hwnd(), WM_UNICHAR, 0x1F600, 1);
        require(edit->text() == L"Word\U0001F600", "Supplementary keyboard input failed");
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_BACK, 1);
        SendMessageW(window.hwnd(), WM_CHAR, VK_BACK, 1);
        SendMessageW(window.hwnd(), WM_KEYUP, VK_BACK, 0xc0000001);
        require(edit->text() == L"Word", "Backspace split a surrogate pair");
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_RETURN, 1);
        SendMessageW(window.hwnd(), WM_CHAR, L'\r', 1);
        SendMessageW(window.hwnd(), WM_KEYUP, VK_RETURN, 0xc0000001);
        SendMessageW(window.hwnd(), WM_CHAR, L'中', 1);
        require(edit->text().ends_with(L"\r中"), "Multiline typing failed");
        SendMessageW(window.hwnd(), WM_IME_CHAR, L'漢', 1);
        window.rethrow_callback_error();
        require(edit->text().ends_with(L"中漢"), "Committed IME character did not reach the document");
        edit->set_read_only(true);
        SendMessageW(window.hwnd(), WM_CHAR, L'x', 1);
        edit->replace_selection(L"no");
        require(edit->text().ends_with(L"中漢") && !edit->undo(), "Read-only input changed document");
        edit->set_read_only(false);
        edit->set_text(L"replacement");
        require(!edit->undo() && !edit->can_undo(), "Replacing the document retained undo history");
        // A new style: the font, colors and padding change, and the element sizes for the font.
        auto style = edit->options();
        style.fontSize = 24;
        style.padding = 10;
        style.foreground = RGB(200, 0, 0);
        edit->set_style(style);
        CHARFORMAT2W format{};
        format.cbSize = sizeof(format);
        edit->send(EM_GETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));
        require(edit->options().fontSize == 24 && edit->options().multiline && format.yHeight == 24 * 15 && format.crTextColor == RGB(200, 0, 0),
            "set_style did not reach Rich Edit or did not keep the rest");
        bool threw = false;
        try { style.fontSize = 0; edit->set_style(style); } catch (const std::invalid_argument&) { threw = true; }
        require(threw && edit->options().fontSize == 24, "An invalid style was taken");
        flush(window);
        edit->set_text(L"alpha beta");
        const auto mouse = MAKELPARAM(int(15 * window.scale()), int(12 * window.scale()));
        SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, mouse);
        SendMessageW(window.hwnd(), WM_LBUTTONUP, 0, mouse);
        SendMessageW(window.hwnd(), WM_LBUTTONDBLCLK, MK_LBUTTON, mouse);
        SendMessageW(window.hwnd(), WM_LBUTTONUP, 0, mouse);
        const auto word = edit->selection();
        require(word.start == 0 && word.end >= 5 && word.end <= 6, "Double-click did not select the clicked word");
        require(GetWindow(window.hwnd(), GW_CHILD) == nullptr, "Editor created a child HWND");
        window.rethrow_callback_error();
        require(app.post([&] { PostMessageW(window.hwnd(), WM_CLOSE, 0, 0); }), "Queue close");
        app.run();
        require(changes == 1, "Text change notifications did not coalesce");
    }
    app.close();
    return 0;
}

int clipboard(const testing::Options&) {
    // Clipboards belong to window stations. Use a new station even on a private desktop.
    auto originalStation = GetProcessWindowStation();
    auto originalDesktop = GetThreadDesktop(GetCurrentThreadId());
    auto station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    THROW_LAST_ERROR_IF_NULL(station);
    const auto closeStation = wil::scope_exit([&] { CloseWindowStation(station); });
    THROW_IF_WIN32_BOOL_FALSE(SetProcessWindowStation(station));
    const auto restoreStation = wil::scope_exit([&] { SetProcessWindowStation(originalStation); });
    auto desk = CreateDesktopW(L"Clipboard", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    THROW_LAST_ERROR_IF_NULL(desk);
    const auto closeDesktop = wil::scope_exit([&] { CloseDesktop(desk); });
    THROW_IF_WIN32_BOOL_FALSE(SetThreadDesktop(desk));
    const auto restoreDesktop = wil::scope_exit([&] { SetThreadDesktop(originalDesktop); });
    TextBox source{{.name = L"From"}, L"日本語 \U0001f642"};
    TextBox destination{{.name = L"To"}};
    source.select(0, -1);
    source.copy();
    destination.paste();
    require(source.text() == destination.text(), "Unicode clipboard round trip");
    source.cut();
    require(source.text().empty() && source.undo(), "Cut did not participate in undo");
    destination.set_read_only(true);
    destination.select(0, -1);
    destination.cut();
    require(destination.text() == source.text(), "Read-only clipboard operation changed text");
    {
        TextBox transient{{.name = L"Temporary"}, L"Copied before closing"};
        transient.select(0, -1);
        transient.copy();
    }
    destination.set_read_only(false);
    destination.paste();
    require(destination.text() == L"Copied before closing", "Destroying the source discarded the clipboard");
    return 0;
}

// Exercise the focused Rich Edit text store through TSF, without changing the user's IME profile.
struct CompositionSession
    : winrt::implements<CompositionSession, ITfEditSession, ITfCompositionSink, winrt::non_agile> {
    explicit CompositionSession(ITfContext* owner) : context(owner) {}
    wil::com_ptr<ITfContext> context;
    wil::com_ptr<ITfComposition> composition;
    HRESULT STDMETHODCALLTYPE OnCompositionTerminated(TfEditCookie, ITfComposition*) noexcept override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) noexcept override {
        try {
            wil::com_ptr<ITfRange> range;
            if (!composition) {
                TF_SELECTION selected{};
                ULONG count{};
                THROW_IF_FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selected, &count));
                range.attach(selected.range);
                THROW_IF_FAILED(context.query<ITfContextComposition>()->StartComposition(cookie, range.get(), this,
                                                                                         composition.put()));
                RETURN_HR_IF_NULL(E_FAIL, composition);
                return range->SetText(cookie, 0, L"にほん", 3);
            }
            RETURN_IF_FAILED(composition->GetRange(range.put()));
            RETURN_IF_FAILED(range->SetText(cookie, 0, L"日本", 2));
            return composition->EndComposition(cookie);
        }
        CATCH_RETURN();
    }
};

int composition_input(const testing::Options& options) {
    Application app{options.warp};
    {
        auto root = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::stack});
        auto first = std::make_shared<TextBox>(TextBox::Options{.name = L"Subject"}, L"Draft");
        auto box = std::make_shared<TextBox>(TextBox::Options{.name = L"Composition"});
        root->add(first);
        root->add(box);
        ElementWindow window{app, L"Composition input", 200, 110, root};
        window.show(SW_SHOWNOACTIVATE);
        flush(window);
        window.focus();
        window.elements().focus(box.get());
        wil::com_ptr<ITfThreadMgr> manager;
        THROW_IF_FAILED(
            CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(manager.put())));
        TfClientId id{};
        THROW_IF_FAILED(manager->Activate(&id));
        const auto deactivate = wil::scope_exit([&] { manager->Deactivate(); });
        wil::com_ptr<ITfDocumentMgr> document;
        THROW_IF_FAILED(manager->GetFocus(document.put()));
        require(!!document, "Editor did not register a focused TSF document");
        wil::com_ptr<ITfContext> context;
        THROW_IF_FAILED(document->GetTop(context.put()));
        require(!!context, "Focused editor has no TSF context");
        auto session = winrt::make_self<CompositionSession>(context.get());
        const auto edit = [&] {
            HRESULT result{};
            THROW_IF_FAILED(context->RequestEditSession(id, session.get(), TF_ES_SYNC | TF_ES_READWRITE, &result));
            THROW_IF_FAILED(result);
        };
        edit();
        require(box->text() == L"にほん", "TSF preedit did not appear in the editor");
        edit();
        require(box->text() == L"日本" && first->text() == L"Draft",
                "TSF commit reached the wrong field or lost preedit");
    }
    app.close();
    return 0;
}

int rendering(const testing::Options& options) {
    Application app{options.warp};
    {
        auto root = std::make_shared<Element>();
        TextBox::Options style{.name = L"Notes",
                               .multiline = true,
                               .fontSize = 20,
                               .padding = 8,
                               .foreground = RGB(255, 255, 255),
                               .background = RGB(20, 40, 60),
                               .selectionColor = RGB(170, 70, 80)};
        auto edit = std::make_shared<TextBox>(style, L"Windowless editing wraps across lines 日本語");
        root->add(edit);
        edit->set_offset({24, 12});
        ElementWindow window{app, L"Notes", 206, 130, root};
        window.show(SW_SHOWNOACTIVATE);
        flush(window);
        const auto pixel = [&](int x, int y) {
            return testing::surface_pixel(app, window.composition_target().surface(), LONG(x * window.scale()),
                                          LONG(y * window.scale()));
        };
        require(pixel(18, 20).a == 0 && testing::matches(pixel(26, 14), 0x14283c), "Editor clip or translation failed");
        std::optional<POINT> ink;
        for (int y = 20; y < 48 && !ink; y += 2) {
            for (int x = 32; x < 110 && !ink; x += 2) {
                if (pixel(x, y).r > 170) { ink = POINT{x, y}; }
            }
        }
        require(ink.has_value(), "Rich Edit glyphs missing from the composition surface");
        flush(window);
        window.invalidate({float(ink->x), float(ink->y), 2, 2});
        flush(window);
        require(pixel(ink->x, ink->y).r > 170, "Partial repaint erased the glyph under the dirty rectangle");
        window.focus();
        edit->select(0, -1);
        flush(window);
        const auto lines = edit->selection_rectangles();
        require(lines.size() >= 2 && lines.front().x >= 0 && lines.back().y > lines.front().y,
                "Selection rectangles did not follow the translated, wrapped editor");
        bool highlight{};
        for (int y = 20; y < 53 && !highlight; y += 3) {
            for (int x = 35; x < 125 && !highlight; x += 3) {
                const auto p = pixel(x, y);
                highlight = p.r > p.b + 24 && p.r > p.g + 24;
            }
        }
        require(highlight, "Selection missing or ignored the palette");
        edit->select(0, 0);
        const auto start = edit->caret_bounds();
        edit->select(32, 32);
        require(edit->caret_bounds().y > start.y, "Caret failed to follow wrapped text");
        app.graphics().recreate();
        window.invalidate();
        flush(window);
        edit->set_painter([](ScopedSurfaceDraw& draw, TextBox& box) {
            const auto r = box.bounds();
            draw.context()->FillRoundedRectangle({{0, 0, r.width, r.height}, 8, 8}, draw.solid_brush(0x075D60));
            box.draw_text(draw);
            box.draw_caret(draw);
        });
        edit->set_text(L"first\rsecond\rthird\rfourth\rfifth\rsixth\rseventh\rlast");
        edit->select(0, 0);
        flush(window);
        require(testing::matches(pixel(28, 25), 0x075d60), "Custom painter did not replace editor chrome");
        edit->select(42, 42);
        flush(window);
        const auto end = edit->caret_bounds();
        require(end.height > 0 && end.y >= 0 && end.y + end.height <= edit->bounds().height + 1,
                "Scrolling failed to keep insertion point inside the viewport");
        root->remove(*edit);
        require(!window.elements().focused(), "Detached editor retained focus");
        root->add(edit);
        flush(window);
        window.elements().focus(edit.get());
        SendMessageW(window.hwnd(), WM_CHAR, L'!', 1);
        require(edit->text().find(L'!') != std::wstring::npos, "Reattached editor stopped accepting input");
    }
    app.close();
    return 0;
}
}
int accessibility(const testing::Options& options) {
    Application app{options.warp};
    {
        auto root = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::stack});
        auto edit = std::make_shared<TextBox>(TextBox::Options{.name = L"Subject"}, L"日本語");
        auto second = std::make_shared<TextBox>(TextBox::Options{.name = L"Body", .multiline = true}, L"Second field");
        root->add(edit);
        root->add(second);
        ElementWindow window{app, L"Accessible editor", 240, 160, root};
        window.show(SW_SHOWNOACTIVATE);
        flush(window);
        const auto hwnd = window.hwnd();
        std::exception_ptr error;
        std::jthread client([&] {
            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                const auto uninit = wil::scope_exit([] { winrt::uninit_apartment(); });
                wil::com_ptr<IUIAutomation> uia;
                THROW_IF_FAILED(
                    CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(uia.put())));
                wil::com_ptr<IUIAutomationElement> pane, node, sibling;
                THROW_IF_FAILED(uia->ElementFromHandle(hwnd, pane.put()));
                wil::com_ptr<IUIAutomationTreeWalker> walker;
                THROW_IF_FAILED(uia->get_ControlViewWalker(walker.put()));
                VARIANT editType{};
                editType.vt = VT_I4;
                editType.lVal = UIA_EditControlTypeId;
                wil::com_ptr<IUIAutomationCondition> isEdit;
                THROW_IF_FAILED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, editType, isEdit.put()));
                THROW_IF_FAILED(pane->FindFirst(TreeScope_Children, isEdit.get(), node.put()));
                require(!!node, "UIA could not find the windowless editor");
                wil::unique_bstr name;
                THROW_IF_FAILED(node->get_CurrentName(name.put()));
                require(name && std::wstring_view(name.get()) == L"Subject", "UIA editor label");
                THROW_IF_FAILED(walker->GetNextSiblingElement(node.get(), sibling.put()));
                require(!!sibling, "UIA could not navigate between editors");
                THROW_IF_FAILED(sibling->SetFocus());
                BOOL focused{};
                THROW_IF_FAILED(sibling->get_CurrentHasKeyboardFocus(&focused));
                require(focused, "UI Automation did not focus the requested editor");
                wil::com_ptr<IUIAutomationTextPattern> text;
                THROW_IF_FAILED(node->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(text.put())));
                require(!!text, "Missing TextPattern");
                wil::com_ptr<IUIAutomationTextRange> range;
                THROW_IF_FAILED(text->get_DocumentRange(range.put()));
                wil::unique_bstr value;
                THROW_IF_FAILED(range->GetText(-1, value.put()));
                require(value && std::wstring_view(value.get()).starts_with(L"日本語"), "Screen reader document range");
                THROW_IF_FAILED(range->Select());
                SAFEARRAY* rects{};
                THROW_IF_FAILED(range->GetBoundingRectangles(&rects));
                const auto release = wil::scope_exit([&] { SafeArrayDestroy(rects); });
                require(rects && rects->rgsabound[0].cElements >= 4, "Missing screen reader selection geometry");
                wil::com_ptr<IUIAutomationValuePattern> pattern;
                THROW_IF_FAILED(sibling->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(pattern.put())));
                require(!!pattern, "Missing ValuePattern");
                wil::unique_bstr replacement{SysAllocString(L"Changed by UI Automation")};
                THROW_IF_FAILED(pattern->SetValue(replacement.get()));
            } catch (...) { error = std::current_exception(); }
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        });
        app.run();
        client.join();
        if (error) { std::rethrow_exception(error); }
        require(second->text() == L"Changed by UI Automation", "UIA edits did not reach document");
    }
    app.close();
    return 0;
}
int main(int argc, char** argv) {
    return testing::run(argc, argv,
                        {{"editing", editing},
                         {"cursor", cursor},
                         {"rendering", rendering},
                         {"clipboard", clipboard},
                         {"accessibility", accessibility},
                         {"composition", composition_input}});
}
