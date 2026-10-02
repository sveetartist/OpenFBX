Rectangle GetSelectionMaskButtonRect(float hierarchyBlockW)
{
    return Rectangle{hierarchyBlockW + 10.0f, 104.0f, 144.0f, 28.0f};
}

Rectangle GetSelectionMaskMenuRect(float hierarchyBlockW)
{
    const Rectangle button = GetSelectionMaskButtonRect(hierarchyBlockW);
    constexpr float height = 92.0f;
    const float bottom = static_cast<float>(GetScreenHeight()) - gBottomPanelReservedHeight - 4.0f;
    float y = button.y + button.height + 4.0f;
    if (y + height > bottom) y = button.y - height - 4.0f;
    y = ClampFloat(y, 65.0f, std::max(65.0f, bottom - height));
    return Rectangle{button.x, y, button.width, height};
}

Rectangle GetSelectionMaskItemRect(Rectangle menu, int index)
{
    return Rectangle{menu.x + 4.0f, menu.y + 4.0f + index * 28.0f, menu.width - 8.0f, 28.0f};
}

bool UpdateSelectionMaskInput(SelectionMask& mask, bool& open, float hierarchyBlockW, bool inputBlocked)
{
    if (inputBlocked)
    {
        open = false;
        return false;
    }
    const Vector2 mouse = GetMousePosition();
    const Rectangle button = GetSelectionMaskButtonRect(hierarchyBlockW);
    const bool leftPressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (open)
    {
        if (IsKeyPressed(KEY_ESCAPE))
        {
            open = false;
            return true;
        }
        const Rectangle menu = GetSelectionMaskMenuRect(hierarchyBlockW);
        bool* entries[] = {&mask.geometry, &mask.bones, &mask.empties};
        for (int index = 0; index < 3; ++index)
            if (leftPressed && CheckCollisionPointRec(mouse, GetSelectionMaskItemRect(menu, index)))
            {
                *entries[index] = !*entries[index];
                return true;
            }
        if (leftPressed || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE))
            open = false;
        return true;
    }
    if (!CheckCollisionPointRec(mouse, button)) return false;
    if (leftPressed) open = true;
    return true;
}

void DrawSelectionMask(Font font, const SelectionMask& mask, bool open, float hierarchyBlockW)
{
    const Rectangle button = GetSelectionMaskButtonRect(hierarchyBlockW);
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), button);
    DrawRectangleRec(button, hovered ? Color{54, 63, 72, 255} : Color{35, 40, 46, 255});
    DrawRectangleLinesEx(button, 1.0f, Color{70, 80, 90, 255});
    DrawUiText(font, "Selection Mask", button.x + 8.0f, button.y + 7.0f, 13.0f, Color{205, 213, 220, 255});
    const float arrowX = button.x + button.width - 13.0f;
    const float arrowY = button.y + button.height * 0.5f;
    const float direction = open ? -1.0f : 1.0f;
    DrawLineEx(Vector2{arrowX - 4.0f, arrowY - direction * 2.0f}, Vector2{arrowX, arrowY + direction * 2.0f}, 1.5f, RAYWHITE);
    DrawLineEx(Vector2{arrowX, arrowY + direction * 2.0f}, Vector2{arrowX + 4.0f, arrowY - direction * 2.0f}, 1.5f, RAYWHITE);
    if (open || !mask.geometry || !mask.bones || !mask.empties)
        DrawRectangleLinesEx(button, 1.0f, Color{128, 188, 235, 255});
    if (!open) return;
    const Rectangle menu = GetSelectionMaskMenuRect(hierarchyBlockW);
    DrawRectangleRec(menu, Color{24, 27, 31, 255});
    DrawRectangleLinesEx(menu, 1.0f, Color{78, 88, 98, 255});
    const bool values[] = {mask.geometry, mask.bones, mask.empties};
    const char* labels[] = {"Geometry", "Bones", "Empties"};
    for (int index = 0; index < 3; ++index)
    {
        const Rectangle row = GetSelectionMaskItemRect(menu, index);
        if (CheckCollisionPointRec(GetMousePosition(), row)) DrawRectangleRec(row, Color{54, 63, 72, 255});
        const Rectangle check{row.x + 6.0f, row.y + 7.0f, 14.0f, 14.0f};
        DrawRectangleRec(check, values[index] ? Color{58, 78, 98, 255} : Color{24, 27, 31, 255});
        DrawRectangleLinesEx(check, 1.0f, values[index] ? Color{128, 188, 235, 255} : Color{78, 88, 98, 255});
        if (values[index])
        {
            DrawLineEx(Vector2{check.x + 3, check.y + 7}, Vector2{check.x + 6, check.y + 10}, 2.0f, RAYWHITE);
            DrawLineEx(Vector2{check.x + 6, check.y + 10}, Vector2{check.x + 11, check.y + 4}, 2.0f, RAYWHITE);
        }
        DrawUiText(font, labels[index], row.x + 28.0f, row.y + 7.0f, 14.0f, Color{205, 213, 220, 255});
    }
}
