#include "UiPrimitives.h"

#include <string>
#include <algorithm>

namespace openfbx
{
namespace
{
bool pointerBlocked = false;
}

void SetUiPointerBlocked(bool blocked) { pointerBlocked = blocked; }
bool UiMouseButtonPressed(int button) { return !pointerBlocked && IsMouseButtonPressed(button); }
bool UiMouseButtonDown(int button) { return !pointerBlocked && IsMouseButtonDown(button); }
bool UiMouseButtonReleased(int button) { return !pointerBlocked && IsMouseButtonReleased(button); }
float UiMouseWheelMove() { return pointerBlocked ? 0.0f : GetMouseWheelMove(); }

Font LoadTechnicalFont()
{
    const char* candidates[] = {
        "C:/Windows/Fonts/CascadiaMono.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/lucon.ttf"
    };

    for (const char* path : candidates)
    {
        if (FileExists(path))
        {
            Font font = LoadFontEx(path, 20, nullptr, 0);
            if (font.texture.id != 0)
            {
                SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
                return font;
            }
        }
    }

    return GetFontDefault();
}

void DrawUiText(Font font, const char* text, float x, float y, float size, Color color)
{
    DrawTextEx(font, text, Vector2{ x, y }, size, 1.0f, color);
}

void DrawUiTextClipped(Font font, const char* text, float x, float y, float size, float maxWidth, Color color)
{
    if (!text || maxWidth <= 0.0f) return;

    if (MeasureTextEx(font, text, size, 1.0f).x <= maxWidth)
    {
        DrawUiText(font, text, x, y, size, color);
        return;
    }

    std::string clipped = text;
    const char* ellipsis = "...";
    while (!clipped.empty())
    {
        clipped.pop_back();
        const std::string candidate = clipped + ellipsis;
        if (MeasureTextEx(font, candidate.c_str(), size, 1.0f).x <= maxWidth)
        {
            DrawUiText(font, candidate.c_str(), x, y, size, color);
            return;
        }
    }
}

bool DrawPanelTab(Font font, Rectangle bounds, const char* label, bool selected, Color statusColor)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds, selected ? Color{ 48, 70, 92, 255 } : hovered ? Color{ 34, 39, 45, 255 } : Color{ 24, 27, 31, 245 });
    if (statusColor.a > 0) DrawRectangleLinesEx(bounds, 1.0f, statusColor);
    float fontSize = MeasureTextEx(font, label, 14.0f, 1.0f).x > bounds.width - 10.0f ? 12.0f : 14.0f;
    if (statusColor.a > 0)
        while (fontSize > 10.0f && MeasureTextEx(font, label, fontSize, 1.0f).x > bounds.width - 10.0f) fontSize -= 1.0f;
    const Color textColor = statusColor.a > 0 ? statusColor : selected ? RAYWHITE : Color{ 180, 190, 200, 255 };
    DrawUiTextClipped(font, label, bounds.x + 5.0f, bounds.y + 5.0f, fontSize, bounds.width - 10.0f, textColor);
    return hovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool DrawPanelButton(Font font, Rectangle bounds, const char* label)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds, hovered ? Color{ 54, 63, 72, 255 } : Color{ 35, 40, 46, 255 });
    DrawRectangleLinesEx(bounds, 1.0f, Color{ 70, 80, 90, 255 });
    const float preferredSize = MeasureTextEx(font, label, 14.0f, 1.0f).x <= bounds.width - 10.0f ? 14.0f : 12.0f;
    const float fontSize = std::min(preferredSize, std::max(0.0f, bounds.height - 8.0f));
    const float available = std::max(0.0f, bounds.width - 10.0f);
    const float textWidth = std::min(available, MeasureTextEx(font, label, fontSize, 1.0f).x);
    if (fontSize > 0) DrawUiTextClipped(font, label, bounds.x + (bounds.width - textWidth) * 0.5f,
                                    bounds.y + (bounds.height - fontSize) * 0.5f, fontSize, available, Color{ 205, 213, 220, 255 });
    return hovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool DrawMenuItem(Font font, Rectangle bounds, const char* text, bool selected)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds, selected ? Color{ 48, 70, 90, 255 } : hovered ? Color{ 46, 50, 56, 255 } : Color{ 28, 31, 35, 245 });
    DrawUiTextClipped(font, text, bounds.x + 10.0f, bounds.y + 5.0f, 16.0f, bounds.width - 20.0f, selected ? RAYWHITE : Color{ 205, 213, 220, 255 });
    return hovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
}
