#pragma once

namespace ozo
{

//==============================================================================
// 版面尺寸 —— 改这里就能整体调整布局
//
// Editor 和 PanelLayer 都要用（面板是在 PanelLayer 里画的），
// 所以单独拎出来，两边 include 同一份，别各写一套。
namespace Layout
{
    const int width  = 780;
    const int height = 480;

    const int margin = 20;

    const int headerY = 0,  headerH = 60;

    const int panel1Y = 66,  panel1H = 186;
    const int panel2Y = 258, panel2H = 118;

    const int presetY = 382, presetH = 38;
    const int footerY = 424;
}

} // namespace ozo
