#include "MarkdownRenderer.h"

#include <imgui.h>
#include <md4c.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
enum class EBlockKind
{
    Paragraph,
    Heading,
    Code,
    Quote,
    ListItem,
    Rule
};

struct FInlineStyle
{
    bool bStrong = false;
    bool bEmphasis = false;
    bool bCode = false;
    bool bStrike = false;
    bool bLink = false;
};

bool operator==(const FInlineStyle& Left, const FInlineStyle& Right)
{
    return Left.bStrong == Right.bStrong
        && Left.bEmphasis == Right.bEmphasis
        && Left.bCode == Right.bCode
        && Left.bStrike == Right.bStrike
        && Left.bLink == Right.bLink;
}

struct FFragment
{
    std::string Text;
    FInlineStyle Style;
};

struct FBlock
{
    EBlockKind Kind = EBlockKind::Paragraph;
    unsigned HeadingLevel = 0;
    int Indent = 0;
    std::string Prefix;
    std::string CodeLanguage;
    std::vector<FFragment> Fragments;
};

struct FListState
{
    bool bOrdered = false;
    unsigned NextIndex = 1;
};

struct FParseContext
{
    std::vector<FBlock> Blocks;
    std::vector<FListState> Lists;
    FBlock* Current = nullptr;
    int QuoteDepth = 0;
    bool bInsideListItem = false;
    std::string PendingListPrefix;
    int StrongDepth = 0;
    int EmphasisDepth = 0;
    int CodeDepth = 0;
    int StrikeDepth = 0;
    int LinkDepth = 0;
};

FInlineStyle CurrentStyle(const FParseContext& Context)
{
    return {Context.StrongDepth > 0, Context.EmphasisDepth > 0,
        Context.CodeDepth > 0, Context.StrikeDepth > 0,
        Context.LinkDepth > 0};
}

void StartTextBlock(FParseContext& Context, EBlockKind Kind)
{
    Context.Blocks.push_back({});
    Context.Current = &Context.Blocks.back();
    Context.Current->Kind = Kind;
    Context.Current->Indent = static_cast<int>(Context.Lists.size())
        + Context.QuoteDepth;
    if (Kind == EBlockKind::ListItem)
    {
        Context.Current->Prefix = std::move(Context.PendingListPrefix);
        Context.PendingListPrefix.clear();
    }
}

int EnterBlock(MD_BLOCKTYPE Type, void* Detail, void* UserData)
{
    auto& Context = *static_cast<FParseContext*>(UserData);
    switch (Type)
    {
    case MD_BLOCK_QUOTE:
        ++Context.QuoteDepth;
        break;
    case MD_BLOCK_UL:
        Context.Lists.push_back({false, 1});
        break;
    case MD_BLOCK_OL:
    {
        const auto* Ordered = static_cast<const MD_BLOCK_OL_DETAIL*>(Detail);
        Context.Lists.push_back({true, Ordered ? Ordered->start : 1});
        break;
    }
    case MD_BLOCK_LI:
    {
        Context.bInsideListItem = true;
        const auto* Item = static_cast<const MD_BLOCK_LI_DETAIL*>(Detail);
        if (Item && Item->is_task)
            Context.PendingListPrefix = (Item->task_mark == ' ' ? "[ ] " : "[x] ");
        else if (!Context.Lists.empty() && Context.Lists.back().bOrdered)
            Context.PendingListPrefix = std::to_string(Context.Lists.back().NextIndex++) + ". ";
        else
            Context.PendingListPrefix = "- ";
        break;
    }
    case MD_BLOCK_H:
        StartTextBlock(Context, EBlockKind::Heading);
        Context.Current->HeadingLevel =
            static_cast<const MD_BLOCK_H_DETAIL*>(Detail)->level;
        break;
    case MD_BLOCK_CODE:
        StartTextBlock(Context, EBlockKind::Code);
        if (Detail)
        {
            const auto* Code = static_cast<const MD_BLOCK_CODE_DETAIL*>(Detail);
            Context.Current->CodeLanguage.assign(Code->lang.text, Code->lang.size);
        }
        break;
    case MD_BLOCK_P:
        StartTextBlock(Context, Context.bInsideListItem
            ? EBlockKind::ListItem
            : (Context.QuoteDepth > 0 ? EBlockKind::Quote : EBlockKind::Paragraph));
        break;
    case MD_BLOCK_HR:
        Context.Blocks.push_back({});
        Context.Blocks.back().Kind = EBlockKind::Rule;
        Context.Current = nullptr;
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        StartTextBlock(Context, EBlockKind::Paragraph);
        Context.Current->Prefix = "| ";
        break;
    default:
        break;
    }
    return 0;
}

int LeaveBlock(MD_BLOCKTYPE Type, void*, void* UserData)
{
    auto& Context = *static_cast<FParseContext*>(UserData);
    switch (Type)
    {
    case MD_BLOCK_QUOTE:
        --Context.QuoteDepth;
        break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        if (!Context.Lists.empty()) Context.Lists.pop_back();
        break;
    case MD_BLOCK_LI:
        Context.bInsideListItem = false;
        Context.PendingListPrefix.clear();
        break;
    case MD_BLOCK_H:
    case MD_BLOCK_CODE:
    case MD_BLOCK_P:
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        Context.Current = nullptr;
        break;
    default:
        break;
    }
    return 0;
}

int EnterSpan(MD_SPANTYPE Type, void*, void* UserData)
{
    auto& Context = *static_cast<FParseContext*>(UserData);
    switch (Type)
    {
    case MD_SPAN_STRONG: ++Context.StrongDepth; break;
    case MD_SPAN_EM: ++Context.EmphasisDepth; break;
    case MD_SPAN_CODE: ++Context.CodeDepth; break;
    case MD_SPAN_DEL: ++Context.StrikeDepth; break;
    case MD_SPAN_A: ++Context.LinkDepth; break;
    default: break;
    }
    return 0;
}

int LeaveSpan(MD_SPANTYPE Type, void*, void* UserData)
{
    auto& Context = *static_cast<FParseContext*>(UserData);
    switch (Type)
    {
    case MD_SPAN_STRONG: --Context.StrongDepth; break;
    case MD_SPAN_EM: --Context.EmphasisDepth; break;
    case MD_SPAN_CODE: --Context.CodeDepth; break;
    case MD_SPAN_DEL: --Context.StrikeDepth; break;
    case MD_SPAN_A: --Context.LinkDepth; break;
    default: break;
    }
    return 0;
}

int AddText(MD_TEXTTYPE Type, const MD_CHAR* Text, MD_SIZE Size, void* UserData)
{
    auto& Context = *static_cast<FParseContext*>(UserData);
    if (!Context.Current)
        StartTextBlock(Context, Context.QuoteDepth > 0
            ? EBlockKind::Quote : EBlockKind::Paragraph);
    std::string Value;
    if (Type == MD_TEXT_BR) Value = "\n";
    else if (Type == MD_TEXT_SOFTBR) Value = " ";
    else if (Type == MD_TEXT_NULLCHAR) Value = "\xEF\xBF\xBD";
    else Value.assign(Text, Size);
    const FInlineStyle Style = CurrentStyle(Context);
    if (!Context.Current->Fragments.empty() &&
        Context.Current->Fragments.back().Style == Style)
    {
        Context.Current->Fragments.back().Text += Value;
    }
    else
    {
        Context.Current->Fragments.push_back({std::move(Value), Style});
    }
    return 0;
}

std::vector<FBlock> ParseMarkdown(std::string_view Markdown)
{
    FParseContext Context;
    MD_PARSER Parser {};
    Parser.flags = MD_DIALECT_GITHUB | MD_FLAG_NOHTML;
    Parser.enter_block = EnterBlock;
    Parser.leave_block = LeaveBlock;
    Parser.enter_span = EnterSpan;
    Parser.leave_span = LeaveSpan;
    Parser.text = AddText;
    if (md_parse(Markdown.data(), static_cast<MD_SIZE>(Markdown.size()),
            &Parser, &Context) != 0)
    {
        FBlock Fallback;
        Fallback.Fragments.push_back({std::string(Markdown), {}});
        return {std::move(Fallback)};
    }
    return Context.Blocks;
}

const std::vector<FBlock>& CachedMarkdown(std::string_view Markdown)
{
    static std::unordered_map<std::string, std::vector<FBlock>> Cache;
    static std::deque<std::string> InsertionOrder;
    const std::string Key(Markdown);
    const auto Existing = Cache.find(Key);
    if (Existing != Cache.end()) return Existing->second;
    if (InsertionOrder.size() >= 512)
    {
        Cache.erase(InsertionOrder.front());
        InsertionOrder.pop_front();
    }
    InsertionOrder.push_back(Key);
    return Cache.emplace(Key, ParseMarkdown(Markdown)).first->second;
}

std::string PlainText(const FBlock& Block)
{
    std::string Text = Block.Prefix;
    for (const FFragment& Fragment : Block.Fragments) Text += Fragment.Text;
    return Text;
}

void DrawInline(const FBlock& Block, const ImVec4& BaseColor)
{
    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    ImFont* Font = ImGui::GetFont();
    const float FontSize = ImGui::GetFontSize();
    const float Scale = FontSize / Font->FontSize;
    const ImVec2 Start = ImGui::GetCursorScreenPos();
    const float Width = std::max(40.0f, ImGui::GetContentRegionAvail().x);
    const float Right = Start.x + Width;
    const float LineHeight = ImGui::GetTextLineHeightWithSpacing();
    float X = Start.x;
    float Y = Start.y;

    auto NewLine = [&]() { X = Start.x; Y += LineHeight; };
    auto DrawPiece = [&](const char* Begin, const char* End,
        const FInlineStyle& Style)
    {
        ImVec4 Color = BaseColor;
        if (Style.bLink) Color = ImVec4(0.38f, 0.68f, 1.0f, 1.0f);
        else if (Style.bCode) Color = ImVec4(0.96f, 0.72f, 0.45f, 1.0f);
        else if (Style.bEmphasis) Color.w = 0.82f;
        const ImVec2 Size = Font->CalcTextSizeA(FontSize, 100000.0f, 0.0f, Begin, End);
        if (Style.bCode)
            DrawList->AddRectFilled(ImVec2(X - 2.0f, Y - 1.0f),
                ImVec2(X + Size.x + 2.0f, Y + FontSize + 2.0f),
                ImGui::GetColorU32(ImVec4(0.16f, 0.17f, 0.19f, 1.0f)), 2.0f);
        DrawList->AddText(Font, FontSize, ImVec2(X, Y),
            ImGui::GetColorU32(Color), Begin, End);
        if (Style.bStrong)
            DrawList->AddText(Font, FontSize, ImVec2(X + 0.45f, Y),
                ImGui::GetColorU32(Color), Begin, End);
        if (Style.bLink)
            DrawList->AddLine(ImVec2(X, Y + FontSize + 1.0f),
                ImVec2(X + Size.x, Y + FontSize + 1.0f),
                ImGui::GetColorU32(Color));
        if (Style.bStrike)
            DrawList->AddLine(ImVec2(X, Y + FontSize * 0.55f),
                ImVec2(X + Size.x, Y + FontSize * 0.55f),
                ImGui::GetColorU32(Color));
        X += Size.x;
    };

    if (!Block.Prefix.empty())
    {
        FInlineStyle PrefixStyle;
        DrawPiece(Block.Prefix.data(), Block.Prefix.data() + Block.Prefix.size(), PrefixStyle);
    }
    for (const FFragment& Fragment : Block.Fragments)
    {
        const char* Cursor = Fragment.Text.data();
        const char* End = Cursor + Fragment.Text.size();
        while (Cursor < End)
        {
            if (*Cursor == '\n')
            {
                ++Cursor;
                NewLine();
                continue;
            }
            const char* PhysicalEnd = static_cast<const char*>(
                std::memchr(Cursor, '\n', static_cast<std::size_t>(End - Cursor)));
            if (!PhysicalEnd) PhysicalEnd = End;
            const float Available = Right - X;
            const char* Wrap = Font->CalcWordWrapPositionA(
                Scale, Cursor, PhysicalEnd, Available);
            if (Wrap <= Cursor)
            {
                if (X > Start.x) { NewLine(); continue; }
                Wrap = Font->CalcWordWrapPositionA(
                    Scale, Cursor, PhysicalEnd, Width);
                if (Wrap <= Cursor) Wrap = PhysicalEnd;
            }
            DrawPiece(Cursor, Wrap, Fragment.Style);
            Cursor = Wrap;
            while (Cursor < PhysicalEnd && (*Cursor == ' ' || *Cursor == '\t')) ++Cursor;
            if (Cursor < PhysicalEnd) NewLine();
        }
    }
    ImGui::Dummy(ImVec2(Width, (Y - Start.y) + LineHeight));
}

void DrawCodeBlock(const FBlock& Block)
{
    const std::string Code = PlainText(Block);
    constexpr std::size_t CollapsedLineLimit = 12;
    const std::size_t LineCount = static_cast<std::size_t>(
        std::count(Code.begin(), Code.end(), '\n')) + 1;
    static std::unordered_set<ImGuiID> ExpandedBlocks;
    const ImGuiID ExpansionId = ImGui::GetID("##MarkdownCodeExpansionState");
    const bool bCanCollapse = LineCount > CollapsedLineLimit;
    const bool bExpanded = ExpandedBlocks.contains(ExpansionId);

    if (!Block.CodeLanguage.empty()) ImGui::TextDisabled("%s", Block.CodeLanguage.c_str());
    const float Right = ImGui::GetWindowContentRegionMax().x;
    const float ExpandWidth = bCanCollapse ? ImGui::GetFrameHeight() + 4.0f : 0.0f;
    if (!Block.CodeLanguage.empty())
        ImGui::SameLine(Right - 78.0f - ExpandWidth);
    else
        ImGui::SetCursorPosX(Right - 78.0f - ExpandWidth);
    if (ImGui::SmallButton("Copy code")) ImGui::SetClipboardText(Code.c_str());
    if (bCanCollapse)
    {
        ImGui::SameLine();
        if (ImGui::ArrowButton("##ToggleCodeExpansion",
                bExpanded ? ImGuiDir_Up : ImGuiDir_Down))
        {
            if (bExpanded) ExpandedBlocks.erase(ExpansionId);
            else ExpandedBlocks.insert(ExpansionId);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(bExpanded ? "Collapse code" : "Expand code");
    }

    std::string VisibleCode = Code;
    if (bCanCollapse && !bExpanded)
    {
        std::size_t Cursor = 0;
        for (std::size_t Line = 0; Line < CollapsedLineLimit && Cursor != std::string::npos; ++Line)
            Cursor = VisibleCode.find('\n', Cursor) == std::string::npos
                ? std::string::npos
                : VisibleCode.find('\n', Cursor) + 1;
        if (Cursor != std::string::npos)
        {
            VisibleCode.resize(Cursor);
            VisibleCode += "...";
        }
    }

    const float WrapWidth = std::max(80.0f, ImGui::GetContentRegionAvail().x - 12.0f);
    const float Height = std::max(42.0f,
        ImGui::CalcTextSize(VisibleCode.c_str(), nullptr, false, WrapWidth).y + 12.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.11f, 0.13f, 1.0f));
    ImGui::BeginChild("##MarkdownCode", ImVec2(-1.0f, Height), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextWrapped("%s", VisibleCode.c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
}

bool DrawMarkdown(std::string_view Markdown)
{
    const std::vector<FBlock>& Blocks = CachedMarkdown(Markdown);
    const ImVec2 Start = ImGui::GetCursorScreenPos();
    for (std::size_t Index = 0; Index < Blocks.size(); ++Index)
    {
        const FBlock& Block = Blocks[Index];
        ImGui::PushID(static_cast<int>(Index));
        if (Block.Indent > 0) ImGui::Indent(Block.Indent * 14.0f);
        switch (Block.Kind)
        {
        case EBlockKind::Heading:
        {
            const ImVec4 Color = Block.HeadingLevel <= 2
                ? ImVec4(0.92f, 0.94f, 0.98f, 1.0f)
                : ImVec4(0.82f, 0.86f, 0.92f, 1.0f);
            DrawInline(Block, Color);
            if (Block.HeadingLevel <= 2) ImGui::Separator();
            break;
        }
        case EBlockKind::Code:
            DrawCodeBlock(Block);
            break;
        case EBlockKind::Quote:
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.72f, 0.80f, 1.0f));
            DrawInline(Block, ImVec4(0.66f, 0.72f, 0.80f, 1.0f));
            ImGui::PopStyleColor();
            break;
        case EBlockKind::Rule:
            ImGui::Separator();
            break;
        default:
            DrawInline(Block, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            break;
        }
        if (Block.Indent > 0) ImGui::Unindent(Block.Indent * 14.0f);
        ImGui::Spacing();
        ImGui::PopID();
    }
    const ImVec2 End(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x,
        ImGui::GetCursorScreenPos().y);
    const bool bHovered = ImGui::IsMouseHoveringRect(Start, End, true);
    if (bHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    return bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
}
}
