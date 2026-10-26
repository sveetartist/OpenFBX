// Transfer maps between UV channels on the unchanged triangle surface.
struct TextureBakeOptions
{
    int resolution = 1024;
    int padding = 8;
    int dilation = 8; // Edge-color extension in pixels; zero disables it.
    int material = -1; // -1: all materials, including untextured geometry
    int sourceUvSet = -1; // -1: current material bindings (Auto per mesh).
    int targetUvSet = -1; // Existing destination channel; never edited by transfer.
    bool repack = false; // Optional atlas builder; the transfer dialog never packs.
    int layoutUvSet = -1; // Prepared layout for the legacy packing path.
    std::filesystem::path parentDirectory;
};

struct BakeSourceImage
{
    int slot = 0, width = 0, height = 0;
    std::vector<Color> pixels;
};
struct BakeUvTransform { double minU = 0, minV = 0, scale = 1; Vector2 origin{}; };
struct BakeNormalFrame
{
    std::array<Vector3,3> normals;
    std::array<Vector4,3> sourceTangents, targetTangents;
};
struct BakeMaterialInput
{
    int material = 0;
    std::string sourceUv, materialName;
    std::vector<UvTriangleSample> source, layout, target;
    std::vector<BakeUvTransform> transforms;
    std::vector<BakeSourceImage> images;
    std::vector<BakeNormalFrame> normalFrames;
    bool directTransfer = false, normalDirectX = false;
};
struct TextureBakeInput
{
    TextureBakeOptions options;
    std::string targetName, folderName, layoutName;
    std::vector<float> targetUvs;
    std::vector<unsigned char> targetPresence;
    std::vector<BakeMaterialInput> materials;
};
struct BakeOutputMap { int material = 0, slot = 0; std::string path; };
struct TextureBakeResult
{
    TextureBakeInput input;
    std::filesystem::path directory;
    std::vector<BakeOutputMap> maps;
    std::string error;
};

double BakeCross(Vector2 a, Vector2 b, Vector2 c)
{
    return (double(b.x) - a.x) * (double(c.y) - a.y) - (double(b.y) - a.y) * (double(c.x) - a.x);
}

bool PackBakeMaterial(BakeMaterialInput& material, TextureBakeInput& input, std::string& error)
{
    const auto& layout = material.layout.empty() ? material.source : material.layout;
    const auto islands = CalculateUvIslandStats(layout,1,1);
    struct Box { int island; double width, height; Vector2 origin{}; };
    std::vector<Box> boxes;
    double maximum = 0;
    for (int i = 0; i < static_cast<int>(islands.size()); ++i)
    {
        const auto& island = islands[i];
        const double w = double(island.maxU)-island.minU, h = double(island.maxV)-island.minV;
        boxes.push_back({i, w, h}); maximum = std::max({maximum, w, h});
    }
    if (boxes.empty() || maximum <= 0) { error = "No valid UV islands to bake."; return false; }
    std::stable_sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) { return a.height > b.height; });
    const double border = double(std::max(input.options.padding, input.options.dilation) + 2) / input.options.resolution;
    auto place = [&](double scale)
    {
        double x = border, y = border, row = 0;
        for (auto& box : boxes)
        {
            const double w = box.width*scale, h = box.height*scale;
            if (x+w+border > 1) { x = border; y += row + 2*border; row = 0; }
            if (x+w+border > 1 || y+h+border > 1) return false;
            box.origin = {float(x), float(y)}; x += w+2*border; row = std::max(row, h);
        }
        return true;
    };
    double low = 0, high = 1/maximum;
    for (int i = 0; i < 48; ++i) { const double middle = (low+high)*.5; if (place(middle)) low = middle; else high = middle; }
    // Leave rounding room when converting to single precision UV attributes.
    low *= 1 - 1e-6;
    if (low <= 0 || !place(low)) { error = "Islands and borders do not fit. Reduce padding/dilation or increase bake resolution."; return false; }
    material.target = layout;
    material.transforms.resize(material.source.size());
    for (const auto& box : boxes)
    {
        const auto& island = islands[box.island];
        for (int t : island.triangles) material.transforms[t] = {island.minU, island.minV, low, box.origin};
        for (int t : island.triangles)
        for (int c = 0; c < 3; ++c)
        {
            const auto source = layout[t].uv[c];
            const Vector2 uv{float(box.origin.x + (double(source.x)-island.minU)*low),
                             float(box.origin.y + (double(source.y)-island.minV)*low)};
            material.target[t].uv[c] = uv;
            const size_t vertex = static_cast<size_t>(material.source[t].vertexStart+c);
            input.targetUvs[vertex*2] = uv.x; input.targetUvs[vertex*2+1] = uv.y;
            input.targetPresence[vertex] = 1;
        }
    }
    if (input.options.layoutUvSet >= 0)
    {
        // Fit one inverse per island using its bounds, avoiding skinny-triangle
        // error. Validate every corner so rotated/distorted or ambiguous source
        // mappings cannot silently produce bad color or tangent-normal maps.
        for (const auto& island : islands)
        {
            double minU = INFINITY, minV = INFINITY, maxU = -INFINITY, maxV = -INFINITY,
                   targetMinU = INFINITY, targetMinV = INFINITY, targetMaxU = -INFINITY, targetMaxV = -INFINITY;
            for (int t : island.triangles) for (int c = 0; c < 3; ++c)
            {
                const auto s = material.source[t].uv[c], target = material.target[t].uv[c];
                minU = std::min(minU,double(s.x)); minV = std::min(minV,double(s.y));
                maxU = std::max(maxU,double(s.x)); maxV = std::max(maxV,double(s.y));
                targetMinU = std::min(targetMinU,double(target.x)); targetMinV = std::min(targetMinV,double(target.y));
                targetMaxU = std::max(targetMaxU,double(target.x)); targetMaxV = std::max(targetMaxV,double(target.y));
            }
            const double scale = maxU-minU >= maxV-minV ? (targetMaxU-targetMinU)/(maxU-minU) : (targetMaxV-targetMinV)/(maxV-minV);
            if (!std::isfinite(scale) || scale <= 0) { error = "The layout UV channel contains collapsed islands. Repair it or choose Auto."; return false; }
            for (int t : island.triangles)
            {
                for (int c = 0; c < 3; ++c)
                {
                    const auto source = material.source[t].uv[c], target = material.target[t].uv[c];
                    const double u = targetMinU+(double(source.x)-minU)*scale, v = targetMinV+(double(source.y)-minV)*scale;
                    if (std::hypot(u-target.x,v-target.y) > 1e-6)
                    { error = "Layout islands do not preserve the material's source UV orientation/seams. Choose a layout derived from those UVs or Auto."; return false; }
                }
                material.transforms[t] = {minU,minV,scale,{float(targetMinU),float(targetMinV)}};
            }
        }
    }
    // Existing intra-island overlaps are unambiguous: their inverse UV map is
    // identical. Boxes of separate islands are disjoint with reserved gutters.
    const bool hasNormal = std::any_of(material.images.begin(), material.images.end(), [](const BakeSourceImage& image) { return image.slot == static_cast<int>(PbrTextureSlot::Normal); });
    if (hasNormal)
        for (size_t t = 0; t < material.target.size(); ++t)
            if (std::abs(BakeCross(material.target[t].uv[0], material.target[t].uv[1], material.target[t].uv[2])) <= 1e-8 ||
                std::abs(BakeCross(material.source[t].uv[0], material.source[t].uv[1], material.source[t].uv[2])) <= 1e-8)
            { error = "A normal-mapped triangle has unstable UV tangents. Enlarge its source UVs or increase bake resolution."; return false; }
    return true;
}

// Positive-area intersection only: shared triangle edges are valid UV coverage.
bool BakeTargetTrianglesOverlap(const UvTriangleSample& a, const UvTriangleSample& b,
                                std::vector<std::array<double,2>>* intersection = nullptr)
{
    struct Point { double x, y; };
    std::array<Point,8> polygon{}, clipped{};
    int count = 3;
    for (int c = 0; c < 3; ++c) polygon[c] = {a.uv[c].x,a.uv[c].y};
    const double sign = BakeCross(b.uv[0],b.uv[1],b.uv[2]) > 0 ? 1 : -1;
    for (int edge = 0; edge < 3 && count; ++edge)
    {
        const auto p = b.uv[edge], q = b.uv[(edge+1)%3];
        auto distance = [&](Point r) { return sign*((double(q.x)-p.x)*(r.y-p.y)-(double(q.y)-p.y)*(r.x-p.x)); };
        int next = 0; auto previous = polygon[count-1]; double dp = distance(previous);
        for (int i = 0; i < count; ++i)
        {
            const auto current = polygon[i]; const double dc = distance(current);
            if ((dp < 0 && dc > 0) || (dp > 0 && dc < 0))
            {
                const double t = dp/(dp-dc);
                clipped[next++] = {previous.x+t*(current.x-previous.x),previous.y+t*(current.y-previous.y)};
            }
            if (dc >= 0) clipped[next++] = current;
            previous = current; dp = dc;
        }
        polygon = clipped; count = next;
    }
    if (count < 3) return false;
    double area = 0;
    for (int i = 1; i+1 < count; ++i)
        area += (polygon[i].x-polygon[0].x)*(polygon[i+1].y-polygon[0].y) -
                (polygon[i].y-polygon[0].y)*(polygon[i+1].x-polygon[0].x);
    const double smaller = std::min(std::abs(BakeCross(a.uv[0],a.uv[1],a.uv[2])),std::abs(BakeCross(b.uv[0],b.uv[1],b.uv[2])));
    const bool overlaps = std::abs(area) > std::max(1e-20,smaller*1e-9);
    if (overlaps && intersection)
        for (int i = 0; i < count; ++i) intersection->push_back({polygon[i].x,polygon[i].y});
    return overlaps;
}

std::array<double,3> BakeBarycentricAt(const UvTriangleSample& triangle, double u, double v);

std::array<double,2> TransferSourceUvAt(const BakeMaterialInput& material, int t, const std::array<double,2>& target)
{
    if (t < static_cast<int>(material.transforms.size()) && material.transforms[t].scale > 0)
    {
        const auto& transform = material.transforms[t];
        return {transform.minU+(target[0]-transform.origin.x)/transform.scale,
                transform.minV+(target[1]-transform.origin.y)/transform.scale};
    }
    const auto weights = BakeBarycentricAt(material.target[t],target[0],target[1]);
    std::array<double,2> uv{};
    for (int c = 0; c < 3; ++c)
    { uv[0] += weights[c]*material.source[t].uv[c].x; uv[1] += weights[c]*material.source[t].uv[c].y; }
    return uv;
}

// Repacking translates and uniformly scales whole islands, including any folds
// already inside them. Keep that shared inverse rather than fitting each skinny
// face independently: overlapping faces then sample exactly the same source.
void FitTransferIslandTransforms(BakeMaterialInput& material)
{
    material.transforms.assign(material.source.size(),BakeUvTransform{0,0,0,{}});
    for (const auto& island : CalculateUvIslandStats(material.target,1,1))
    {
        double minU = INFINITY, minV = INFINITY, maxU = -INFINITY, maxV = -INFINITY;
        for (int t : island.triangles) for (const auto uv : material.source[t].uv)
        { minU = std::min(minU,double(uv.x)); minV = std::min(minV,double(uv.y));
          maxU = std::max(maxU,double(uv.x)); maxV = std::max(maxV,double(uv.y)); }
        const double scale = maxU-minU >= maxV-minV ? (double(island.maxU)-island.minU)/(maxU-minU) :
                                                                    (double(island.maxV)-island.minV)/(maxV-minV);
        if (!std::isfinite(scale) || scale <= 0) continue;
        bool uniform = true;
        for (int t : island.triangles) for (int c = 0; c < 3; ++c)
        {
            const auto source = material.source[t].uv[c], target = material.target[t].uv[c];
            if (std::hypot(island.minU+(double(source.x)-minU)*scale-target.x,
                           island.minV+(double(source.y)-minV)*scale-target.y) > 1e-7) uniform = false;
        }
        if (uniform) for (int t : island.triangles)
            material.transforms[t] = {minU,minV,scale,{island.minU,island.minV}};
    }
}

bool TransferOverlapIsCompatible(const BakeMaterialInput& material, int a, int b,
                                 const std::vector<std::array<double,2>>& intersection, bool hasNormal)
{
    // A positive uniform island transform preserves the normal map's basis even
    // when its faces have different geometry. Other overlapping normal mappings
    // need distinct atlas space to avoid sharing incompatible tangent directions.
    if (hasNormal && (material.transforms[a].scale <= 0 || material.transforms[b].scale <= 0)) return false;
    int dimension = 1;
    for (const auto& image : material.images) dimension = std::max({dimension,image.width,image.height});
    const double tolerance = std::min(1e-7,.001/dimension);
    for (const auto& point : intersection)
    {
        const auto sourceA = TransferSourceUvAt(material,a,point), sourceB = TransferSourceUvAt(material,b,point);
        if (std::abs(sourceA[0]-sourceB[0]) > tolerance || std::abs(sourceA[1]-sourceB[1]) > tolerance) return false;
    }
    return true;
}

bool PrepareTextureTransfer(const ModelTab& tab, BakeMaterialInput& material, std::string& error)
{
    material.target = material.layout;
    FitTransferIslandTransforms(material);
    const bool hasNormal = std::any_of(material.images.begin(),material.images.end(),[](const BakeSourceImage& image) {
        return image.slot == static_cast<int>(PbrTextureSlot::Normal); });
    UvOverlapTree tree;
    tree.triangles.resize(material.target.size());
    for (int i = 0; i < static_cast<int>(tree.triangles.size()); ++i) tree.triangles[i] = i;
    if (!tree.triangles.empty()) BuildUvOverlapTreeNode(tree,material.target,0,static_cast<int>(tree.triangles.size()));
    for (int t = 0; t < static_cast<int>(material.target.size()); ++t)
    {
        const auto bounds = GetUvTriangleBounds(material.target[t]);
        const auto overlaps = [&](auto&& self, int nodeIndex) -> bool
        {
            const auto& node = tree.nodes[nodeIndex];
            if (bounds.maxU <= node.bounds.minU || bounds.minU >= node.bounds.maxU ||
                bounds.maxV <= node.bounds.minV || bounds.minV >= node.bounds.maxV) return false;
            if (node.left >= 0) return self(self,node.left) || self(self,node.right);
            for (int i = node.begin; i < node.end; ++i)
            {
                const int other = tree.triangles[i];
                if (other <= t) continue;
                std::vector<std::array<double,2>> intersection;
                if (BakeTargetTrianglesOverlap(material.target[t],material.target[other],&intersection) &&
                    !TransferOverlapIsCompatible(material,t,other,intersection,hasNormal)) return true;
            }
            return false;
        };
        if (overlaps(overlaps,0))
        { error = "Target UVs overlap with conflicting source samples or normal directions within material " + material.materialName +
                  ". Separate those faces in the target UV set before transferring textures."; return false; }
    }
    if (!hasNormal) return true;
    std::vector<float> positions, normals, sourceUvs, targetUvs, sourceTangents, targetTangents;
    positions.reserve(material.source.size()*9); normals.reserve(material.source.size()*9);
    sourceUvs.reserve(material.source.size()*6); targetUvs.reserve(material.source.size()*6);
    for (size_t t = 0; t < material.source.size(); ++t)
    {
        if (std::abs(BakeCross(material.source[t].uv[0],material.source[t].uv[1],material.source[t].uv[2])) <= 1e-8 ||
            std::abs(BakeCross(material.target[t].uv[0],material.target[t].uv[1],material.target[t].uv[2])) <= 1e-8)
        { error = "A normal-mapped triangle has unstable source or target UV tangents. Enlarge its UVs before transferring textures."; return false; }
        for (int c = 0; c < 3; ++c)
        {
            const auto p = material.source[t].position[c]; positions.insert(positions.end(),{p.x,p.y,p.z});
            const size_t base = size_t(material.source[t].vertexStart+c)*3;
            if (base+2 >= tab.loaded.bindNormals.size()) { error = "Geometry is missing normals for normal-map transfer."; return false; }
            const auto& n = tab.loaded.bindNormals; normals.insert(normals.end(),{n[base],n[base+1],n[base+2]});
            const auto s = material.source[t].uv[c], target = material.target[t].uv[c];
            sourceUvs.insert(sourceUvs.end(),{s.x,s.y}); targetUvs.insert(targetUvs.end(),{target.x,target.y});
        }
    }
    BuildMeshTangents(positions,normals,sourceUvs.data(),sourceTangents);
    BuildMeshTangents(positions,normals,targetUvs.data(),targetTangents);
    material.normalFrames.resize(material.source.size());
    for (size_t t = 0; t < material.source.size(); ++t) for (int c = 0; c < 3; ++c)
    {
        const size_t v = t*3+c;
        material.normalFrames[t].normals[c] = {normals[v*3],normals[v*3+1],normals[v*3+2]};
        material.normalFrames[t].sourceTangents[c] = {sourceTangents[v*4],sourceTangents[v*4+1],sourceTangents[v*4+2],sourceTangents[v*4+3]};
        material.normalFrames[t].targetTangents[c] = {targetTangents[v*4],targetTangents[v*4+1],targetTangents[v*4+2],targetTangents[v*4+3]};
    }
    return true;
}

bool BuildTextureBakeInput(ModelTab& tab, const TextureBakeOptions& options, TextureBakeInput& input, std::string& error)
{
    input = {}; error.clear();
    if (options.resolution < 32 || options.resolution > 4096 ||
        (options.repack && (options.padding < 2 || options.padding > 64 || std::max(options.padding, options.dilation)*4 >= options.resolution)) ||
        options.dilation < 0 || options.dilation > 64)
    { error = "Invalid bake resolution, padding, or dilation."; return false; }
    if (options.material >= static_cast<int>(tab.pbrMaterials.size()) || options.material < -1)
    { error = "Invalid bake material selection."; return false; }
    std::error_code ec;
    if (!std::filesystem::is_directory(options.parentDirectory, ec)) { error = "Choose an existing output folder."; return false; }
    if (tab.loaded.uvSets.empty()) { error = "The model has no source UV channel."; return false; }
    if (options.sourceUvSet < -1 || options.sourceUvSet >= static_cast<int>(tab.loaded.uvSets.size()))
    { error = "The selected source UV channel is unavailable."; return false; }
    if (!options.repack && (options.targetUvSet < 0 || options.targetUvSet >= static_cast<int>(tab.loaded.uvSets.size())))
    { error = "Choose an existing target UV channel."; return false; }
    if (options.layoutUvSet < -1 || options.layoutUvSet >= static_cast<int>(tab.loaded.uvSets.size()))
    { error = "The selected layout UV channel is unavailable."; return false; }
    input.options = options;
    input.layoutName = !options.repack ? tab.loaded.uvSetNames[options.targetUvSet] :
        options.layoutUvSet < 0 ? "Auto (per material)" : tab.loaded.uvSetNames[options.layoutUvSet];
    input.folderName = std::filesystem::path(tab.path).stem().string() + (options.repack ? "_baked" : "_transferred");
    input.targetName = "BakedUV";
    for (int suffix = 2; std::find(tab.loaded.uvSetNames.begin(), tab.loaded.uvSetNames.end(), input.targetName) != tab.loaded.uvSetNames.end(); ++suffix)
        input.targetName = "BakedUV_" + std::to_string(suffix);
    const size_t vertices = tab.loaded.bindVertices.size()/3;
    input.targetUvs.assign(vertices*2, 0); input.targetPresence.assign(vertices, 0);
    if (!options.repack)
    {
        input.targetName = tab.loaded.uvSetNames[options.targetUvSet];
        input.targetUvs = tab.loaded.uvSets[options.targetUvSet];
        if (input.targetUvs.size() != vertices*2) { error = "Target UV channel has missing vertices."; return false; }
        input.targetPresence = options.targetUvSet < static_cast<int>(tab.loaded.uvSetPresence.size()) ?
            tab.loaded.uvSetPresence[options.targetUvSet] : std::vector<unsigned char>(vertices,1);
    }
    const auto vertexMaterials = BuildUvVertexMaterials(tab);
    for (int m = 0; m < static_cast<int>(tab.pbrMaterials.size()); ++m)
    {
        if (options.material >= 0 && options.material != m) continue;
        const int uvIndex = options.sourceUvSet >= 0 ? options.sourceUvSet : GetMaterialUvSetIndex(tab, m);
        if (uvIndex < 0 || uvIndex >= static_cast<int>(tab.loaded.uvSetNames.size())) { error = "A material references a missing source UV channel."; return false; }
        const auto& uvs = tab.loaded.uvSets[uvIndex];
        if (uvs.size() != vertices*2) { error = "Source UV channel has missing vertices."; return false; }
        BakeMaterialInput material; material.material = m;
        material.sourceUv = options.sourceUvSet >= 0 || !tab.pbrMaterials[m].uvSetName.empty() ? tab.loaded.uvSetNames[uvIndex] : "Auto (per mesh)";
        material.directTransfer = !options.repack; material.normalDirectX = tab.pbrMaterials[m].normalDirectX;
        material.materialName = GetMaterialNameForPackage(tab, m);
        for (int node = 0; node < static_cast<int>(tab.loaded.nodes.size()); ++node)
        {
            if (IsDeletedNode(tab, node)) continue;
            std::vector<UvTriangleSample> triangles;
            AppendNodeUvTriangles(tab.loaded, uvs, node, triangles);
            for (auto triangle : triangles)
            {
                const int start = triangle.vertexStart;
                if (start < 0 || start+2 >= static_cast<int>(vertexMaterials.size())) { error = "Invalid triangle vertex mapping."; return false; }
                if (vertexMaterials[start] != m) continue;
                const int sourceSet = options.sourceUvSet >= 0 ? options.sourceUvSet : GetMaterialVertexUvSetIndex(tab, m, start);
                if (sourceSet < 0) { error = "No source UVs on " + tab.loaded.nodes[node].name + "."; return false; }
                const auto& sourceUvs = tab.loaded.uvSets[sourceSet];
                if (sourceUvs.size() != vertices*2) { error = "Incomplete source UV channel."; return false; }
                for (int c = 0; c < 3; ++c)
                {
                    if (vertexMaterials[start+c] != m) { error = "Triangle has inconsistent material assignments."; return false; }
                    const size_t v = static_cast<size_t>(start+c);
                    triangle.uv[c] = GetModelVertexUv(sourceUvs, static_cast<int>(v));
                    const auto uv = triangle.uv[c]; const auto p = triangle.position[c];
                    if (sourceSet < static_cast<int>(tab.loaded.uvSetPresence.size()) &&
                        (v >= tab.loaded.uvSetPresence[sourceSet].size() || !tab.loaded.uvSetPresence[sourceSet][v]))
                    { error = "Missing UVs in " + material.sourceUv + " on " + tab.loaded.nodes[node].name + ". Choose a complete source channel or limit the material scope."; return false; }
                    if (!std::isfinite(uv.x) || !std::isfinite(uv.y) || std::abs(uv.x) > 20000 || std::abs(uv.y) > 20000 ||
                        !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                    { error = "Nonfinite or excessively large UVs/geometry on " + tab.loaded.nodes[node].name + "."; return false; }
                }
                const float geometryArea = TriangleArea3D(triangle.position[0], triangle.position[1], triangle.position[2]);
                if (std::abs(BakeCross(triangle.uv[0], triangle.uv[1], triangle.uv[2])) < 1e-14 ||
                    !std::isfinite(geometryArea) || geometryArea <= 1e-20)
                { error = "Source has collapsed UVs or degenerate geometry. Repair it before baking."; return false; }
                material.source.push_back(triangle);
                auto layoutTriangle = triangle;
                const int layoutSet = options.repack ? options.layoutUvSet : options.targetUvSet;
                if (layoutSet >= 0)
                {
                    bool present = true;
                    const auto& layoutUvs = tab.loaded.uvSets[layoutSet];
                    if (layoutUvs.size() != vertices*2) { error = "Incomplete layout UV channel."; return false; }
                    if (layoutSet < static_cast<int>(tab.loaded.uvSetPresence.size()))
                        for (int c = 0; c < 3; ++c)
                        {
                            const size_t v = static_cast<size_t>(start+c);
                            const auto& presence = tab.loaded.uvSetPresence[layoutSet];
                            present = present && v < presence.size() && presence[v];
                        }
                    if (!present && !options.repack)
                    { error = "Missing target UVs on " + tab.loaded.nodes[node].name + ". Choose a complete target channel or limit the material scope."; return false; }
                    if (present) for (int c = 0; c < 3; ++c)
                    {
                        layoutTriangle.uv[c] = GetModelVertexUv(layoutUvs,start+c);
                        const auto uv = layoutTriangle.uv[c];
                        if (!std::isfinite(uv.x) || !std::isfinite(uv.y) || std::abs(uv.x) > 20000 || std::abs(uv.y) > 20000)
                        { error = "The layout UV channel has invalid coordinates."; return false; }
                        if (!options.repack && (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1))
                        { error = "Target UVs must lie inside the 0-1 tile. Repack a copy before transferring textures."; return false; }
                    }
                    if (std::abs(BakeCross(layoutTriangle.uv[0],layoutTriangle.uv[1],layoutTriangle.uv[2])) < 1e-14)
                    { error = "The layout UV channel contains collapsed faces. Repair it or choose Auto."; return false; }
                }
                material.layout.push_back(layoutTriangle);
            }
        }
        if (material.source.empty()) continue;
        for (int s = 0; s < static_cast<int>(PbrTextureSlot::Count); ++s)
        {
            const auto& texture = tab.pbrMaterials[m].textures[s];
            if (!texture.loaded) continue;
            for (const auto& reference : tab.loaded.textureReferences)
                if (FindMaterialIndexByName(tab, reference.materialName) == m &&
                    ToLowerAscii(AbsoluteNormalizedPath(reference.filePath).string()) == ToLowerAscii(AbsoluteNormalizedPath(texture.path).string()) &&
                    reference.unsupportedTransform)
                { error = "Source texture uses a transform or wrap mode that this baker does not support."; return false; }
            if (texture.texture.format > PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
            { error = "HDR or compressed source textures are not supported by the PNG baker."; return false; }
            Image image = LoadImageFromTexture(texture.texture);
            if (!image.data) { error = "Cannot read source texture: " + texture.path; return false; }
            Color* colors = LoadImageColors(image);
            if (!colors) { UnloadImage(image); error = "Cannot read source pixels."; return false; }
            BakeSourceImage source; source.slot = s; source.width = image.width; source.height = image.height;
            source.pixels.assign(colors, colors+size_t(image.width)*image.height);
            UnloadImageColors(colors); UnloadImage(image);
            material.images.push_back(std::move(source));
        }
        if (options.repack)
        { if (!PackBakeMaterial(material, input, error)) return false; }
        else if (!PrepareTextureTransfer(tab, material, error)) return false;
        input.materials.push_back(std::move(material));
    }
    if (input.materials.empty()) { error = "No geometry in the selected material scope."; return false; }
    return true;
}

std::array<double, 3> BakeBarycentricAt(const UvTriangleSample& triangle, double u, double v)
{
    const double area = BakeCross(triangle.uv[0], triangle.uv[1], triangle.uv[2]);
    const auto cross = [&](Vector2 a, Vector2 b) { return (double(b.x)-a.x)*(v-a.y)-(double(b.y)-a.y)*(u-a.x); };
    const double a = cross(triangle.uv[1],triangle.uv[2])/area;
    const double b = cross(triangle.uv[2],triangle.uv[0])/area;
    return {a, b, 1-a-b};
}

std::array<double,3> BakeBarycentric(const UvTriangleSample& triangle, Vector2 uv)
{ return BakeBarycentricAt(triangle,uv.x,uv.y); }

std::array<double,4> TransferBakeNormal(const BakeMaterialInput& material, int triangle,
                                      const std::array<double,3>& weights, std::array<double,4> color)
{
    const auto& frame = material.normalFrames[triangle];
    Vector3 normal{}, source{}, target{}; double sourceSign = 0, targetSign = 0;
    for (int c = 0; c < 3; ++c)
    {
        const float w = float(weights[c]);
        normal = Vector3Add(normal,Vector3Scale(frame.normals[c],w));
        const auto s = frame.sourceTangents[c], t = frame.targetTangents[c];
        source = Vector3Add(source,Vector3Scale({s.x,s.y,s.z},w));
        target = Vector3Add(target,Vector3Scale({t.x,t.y,t.z},w));
        sourceSign += s.w*weights[c]; targetSign += t.w*weights[c];
    }
    normal = NormalizeOrFallback(normal,{0,1,0});
    source = NormalizeOrFallback(Vector3Subtract(source,Vector3Scale(normal,Vector3DotProduct(normal,source))),ChoosePerpendicular(normal));
    target = NormalizeOrFallback(Vector3Subtract(target,Vector3Scale(normal,Vector3DotProduct(normal,target))),ChoosePerpendicular(normal));
    const auto sourceB = Vector3Scale(Vector3CrossProduct(normal,source),sourceSign < 0 ? -1.0f : 1.0f);
    const auto targetB = Vector3Scale(Vector3CrossProduct(normal,target),targetSign < 0 ? -1.0f : 1.0f);
    const double nx = color[0]/127.5-1, ny = (color[1]/127.5-1)*(material.normalDirectX ? -1 : 1);
    const double tx = nx*Vector3DotProduct(source,target)+ny*Vector3DotProduct(sourceB,target);
    const double ty = nx*Vector3DotProduct(source,targetB)+ny*Vector3DotProduct(sourceB,targetB);
    color[0] = (tx+1)*127.5; color[1] = (ty*(material.normalDirectX ? -1 : 1)+1)*127.5;
    // Tangent frames share the same interpolated normal; Z and alpha stay exact.
    return color;
}

std::array<double, 4> SampleBakeImage(const BakeSourceImage& image, double u, double v)
{
    // Match GL_REPEAT and the viewport's bilinear sampling, including negative UVs.
    const double x = (u-std::floor(u))*image.width-.5, y = (v-std::floor(v))*image.height-.5;
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const double fx = x-ix, fy = y-iy;
    std::array<double, 4> value{};
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
    {
        const int px = ((ix+dx)%image.width+image.width)%image.width, py = ((iy+dy)%image.height+image.height)%image.height;
        const auto color = image.pixels[size_t(py)*image.width+px];
        const double weight = (dx ? fx : 1-fx)*(dy ? fy : 1-fy);
        value[0] += color.r*weight; value[1] += color.g*weight; value[2] += color.b*weight; value[3] += color.a*weight;
    }
    return value;
}

// Clip the triangle to a texel and sample inside their intersection. This covers
// thin/subpixel faces without extrapolating their UVs or treating empty space as black.
bool BakeTriangleTexelSample(const UvTriangleSample& triangle, int x, int y, int resolution,
                             std::array<double, 2>& sample)
{
    struct Point { double x, y; };
    std::array<Point, 8> polygon{}, clipped{};
    int count = 3;
    for (int i = 0; i < count; ++i)
        polygon[i] = {double(triangle.uv[i].x)*resolution, double(triangle.uv[i].y)*resolution};
    for (int plane = 0; plane < 4 && count; ++plane)
    {
        const auto distance = [&](Point p) {
            switch (plane) {
                case 0: return p.x-x;
                case 1: return x+1-p.x;
                case 2: return p.y-y;
                default: return y+1-p.y;
            }
        };
        int next = 0;
        Point previous = polygon[count-1]; double dp = distance(previous);
        for (int i = 0; i < count; ++i)
        {
            const Point current = polygon[i]; const double dc = distance(current);
            // Strict crossings avoid duplicating vertices on a clipping plane.
            if ((dp < 0 && dc > 0) || (dp > 0 && dc < 0))
            {
                const double t = dp/(dp-dc);
                clipped[next++] = {previous.x+t*(current.x-previous.x), previous.y+t*(current.y-previous.y)};
            }
            if (dc >= 0) clipped[next++] = current;
            previous = current; dp = dc;
        }
        polygon = clipped; count = next;
    }
    if (count < 3) return false;
    // Relative coordinates avoid cancellation for very small intersections.
    const Point origin = polygon[0];
    double area = 0, sx = 0, sy = 0;
    for (int i = 0; i < count; ++i)
    {
        const Point a{polygon[i].x-origin.x, polygon[i].y-origin.y};
        const Point b{polygon[(i+1)%count].x-origin.x, polygon[(i+1)%count].y-origin.y};
        area += a.x*b.y-a.y*b.x; sx += a.x; sy += a.y;
    }
    if (area == 0) return false; // Edge-only contact contains no surface to bake.
    sample = {(origin.x+sx/count)/resolution, (origin.y+sy/count)/resolution};
    return true;
}

// Every covered subpixel belongs to an exact target triangle. Uncovered border
// samples do not contribute transparent black. Shared edges are sampled once.
bool RasterBakeMaterial(const BakeMaterialInput& material, int resolution, int dilation,
                        std::vector<std::vector<Color>>& images, std::string& error)
{
    constexpr int samples = 4;
    const size_t pixels = size_t(resolution)*resolution;
    std::vector<int> owners(pixels*samples, -1);
    std::vector<int> fallbackIndices(pixels, -1);
    std::vector<std::array<double, 2>> fallbackSamples;
    for (int t = 0; t < static_cast<int>(material.target.size()); ++t)
    {
        const auto& triangle = material.target[t];
        const double area = BakeCross(triangle.uv[0], triangle.uv[1], triangle.uv[2]);
        if (!std::isfinite(area) || area == 0)
        { error = "A target UV triangle collapsed. Repair its UVs before baking."; return false; }
        const auto bounds = GetUvTriangleBounds(triangle);
        const int minX = ClampInt(int(std::floor(bounds.minU*resolution)), 0, resolution-1),
                  maxX = ClampInt(int(std::floor(bounds.maxU*resolution)), 0, resolution-1),
                  minY = ClampInt(int(std::floor(bounds.minV*resolution)), 0, resolution-1),
                  maxY = ClampInt(int(std::floor(bounds.maxV*resolution)), 0, resolution-1);
        for (int y = minY; y <= maxY; ++y) for (int x = minX; x <= maxX; ++x)
        for (int s = 0; s < samples; ++s)
        {
            const Vector2 uv{(x+.25f+.5f*(s%2))/resolution, (y+.25f+.5f*(s/2))/resolution};
            const auto w = BakeBarycentric(triangle, uv);
            if (w[0] < -1e-10 || w[1] < -1e-10 || w[2] < -1e-10) continue;
            int& owner = owners[(size_t(y)*resolution+x)*samples+s];
            if (owner < 0) owner = t;
        }
    }
    // Keep regular samples wherever they exist. For texels missed by the grid,
    // use a point on the actual surface, then extend these seeds into the gutters.
    for (int t = 0; t < static_cast<int>(material.target.size()); ++t)
    {
        const auto& triangle = material.target[t];
        const auto bounds = GetUvTriangleBounds(triangle);
        const int minX = ClampInt(int(std::floor(bounds.minU*resolution)), 0, resolution-1),
                  maxX = ClampInt(int(std::floor(bounds.maxU*resolution)), 0, resolution-1),
                  minY = ClampInt(int(std::floor(bounds.minV*resolution)), 0, resolution-1),
                  maxY = ClampInt(int(std::floor(bounds.maxV*resolution)), 0, resolution-1);
        for (int y = minY; y <= maxY; ++y) for (int x = minX; x <= maxX; ++x)
        {
            const size_t p = size_t(y)*resolution+x;
            bool covered = false;
            for (int s = 0; s < samples; ++s) covered |= owners[p*samples+s] >= 0;
            if (covered) continue;
            std::array<double, 2> uv{};
            if (!BakeTriangleTexelSample(triangle, x, y, resolution, uv)) continue;
            owners[p*samples] = t;
            fallbackIndices[p] = static_cast<int>(fallbackSamples.size());
            fallbackSamples.push_back(uv);
        }
    }
    // A breadth-first nearest covered pixel extends maps into the reserved gutters.
    // It runs once for all slots, keeping normal/data/color maps aligned.
    std::vector<int> nearest(pixels, -1), queue;
    std::vector<unsigned char> distance(pixels, 255);
    queue.reserve(pixels);
    for (size_t p = 0; p < pixels; ++p)
        for (int s = 0; s < samples; ++s) if (owners[p*samples+s] >= 0)
        { nearest[p] = static_cast<int>(p); distance[p] = 0; queue.push_back(static_cast<int>(p)); break; }
    for (size_t head = 0; head < queue.size(); ++head)
    {
        const int p = queue[head], x = p%resolution, y = p/resolution;
        if (distance[p] >= dilation) continue;
        const int neighbors[8] = {x ? p-1 : -1, x+1<resolution ? p+1 : -1, y ? p-resolution : -1, y+1<resolution ? p+resolution : -1,
            x && y ? p-resolution-1 : -1, x+1<resolution && y ? p-resolution+1 : -1,
            x && y+1<resolution ? p+resolution-1 : -1, x+1<resolution && y+1<resolution ? p+resolution+1 : -1};
        for (int n : neighbors) if (n >= 0 && nearest[n] < 0)
        { nearest[n] = nearest[p]; distance[n] = distance[p]+1; queue.push_back(n); }
    }
    images.clear();
    // One output at a time in the caller would save memory, but retaining these
    // bounded RGBA atlases also permits validation before any file is written.
    for (const auto& sourceImage : material.images)
    {
        std::vector<Color> output(pixels, Color{0,0,0,0});
        for (size_t p = 0; p < pixels; ++p)
        {
            if (distance[p] != 0) continue;
            std::array<double, 4> sum{}; int count = 0;
            for (int s = 0; s < samples; ++s)
            {
                const int t = owners[p*samples+s]; if (t < 0) continue;
                const std::array<double, 2> uv = fallbackIndices[p] >= 0 ? fallbackSamples[fallbackIndices[p]] :
                    std::array<double, 2>{(float(p%resolution)+.25f+.5f*(s%2))/resolution,
                                          (float(p/resolution)+.25f+.5f*(s/2))/resolution};
                // This inverse affine map equals barycentric reprojection for
                // our uniform island packing, avoiding skinny-triangle error.
                double u = 0, v = 0;
                std::array<double,3> weights{};
                if (material.directTransfer)
                {
                    weights = BakeBarycentricAt(material.target[t],uv[0],uv[1]);
                    const auto source = TransferSourceUvAt(material,t,uv);
                    u = source[0]; v = source[1];
                }
                else
                {
                    const auto& transform = material.transforms[t];
                    u = transform.minU + (uv[0]-transform.origin.x)/transform.scale;
                    v = transform.minV + (uv[1]-transform.origin.y)/transform.scale;
                }
                auto color = SampleBakeImage(sourceImage, u, v);
                if (material.directTransfer && sourceImage.slot == static_cast<int>(PbrTextureSlot::Normal) &&
                    material.transforms[t].scale <= 0)
                    color = TransferBakeNormal(material,t,weights,color);
                for (int c = 0; c < 4; ++c) sum[c] += color[c];
                ++count;
            }
            output[p] = {static_cast<unsigned char>(std::clamp(std::lround(sum[0]/count), 0L, 255L)),
                         static_cast<unsigned char>(std::clamp(std::lround(sum[1]/count), 0L, 255L)),
                         static_cast<unsigned char>(std::clamp(std::lround(sum[2]/count), 0L, 255L)),
                         static_cast<unsigned char>(std::clamp(std::lround(sum[3]/count), 0L, 255L))};
        }
        for (size_t p = 0; p < pixels; ++p) if (nearest[p] >= 0 && distance[p] != 0) output[p] = output[nearest[p]];
        images.push_back(std::move(output));
    }
    return true;
}

TextureBakeResult RunTextureBake(TextureBakeInput input, const std::shared_ptr<std::atomic<int>>& progress = {})
{
    TextureBakeResult result; result.input = std::move(input);
    try
    {
        // create_directory is the ownership boundary: never overwrite or clean
        // an existing folder, even if another process wins the name race.
        for (int suffix = 1; ; ++suffix)
        {
            const auto path = result.input.options.parentDirectory /
                (result.input.folderName + (suffix == 1 ? "" : "_" + std::to_string(suffix)));
            std::error_code ec;
            if (std::filesystem::create_directory(path, ec)) { result.directory = std::filesystem::absolute(path); break; }
            if (ec && ec != std::errc::file_exists) throw std::runtime_error("Cannot create bake output folder: " + ec.message());
        }
        int completed = 0;
        std::ofstream manifest(result.directory / "bake.txt");
        if (!manifest) throw std::runtime_error("Cannot write bake manifest.");
        manifest << "Operation: " << (result.input.options.repack ? "Repack and bake" : "Transfer textures (UVs unchanged)")
                 << "\nTarget UV: " << result.input.targetName << "\nLayout UV: " << result.input.layoutName << "\nResolution: " << result.input.options.resolution;
        if (result.input.options.repack) manifest << "\nPadding: " << result.input.options.padding;
        manifest << "\nDilation: " << result.input.options.dilation
                 << "\nSampling: 2x2 with conservative coverage; bilinear repeat; same-triangle correspondence\n";
        for (auto& material : result.input.materials)
        {
            std::vector<std::vector<Color>> images;
            if (!RasterBakeMaterial(material, result.input.options.resolution, result.input.options.dilation, images, result.error))
                throw std::runtime_error(result.error);
            manifest << "Material " << material.material << " (" << material.materialName << "): " << material.sourceUv << " -> " << result.input.targetName << '\n';
            for (size_t i = 0; i < images.size(); ++i)
            {
                const auto filename = "material_" + std::to_string(material.material) + "_" +
                    ToLowerAscii(GetPbrTextureSlotName(static_cast<PbrTextureSlot>(material.images[i].slot))) + ".png";
                const auto path = result.directory / filename;
                Image image{images[i].data(), result.input.options.resolution, result.input.options.resolution, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                if (!ExportImage(image, path.string().c_str())) throw std::runtime_error("Cannot write baked texture: " + path.string());
                result.maps.push_back({material.material, material.images[i].slot, path.string()});
                manifest << "  " << filename << '\n';
            }
            // Source CPU images can be large; retain just the plan for application.
            material.images.clear();
            if (progress) progress->store(++completed*100/static_cast<int>(result.input.materials.size()));
        }
        manifest.close();
        if (!manifest) throw std::runtime_error("Cannot finish bake manifest.");
    }
    catch (const std::exception& e)
    {
        result.error = e.what();
        if (!result.directory.empty()) { std::error_code ec; std::filesystem::remove_all(result.directory, ec); }
        result.directory.clear(); result.maps.clear();
    }
    return result;
}

bool ApplyTextureBake(ModelTab& tab, TextureBakeResult& result, std::string& error)
{
    if (!result.error.empty()) { error = result.error; return false; }
    const int target = result.input.options.targetUvSet;
    if (!result.input.options.repack && (target < 0 || target >= static_cast<int>(tab.loaded.uvSets.size()) ||
        target >= static_cast<int>(tab.loaded.uvSetNames.size()) || tab.loaded.uvSetNames[target] != result.input.targetName ||
        tab.loaded.uvSets[target] != result.input.targetUvs))
    { error = "The target UV set changed during transfer. Run the transfer again."; return false; }
    std::vector<PbrTexture> staged;
    for (const auto& map : result.maps)
    {
        Texture2D texture = LoadTexture(map.path.c_str());
        if (!texture.id)
        {
            error = "Cannot load baked texture: " + map.path;
            std::error_code ec; std::filesystem::remove_all(result.directory, ec);
            result.directory.clear(); return false;
        }
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        PbrTexture pbr; pbr.texture = texture; pbr.loaded = true; pbr.path = map.path;
        pbr.resource = std::shared_ptr<Texture2D>(new Texture2D(texture), [](Texture2D* resource) { if (IsWindowReady()) UnloadTexture(*resource); delete resource; });
        staged.push_back(std::move(pbr));
    }
    // Allocate the replacement scene data before touching the live scene.
    auto newUvs = tab.loaded.uvSets;
    auto newPresence = tab.loaded.uvSetPresence;
    auto newNames = tab.loaded.uvSetNames;
    auto newMaterials = tab.pbrMaterials;
    if (result.input.options.repack)
    {
        newUvs.push_back(result.input.targetUvs);
        newPresence.push_back(result.input.targetPresence);
        newNames.push_back(result.input.targetName);
    }
    for (const auto& material : result.input.materials) newMaterials[material.material].uvSetName = result.input.targetName;
    for (size_t i = 0; i < result.maps.size(); ++i)
    {
        const auto& map = result.maps[i];
        newMaterials[map.material].textures[map.slot] = std::move(staged[i]);
    }
    PushUndoSnapshot(tab);
    tab.loaded.uvSets.swap(newUvs);
    tab.loaded.uvSetPresence.swap(newPresence);
    tab.loaded.uvSetNames.swap(newNames);
    tab.pbrMaterials.swap(newMaterials);
    for (const auto& map : result.maps)
    {
        ApplyPbrTextureToModel(tab, map.material, static_cast<PbrTextureSlot>(map.slot));
    }
    if (result.input.options.repack) tab.loaded.uvSetsEdited = true;
    tab.selectedUvSet = result.input.options.repack ? static_cast<int>(tab.loaded.uvSets.size())-1 : target;
    tab.selectedUvIslands.clear(); tab.viewportUvIslandCache = {};
    RefreshUvMeshBuffers(tab); RefreshDisplayedMesh(tab); SyncMaterialSettings(tab);
    error.clear(); return true;
}

struct TextureBakeDialog
{
    bool open = false;
    bool justOpened = false;
    ModelTab* tab = nullptr;
    TextureBakeOptions options;
    bool adjustingDilation = false;
    Rectangle bounds{};
    bool dragging = false;
    Vector2 dragOffset{};
    std::future<TextureBakeResult> job;
    std::shared_ptr<std::atomic<int>> progress;
    std::string error;
};
TextureBakeDialog gTextureBakeDialog;

Rectangle GetTextureBakeBounds()
{
    auto& bounds = gTextureBakeDialog.bounds;
    bounds.width = std::min(620.0f,float(GetScreenWidth())-24);
    bounds.height = std::min(464.0f,float(GetScreenHeight())-24);
    bounds.x = ClampFloat(bounds.x,12,std::max(12.0f,float(GetScreenWidth())-bounds.width-12));
    bounds.y = ClampFloat(bounds.y,12,std::max(12.0f,float(GetScreenHeight())-bounds.height-12));
    return bounds;
}

void OpenTextureBakeDialog(ModelTab& tab)
{
    if (gTextureBakeDialog.open) return;
    gTextureBakeDialog.open = true; gTextureBakeDialog.tab = &tab; gTextureBakeDialog.options = {};
    gTextureBakeDialog.justOpened = true;
    gTextureBakeDialog.error.clear();
    gTextureBakeDialog.adjustingDilation = false;
    gTextureBakeDialog.dragging = false;
    const float width = std::min(620.0f,float(GetScreenWidth())-24);
    const float height = std::min(464.0f,float(GetScreenHeight())-24);
    gTextureBakeDialog.bounds = {(GetScreenWidth()-width)/2,(GetScreenHeight()-height)/2,width,height};
    gTextureBakeDialog.options.targetUvSet = tab.selectedUvSet;
    gTextureBakeDialog.options.parentDirectory = tab.path.empty() ? std::filesystem::current_path() : AbsoluteNormalizedPath(tab.path).parent_path();
}

void DrawTextureBakeDialog(Font font, std::string& notice, std::string& error)
{
    auto& dialog = gTextureBakeDialog;
    if (!dialog.open) return;
    openfbx::SetUiPointerBlocked(dialog.justOpened);
    dialog.justOpened = false;
    if (dialog.job.valid() && dialog.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        auto result = dialog.job.get();
        if (ApplyTextureBake(*dialog.tab, result, dialog.error))
        {
            notice = "Transferred textures to " + result.input.targetName + " in " + result.directory.string() + ". Save the FBX to keep material bindings and texture paths.";
            error.clear(); dialog.open = false; dialog.tab = nullptr; return;
        }
        error = dialog.error;
    }
    const bool running = dialog.job.valid();
    const Vector2 mouse = GetMousePosition();
    Rectangle panel = GetTextureBakeBounds();
    const Rectangle close{panel.x+panel.width-32,panel.y+6,24,24};
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) dialog.dragging = false;
    if (openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(mouse,close))
        {
            if (!running) { dialog.open = false; dialog.tab = nullptr; return; }
        }
        else if (CheckCollisionPointRec(mouse,{panel.x,panel.y,panel.width,38}))
        { dialog.dragging = true; dialog.dragOffset = {mouse.x-panel.x,mouse.y-panel.y}; }
    }
    if (dialog.dragging)
    {
        dialog.bounds.x = mouse.x-dialog.dragOffset.x; dialog.bounds.y = mouse.y-dialog.dragOffset.y;
        panel = GetTextureBakeBounds();
    }
    const float width = panel.width, height = panel.height;
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0,0,0,100});
    DrawRectangleRec({panel.x+4,panel.y+4,width,height},Color{0,0,0,110});
    DrawRectangleRec(panel, Color{27,31,37,255}); DrawRectangleLinesEx(panel, 1, Color{80,100,120,255});
    DrawRectangleRec({panel.x+1,panel.y+1,width-2,37},Color{42,50,59,255});
    DrawUiText(font,"TRANSFER TEXTURES",panel.x+14,panel.y+10,18,RAYWHITE);
    DrawUiText(font,"X",panel.x+width-25,panel.y+10,18,running ? DARKGRAY : LIGHTGRAY);
    const float x = panel.x+20, w = width-40; float y = panel.y+52;
    DrawUiText(font, "Bake maps from source UVs into the target UVs without repacking.", x, y, 14, LIGHTGRAY); y += 22;
    DrawUiText(font, "Both UV sets stay unchanged. Materials will use the target UVs.", x, y, 14, LIGHTGRAY); y += 22;
    auto button = [&](const char* text) { const bool clicked = DrawPanelButton(font, {x,y,w,26}, text); y += 32; return clicked && !running; };
    if (button(dialog.options.material < 0 ? "Scope: all materials" : "Scope: selected material"))
        dialog.options.material = dialog.options.material < 0 ? dialog.tab->selectedMaterial : -1;
    const std::string sourceLabel = "Source UV: " + (dialog.options.sourceUvSet < 0 ? std::string("Auto (material UVs)") :
        dialog.tab->loaded.uvSetNames[dialog.options.sourceUvSet]);
    if (button(sourceLabel.c_str()))
    {
        ++dialog.options.sourceUvSet;
        if (dialog.options.sourceUvSet >= static_cast<int>(dialog.tab->loaded.uvSets.size())) dialog.options.sourceUvSet = -1;
    }
    const std::string targetLabel = "Target UV: " + dialog.tab->loaded.uvSetNames[dialog.options.targetUvSet];
    if (button(targetLabel.c_str()))
        dialog.options.targetUvSet = (dialog.options.targetUvSet+1)%static_cast<int>(dialog.tab->loaded.uvSets.size());
    const std::string resolution = "Resolution: " + std::to_string(dialog.options.resolution) + " x " + std::to_string(dialog.options.resolution);
    if (button(resolution.c_str())) dialog.options.resolution = dialog.options.resolution == 4096 ? 512 : dialog.options.resolution*2;
    const Rectangle dilationHit{x,y+20,w,24};
    const bool dilationHovered = CheckCollisionPointRec(mouse,dilationHit);
    if (running || !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) dialog.adjustingDilation = false;
    if (!running && dilationHovered && openfbx::UiMouseButtonPressed(MOUSE_BUTTON_LEFT)) dialog.adjustingDilation = true;
    if (dialog.adjustingDilation)
        dialog.options.dilation = ClampInt(static_cast<int>(std::lround(64*ClampFloat((mouse.x-x-6)/(w-12),0,1))),0,64);
    const std::string dilation = dialog.options.dilation == 0 ? "Texture dilation: Off" :
        "Texture dilation: " + std::to_string(dialog.options.dilation) + " pixels";
    DrawUiText(font,dilation.c_str(),x,y,14,LIGHTGRAY);
    const float amount = dialog.options.dilation/64.0f;
    const Color accent = running ? Color{70,80,90,255} :
        dilationHovered || dialog.adjustingDilation ? Color{125,190,240,255} : Color{85,150,205,255};
    DrawRectangleRec({x+6,y+31,w-12,4},Color{55,64,74,255});
    DrawRectangleRec({x+6,y+31,(w-12)*amount,4},accent);
    DrawRectangleRec({x+1+(w-12)*amount,y+25,10,16},accent); y += 48;
    DrawUiText(font,"0 disables dilation. Use target islands with room for edge extension.",x,y,14,LIGHTGRAY); y += 22;
    if (button("Choose output parent folder..."))
    {
        const std::string path = openfbx::OpenTextureFolderDialog();
        if (!path.empty()) dialog.options.parentDirectory = path;
    }
    DrawUiTextClipped(font, dialog.options.parentDirectory.string().c_str(), x, y, 14, w, LIGHTGRAY); y += 23;
    DrawUiText(font, "A new numbered folder is created; existing files are preserved.", x, y, 14, LIGHTGRAY); y += 25;
    if (!dialog.error.empty())
    {
        size_t split = dialog.error.size();
        while (MeasureTextEx(font, dialog.error.substr(0,split).c_str(), 14, 1).x > w)
        {
            const size_t space = dialog.error.rfind(' ', split-1);
            if (space == std::string::npos || space == 0) break;
            split = space;
        }
        DrawUiTextClipped(font, dialog.error.substr(0,split).c_str(), x, y, 14, w, Color{255,130,120,255});
        if (split < dialog.error.size()) DrawUiTextClipped(font, dialog.error.substr(split+1).c_str(), x, y+18, 14, w, Color{255,130,120,255});
    }
    if (running)
    {
        const std::string status = "Transferring... " + std::to_string(dialog.progress->load()) + "%";
        DrawUiText(font, status.c_str(), x, panel.y+height-40, 16, RAYWHITE);
    }
    else
    {
        if (DrawPanelButton(font, {x,panel.y+height-42,w*.65f,26}, "Transfer Textures"))
        {
            TextureBakeInput input;
            if (BuildTextureBakeInput(*dialog.tab, dialog.options, input, dialog.error))
            {
                dialog.progress = std::make_shared<std::atomic<int>>(0);
                const auto progress = dialog.progress;
                dialog.job = std::async(std::launch::async, [input = std::move(input), progress]() mutable { return RunTextureBake(std::move(input), progress); });
            }
            else error = dialog.error;
        }
        if (DrawPanelButton(font, {x+w*.68f,panel.y+height-42,w*.32f,26}, "Cancel") || IsKeyPressed(KEY_ESCAPE))
        { dialog.open = false; dialog.tab = nullptr; }
    }
}
