#include "UiPrimitives.h"

#include <string>

namespace openfbx
{
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

bool DrawPanelTab(Font font, Rectangle bounds, const char* label, bool selected)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds, selected ? Color{ 48, 70, 92, 255 } : hovered ? Color{ 34, 39, 45, 255 } : Color{ 24, 27, 31, 245 });
    const float fontSize = MeasureTextEx(font, label, 14.0f, 1.0f).x > bounds.width - 10.0f ? 12.0f : 14.0f;
    DrawUiTextClipped(font, label, bounds.x + 5.0f, bounds.y + 5.0f, fontSize, bounds.width - 10.0f, selected ? RAYWHITE : Color{ 180, 190, 200, 255 });
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool DrawPanelButton(Font font, Rectangle bounds, const char* label)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds, hovered ? Color{ 54, 63, 72, 255 } : Color{ 35, 40, 46, 255 });
    DrawRectangleLinesEx(bounds, 1.0f, Color{ 70, 80, 90, 255 });
    const Vector2 size = MeasureTextEx(font, label, 14.0f, 1.0f);
    DrawUiText(font, label, bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + 4.0f, 14.0f, Color{ 205, 213, 220, 255 });
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool DrawMenuItem(Font font, Rectangle bounds, const char* text, bool selected)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds, selected ? Color{ 48, 70, 90, 255 } : hovered ? Color{ 46, 50, 56, 255 } : Color{ 28, 31, 35, 245 });
    DrawUiText(font, text, bounds.x + 10.0f, bounds.y + 5.0f, 16.0f, selected ? RAYWHITE : Color{ 205, 213, 220, 255 });
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
}
