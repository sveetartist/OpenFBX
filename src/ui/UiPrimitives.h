#pragma once

#include "raylib.h"

namespace openfbx
{
// Block pointer input to lower UI layers while an overlay owns it.
void SetUiPointerBlocked(bool blocked);
bool UiMouseButtonPressed(int button);
bool UiMouseButtonDown(int button);
bool UiMouseButtonReleased(int button);
float UiMouseWheelMove();
Font LoadTechnicalFont();
void DrawUiText(Font font, const char* text, float x, float y, float size, Color color);
void DrawUiTextClipped(Font font, const char* text, float x, float y, float size, float maxWidth, Color color);
bool DrawPanelTab(Font font, Rectangle bounds, const char* label, bool selected);
bool DrawPanelButton(Font font, Rectangle bounds, const char* label);
bool DrawMenuItem(Font font, Rectangle bounds, const char* text, bool selected = false);
}
