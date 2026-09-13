bool DrawChannelButton(Font font, Rectangle bounds, PackedChannel channel)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    DrawRectangleRec(bounds, GetPackedChannelColor(channel, hovered));
    DrawRectangleLinesEx(bounds, 1.0f, Color{ 205, 213, 220, 120 });
    const char* label = GetPackedChannelName(channel);
    const Vector2 size = MeasureTextEx(font, label, 14.0f, 1.0f);
    DrawUiText(font, label, bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + 4.0f, 14.0f, RAYWHITE);
    return hovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool DrawNormalModeButton(Font font, Rectangle bounds, bool directX)
{
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    const Color fill = directX
        ? (hovered ? Color{ 68, 114, 205, 255 } : Color{ 50, 84, 156, 255 })
        : (hovered ? Color{ 62, 160, 76, 255 } : Color{ 45, 118, 58, 255 });
    DrawRectangleRec(bounds, fill);
    DrawRectangleLinesEx(bounds, 1.0f, Color{ 205, 213, 220, 120 });
    const char* label = directX ? "DX" : "GL";
    const Vector2 size = MeasureTextEx(font, label, 14.0f, 1.0f);
    DrawUiText(font, label, bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + 4.0f, 14.0f, RAYWHITE);
    return hovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void DrawTextureThumbnail(Font font, Rectangle bounds, const PbrTexture& texture, bool dropTarget)
{
    DrawRectangleRec(bounds, dropTarget ? Color{ 42, 62, 74, 255 } : Color{ 18, 21, 25, 255 });
    if (texture.loaded)
    {
        const Rectangle source{ 0.0f, 0.0f, static_cast<float>(texture.texture.width), static_cast<float>(texture.texture.height) };
        DrawTexturePro(texture.texture, source, Rectangle{ bounds.x + 2.0f, bounds.y + 2.0f, bounds.width - 4.0f, bounds.height - 4.0f }, Vector2{}, 0.0f, WHITE);
    }
    else
    {
        const Vector2 size = MeasureTextEx(font, "-", 18.0f, 1.0f);
        DrawUiText(font, "-", bounds.x + (bounds.width - size.x) * 0.5f, bounds.y + (bounds.height - size.y) * 0.5f, 18.0f, Color{ 120, 132, 144, 255 });
    }
    DrawRectangleLinesEx(bounds, 1.0f, dropTarget ? Color{ 120, 190, 230, 255 } : Color{ 70, 80, 90, 255 });
}

void DrawTextureContextMenu(Font font, ModelTab& tab, TextureClipboard& clipboard, std::string& notice, std::string& error)
{
    if (!clipboard.menuOpen) return;

    const Rectangle menu{ clipboard.position.x, clipboard.position.y, 116.0f, 62.0f };
    DrawRectangleRec(menu, Color{ 24, 27, 31, 248 });
    DrawRectangleLinesEx(menu, 1.0f, Color{ 80, 90, 100, 255 });

    const Rectangle copyButton{ menu.x, menu.y, menu.width, 30.0f };
    const Rectangle pasteButton{ menu.x, menu.y + 30.0f, menu.width, 30.0f };
    const bool validMaterial = clipboard.materialIndex >= 0 && clipboard.materialIndex < static_cast<int>(tab.pbrMaterials.size());
    const PbrTexture* texture = validMaterial ? &GetPbrTexture(tab.pbrMaterials[static_cast<size_t>(clipboard.materialIndex)], clipboard.slot) : nullptr;
    const bool canCopy = texture && texture->loaded;
    const bool canPaste = !clipboard.path.empty();

    if (clipboard.justOpened)
    {
        clipboard.justOpened = false;
    }
    else
    {
        if (canCopy && DrawPanelButton(font, copyButton, "Copy"))
        {
            clipboard.path = texture->path;
            clipboard.menuOpen = false;
            notice = "Copied texture: " + std::string(GetFileName(clipboard.path.c_str()));
            error.clear();
            return;
        }
        if (canPaste && DrawPanelButton(font, pasteButton, "Paste"))
        {
            EditSnapshot before = CaptureEditSnapshot(tab);
            std::string loadError;
            if (LoadPbrTexture(tab, clipboard.materialIndex, clipboard.slot, clipboard.path, loadError))
            {
                PushUndoSnapshot(tab, std::move(before));
                notice = std::string("Pasted texture to ") + GetPbrTextureSlotName(clipboard.slot) + ".";
                error.clear();
            }
            else
            {
                error = loadError;
                notice.clear();
            }
            clipboard.menuOpen = false;
            return;
        }
        if (openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(GetMousePosition(), menu))
        {
            clipboard.menuOpen = false;
        }
    }

    if (!canCopy)
    {
        DrawRectangleRec(copyButton, Color{ 28, 31, 35, 245 });
        DrawUiText(font, "Copy", copyButton.x + 10.0f, copyButton.y + 5.0f, 16.0f, Color{ 105, 115, 124, 255 });
    }
    if (!canPaste)
    {
        DrawRectangleRec(pasteButton, Color{ 28, 31, 35, 245 });
        DrawUiText(font, "Paste", pasteButton.x + 10.0f, pasteButton.y + 5.0f, 16.0f, Color{ 105, 115, 124, 255 });
    }
}

bool IsFiniteFloat(float value)
{
    return std::isfinite(value);
}

bool IsFiniteVector(Vector3 value)
{
    return IsFiniteFloat(value.x) && IsFiniteFloat(value.y) && IsFiniteFloat(value.z);
}

void AddValidationIssue(std::vector<ValidatorIssue>& issues, ValidatorSeverity severity, const std::string& category, const std::string& message, int node = -1)
{
    issues.push_back(ValidatorIssue{ severity, category, message, node });
}

int ValidatorSeverityRank(ValidatorSeverity severity)
{
    switch (severity)
    {
    case ValidatorSeverity::Error: return 2;
    case ValidatorSeverity::Warning: return 1;
    case ValidatorSeverity::Info: return 0;
    }
    return 0;
}

const char* GetValidatorSeverityName(ValidatorSeverity severity)
{
    switch (severity)
    {
    case ValidatorSeverity::Error: return "ERR";
    case ValidatorSeverity::Warning: return "WARN";
    case ValidatorSeverity::Info: return "INFO";
    }
    return "INFO";
}

Color GetValidatorSeverityColor(ValidatorSeverity severity)
{
    switch (severity)
    {
    case ValidatorSeverity::Error: return Color{ 216, 92, 92, 255 };
    case ValidatorSeverity::Warning: return Color{ 220, 168, 72, 255 };
    case ValidatorSeverity::Info: return Color{ 104, 168, 220, 255 };
    }
    return Color{ 104, 168, 220, 255 };
}

std::string GetMaterialDisplayName(const LoadedFbxModel& loaded, int materialIndex)
{
    if (materialIndex >= 0 && materialIndex < static_cast<int>(loaded.materialNames.size()))
    {
        return loaded.materialNames[static_cast<size_t>(materialIndex)];
    }
    return std::string("Material ") + std::to_string(materialIndex + 1);
}

void ValidateTextures(const ModelTab& tab, std::vector<ValidatorIssue>& issues)
{
    const int materialCount = tab.loaded.hasMesh ? std::max(1, tab.loaded.model.materialCount) : 0;
    int missingDiffuse = 0;
    int missingNormal = 0;

    for (int materialIndex = 0; materialIndex < materialCount; ++materialIndex)
    {
        const PbrMaterialState* material = materialIndex < static_cast<int>(tab.pbrMaterials.size()) ? &tab.pbrMaterials[static_cast<size_t>(materialIndex)] : nullptr;
        if (!material || !GetPbrTexture(*material, PbrTextureSlot::Diffuse).loaded)
        {
            ++missingDiffuse;
        }
        if (!material || !GetPbrTexture(*material, PbrTextureSlot::Normal).loaded)
        {
            ++missingNormal;
        }

        if (!material) continue;

        for (int slotIndex = 0; slotIndex < static_cast<int>(PbrTextureSlot::Count); ++slotIndex)
        {
            const PbrTextureSlot slot = static_cast<PbrTextureSlot>(slotIndex);
            const PbrTexture& texture = GetPbrTexture(*material, slot);
            if (texture.path.empty()) continue;

            std::error_code existsError;
            const bool sourceExists = std::filesystem::exists(texture.path, existsError);
            if (!texture.loaded || texture.texture.id == 0 || texture.texture.width <= 0 || texture.texture.height <= 0 || !sourceExists)
            {
                AddValidationIssue(issues,
                                   ValidatorSeverity::Error,
                                   "Broken texture",
                                   GetMaterialDisplayName(tab.loaded, materialIndex) + " " + GetPbrTextureSlotName(slot) + ": " + texture.path);
            }
        }
    }

    if (missingDiffuse > 0)
    {
        AddValidationIssue(issues, ValidatorSeverity::Warning, "Missing texture", std::to_string(missingDiffuse) + " material(s) have no diffuse texture assigned.");
    }
    if (missingNormal > 0)
    {
        AddValidationIssue(issues, ValidatorSeverity::Info, "Missing texture", std::to_string(missingNormal) + " material(s) have no normal texture assigned.");
    }
}

void ValidateDuplicateNames(const LoadedFbxModel& loaded, std::vector<ValidatorIssue>& issues)
{
    std::unordered_map<std::string, int> nodeNames;
    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        if (IsSceneRootNode(loaded, i)) continue;

        const std::string& name = loaded.nodes[static_cast<size_t>(i)].name;
        if (name.empty()) continue;
        const int count = ++nodeNames[name];
        if (count == 2)
        {
            AddValidationIssue(issues, ValidatorSeverity::Warning, "Duplicate name", "Duplicate node name: " + name, i);
        }
    }

    std::unordered_map<std::string, int> materialNames;
    for (const std::string& name : loaded.materialNames)
    {
        if (!name.empty() && ++materialNames[name] == 2)
        {
            AddValidationIssue(issues, ValidatorSeverity::Warning, "Duplicate name", "Duplicate material name: " + name);
        }
    }

    std::unordered_map<std::string, int> animationNames;
    for (const AnimationClip& clip : loaded.animations)
    {
        if (!clip.name.empty() && ++animationNames[clip.name] == 2)
        {
            AddValidationIssue(issues, ValidatorSeverity::Warning, "Duplicate name", "Duplicate animation name: " + clip.name);
        }
    }
}

void ValidateTransforms(const LoadedFbxModel& loaded, std::vector<ValidatorIssue>& issues)
{
    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        if (IsSceneRootNode(loaded, i)) continue;

        const SceneNode& node = loaded.nodes[static_cast<size_t>(i)];
        if (!IsFiniteVector(node.position) || !IsFiniteVector(node.rotation) || !IsFiniteVector(node.scale) ||
            !IsFiniteVector(node.axisX) || !IsFiniteVector(node.axisY) || !IsFiniteVector(node.axisZ))
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Invalid transform", "Non-finite transform values on " + node.name, i);
            continue;
        }

        if (std::fabs(node.scale.x) <= 0.000001f || std::fabs(node.scale.y) <= 0.000001f || std::fabs(node.scale.z) <= 0.000001f)
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Invalid transform", "Zero or near-zero scale on " + node.name, i);
        }
        else if (!IsUnitScale(node.scale))
        {
            AddValidationIssue(issues, ValidatorSeverity::Warning, "Unapplied scale", node.name + " scale is not 1, 1, 1.", i);
        }

        const float axisXLength = Vector3Length(node.axisX);
        const float axisYLength = Vector3Length(node.axisY);
        const float axisZLength = Vector3Length(node.axisZ);
        if (axisXLength <= 0.000001f || axisYLength <= 0.000001f || axisZLength <= 0.000001f)
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Invalid transform", "Zero transform axis on " + node.name, i);
        }
        else if (std::fabs(axisXLength - 1.0f) > 0.02f || std::fabs(axisYLength - 1.0f) > 0.02f || std::fabs(axisZLength - 1.0f) > 0.02f ||
                 std::fabs(Vector3DotProduct(node.axisX, node.axisY)) > 0.03f ||
                 std::fabs(Vector3DotProduct(node.axisX, node.axisZ)) > 0.03f ||
                 std::fabs(Vector3DotProduct(node.axisY, node.axisZ)) > 0.03f)
        {
            AddValidationIssue(issues, ValidatorSeverity::Warning, "Invalid transform", "Skewed or non-orthogonal axes on " + node.name, i);
        }
    }
}

void ValidateGeometry(const LoadedFbxModel& loaded, std::vector<ValidatorIssue>& issues)
{
    if (loaded.hasMesh && loaded.bindNormals.size() != loaded.bindVertices.size())
    {
        AddValidationIssue(issues, ValidatorSeverity::Error, "Missing normals", "Normal buffer length does not match vertex buffer length.");
    }
    if (loaded.hasMesh && loaded.uvSets.empty())
    {
        AddValidationIssue(issues, ValidatorSeverity::Warning, "Missing UVs", "Model has no UV sets.");
    }

    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        const SceneNode& node = loaded.nodes[static_cast<size_t>(i)];
        if (node.type != SceneNodeType::Mesh) continue;

        if (node.meshVertexCount <= 0)
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Empty mesh", node.name + " contains no triangles.", i);
            continue;
        }
        if (!node.meshHadUvs)
        {
            AddValidationIssue(issues, ValidatorSeverity::Warning, "Missing UVs", node.name + " has missing or unmapped UV data.", i);
        }
        if (!node.meshHadNormals)
        {
            AddValidationIssue(issues, ValidatorSeverity::Warning, "Missing normals", node.name + " had missing authored normals.", i);
        }
        if (node.degenerateTriangleCount > 0)
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Degenerate triangles", node.name + " has " + std::to_string(node.degenerateTriangleCount) + " degenerate triangle(s).", i);
        }
    }
}

void ValidateSkinning(const LoadedFbxModel& loaded, std::vector<ValidatorIssue>& issues)
{
    bool hasSkinnedMesh = false;
    std::unordered_map<std::string, bool> influencedBones;
    for (const SkinnedVertex& vertex : loaded.skinnedVertices)
    {
        if (!vertex.influences.empty()) hasSkinnedMesh = true;
        for (const SkinnedVertexInfluence& influence : vertex.influences)
        {
            if (!influence.boneName.empty())
            {
                influencedBones[influence.boneName] = true;
            }
        }
    }

    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        const SceneNode& node = loaded.nodes[static_cast<size_t>(i)];
        if (node.type != SceneNodeType::Mesh) continue;

        if (node.badSkinWeightCount > 0)
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Bad weights", node.name + " has " + std::to_string(node.badSkinWeightCount) + " vertex weight sum(s) outside 1.0.", i);
        }
        if (node.missingSkinWeightCount > 0)
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Bad weights", node.name + " has " + std::to_string(node.missingSkinWeightCount) + " skinned vertex/vertices with no weights.", i);
        }
    }

    if (!hasSkinnedMesh) return;

    if (loaded.bonePoses.empty())
    {
        AddValidationIssue(issues, ValidatorSeverity::Error, "Missing bind poses", "Skinned mesh has no skeleton bind poses.");
    }

    for (int i = 0; i < static_cast<int>(loaded.nodes.size()); ++i)
    {
        const SceneNode& node = loaded.nodes[static_cast<size_t>(i)];
        if (node.type == SceneNodeType::Bone && influencedBones.find(node.name) != influencedBones.end() && !node.hasSkinBindPose)
        {
            AddValidationIssue(issues, ValidatorSeverity::Error, "Missing bind poses", "Influenced bone has no explicit skin bind pose: " + node.name, i);
        }
    }
}

void ValidateTexelDensityConsistency(const ModelTab& tab, std::vector<ValidatorIssue>& issues);
void ValidateUvIslandOverlaps(const ModelTab& tab, std::vector<ValidatorIssue>& issues);

std::vector<ValidatorIssue> BuildValidationIssues(const ModelTab& tab)
{
    std::vector<ValidatorIssue> issues;
    ValidateTextures(tab, issues);
    ValidateDuplicateNames(tab.loaded, issues);
    ValidateTransforms(tab.loaded, issues);
    ValidateGeometry(tab.loaded, issues);
    ValidateTexelDensityConsistency(tab, issues);
    ValidateUvIslandOverlaps(tab, issues);
    ValidateSkinning(tab.loaded, issues);

    std::stable_sort(issues.begin(), issues.end(), [](const ValidatorIssue& a, const ValidatorIssue& b)
    {
        return ValidatorSeverityRank(a.severity) > ValidatorSeverityRank(b.severity);
    });
    return issues;
}

void DrawValidatorPanel(Font font, ModelTab& tab, HierarchyPanelState& panel, float panelX, float panelY, float panelW)
{
    std::vector<ValidatorIssue> issues = BuildValidationIssues(tab);
    const float contentX = panelX + 12.0f;
    const float panelH = GetHierarchyPanelHeight();
    float y = panelY;

    int errors = 0;
    int warnings = 0;
    int infos = 0;
    for (const ValidatorIssue& issue : issues)
    {
        if (issue.severity == ValidatorSeverity::Error) ++errors;
        else if (issue.severity == ValidatorSeverity::Warning) ++warnings;
        else ++infos;
    }

    DrawUiText(font, "VALIDATOR", contentX, y, 16.0f, Color{ 165, 182, 196, 255 });
    y += 28.0f;

    char summary[192] = {};
    std::snprintf(summary, sizeof(summary), "%d errors    %d warnings    %d info", errors, warnings, infos);
    DrawUiTextClipped(font, summary, contentX, y, 14.0f, panelW - 24.0f, Color{ 205, 213, 220, 255 });
    y += 28.0f;

    if (issues.empty())
    {
        DrawUiTextClipped(font, "No validation issues found.", contentX, y, 14.0f, panelW - 24.0f, Color{ 128, 140, 152, 255 });
        panel.validatorScroll = 0.0f;
        return;
    }

    // Issues arrive severity-sorted, so groups and their contents retain priority.
    struct Group
    {
        std::string category;
        std::vector<const ValidatorIssue*> issues;
    };
    std::vector<Group> groups;
    for (const ValidatorIssue& issue : issues)
    {
        auto group = std::find_if(groups.begin(), groups.end(), [&](const Group& candidate)
        {
            return candidate.category == issue.category;
        });
        if (group == groups.end()) groups.push_back(Group{ issue.category, { &issue } });
        else group->issues.push_back(&issue);
    }

    const float buttonW = (panelW - 30.0f) * 0.5f;
    if (DrawPanelButton(font, Rectangle{ contentX, y, buttonW, 24.0f }, "Expand All"))
    {
        for (const Group& group : groups) tab.expandedValidationGroups[group.category] = true;
    }
    if (DrawPanelButton(font, Rectangle{ contentX + buttonW + 6.0f, y, buttonW, 24.0f }, "Collapse All"))
    {
        for (const Group& group : groups) tab.expandedValidationGroups[group.category] = false;
        panel.validatorScroll = 0.0f;
    }
    y += 32.0f;

    constexpr float headerH = 32.0f;
    constexpr float rowH = 54.0f;
    const Rectangle listBounds{ contentX, y, panelW - 24.0f, std::max(1.0f, panelH - (y - 61.0f) - 10.0f) };
    float contentH = 0.0f;
    for (const Group& group : groups)
    {
        contentH += headerH;
        if (tab.expandedValidationGroups[group.category]) contentH += rowH * static_cast<float>(group.issues.size());
    }
    const float maxScroll = std::max(0.0f, contentH - listBounds.height);
    const Vector2 mouse = GetMousePosition();
    const bool inList = CheckCollisionPointRec(mouse, listBounds);
    if (inList) panel.validatorScroll -= openfbx::UiMouseWheelMove() * rowH * 2.0f;
    panel.validatorScroll = ClampFloat(panel.validatorScroll, 0.0f, maxScroll);
    float rowY = listBounds.y - panel.validatorScroll;
    const float rowW = listBounds.width - (maxScroll > 0.0f ? 10.0f : 0.0f);
    std::string toggledGroup;

    BeginScissorMode(static_cast<int>(listBounds.x), static_cast<int>(listBounds.y), static_cast<int>(listBounds.width), static_cast<int>(listBounds.height));
    for (const Group& group : groups)
    {
        const bool expanded = tab.expandedValidationGroups[group.category];
        const Rectangle header{ listBounds.x, rowY, rowW, headerH - 3.0f };
        const bool hoveredHeader = inList && CheckCollisionPointRec(mouse, header);
        if (rowY + headerH > listBounds.y && rowY < listBounds.y + listBounds.height)
        {
            DrawRectangleRec(header, hoveredHeader ? Color{ 48, 57, 66, 255 } : Color{ 35, 42, 49, 255 });
            const std::string label = std::string(expanded ? "v " : "> ") + group.category + " (" + std::to_string(group.issues.size()) + ")";
            DrawUiTextClipped(font, label.c_str(), header.x + 8.0f, header.y + 7.0f, 14.0f, header.width - 16.0f,
                              GetValidatorSeverityColor(group.issues.front()->severity));
            if (hoveredHeader && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT)) toggledGroup = group.category;
        }
        rowY += headerH;
        if (!expanded) continue;
        for (const ValidatorIssue* entry : group.issues)
        {
            const ValidatorIssue& issue = *entry;
            if (rowY + rowH > listBounds.y && rowY < listBounds.y + listBounds.height)
            {
                const Rectangle row{ listBounds.x + 8.0f, rowY, rowW - 8.0f, rowH - 4.0f };
                const bool hovered = inList && CheckCollisionPointRec(mouse, row);
                const bool selectedNode = issue.node >= 0 && IsNodeSelected(tab, issue.node);
                DrawRectangleRec(row, selectedNode ? Color{ 48, 70, 92, 255 } : hovered ? Color{ 34, 39, 45, 255 } : Color{ 24, 27, 31, 220 });
                DrawRectangleLinesEx(row, 1.0f, Color{ 54, 62, 70, 255 });
                DrawUiText(font, GetValidatorSeverityName(issue.severity), row.x + 8.0f, row.y + 7.0f, 13.0f, GetValidatorSeverityColor(issue.severity));
                DrawUiTextClipped(font, issue.message.c_str(), row.x + 8.0f, row.y + 27.0f, 13.0f, row.width - 16.0f, Color{ 154, 166, 178, 255 });
                if (hovered && issue.node >= 0 && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT))
                {
                    SelectNode(tab, issue.node, IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
                }
            }
            rowY += rowH;
        }
    }
    EndScissorMode();

    // Apply after drawing so expanding cannot dispatch the same click to a new row.
    if (!toggledGroup.empty()) tab.expandedValidationGroups[toggledGroup] = !tab.expandedValidationGroups[toggledGroup];
    if (maxScroll > 0.0f)
    {
        const float trackX = listBounds.x + listBounds.width - 6.0f;
        const float thumbH = std::min(listBounds.height, std::max(24.0f, listBounds.height * (listBounds.height / contentH)));
        const float thumbY = listBounds.y + (listBounds.height - thumbH) * (panel.validatorScroll / maxScroll);
        DrawRectangle(static_cast<int>(trackX), static_cast<int>(listBounds.y), 4, static_cast<int>(listBounds.height), Color{ 44, 49, 55, 255 });
        DrawRectangle(static_cast<int>(trackX - 1.0f), static_cast<int>(thumbY), 6, static_cast<int>(thumbH), Color{ 112, 124, 136, 255 });
    }
}

void DrawSceneStatsPanel(Font font, const ModelTab& tab, float panelX, float panelY, float panelW)
{
    const SceneStats stats = CalculateSceneStats(tab.loaded);
    const BoundingBox& bounds = tab.loaded.bounds;
    const Vector3 size = Vector3Subtract(bounds.max, bounds.min);
    const float contentX = panelX + 12.0f;
    float y = panelY;
    char line[256] = {};

    DrawUiText(font, "SCENE STATS", contentX, y, 16.0f, Color{ 165, 182, 196, 255 });
    y += 30.0f;

    std::snprintf(line, sizeof(line), "Nodes: %d", stats.nodes);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Models: %d", stats.meshes);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Bones: %d", stats.bones);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Empties: %d", stats.empties);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 34.0f;

    DrawUiText(font, "GEOMETRY", contentX, y, 16.0f, Color{ 165, 182, 196, 255 });
    y += 30.0f;
    std::snprintf(line, sizeof(line), "Vertices: %d", stats.vertices);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Polygons: %d", stats.polygons);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Render triangles: %d", stats.triangles);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Materials: %d", stats.materials);
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Animations: %zu", tab.loaded.animations.size());
    DrawUiText(font, line, contentX, y, 15.0f, Color{ 205, 213, 220, 255 });
    y += 34.0f;

    DrawUiText(font, "SIZE IN METERS", contentX, y, 16.0f, Color{ 165, 182, 196, 255 });
    y += 30.0f;
    std::snprintf(line, sizeof(line), "Width X:  %.3f m", size.x);
    DrawUiTextClipped(font, line, contentX, y, 15.0f, panelW - 24.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Height Y: %.3f m", size.y);
    DrawUiTextClipped(font, line, contentX, y, 15.0f, panelW - 24.0f, Color{ 205, 213, 220, 255 });
    y += 24.0f;
    std::snprintf(line, sizeof(line), "Depth Z:  %.3f m", size.z);
    DrawUiTextClipped(font, line, contentX, y, 15.0f, panelW - 24.0f, Color{ 205, 213, 220, 255 });
}

void DrawPbrTextureRow(Font font,
                       ModelTab& tab,
                       int materialIndex,
                       PbrTextureSlot slot,
                       float panelX,
                       float panelW,
                       float& y,
                       const std::vector<std::string>& droppedPaths,
                       bool& droppedTextureHandled,
                       TextureClipboard& clipboard,
                       std::string& notice,
                       std::string& error,
                       bool inputBlocked)
{
    const float contentX = panelX + 12.0f;
    EnsurePbrMaterialStates(tab);
    PbrMaterialState& material = tab.pbrMaterials[static_cast<size_t>(materialIndex)];
    const PbrTexture& texture = GetPbrTexture(material, slot);
    const Rectangle thumbnail{ contentX, y - 2.0f, 44.0f, 44.0f };
    const bool thumbnailHovered = CheckCollisionPointRec(GetMousePosition(), thumbnail);
    DrawTextureThumbnail(font, thumbnail, texture, !inputBlocked && thumbnailHovered && !droppedPaths.empty());

    if (!inputBlocked && !droppedTextureHandled && thumbnailHovered)
    {
        for (const std::string& droppedPath : droppedPaths)
        {
            const std::filesystem::path texturePath(droppedPath);
            if (!IsTextureExtension(texturePath)) continue;

            std::string loadError;
            const bool shouldLoadOrm = IsOrmTextureName(ToLower(texturePath.stem().string())) &&
                                       (slot == PbrTextureSlot::AmbientOcclusion ||
                                        slot == PbrTextureSlot::Roughness ||
                                        slot == PbrTextureSlot::Metallic);
            if (shouldLoadOrm)
            {
                EditSnapshot before = CaptureEditSnapshot(tab);
                const int loadedCount = LoadOrmTexture(tab, materialIndex, droppedPath, loadError);
                if (loadedCount > 0)
                {
                    PushUndoSnapshot(tab, std::move(before));
                    notice = "Dropped ORM texture to AO, Roughness, and Metallic: " + droppedPath;
                    error.clear();
                }
                else
                {
                    error = loadError;
                    notice.clear();
                }
            }
            else
            {
                EditSnapshot before = CaptureEditSnapshot(tab);
                if (LoadPbrTexture(tab, materialIndex, slot, droppedPath, loadError))
                {
                    PushUndoSnapshot(tab, std::move(before));
                    notice = std::string("Dropped ") + GetPbrTextureSlotName(slot) + ": " + droppedPath;
                    error.clear();
                }
                else
                {
                    error = loadError;
                    notice.clear();
                }
            }
            droppedTextureHandled = true;
            break;
        }
    }

    if (!inputBlocked && thumbnailHovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_RIGHT))
    {
        clipboard.menuOpen = true;
        clipboard.justOpened = true;
        clipboard.materialIndex = materialIndex;
        clipboard.slot = slot;
        clipboard.position = GetMousePosition();
    }

    char label[128] = {};
    std::snprintf(label, sizeof(label), "%s", GetPbrTextureSlotName(slot));
    DrawUiText(font, label, contentX + 52.0f, y, 15.0f, Color{ 205, 213, 220, 255 });

    float buttonRight = panelX + panelW - 14.0f;
    if (slot == PbrTextureSlot::Roughness || slot == PbrTextureSlot::Metallic || slot == PbrTextureSlot::AmbientOcclusion)
    {
        PackedChannel* channel = slot == PbrTextureSlot::Roughness ? &material.roughnessChannel :
                                 slot == PbrTextureSlot::Metallic ? &material.metallicChannel : &material.aoChannel;
        const Rectangle channelButton{ buttonRight - 38.0f, y - 2.0f, 34.0f, 22.0f };
        if (!inputBlocked && DrawChannelButton(font, channelButton, *channel))
        {
            PushUndoSnapshot(tab);
            *channel = NextPackedChannel(*channel);
        }
        buttonRight -= 44.0f;
    }
    else if (slot == PbrTextureSlot::Normal)
    {
        const Rectangle normalModeButton{ buttonRight - 38.0f, y - 2.0f, 34.0f, 22.0f };
        if (!inputBlocked && DrawNormalModeButton(font, normalModeButton, material.normalDirectX))
        {
            PushUndoSnapshot(tab);
            material.normalDirectX = !material.normalDirectX;
        }
        buttonRight -= 44.0f;
    }
    else if (slot == PbrTextureSlot::Opacity)
    {
        const Rectangle channelButton{ buttonRight - 48.0f, y - 2.0f, 44.0f, 22.0f };
        if (!inputBlocked && DrawPanelButton(font, channelButton, GetOpacityChannelName(material.opacityChannel)))
        {
            PushUndoSnapshot(tab);
            material.opacityChannel = NextOpacityChannel(material.opacityChannel);
        }
        buttonRight -= 54.0f;
    }

    const Rectangle clearButton{ buttonRight - 48.0f, y - 2.0f, 48.0f, 22.0f };
    const Rectangle loadButton{ buttonRight - 102.0f, y - 2.0f, 48.0f, 22.0f };
    if (!inputBlocked && DrawPanelButton(font, loadButton, "Load"))
    {
        const std::string path = OpenTextureFileDialog();
        if (!path.empty())
        {
            EditSnapshot before = CaptureEditSnapshot(tab);
            std::string loadError;
            const bool shouldLoadOrm = IsOrmPackedPbrSlot(slot) && IsOrmTexturePath(path);
            const int loadedCount = shouldLoadOrm ? LoadOrmTexture(tab, materialIndex, path, loadError) : 0;
            if ((shouldLoadOrm && loadedCount > 0) || (!shouldLoadOrm && LoadPbrTexture(tab, materialIndex, slot, path, loadError)))
            {
                PushUndoSnapshot(tab, std::move(before));
                notice = shouldLoadOrm
                    ? "Loaded ORM texture to AO, Roughness, and Metallic: " + path
                    : std::string("Loaded ") + GetPbrTextureSlotName(slot) + ": " + path;
                error.clear();
            }
            else
            {
                error = loadError;
                notice.clear();
            }
        }
    }
    if (!inputBlocked && DrawPanelButton(font, clearButton, "Clear"))
    {
        if (texture.loaded)
        {
            PushUndoSnapshot(tab);
            UnloadPbrTexture(tab, materialIndex, slot);
            notice = std::string("Cleared ") + GetPbrTextureSlotName(slot) + " map.";
            error.clear();
        }
    }

    y += 22.0f;
    DrawUiTextClipped(font, texture.loaded ? GetFileName(texture.path.c_str()) : "No texture", contentX + 52.0f, y, 13.0f, panelW - 80.0f, texture.loaded ? Color{ 160, 205, 230, 255 } : Color{ 120, 130, 140, 255 });
    y += 34.0f;
}

void DrawMaterialsPanel(Font font,
                        ModelTab& tab,
                        float panelX,
                        float panelY,
                        float panelW,
                        const std::vector<std::string>& droppedPaths,
                        bool& droppedTextureHandled,
                        TextureClipboard& clipboard,
                        std::string& notice,
                        std::string& error,
                        bool inputBlocked)
{
    EnsurePbrMaterialStates(tab);
    const float contentX = panelX + 12.0f;
    const int materialCount = static_cast<int>(tab.pbrMaterials.size());
    float y = panelY;

    DrawUiText(font, "MATERIALS", contentX, y, 16.0f, Color{ 165, 182, 196, 255 });
    y += 28.0f;

    for (int i = 0; i < materialCount; ++i)
    {
        const bool selected = i == tab.selectedMaterial;
        const Rectangle row{ contentX, y, panelW - 24.0f, 22.0f };
        const Vector2 mouse = GetMousePosition();
        const bool hovered = CheckCollisionPointRec(mouse, row);
        DrawRectangleRec(row, selected ? Color{ 48, 70, 92, 255 } : hovered ? Color{ 34, 39, 45, 255 } : Color{ 24, 27, 31, 220 });
        const std::string name = i < static_cast<int>(tab.loaded.materialNames.size()) ? tab.loaded.materialNames[static_cast<size_t>(i)] : std::string("Material ") + std::to_string(i + 1);
        DrawUiTextClipped(font, name.c_str(), row.x + 8.0f, row.y + 3.0f, 14.0f, row.width - 16.0f, selected ? RAYWHITE : Color{ 185, 195, 205, 255 });
        if (!inputBlocked && hovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            tab.selectedMaterial = i;
        }
        y += 24.0f;
    }
    const float clearAllW = 74.0f;
    const Rectangle loadFolderButton{ contentX, y, panelW - 30.0f - clearAllW, 24.0f };
    const Rectangle clearAllButton{ contentX + loadFolderButton.width + 6.0f, y, clearAllW, 24.0f };
    if (!inputBlocked && DrawPanelButton(font, loadFolderButton, "Load Textures From Folder"))
    {
        const std::string pickedPath = openfbx::OpenTextureFolderDialog();
        std::string autoloadError;
        EditSnapshot before = CaptureEditSnapshot(tab);
        const int loadedCount = pickedPath.empty() ? 0 : LoadPbrTexturesFromFolder(tab, tab.selectedMaterial, std::filesystem::path(pickedPath), autoloadError);
        if (loadedCount > 0)
        {
            PushUndoSnapshot(tab, std::move(before));
        }
        if (!autoloadError.empty())
        {
            error = autoloadError;
            notice.clear();
        }
        else if (!pickedPath.empty())
        {
            char message[128] = {};
            std::snprintf(message, sizeof(message), "Loaded %d texture%s from folder and subfolders.", loadedCount, loadedCount == 1 ? "" : "s");
            notice = message;
            error.clear();
        }
    }
    if (!inputBlocked && DrawPanelButton(font, clearAllButton, "Clear All"))
    {
        bool hadAnyTexture = false;
        for (int i = 0; i < static_cast<int>(PbrTextureSlot::Count); ++i)
        {
            if (GetPbrTexture(tab.pbrMaterials[static_cast<size_t>(tab.selectedMaterial)], static_cast<PbrTextureSlot>(i)).loaded)
            {
                hadAnyTexture = true;
                break;
            }
        }
        if (hadAnyTexture)
        {
            PushUndoSnapshot(tab);
        }
        for (int i = 0; i < static_cast<int>(PbrTextureSlot::Count); ++i)
        {
            UnloadPbrTexture(tab, tab.selectedMaterial, static_cast<PbrTextureSlot>(i));
        }
        notice = "Cleared all maps for selected material.";
        error.clear();
    }
    y += 34.0f;

    DrawPbrTextureRow(font, tab, tab.selectedMaterial, PbrTextureSlot::Diffuse, panelX, panelW, y, droppedPaths, droppedTextureHandled, clipboard, notice, error, inputBlocked);
    DrawPbrTextureRow(font, tab, tab.selectedMaterial, PbrTextureSlot::Normal, panelX, panelW, y, droppedPaths, droppedTextureHandled, clipboard, notice, error, inputBlocked);

    DrawPbrTextureRow(font, tab, tab.selectedMaterial, PbrTextureSlot::Roughness, panelX, panelW, y, droppedPaths, droppedTextureHandled, clipboard, notice, error, inputBlocked);
    DrawPbrTextureRow(font, tab, tab.selectedMaterial, PbrTextureSlot::Metallic, panelX, panelW, y, droppedPaths, droppedTextureHandled, clipboard, notice, error, inputBlocked);
    DrawPbrTextureRow(font, tab, tab.selectedMaterial, PbrTextureSlot::AmbientOcclusion, panelX, panelW, y, droppedPaths, droppedTextureHandled, clipboard, notice, error, inputBlocked);
    DrawPbrTextureRow(font, tab, tab.selectedMaterial, PbrTextureSlot::Emissive, panelX, panelW, y, droppedPaths, droppedTextureHandled, clipboard, notice, error, inputBlocked);
    DrawPbrTextureRow(font, tab, tab.selectedMaterial, PbrTextureSlot::Opacity, panelX, panelW, y, droppedPaths, droppedTextureHandled, clipboard, notice, error, inputBlocked);
    if (!inputBlocked)
    {
        DrawTextureContextMenu(font, tab, clipboard, notice, error);
    }
}

