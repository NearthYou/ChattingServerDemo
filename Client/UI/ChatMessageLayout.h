#pragma once

namespace chat::ui
{
    constexpr float ClampedOverlayExtent(
        float desiredExtent,
        float availableExtent,
        float margin = 24.0f) noexcept
    {
        const float safeAvailable = availableExtent > margin ? availableExtent - margin : 1.0f;
        return desiredExtent < safeAvailable ? desiredExtent : safeAvailable;
    }

    constexpr float MessageBubbleMaxWidth(float availableWidth) noexcept
    {
        const float safeWidth = availableWidth > 0.0f ? availableWidth : 0.0f;
        const float desiredWidth = safeWidth * 0.65f;
        const float minimumWidth = safeWidth < 48.0f ? safeWidth : 48.0f;
        return desiredWidth < minimumWidth
            ? minimumWidth
            : (desiredWidth > safeWidth ? safeWidth : desiredWidth);
    }

    constexpr float MessageBubbleWidth(float availableWidth, float contentWidth) noexcept
    {
        const float maximumWidth = MessageBubbleMaxWidth(availableWidth);
        const float minimumWidth = maximumWidth < 48.0f ? maximumWidth : 48.0f;
        const float safeContentWidth = contentWidth > 0.0f ? contentWidth : 0.0f;
        const float desiredWidth = safeContentWidth + 24.0f;
        return desiredWidth < minimumWidth
            ? minimumWidth
            : (desiredWidth > maximumWidth ? maximumWidth : desiredWidth);
    }

    constexpr float RightAlignedMessageX(
        float contentStartX,
        float availableWidth,
        float textWidth) noexcept
    {
        const float remainingWidth = availableWidth - textWidth;
        return contentStartX + (remainingWidth > 0.0f ? remainingWidth : 0.0f);
    }
}
