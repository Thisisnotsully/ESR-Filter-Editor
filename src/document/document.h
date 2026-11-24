#pragma once
// A loot filter file as an editable document.
//
// The document keeps the file's text and its syntax tree. Reading it and
// writing it back without edits gives the same bytes. Each edit changes only
// the bytes it is about (plus the commas it needs), in the layout around it,
// then reads the file again; an edit that would leave a file the plugin
// refuses is undone and reported.
//
// Inside "rules", whole-line comments are structure:
//   // ==== Name    starts a part
//   // ---- Name    starts a section
//   a comment line directly under a heading is its description;
//   comment lines directly above a rule are the rule's line comment.
// A rule's "comment" field is its title, "R### | what it does | action".
//
// Switched-off rules sit in a top-level "disabledRules" list as
// {"at": <index in rules>, "rule": {...}} (the web editor's scheme; the plugin
// ignores the key). A comment line "// from: Part > Section" above an entry
// says which section the rule came from, so it goes back there.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json.h"
#include "syntax.h"

namespace document {

enum class Level : std::uint8_t { Part, Section };

struct Heading {
    Level level = Level::Section;
    std::string name;
    std::string description;  // lines joined with \n; empty when none
    std::uint32_t line = 0;
    int part = -1;       // a section's part (index in Headings()), -1 if none
    int firstRule = 0;   // the active rules under it: [firstRule, endRule)
    int endRule = 0;     // (for a part, all of its sections)
};

struct Rule {
    int index = 0;           // position in "rules", or in "disabledRules" when disabled
    bool disabled = false;
    int at = -1;             // a disabled rule's "at"
    std::string id;          // "R001" / "U001" from the title, empty if none
    std::string title;       // the "comment" field
    std::string lineComment; // lines joined with \n; empty when none
    int part = -1;           // Headings() indices, -1 when none
    int section = -1;        // (a disabled rule: where enabling puts it back)
    Value value;
    std::uint32_t line = 0;
};

struct FilterLevel {
    std::int64_t id = 0;
    Value name;  // a string, or an object of language -> string
};

struct Settings {
    std::optional<std::int64_t> formatVersion;
    std::string title;
    std::string author;
    std::string version;
    std::string description;
    std::string updateUrl;
    std::vector<FilterLevel> filterLevels;
    std::optional<std::int64_t> defaultLevel;
    std::optional<std::int64_t> soundVolume;
    std::optional<bool> hideUnmatchedItems;
};

struct Style {
    std::string name;
    Value value;
    std::uint32_t line = 0;
};

// Where a rule or heading goes in "rules".
struct Position {
    enum class Kind : std::uint8_t { Before, After, StartOf, EndOf, Begin, End };
    Kind kind = Kind::End;
    int index = 0;  // a rule (Before/After) or a heading (StartOf/EndOf)

    static auto Before(int rule) -> Position { return {Kind::Before, rule}; }
    static auto After(int rule) -> Position { return {Kind::After, rule}; }
    static auto StartOf(int heading) -> Position { return {Kind::StartOf, heading}; }
    static auto EndOf(int heading) -> Position { return {Kind::EndOf, heading}; }
    static auto Begin() -> Position { return {Kind::Begin, 0}; }
    static auto End() -> Position { return {Kind::End, 0}; }
};

// The result of an edit: empty error means it was done.
struct Status {
    std::string error;
    explicit operator bool() const { return error.empty(); }
};

class Document {
public:
    // Reads text. False when the plugin would refuse the file (diags holds the
    // error); warnings about the filter's structure are added either way.
    static auto Parse(std::string text, Document& out, std::vector<Diag>& diags) -> bool;

    auto Text() const -> const std::string& { return text_; }
    auto Tree() const -> const Syntax& { return syntax_; }
    auto Warnings() const -> const std::vector<Diag>& { return warnings_; }

    // Read model.
    auto HeaderComment() const -> const std::string& { return header_; }
    auto GetSettings() const -> const Settings& { return settings_; }
    auto Styles() const -> const std::vector<Style>& { return styles_; }
    // Active rules first (Rules()[i].index == i), then the disabled ones.
    auto Rules() const -> const std::vector<Rule>& { return rules_; }
    auto ActiveCount() const -> int { return activeCount_; }
    auto Headings() const -> const std::vector<Heading>& { return headings_; }
    // Index in Rules() of the rule with this id, or -1.
    auto FindRule(std::string_view id) const -> int;
    // Index in Headings() of a part, or of a section in a part; -1 if none.
    auto FindHeading(std::string_view part, std::string_view section = {}) const -> int;
    // A top-level member's value.
    auto TopLevel(std::string_view key) const -> std::optional<Value>;

    // Edits. A rule is an index in Rules(), a heading an index in Headings().

    // Sets a rule field. An existing field keeps its place and its value's
    // layout (one line, or wrapped as before). A new field goes before
    // beforeKey when the rule has it, else at the end.
    auto SetField(int rule, std::string_view key, const Value& value, std::string_view beforeKey = {}) -> Status;
    auto RemoveField(int rule, std::string_view key) -> Status;
    auto SetTitle(int rule, std::string_view title) -> Status;
    // Empty text removes it.
    auto SetLineComment(int rule, std::string_view text) -> Status;

    // A new rule, written on one line, with an optional line comment.
    auto InsertRule(Position where, const Value& rule, std::string_view lineComment = {}) -> Status;
    auto DeleteRule(int rule) -> Status;
    // Moves an active rule with its line comment and its own layout.
    auto MoveRule(int rule, Position where) -> Status;
    // Moves an active rule into "disabledRules" (made when missing).
    auto DisableRule(int rule) -> Status;
    // Puts a disabled rule back at its "at" position; drops "disabledRules"
    // when it becomes empty.
    auto EnableRule(int rule) -> Status;

    auto AddHeading(Level level, Position where, std::string_view name, std::string_view description = {}) -> Status;
    auto RenameHeading(int heading, std::string_view name) -> Status;
    // Empty text removes it.
    auto SetHeadingDescription(int heading, std::string_view text) -> Status;
    // Removes the heading line and its description; its rules join the one above.
    auto RemoveHeading(int heading) -> Status;

    // Sets a top-level member. A new one goes before beforeKey when present,
    // else before "styles", else before "rules", else at the end.
    auto SetSetting(std::string_view key, const Value& value, std::string_view beforeKey = {}) -> Status;
    // Removes a top-level member (every copy of the key); fine when there is none.
    auto RemoveSetting(std::string_view key) -> Status;

private:
    // Where a rule's or heading's text is.
    struct RuleSpan {
        int node = -1;           // the rule object's node
        int container = -1;      // "rules" or "disabledRules" array node
        int item = -1;           // item index in that array
        int entry = -1;          // disabled: the {"at", "rule"} node
        bool ownsLines = false;  // the item starts its line and nothing follows it
        std::size_t blockBegin = 0;  // line start of its line comment (or of itself)
        std::size_t ownBegin = 0;    // line start of the item itself
        std::size_t ownEnd = 0;      // after the newline that ends the item
        bool hasMarker = false;      // disabled: has a "// from:" line
        std::string marker;          // disabled: "Part > Section"
        int gap = 0;                 // active: rules before it (its own index)
    };
    struct HeadingSpan {
        std::size_t begin = 0;       // line start
        std::size_t lineEnd = 0;     // after the heading line's newline
        std::size_t nameBegin = 0;   // the name's bytes
        std::size_t nameEnd = 0;
        std::size_t blockEnd = 0;    // after the description lines
        int gap = 0;                 // active rules before it
    };
    // A place to insert into "rules".
    struct Slot {
        int index = 0;               // logical position among the active rules
        std::size_t offset = 0;      // a line start (lineMode) or a byte offset
        bool lineMode = true;
        bool blankAround = false;    // an empty heading: keep blank lines around
        int inlineAfter = -1;        // inline mode: insert after this item
        int part = -1;
        int section = -1;
    };

    std::string text_;
    Syntax syntax_;
    std::vector<Diag> warnings_;
    std::string nl_ = "\n";
    std::size_t bom_ = 0;

    std::string header_;
    Settings settings_;
    std::vector<Style> styles_;
    std::vector<Rule> rules_;
    std::vector<RuleSpan> ruleSpans_;
    int activeCount_ = 0;
    std::vector<Heading> headings_;
    std::vector<HeadingSpan> headingSpans_;
    int rulesNode_ = -1;
    int disabledNode_ = -1;
    int rulesMember_ = -1;     // item index of "rules" in the root
    int disabledMember_ = -1;  // item index of "disabledRules" in the root

    struct Splice {
        std::size_t begin;
        std::size_t end;
        std::string text;
    };

    auto Rebuild(std::string text, std::vector<Diag>* diags) -> bool;
    void BuildModel();
    void BuildRules();
    void BuildDisabled();
    auto Apply(std::vector<Splice> splices) -> Status;

    // Text and token helpers.
    auto Tk(int i) const -> const Token& { return syntax_.tokens[static_cast<std::size_t>(i)]; }
    auto Nd(int i) const -> const Node& { return syntax_.nodes[static_cast<std::size_t>(i)]; }
    auto TokText(int i) const -> std::string_view;
    auto NodeBegin(int node) const -> std::size_t { return Tk(Nd(node).first).begin; }
    auto NodeEnd(int node) const -> std::size_t { return Tk(Nd(node).last).end; }
    auto NodeText(int node) const -> std::string_view;
    auto ItemFirst(int container, int item) const -> int;  // first token
    auto ItemLast(int container, int item) const -> int;   // comma, or the value's last token
    auto LineStart(std::size_t offset) const -> std::size_t;
    auto NextLine(std::size_t offset) const -> std::size_t;  // after the newline at or after offset
    auto PrevLine(std::size_t lineStart) const -> std::size_t;  // npos at the first line
    auto IsBlankLine(std::size_t lineStart) const -> bool;
    auto IndentAt(std::size_t lineStart) const -> std::string;
    auto BeginsLine(std::size_t offset) const -> bool;
    auto RestOfLineIsTrivia(int token) const -> bool;  // only spaces/comments after token
    auto WholeLineComment(int token) const -> bool;
    auto CommentText(int token) const -> std::string;
    auto CommentLines(std::string_view indent, std::string_view text) const -> std::string;
    auto ItemOwnsLines(int container, int item) const -> bool;
    auto ItemIndent(int container) const -> std::string;
    auto LeadingCommentBegin(int container, int item) const -> std::size_t;

    // Generic item edits on an array or object node.
    auto RenderLike(const Value& value, int oldNode) const -> std::string;
    auto InsertItemSplices(int container, int index, const std::string& item, std::string_view comment,
                           std::vector<Splice>& out) const -> bool;
    auto RemoveItemSplices(int container, int item, std::size_t blockBegin, std::vector<Splice>& out) const -> void;
    auto SetMember(int object, std::string_view key, const Value& value, std::string_view beforeKey) -> Status;
    auto RemoveMember(int object, std::string_view key) -> Status;

    // Rules array helpers.
    auto ResolveSlot(Position where, Slot& slot, std::string& error) const -> bool;
    auto SlotSplices(const Slot& slot, const std::string& item, std::string_view comment,
                     std::vector<Splice>& out) const -> void;
    auto HeadingSlot(int heading) const -> Slot;
    auto EnableSlot(int at, const std::string* marker) const -> Slot;
    auto RuleBlockSplices(int rule, std::vector<Splice>& out) const -> void;
    auto SectionLabel(int part, int section) const -> std::string;
    auto ExpandEmptyRules() -> Status;
};

}  // namespace document
