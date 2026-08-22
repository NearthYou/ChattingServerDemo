#pragma once

namespace chat::ui
{
    constexpr float RightAlignedMessageX(
        float contentStartX,
        float availableWidth,
        float textWidth) noexcept
    {
        const float remainingWidth = availableWidth - textWidth;
        return contentStartX + (remainingWidth > 0.0f ? remainingWidth : 0.0f);
    }
}
