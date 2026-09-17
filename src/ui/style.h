#pragma once

namespace ui {

// Applies the PVM OSD look to ImGui's global style: flat black, no
// rounding, no shadows/borders, white text. Call once after
// ImGui::CreateContext(), before the first frame.
void applyPvmStyle();

}  // namespace ui
