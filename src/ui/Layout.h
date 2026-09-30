#pragma once

namespace ozo
{

//==============================================================================
// 版面尺寸 —— EXPERT 抽屉浮窗位于右侧 1/3 区域 (宽 320px)
//==============================================================================
namespace Layout
{
    const int width  = 960;
    const int height = 490;

    const int margin = 20;

    const int headerY = 0,  headerH = 60;

    // EXPERT 侧边抽屉浮窗
    const int drawerW = 320;
    const int drawerX = width - drawerW; // 640

    // 主视区（ONE-KNOB 区域）
    const int mainViewW = width;            // 收起时全幅 960
    const int mainViewCollapsedW = drawerX; // 展开时左侧 640

    const int footerY = 464;
}

} // namespace ozo
