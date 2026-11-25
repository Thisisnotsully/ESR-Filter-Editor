// Tests of the overlay's parts that have no Windows in them: UTF conversion, UI strings, colour codes and highlights,
// word wrapping, focus moves, list sums, text editing, the on-screen keyboard, the help rows, and the Form running the
// DemoView with a fake canvas and a fake platform (mouse, keys, controller, keyboard, dialog, choices).
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "color_text.hpp"
#include "demo_view.hpp"
#include "focus_nav.hpp"
#include "form.hpp"
#include "help.hpp"
#include "list_math.hpp"
#include "osk.hpp"
#include "text_edit.hpp"
#include "ui_text.hpp"
#include "utf.hpp"

using namespace esrfe::overlay;

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        ++checks;                                                      \
        if (!(cond)) {                                                 \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);       \
            ++failures;                                                \
        }                                                              \
    } while (0)

// ---- fakes ---------------------------------------------------------------------------------------------------------

// Every UTF-16 unit is 7 px wide, a line is 16 px high (20 for the title font); wrapping follows WrapPieces.
class FakeCanvas final : public Canvas {
public:
    Theme theme;
    int texts = 0;
    int clipDepth = 0;
    int maxClipDepth = 0;
    bool clipUnbalanced = false;
    std::vector<std::string> drawn;
    const Highlight* highlight = nullptr;
    int highlightedTexts = 0;

    [[nodiscard]] auto GetTheme() const -> const Theme& override { return theme; }
    [[nodiscard]] auto S(int value) const -> int override { return value; }
    auto LineHeight(Font font) -> int override { return font == Font::Title ? 20 : 16; }
    auto TextWidth(std::string_view utf8, Font, unsigned flags) -> int override {
        const std::string plain = (flags & text_flags::kRaw) ? std::string(utf8) : StripColorCodes(utf8);
        return static_cast<int>(Utf8ToUtf16(plain).size()) * 7;
    }
    auto TextWidth16(std::u16string_view text, Font) -> int override { return static_cast<int>(text.size()) * 7; }
    auto TextHeight(std::string_view utf8, int width, Font font, unsigned) -> int override {
        const auto pieces = ColorPieces(utf8, theme.body, theme);
        const Wrapped w = WrapPieces(pieces, width, true, [](std::u16string_view t) { return static_cast<int>(t.size()) * 7; });
        return w.lines * LineHeight(font);
    }
    void Fill(const Box&, Rgb) override {}
    void FillAlpha(const Box&, Rgb, std::uint8_t) override {}
    void Frame(const Box&, Rgb) override {}
    void CornerShadow(const Box&, int) override {}
    void DropShadow(const Box&, int) override {}
    void Text(std::string_view utf8, const Box&, Rgb, Font, unsigned flags) override {
        ++texts;
        drawn.emplace_back(utf8);
        if (highlight != nullptr && (flags & text_flags::kNoHighlight) == 0 &&
            !HighlightRanges(*highlight, Utf8ToUtf16(StripColorCodes(utf8))).empty())
            ++highlightedTexts;
    }
    void Text16(std::u16string_view text, const Box&, Rgb, Font, unsigned) override {
        ++texts;
        drawn.push_back(Utf16ToUtf8(text));
    }
    auto Image(std::string_view, const Box&) -> bool override { return false; }
    std::vector<std::string> pictures;  // the keys of Pixels pictures drawn
    void Pixels(std::string_view key, int, int, const std::vector<std::uint8_t>&, const Box&) override { pictures.emplace_back(key); }
    void PushClip(const Box&) override { maxClipDepth = std::max(maxClipDepth, ++clipDepth); }
    void PopClip() override {
        if (--clipDepth < 0) clipUnbalanced = true;
    }
    void SetHighlight(const Highlight* h) override { highlight = h; }
};

class FakePlatform final : public Platform {
public:
    FakeCanvas canvas;
    std::uint64_t now = 100000;
    bool pad = false;
    std::string clipboard;
    int repaints = 0;
    int closes = 0;
    std::vector<std::shared_ptr<View>> pushed;
    int pops = 0;
    std::vector<std::function<void(Ui&)>> posted;

    auto GetMeasurer() -> Measurer& override { return canvas; }
    void Repaint() override { ++repaints; }
    [[nodiscard]] auto Now() const -> std::uint64_t override { return now; }
    [[nodiscard]] auto PadActive() const -> bool override { return pad; }
    [[nodiscard]] auto PadKind() const -> pad::Kind override { return pad ? pad::Kind::Xbox : pad::Kind::None; }
    auto ClipboardText() -> std::string override { return clipboard; }
    void SetClipboardText(const std::string& utf8) override { clipboard = utf8; }
    void PostToUi(std::function<void(Ui&)> work) override { posted.push_back(std::move(work)); }
    auto RunOnGame(std::function<void()> work, std::function<void(Ui&)> then, GameThread) -> bool override {
        work();
        posted.push_back(std::move(then));
        return true;
    }
    void Close() override { ++closes; }
    void PushView(std::shared_ptr<View> view) override { pushed.push_back(std::move(view)); }
    void PopView() override { ++pops; }
};

// A small Ui for widget tests without a view.
struct Harness {
    FakePlatform platform;
    std::shared_ptr<DemoView> view = std::make_shared<DemoView>(1000);
    Form form{platform, view};
    Box client{0, 0, 1152, 778};
    Box content{32, 72, 1120, 740};
    Harness() { Layout(); }
    void Layout() { form.Layout(client, content); }
    void Paint() {
        Layout();
        form.Paint(platform.canvas);
    }
    void Type(const std::string& text) {
        for (char16_t c : Utf8ToUtf16(text)) form.Char(c);
        Layout();
    }
    auto Pad(pad::Button b, bool repeat = false) -> bool {
        const bool used = form.PadPress(PadEvent{b, repeat});
        Layout();
        return used;
    }
    auto Key(esrfe::overlay::Key k, Mods mods = {}) -> bool {
        KeyEvent e;
        e.key = k;
        e.mods = mods;
        const bool used = form.KeyDown(e);
        Layout();
        return used;
    }
    void Click(int x, int y) {
        MouseEvent down;
        down.kind = MouseKind::Down;
        down.x = x;
        down.y = y;
        form.Mouse(down);
        MouseEvent up = down;
        up.kind = MouseKind::Up;
        form.Mouse(up);
        Layout();
    }
    void ClickCentre(const Widget& w) { Click(w.box.CentreX(), w.box.CentreY()); }
};

// ---- utf -----------------------------------------------------------------------------------------------------------

static void UtfTests() {
    const std::string mixed = "R\xC3\xA9sum\xC3\xA9 \xE2\x80\xA6 \xF0\x9F\x98\x80 \xE6\x97\xA5";  // Résumé … 😀 日
    const std::u16string wide = Utf8ToUtf16(mixed);
    CHECK(wide.size() == 13);  // the emoji is a surrogate pair
    CHECK(IsHighSurrogate(wide[9]) && IsLowSurrogate(wide[10]));
    CHECK(Utf16ToUtf8(wide) == mixed);
    CHECK(Utf8ToUtf16("\xC3") == u"�");          // cut short
    CHECK(Utf8ToUtf16("a\xFF" "b") == u"a�" u"b");  // not UTF-8
    CHECK(Utf8ToUtf16("\xC0\x80") == u"�");      // overlong
    const char16_t lone[] = {u'x', 0xD800, u'y', 0};
    CHECK(Utf16ToUtf8(lone) == "x\xEF\xBF\xBDy");
    CHECK(WideToUtf8(L"äö") == "\xC3\xA4\xC3\xB6");
    CHECK(Utf8ToWide("\xC3\xA4") == L"ä");
}

// ---- ui strings ----------------------------------------------------------------------------------------------------

static void UiTextTests() {
    const auto strings = ParseUiStrings("\xEF\xBB\xBFNo matches\tKeine Treffer\r\n{0} of {1} entries\t{0} von {1} Eintr\xC3\xA4gen\n"
                                        "\tno key\nno tab\nAll\tAlle");
    CHECK(strings.size() == 3);
    CHECK(strings.at("No matches") == "Keine Treffer");  // the BOM and the CR are gone
    SetUiStrings(strings);
    CHECK(UiText("No matches") == "Keine Treffer");
    CHECK(UiText("Not translated") == "Not translated");  // English stays
    CHECK(UiFormat("{0} of {1} entries", {"3", "10"}) == "3 von 10 Eintr\xC3\xA4gen");
    CHECK(FormatArgs("{0}+{0}={1}", {"2", "4"}) == "2+2=4");
    CHECK(FormatArgs("{1}", {"a"}) == "{1}");  // a missing argument stays as written
    SetUiStrings({});
    CHECK(UiText("No matches") == "No matches");

    CHECK(LocaleFromLoaderToml("[game]\ntext_locale = \"deDE\"\n") == "deDE");
    CHECK(LocaleFromLoaderToml("text_locale=\"frFR\"") == "frFR");
    CHECK(LocaleFromLoaderToml("text_locale = \"enUS\"\n").empty());  // English
    CHECK(LocaleFromLoaderToml("text_locale = \"xxXX\"\n").empty());  // not a game language
    CHECK(LocaleFromLoaderToml("# text_locale = \"deDE\"\n").empty());  // a comment
    CHECK(LocaleFromLoaderToml("text_locale =\nother = \"deDE\"").empty());  // no value on its line
    CHECK(UiStringsPath("data", "").empty());
    CHECK(UiStringsPath("data", "deDE") == std::filesystem::path("data") / "locales" / "deDE" / "ui-strings.txt");
}

// ---- colour codes, highlight, wrapping -----------------------------------------------------------------------------

static void ColorTests() {
    const std::string y = std::string(kColorMarker);
    const auto runs = SplitColorCodes(y + "4Shako" + y + "0 Harlequin " + y + "x" + y);
    CHECK(runs.size() == 2);
    CHECK(runs[0].code == '4' && runs[0].text == "Shako");
    CHECK(runs[1].code == '0' && runs[1].text == " Harlequin " + y + "x" + y);  // ÿc without a code stays text
    CHECK(StripColorCodes(y + "1Red" + y + ";Purple") == "RedPurple");
    CHECK(FirstColorCode("plain " + y + "2green") == '\0');
    CHECK(FirstColorCode(y + "2green") == '2');
    CHECK(HasColorCodes("a" + y + "3b") && !HasColorCodes("abc"));
    Theme theme;
    const auto pieces = ColorPieces("A" + y + "3B" + y + "QC", Rgb{1, 2, 3}, theme);
    CHECK(pieces.size() == 3);
    CHECK(pieces[0].color == (Rgb{1, 2, 3}));
    CHECK(pieces[1].color == (Rgb{110, 140, 255}));  // magic blue
    CHECK(pieces[2].color == theme.body);           // a code with no colour here: the body colour

    const Highlight h = MakeHighlight("harle -shako");
    CHECK(!h.Empty());
    auto ranges = HighlightRanges(h, u"The Harlequin Crest, harlequin");
    CHECK(ranges.size() == 2 && ranges[0].first == 4 && ranges[0].second == 9);
    ranges = HighlightRanges(MakeHighlight("resume"), u"Résumé here");  // accents fold
    CHECK(ranges.size() == 1 && ranges[0].first == 0 && ranges[0].second == 6);
    CHECK(MakeHighlight("-only").Empty());  // only excluded terms: nothing to mark
    const auto marked = HighlightPieces(h, {Piece{u"xHarleY", Rgb{}, false, false}});
    CHECK(marked.size() == 3 && marked[1].highlight && marked[1].text == u"Harle" && marked[2].continues);

    const auto measure = [](std::u16string_view t) { return static_cast<int>(t.size()) * 10; };
    Wrapped w = WrapPieces({Piece{u"aaa bbb ccc", Rgb{}, false, false}}, 75, true, measure);
    CHECK(w.lines == 2);  // "aaa bbb" is 70 px, " ccc" does not fit
    CHECK(w.placed.back().text == u"ccc" && w.placed.back().line == 1 && w.placed.back().x == 0);  // no leading space
    w = WrapPieces({Piece{u"aaa bbb ccc", Rgb{}, false, false}}, 75, false, measure);
    CHECK(w.lines == 1);
    w = WrapPieces({Piece{u"one\ntwo", Rgb{}, false, false}}, 1000, true, measure);
    CHECK(w.lines == 2);
    // A colour change inside a word does not allow a break there.
    w = WrapPieces({Piece{u"xx ab", Rgb{}, false, false}, Piece{u"cd", Rgb{9, 9, 9}, false, false}}, 50, true, measure);
    CHECK(w.lines == 2 && w.placed.back().line == 1 && w.placed[w.placed.size() - 2].line == 1);
    CHECK(WrapPieces({}, 100, true, measure).lines == 0);
}

// ---- focus ---------------------------------------------------------------------------------------------------------

static void FocusTests() {
    // A tall list on the left, a column of three buttons on the right, a search box above the list.
    const std::vector<Box> boxes = {
        Box{0, 0, 300, 30},      // 0 search
        Box{0, 40, 300, 600},    // 1 list
        Box{320, 0, 500, 30},    // 2 button top
        Box{320, 300, 500, 330}, // 3 button middle
        Box{320, 560, 500, 590}, // 4 button bottom
    };
    CHECK(NavNeighbor(boxes, 0, 0, 1) == 1);   // search -> list
    CHECK(NavNeighbor(boxes, 1, 0, -1) == 0);  // list -> search
    CHECK(NavNeighbor(boxes, 1, 1, 0) == 3);   // list -> the button level with its middle
    CHECK(NavNeighbor(boxes, 4, -1, 0) == 1);  // a bottom button -> the list beside it
    CHECK(NavNeighbor(boxes, 2, 0, 1) == 3);
    CHECK(NavNeighbor(boxes, 3, 0, 1) == 4);
    CHECK(NavNeighbor(boxes, 4, 0, 1) == -1);  // nothing below
    CHECK(NavNeighbor(boxes, 0, 1, 0) == 2);
    CHECK(NavNeighbor(boxes, 2, -1, 0) == 0);
    CHECK(NavNeighbor(boxes, 1, 1, 0, {true, true, true, false, true}) != 3);  // a disabled box is skipped
    CHECK(NavNeighbor(boxes, 9, 1, 0) == -1);
    // A row of chips: left/right walk it, down from any goes to the button below.
    const std::vector<Box> chips = {Box{0, 0, 50, 20}, Box{60, 0, 110, 20}, Box{120, 0, 170, 20}, Box{0, 40, 170, 70}};
    CHECK(NavNeighbor(chips, 0, 1, 0) == 1 && NavNeighbor(chips, 1, 1, 0) == 2 && NavNeighbor(chips, 2, 1, 0) == -1);
    CHECK(NavNeighbor(chips, 2, 0, 1) == 3 && NavNeighbor(chips, 3, 0, -1) == 1);  // up lands on the chip over its middle
    CHECK(TabStep({true, false, true}, 0, 1) == 2);
    CHECK(TabStep({true, false, true}, 2, 1) == 0);  // wraps
    CHECK(TabStep({true, false, true}, 0, -1) == 2);
    CHECK(TabStep({true, false, true}, -1, 1) == 0);
    CHECK(TabStep({false, false}, 0, 1) == -1);
}

// ---- list sums -----------------------------------------------------------------------------------------------------

static void ListTests() {
    // 729 rules, 30 px a row, 400 px tall.
    CHECK(MaxScroll(729, 30, 400) == 729 * 30 - 400);
    CHECK(ClampScroll(-5, 729, 30, 400) == 0);
    CHECK(ClampScroll(1 << 30, 729, 30, 400) == 729 * 30 - 400);
    RowRange r = VisibleRows(729, 30, 0, 400);
    CHECK(r.first == 0 && r.last == 14);  // 13 rows and a part
    r = VisibleRows(729, 30, 45, 400);
    CHECK(r.first == 1 && r.last == 15);
    r = VisibleRows(729, 30, MaxScroll(729, 30, 400), 400);
    CHECK(r.last == 729 && r.first == 715);
    CHECK(VisibleRows(0, 30, 0, 400).last == 0);
    CHECK(ScrollToShow(0, 20, 729, 30, 400) == 21 * 30 - 400);  // down: the row's bottom at the view's bottom
    CHECK(ScrollToShow(600, 5, 729, 30, 400) == 150);           // up: the row's top at the view's top
    CHECK(ScrollToShow(150, 8, 729, 30, 400) == 150);           // already showing: no move
    CHECK(RowAt(0, 729, 30, 0) == 0 && RowAt(29, 729, 30, 0) == 0 && RowAt(30, 729, 30, 0) == 1);
    CHECK(RowAt(10, 729, 30, 728 * 30) == 728 && RowAt(40, 729, 30, 728 * 30) == -1 && RowAt(-1, 729, 30, 0) == -1);
    Thumb t = ThumbFor(100, 400, 400, 729LL * 30, 0, 28);
    CHECK(t.shown && t.top == 100 && t.height == 28);  // the minimum
    t = ThumbFor(100, 400, 400, 729LL * 30, MaxScroll(729, 30, 400), 28);
    CHECK(t.top + t.height == 500);  // at the end
    CHECK(!ThumbFor(0, 400, 400, 300, 0, 28).shown);
    CHECK(ScrollFromDrag(0, 372, 400, 28, 729LL * 30, 400) == MaxScroll(729, 30, 400));
    CHECK(ScrollFromDrag(1000, -10000, 400, 28, 729LL * 30, 400) == 0);
    WheelAccumulator wheel;
    CHECK(wheel.Add(30) == 0 && wheel.Add(30) == 0 && wheel.Add(30) == 0 && wheel.Add(30) == 1);  // a touchpad's small steps
    CHECK(wheel.Add(-240) == -2);
    CHECK(wheel.Add(100) == 0 && wheel.Add(-100) == 0 && wheel.Add(-20) == -1);  // turning back drops the rest
    CHECK(wheel.Add(360) == 3);
}

// ---- text editing --------------------------------------------------------------------------------------------------

static void EditTests() {
    TextEdit e;
    for (char16_t c : std::u16string(u"hello world")) e.TypeChar(c);
    CHECK(e.Text() == u"hello world" && e.Caret() == 11);
    e.Left(false, true);
    CHECK(e.Caret() == 6);
    e.Left(true, true);  // select "hello "
    CHECK(e.HasSelection() && e.SelectedText() == u"hello ");
    e.TypeChar(u'X');
    CHECK(e.Text() == u"Xworld" && e.Caret() == 1);
    e.End();
    CHECK(!e.TypeChar(u'\b') && !e.TypeChar(u'\r') && e.Text() == u"Xworld");  // control characters come as keys
    // A character outside the BMP arrives as two WM_CHARs.
    CHECK(!e.TypeChar(0xD83D));
    CHECK(e.TypeChar(0xDE00));
    CHECK(e.Text() == u"Xworld\U0001F600" && e.Caret() == 8);
    e.Left();
    CHECK(e.Caret() == 6);  // over the whole pair
    e.Right();
    CHECK(e.Backspace() && e.Text() == u"Xworld");  // the pair goes at once
    CHECK(!e.TypeChar(0xDE00));                     // a lone low half is dropped
    e.Home();
    CHECK(e.Delete() && e.Text() == u"world");
    e.SelectAll();
    CHECK(e.Insert(u"a\r\nb\tc") && e.Text() == u"a  b c");  // a pasted line break becomes spaces
    e.Clear();
    e.SetMaxLength(5);
    CHECK(e.Insert(u"1234567") && e.Text() == u"12345");
    CHECK(!e.Insert(u"9"));
    e.SetMaxLength(4);
    e.SetText(u"ab\U0001F600");
    CHECK(e.Text() == u"ab\U0001F600");
    e.SetMaxLength(3);
    e.SetText(u"ab\U0001F600");  // cut inside the pair: the half goes
    CHECK(e.Text() == u"ab");
    e.SetMaxLength(100);
    e.SetText(u"one two  three");
    e.Home();
    e.Right(false, true);
    CHECK(e.Caret() == 4);
    e.Right(false, true);
    CHECK(e.Caret() == 9);
    e.End();
    CHECK(e.Backspace(true) && e.Text() == u"one two  ");
    TextEdit n;
    n.SetFilter(TextEdit::Filter::Integer);
    for (char16_t c : std::u16string(u"-1a2-3")) n.TypeChar(c);
    CHECK(n.Text() == u"-123");
    n.Home();
    CHECK(!n.TypeChar(u'-'));  // one minus only
    const auto width = [](std::u16string_view t) { return static_cast<int>(t.size()) * 10; };
    TextEdit p;
    p.SetText(u"abcdef");
    CHECK(p.PositionAt(-5, width) == 0);
    CHECK(p.PositionAt(14, width) == 1);  // nearer the end of 'b'? 14 is between 10 and 20: nearer 10
    CHECK(p.PositionAt(16, width) == 2);
    CHECK(p.PositionAt(500, width) == 6);
    p.SetText(u"abcdefghijklmnopqrstuvwxyz");  // 260 px in a 100 px box, caret at the end
    int scroll = p.ScrollForCaret(0, 100, width);
    CHECK(scroll > 0 && 260 - scroll <= 100);
    p.Home();
    CHECK(p.ScrollForCaret(scroll, 100, width) == 0);
}

// ---- on-screen keyboard --------------------------------------------------------------------------------------------

static void KeyboardTests() {
    OnScreenKeyboard k;
    k.Open("");
    CHECK(k.IsOpen() && k.Row() == 1 && k.Col() == 0);
    OnScreenKeyboard::Result r = k.PressHighlighted();
    CHECK(r.action == OnScreenKeyboard::Action::Type && r.ch == u'q');
    k.Open("deDE");
    CHECK(k.Rows()[1][5].ch == L'z');  // QWERTZ
    const int last = static_cast<int>(k.Rows().size()) - 1;
    r = k.Press(last, 0);  // the accents key
    CHECK(r.action == OnScreenKeyboard::Action::Toggled && k.Rows()[0][0].ch == L'ä');
    r = k.Press(last, 3);
    CHECK(r.action == OnScreenKeyboard::Action::Done && !k.IsOpen());
    k.Open("ruRU");
    CHECK(k.Rows()[1][0].ch == L'й');  // й
    k.Highlight(99, 99);
    CHECK(k.AtBottomRow());
    CHECK(k.Press(2, static_cast<int>(k.Rows()[2].size()) - 1).action == OnScreenKeyboard::Action::Backspace);

    const pad::OskRows rows = pad::OskLayout(false, "");
    KeyboardMetrics m;  // 22 px half-keys, 34 px keys
    const Box client{0, 0, 1152, 778};
    KeyboardGeometry g = LayoutKeyboard(rows, Box{40, 100, 400, 130}, client, m);
    CHECK(g.box.top == 134 && g.box.left == 40);  // under the text box
    CHECK(g.box.Width() == 20 * 22 + 16);
    CHECK(g.keys.size() == 10 + 10 + 10 + 10 + 4);
    bool inside = true;
    for (const auto& key : g.keys) inside = inside && g.box.Contains(key.box.left, key.box.top) && key.box.right <= g.box.right;
    CHECK(inside);
    g = LayoutKeyboard(rows, Box{40, 700, 400, 730}, client, m);
    CHECK(g.box.bottom == 696);  // no room under it: over it
    g = LayoutKeyboard(rows, Box{900, 100, 1100, 130}, client, m);
    CHECK(g.box.right <= 1152 - 8);  // kept inside the window
    g = LayoutKeyboard(rows, Box{0, 10, 100, 40}, Box{0, 0, 300, 200}, m);
    CHECK(g.box.Width() <= 300 - 16 && g.box.bottom <= 200 - 8);  // a small window: smaller keys
}

// ---- help ----------------------------------------------------------------------------------------------------------

static void HelpTests() {
    CHECK(PadButtonName(Glyph::A, pad::Kind::PlayStation) == "Cross" && PadButtonName(Glyph::A, pad::Kind::Xbox) == "A");
    CHECK(PadButtonName(Glyph::Start, pad::Kind::PlayStation) == "Options" && PadButtonName(Glyph::View, pad::Kind::Xbox) == "View");
    CHECK(std::string(PadIcons(Glyph::B).first) == "buttonright" && PadIcons(Glyph::LBRB).second != nullptr);
    CHECK(PadIconFolder(pad::Kind::PlayStation) == "ps4" && PadIconFolder(pad::Kind::Xbox) == "xbox");
    const std::vector<HelpRow> rows = {{Glyph::A, "Enter", "Select"}, {Glyph::A, "", "Again"}, {Glyph::None, "Ctrl+V", "Paste"},
                                       {Glyph::B, "Esc", "Back"}, {Glyph::None, "Esc", "Twice"}};
    const auto padRows = PadRowsOnly(rows);
    CHECK(padRows.size() == 2 && padRows[0].action == "Select" && padRows[1].pad == Glyph::B);
    const auto keyRows = KeyRowsOnly(rows);
    CHECK(keyRows.size() == 3 && keyRows[1].keys == "Ctrl+V" && keyRows[2].action == "Back");
}

// ---- the Form running the DemoView ---------------------------------------------------------------------------------

static void FormSearchAndList() {
    Harness h;
    DemoView& v = *h.view;
    CHECK(v.ShownRows().size() == 1000);
    h.form.Shown();
    CHECK(h.form.Focused() == &v.Search());  // InitialFocus
    h.Type("ber rune");
    CHECK(v.Search().Value() == "ber rune");
    CHECK(!v.ShownRows().empty() && v.ShownRows().size() < 100);
    bool allBer = true;
    for (int row : v.ShownRows()) allBer = allBer && StripColorCodes(v.RowText(row)).find("Ber Rune") != std::string::npos;
    CHECK(allBer);
    CHECK(v.List().count == static_cast<int>(v.ShownRows().size()));
    // Painting marks the matches and draws only the rows that show.
    h.platform.canvas.texts = 0;
    h.platform.canvas.highlightedTexts = 0;
    h.Paint();
    CHECK(h.platform.canvas.highlightedTexts > 0);
    CHECK(h.platform.canvas.clipDepth == 0 && !h.platform.canvas.clipUnbalanced);
    // Down from the search box: into the list (the controller), and up from its first row back.
    h.platform.pad = true;
    CHECK(h.Pad(pad::Button::Down));
    CHECK(h.form.Focused() == &v.List());
    CHECK(v.List().Selected() == 0);
    CHECK(h.Pad(pad::Button::Down) && v.List().Selected() == 1);
    CHECK(h.Pad(pad::Button::Up) && v.List().Selected() == 0);
    CHECK(h.Pad(pad::Button::Up, true) && h.form.Focused() == &v.List());  // a held Up stops at the first row
    CHECK(h.Pad(pad::Button::Up) && h.form.Focused() == &v.Search());     // a fresh press leaves the list
    // Clearing (X on the search box).
    CHECK(h.Pad(pad::Button::X) && v.Search().Value().empty() && v.ShownRows().size() == 1000);
    // A query of excluded words only filters out.
    h.platform.pad = false;
    h.Type("-rune");
    bool noRunes = true;
    for (int row : v.ShownRows()) noRunes = noRunes && StripColorCodes(v.RowText(row)).find("Rune") == std::string::npos;
    CHECK(noRunes && v.ShownRows().size() < 1000 && !v.ShownRows().empty());
}

static void FormVirtualList() {
    Harness h;
    DemoView& v = *h.view;
    int painted = 0;
    auto original = v.List().paintRow;
    v.List().paintRow = [&](Canvas& c, int i, const Box& b, const ListView::RowState& s) {
        ++painted;
        original(c, i, b, s);
    };
    h.Paint();
    CHECK(painted > 5 && painted < 40);  // 1000 rows, a screenful drawn
    // Scrolling to the end shows the last row; the scroll bar takes a click on its track.
    ListView& list = v.List();
    list.Select(999, nullptr);
    painted = 0;
    h.Paint();
    CHECK(list.Visible().last == 1000 && painted < 40);
    const int before = list.Scroll();
    h.Click(list.box.right - 6, list.box.top + 5);  // the track above the thumb: a page up
    CHECK(list.Scroll() < before);
    // The wheel scrolls three rows a notch.
    MouseEvent wheel;
    wheel.kind = MouseKind::Wheel;
    wheel.x = list.box.CentreX();
    wheel.y = list.box.CentreY();
    wheel.wheelSteps = -1;
    const int s0 = list.Scroll();
    h.form.Mouse(wheel);
    CHECK(list.Scroll() == std::min(s0 + 3 * list.Pitch(), MaxScroll(list.count, list.Pitch(), list.box.Height() + list.rowGap)));
    // A click selects; a second click soon after on the same place opens (a pushed view).
    list.SetScroll(0);
    h.Layout();
    const Box row2 = list.RowBox(2);
    h.Click(row2.CentreX(), row2.CentreY());
    CHECK(list.Selected() == 2);
    h.platform.now += 100;
    h.Click(row2.CentreX(), row2.CentreY());
    CHECK(h.platform.pushed.size() == 1);
    // The right stick scrolls the focused list.
    h.form.Focus(&list);
    const int s1 = list.Scroll();
    CHECK(h.form.ScrollBy(50) && list.Scroll() == s1 + 50);
    // Keys: Down, PageDown, End, Enter.
    CHECK(h.Key(Key::End) && list.Selected() == static_cast<int>(v.ShownRows().size()) - 1);
    CHECK(h.Key(Key::Home) && list.Selected() == 0);
    CHECK(h.Key(Key::PageDown) && list.Selected() == list.PageRows());
    CHECK(h.Key(Key::Enter) && h.platform.pushed.size() == 2);
}

static void FormKeyboardTyping() {
    Harness h;
    DemoView& v = *h.view;
    h.platform.pad = true;
    h.form.Shown();
    // Y opens the on-screen keyboard on the search box; A types the highlighted key ('q').
    CHECK(h.Pad(pad::Button::Y) && h.form.KeyboardOpen() && h.form.KeyboardTarget() == &v.Search());
    h.Paint();
    CHECK(!h.form.KeyboardBox().Empty() && h.form.KeyboardBox().top >= v.Search().box.bottom);
    CHECK(h.Pad(pad::Button::A) && v.Search().Value() == "q");
    CHECK(h.Pad(pad::Button::Right) && h.Pad(pad::Button::A) && v.Search().Value() == "qw");
    CHECK(h.Pad(pad::Button::X) && v.Search().Value() == "q");  // X deletes one
    // A click on a key types it too.
    h.Paint();
    const Box kb = h.form.KeyboardBox();
    h.Click(kb.left + 8 + 44 * 2 + 10, kb.top + 8 + 10);  // the third key of the digits row
    CHECK(v.Search().Value() == "q3");
    // B on a search box with text clears it; B again closes the keyboard.
    CHECK(h.Pad(pad::Button::B) && v.Search().Value().empty() && h.form.KeyboardOpen());
    CHECK(h.Pad(pad::Button::B) && !h.form.KeyboardOpen());
    // Down from the keyboard's bottom row leaves it, into the list; held Down stops at the bottom row.
    CHECK(h.Pad(pad::Button::Y) && h.form.KeyboardOpen());
    for (int i = 0; i < 3; ++i) h.Pad(pad::Button::Down);  // from the letters' first row to the bottom row
    CHECK(h.form.Keyboard().AtBottomRow());
    CHECK(h.Pad(pad::Button::Down, true) && h.form.KeyboardOpen());
    CHECK(h.Pad(pad::Button::Down) && !h.form.KeyboardOpen() && h.form.Focused() == &v.List());
    // The mouse or the keyboard puts the on-screen keyboard away (the wiki's PadMouseUsed).
    h.form.Focus(&v.Search());
    CHECK(h.Pad(pad::Button::A) && h.form.KeyboardOpen());
    h.platform.pad = false;
    h.form.PadModeChanged(false);
    CHECK(!h.form.KeyboardOpen());
    // The IME is wanted while a text box has the focus, at its caret.
    Box caret{};
    CHECK(h.form.TextInputCaret(caret) && v.Search().box.Contains(caret.left, caret.top));
    h.form.Focus(&v.List());
    CHECK(!h.form.TextInputCaret(caret));
}

static void FormTextBoxKeys() {
    Harness h;
    DemoView& v = *h.view;
    TextBox& search = v.Search();
    h.form.Focus(&search);
    h.Type("abc def");
    Mods ctrl;
    ctrl.ctrl = true;
    Mods shift;
    shift.shift = true;
    CHECK(h.Key(Key::Left, ctrl) && search.edit.Caret() == 4);
    CHECK(h.Key(Key::End, shift) && search.edit.SelectedText() == u"def");
    CHECK(h.Key(Key::C, ctrl) && h.platform.clipboard == "def");
    CHECK(h.Key(Key::X, ctrl) && search.Value() == "abc " && v.ShownRows().size() < 1000);
    h.platform.clipboard = "ber";
    CHECK(h.Key(Key::V, ctrl) && search.Value() == "abc ber");
    CHECK(h.Key(Key::A, ctrl) && search.edit.SelectedText() == u"abc ber");
    CHECK(h.Key(Key::Backspace) && search.Value().empty() && v.ShownRows().size() == 1000);
    // Enter in the search box moves to the results.
    h.Type("shako");
    CHECK(h.Key(Key::Enter) && h.form.Focused() == &v.List());
    // Tab walks the widgets; Shift+Tab walks back.
    CHECK(h.Key(Key::Tab) && h.form.Focused() == &v.Ethereal());
    CHECK(h.Key(Key::Tab, shift) && h.form.Focused() == &v.List());
    // Escape with nothing open: not used here (the host goes back or closes).
    CHECK(!h.Key(Key::Escape));
    // A click in the text puts the caret there; a double click selects all.
    h.form.Focus(&search);
    h.Layout();
    h.platform.now += 5000;
    h.Click(search.box.left + 6 + 7 * 2 + 2, search.box.CentreY());
    CHECK(search.edit.Caret() == 2 && !search.edit.HasSelection());
    h.platform.now += 50;
    h.Click(search.box.left + 6 + 7 * 2 + 2, search.box.CentreY());
    CHECK(search.edit.SelectedText() == u"shako");
    // The clear button.
    h.platform.now += 5000;
    h.Click(search.ClearBox().CentreX(), search.ClearBox().CentreY());
    CHECK(search.Value().empty());
}

static void FormControls() {
    Harness h;
    DemoView& v = *h.view;
    h.platform.pad = true;
    // Toggle: A switches it.
    h.form.Focus(&v.Ethereal());
    CHECK(v.Ethereal().on && h.Pad(pad::Button::A) && !v.Ethereal().on);
    // Number box: left / right step, faster while held (1, then 5, then 10); X sets 0; the wheel steps.
    NumberBox& level = v.Level();
    h.form.Focus(&level);
    CHECK(level.value == 85 && h.Pad(pad::Button::Right) && level.value == 86);
    CHECK(h.Pad(pad::Button::Left) && level.value == 85);
    for (int i = 0; i < 6; ++i) h.Pad(pad::Button::Left, true);
    CHECK(level.value == 85 - 5 - 5);  // repeats 1..5 step 1, the sixth steps 5
    CHECK(h.Pad(pad::Button::X) && level.value == 0);
    CHECK(h.Pad(pad::Button::Left) && level.value == 0);  // the minimum holds
    MouseEvent wheel;
    wheel.kind = MouseKind::Wheel;
    wheel.x = level.box.CentreX();
    wheel.y = level.box.CentreY();
    wheel.wheelSteps = 3;
    h.form.Mouse(wheel);
    CHECK(level.value == 3);
    h.Click(level.PlusBox().CentreX(), level.PlusBox().CentreY());
    CHECK(level.value == 4);
    // Typing a number with the keyboard; Escape goes back, Enter keeps.
    h.platform.pad = false;
    h.form.Focus(&level);
    h.Type("120");
    CHECK(level.Typing());
    CHECK(h.Key(Key::Enter) && !level.Typing() && level.value == 99);  // clamped
    h.Type("7");
    CHECK(h.Key(Key::Escape) && level.value == 99);
    // Up / Down leave the number box (they move the focus).
    CHECK(h.Key(Key::Down) && h.form.Focused() != &level);
    // Drop-down: A opens the list, Down, A chooses; B closes without choosing.
    h.platform.pad = true;
    Dropdown& quality = v.Quality();
    h.form.Focus(&quality);
    CHECK(h.Pad(pad::Button::A) && h.form.ChoicesOpen());
    h.Paint();
    CHECK(!quality.PopupBox().Empty());
    CHECK(h.Pad(pad::Button::Down) && h.Pad(pad::Button::Down) && quality.PopupHighlight() == 2);
    CHECK(h.Pad(pad::Button::A) && !h.form.ChoicesOpen() && quality.selected == 2);
    CHECK(v.Status().find("Superior") != std::string::npos);
    CHECK(h.Pad(pad::Button::A) && h.Pad(pad::Button::Down) && h.Pad(pad::Button::B) && !h.form.ChoicesOpen() && quality.selected == 2);
    // With the mouse: a click opens, a click on a row chooses, a click outside closes.
    h.platform.pad = false;
    h.ClickCentre(quality);
    CHECK(h.form.ChoicesOpen());
    h.Paint();
    const Box popup = quality.PopupBox();
    h.Click(popup.CentreX(), popup.top + 4 + 26 * 3 + 5);  // past the scroll offset: a row near the top
    CHECK(!h.form.ChoicesOpen() && quality.selected >= 0);
    h.ClickCentre(quality);
    CHECK(h.form.ChoicesOpen());
    h.Click(5, 5);
    CHECK(!h.form.ChoicesOpen());
}

static void FormDialog() {
    Harness h;
    DemoView& v = *h.view;
    h.platform.pad = true;
    v.List().Select(0, nullptr);
    h.form.Focus(&v.DeleteButton());
    CHECK(h.Pad(pad::Button::A) && h.form.DialogOpen());
    h.Paint();  // dims the window, the dialog on top
    CHECK(h.Pad(pad::Button::A) && !h.form.DialogOpen());  // Cancel has the focus first: nothing deleted
    CHECK(v.ShownRows().size() == 1000 && v.Status() == "Nothing deleted");
    CHECK(h.Pad(pad::Button::A) && h.form.DialogOpen());
    CHECK(h.Pad(pad::Button::Right) && h.Pad(pad::Button::A) && !h.form.DialogOpen());
    CHECK(v.ShownRows().size() == 999 && v.Status() == "Row deleted");
    // B and Escape cancel; the dialog takes all input while open.
    CHECK(h.Pad(pad::Button::A) && h.form.DialogOpen());
    CHECK(h.Pad(pad::Button::Down) && h.form.DialogOpen());
    CHECK(h.Pad(pad::Button::B) && !h.form.DialogOpen() && v.ShownRows().size() == 999);
    h.platform.pad = false;
    CHECK(h.Key(Key::Enter) && h.form.DialogOpen());  // Enter on the focused Delete button
    CHECK(!h.form.Char(u'x') || h.form.DialogOpen());
    CHECK(h.Key(Key::Escape) && !h.form.DialogOpen());
    // Back with nothing open is the host's (it closes the overlay at the first view).
    CHECK(!h.form.Back());
}

static void FormHelpAndTooltips() {
    Harness h;
    DemoView& v = *h.view;
    std::vector<HelpRow> rows;
    h.form.Focus(&v.List());
    h.form.CollectHelp(rows);
    bool pick = false, open = false, back = false, ctrlF = false;
    for (const HelpRow& r : rows) {
        pick = pick || r.action == "Pick a row";
        open = open || (r.pad == Glyph::A && r.action == "Open");
        back = back || (r.pad == Glyph::B && r.keys == "Esc");
        ctrlF = ctrlF || r.keys == "Ctrl+F";
    }
    CHECK(pick && open && back && ctrlF);
    rows.clear();
    h.form.OpenKeyboard(v.Search());
    h.form.CollectHelp(rows);
    CHECK(!rows.empty() && rows[0].action == "Highlight a key");
    h.form.CloseKeyboard();
    // A row's tooltip shows on hover; with a controller, the focused row's.
    h.Layout();
    const Box row = v.List().RowBox(1);
    MouseEvent move;
    move.kind = MouseKind::Move;
    move.x = row.CentreX();
    move.y = row.CentreY();
    h.form.Mouse(move);
    h.platform.canvas.drawn.clear();
    h.Paint();
    bool tip = false;
    for (const std::string& text : h.platform.canvas.drawn) tip = tip || text.find("Row 2 of 1000") != std::string::npos;
    CHECK(tip);
    // Ctrl+F (a view shortcut) focuses the search box.
    KeyEvent f;
    f.key = Key::Other;
    f.vk = 'F';
    f.mods.ctrl = true;
    CHECK(h.form.KeyDown(f) && h.form.Focused() == &v.Search());
}

static void FormPushedView() {
    Harness h;
    DemoView& v = *h.view;
    v.List().Select(4, nullptr);
    v.List().onActivate(4, h.form);
    CHECK(h.platform.pushed.size() == 1);
    // The pushed view runs in its own Form; its Back button pops.
    Form detail(h.platform, h.platform.pushed[0]);
    detail.Layout(h.client, h.content);
    detail.Shown();
    CHECK(detail.Focused() != nullptr);
    h.platform.pad = true;
    CHECK(detail.PadPress(PadEvent{pad::Button::A}) && h.platform.pops == 1);
    CHECK(!detail.PadPress(PadEvent{pad::Button::B}));  // B: the host goes back
    // Work queued for a game thread comes back on the UI thread with a Ui.
    int got = 0;
    CHECK(h.form.RunOnGame([&got] { got = 41; }, [&got](Ui&) { ++got; }, GameThread::Ui));
    CHECK(got == 41 && h.platform.posted.size() == 1);
    h.platform.posted[0](h.form);
    CHECK(got == 42);
}

int main() {
    UtfTests();
    UiTextTests();
    ColorTests();
    FocusTests();
    ListTests();
    EditTests();
    KeyboardTests();
    HelpTests();
    FormSearchAndList();
    FormVirtualList();
    FormKeyboardTyping();
    FormTextBoxKeys();
    FormControls();
    FormDialog();
    FormHelpAndTooltips();
    FormPushedView();
    if (failures == 0) std::printf("overlay_logic_test: all %d checks passed\n", checks);
    else std::printf("overlay_logic_test: %d of %d checks failed\n", failures, checks);
    return failures == 0 ? 0 : 1;
}
