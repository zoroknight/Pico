#pragma once

#include <string>
#include <string_view>

namespace Pico
{
// Parses CommonMark/GFM with MD4C and draws a safe, editor-owned ImGui view.
// Returns true when the user double-clicks the rendered body to select raw text.
bool DrawMarkdown(std::string_view Markdown);

// Keeps persisted/copied UTF-8 intact while omitting glyphs that ImGui's
// non-color font atlas cannot render reliably from the drawn view.
std::string MakeMarkdownDisplayText(std::string_view Text);
}
