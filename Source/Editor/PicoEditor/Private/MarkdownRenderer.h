#pragma once

#include <string_view>

namespace Pico
{
// Parses CommonMark/GFM with MD4C and draws a safe, editor-owned ImGui view.
// Returns true when the user double-clicks the rendered body to select raw text.
bool DrawMarkdown(std::string_view Markdown);
}
