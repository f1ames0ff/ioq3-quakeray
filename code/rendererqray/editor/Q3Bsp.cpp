#include "Q3Bsp.h"

#include "Q3Shader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace
{

constexpr int32_t kQ3BspVersion = 46;
constexpr int32_t kQ3NumLumps = 17;
constexpr size_t kQ3HeaderBytes = 8 + kQ3NumLumps * 8;

constexpr int kLumpEntities = 0;
constexpr int kLumpShaders = 1;
constexpr int kLumpPlanes = 2;
constexpr int kLumpNodes = 3;
constexpr int kLumpLeafs = 4;
constexpr int kLumpModels = 7;
constexpr int kLumpDrawVerts = 10;
constexpr int kLumpDrawIndexes = 11;
constexpr int kLumpSurfaces = 13;
constexpr int kLumpVisibility = 16;

constexpr int32_t kMstPlanar = 1;
constexpr int32_t kMstPatch = 2;
constexpr int32_t kMstTriangleSoup = 3;

constexpr int32_t kSurfSky = 0x4;
constexpr int32_t kSurfNoImpact = 0x10;
constexpr int32_t kSurfNoMarks = 0x20;
constexpr int32_t kSurfNoDraw = 0x80;

constexpr int32_t kContentsLava = 8;
constexpr int32_t kContentsSlime = 16;
constexpr int32_t kContentsWater = 32;
constexpr int32_t kContentsFog = 64;

constexpr int32_t kBspContentsEmpty = -1;
constexpr int32_t kBspContentsSolid = -2;
constexpr uint32_t kMaxLightClusters = 8192;
constexpr int kPatchTessLevel = 8;
constexpr int kPatchFaceStride = 4;

#pragma pack(push, 1)
struct Q3Lump
{
    int32_t fileofs;
    int32_t filelen;
};

struct Q3DShader
{
    char shader[64];
    int32_t surfaceFlags;
    int32_t contentFlags;
};

struct Q3DPlane
{
    float normal[3];
    float dist;
};

struct Q3DNode
{
    int32_t planeNum;
    int32_t children[2];
    int32_t mins[3];
    int32_t maxs[3];
};

struct Q3DLeaf
{
    int32_t cluster;
    int32_t area;
    int32_t mins[3];
    int32_t maxs[3];
    int32_t firstLeafSurface;
    int32_t numLeafSurfaces;
    int32_t firstLeafBrush;
    int32_t numLeafBrushes;
};

struct Q3DModel
{
    float mins[3];
    float maxs[3];
    int32_t firstSurface;
    int32_t numSurfaces;
    int32_t firstBrush;
    int32_t numBrushes;
};

struct Q3DrawVert
{
    float xyz[3];
    float st[2];
    float lightmap[2];
    float normal[3];
    uint8_t color[4];
};

struct Q3DSurface
{
    int32_t shaderNum;
    int32_t fogNum;
    int32_t surfaceType;
    int32_t firstVert;
    int32_t numVerts;
    int32_t firstIndex;
    int32_t numIndexes;
    int32_t lightmapNum;
    int32_t lightmapX;
    int32_t lightmapY;
    int32_t lightmapWidth;
    int32_t lightmapHeight;
    float lightmapOrigin[3];
    float lightmapVecs[3][3];
    int32_t patchWidth;
    int32_t patchHeight;
};
#pragma pack(pop)

static_assert(sizeof(Q3DShader) == 72, "unexpected Q3DShader size");
static_assert(sizeof(Q3DPlane) == 16, "unexpected Q3DPlane size");
static_assert(sizeof(Q3DNode) == 36, "unexpected Q3DNode size");
static_assert(sizeof(Q3DLeaf) == 48, "unexpected Q3DLeaf size");
static_assert(sizeof(Q3DModel) == 40, "unexpected Q3DModel size");
static_assert(sizeof(Q3DrawVert) == 44, "unexpected Q3DrawVert size");
static_assert(sizeof(Q3DSurface) == 104, "unexpected Q3DSurface size");

struct Q3BspDataView
{
    const char *entities = nullptr;
    const Q3DShader *shaders = nullptr;
    const Q3DPlane *planes = nullptr;
    const Q3DNode *nodes = nullptr;
    const Q3DLeaf *leafs = nullptr;
    const Q3DModel *models = nullptr;
    const Q3DrawVert *verts = nullptr;
    const int32_t *indices = nullptr;
    const Q3DSurface *surfaces = nullptr;
    const uint8_t *visibility = nullptr;
    size_t entityBytes = 0;
    size_t visibilityBytes = 0;
    int shaderCount = 0;
    int planeCount = 0;
    int nodeCount = 0;
    int leafCount = 0;
    int modelCount = 0;
    int vertCount = 0;
    int indexCount = 0;
    int surfaceCount = 0;
    Q3Lump lumps[kQ3NumLumps] = {};
};

template <typename T>
bool SliceLump(const std::vector<uint8_t> &bytes, const Q3Lump &lump, const T *&outPtr,
               int &outCount)
{
    outPtr = nullptr;
    outCount = 0;
    if (lump.fileofs < 0 || lump.filelen < 0)
    {
        return false;
    }

    const size_t offset = static_cast<size_t>(lump.fileofs);
    const size_t length = static_cast<size_t>(lump.filelen);
    if (offset > bytes.size() || length > bytes.size() - offset || (length % sizeof(T)) != 0)
    {
        return false;
    }

    outPtr = reinterpret_cast<const T *>(bytes.data() + offset);
    outCount = static_cast<int>(length / sizeof(T));
    return true;
}

bool ValidateCoreGeometryLumps(const std::vector<uint8_t> &bytes, Q3BspDataView &view)
{
    return SliceLump(bytes, view.lumps[kLumpShaders], view.shaders, view.shaderCount) &&
           SliceLump(bytes, view.lumps[kLumpPlanes], view.planes, view.planeCount) &&
           SliceLump(bytes, view.lumps[kLumpNodes], view.nodes, view.nodeCount) &&
           SliceLump(bytes, view.lumps[kLumpLeafs], view.leafs, view.leafCount);
}

bool ValidateMeshAndVisibilityLumps(const std::vector<uint8_t> &bytes, Q3BspDataView &view)
{
    if (!SliceLump(bytes, view.lumps[kLumpModels], view.models, view.modelCount) ||
        !SliceLump(bytes, view.lumps[kLumpDrawVerts], view.verts, view.vertCount) ||
        !SliceLump(bytes, view.lumps[kLumpDrawIndexes], view.indices, view.indexCount) ||
        !SliceLump(bytes, view.lumps[kLumpSurfaces], view.surfaces, view.surfaceCount))
    {
        return false;
    }

    const Q3Lump &visLump = view.lumps[kLumpVisibility];
    if (visLump.fileofs >= 0 && visLump.filelen >= 8 &&
        size_t(visLump.fileofs) <= bytes.size() &&
        size_t(visLump.filelen) <= bytes.size() - size_t(visLump.fileofs))
    {
        view.visibility = bytes.data() + visLump.fileofs;
        view.visibilityBytes = static_cast<size_t>(visLump.filelen);
    }
    return true;
}

bool ParseQ3BspView(const std::vector<uint8_t> &bytes, Q3BspDataView &view, std::string &error)
{
    if (!IsQuake3BspMemory(bytes) || bytes.size() < kQ3HeaderBytes)
    {
        error = "invalid IBSP v46 header";
        return false;
    }

    std::memcpy(view.lumps, bytes.data() + 8, sizeof(view.lumps));

    const Q3Lump &entLump = view.lumps[kLumpEntities];
    if (entLump.fileofs >= 0 && entLump.filelen > 0 &&
        size_t(entLump.fileofs) <= bytes.size() &&
        size_t(entLump.filelen) <= bytes.size() - size_t(entLump.fileofs))
    {
        view.entities = reinterpret_cast<const char *>(bytes.data() + entLump.fileofs);
        view.entityBytes = static_cast<size_t>(entLump.filelen);
    }

    if (!ValidateCoreGeometryLumps(bytes, view) || !ValidateMeshAndVisibilityLumps(bytes, view))
    {
        error = "corrupt Q3 BSP lump bounds";
        return false;
    }

    return true;
}

void NormalizeVec3(float v[3])
{
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1.0e-6f)
    {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
    else
    {
        v[0] = 0.0f;
        v[1] = 0.0f;
        v[2] = 1.0f;
    }
}

void TriangleCross(const float a[3], const float b[3], const float c[3], float out[3])
{
    const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    const float e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    out[0] = e1[1] * e2[2] - e1[2] * e2[1];
    out[1] = e1[2] * e2[0] - e1[0] * e2[2];
    out[2] = e1[0] * e2[1] - e1[1] * e2[0];
}

struct Q3BspClusterState
{
    uint32_t clusterCount = 1;
    std::vector<uint8_t> bounded;
    std::vector<uint8_t> skyFlags;
};

uint32_t ComputeClusterCount(const Q3BspDataView &view)
{
    int maxCluster = -1;
    for (int i = 0; i < view.leafCount; i++)
    {
        maxCluster = std::max(maxCluster, view.leafs[i].cluster);
    }

    uint32_t count = static_cast<uint32_t>(std::max(1, maxCluster + 2));
    if (view.visibility != nullptr && view.visibilityBytes >= 8)
    {
        int32_t visClusters = 0;
        std::memcpy(&visClusters, view.visibility, 4);
        if (visClusters > 0)
        {
            count = std::max(count, uint32_t(visClusters) + 1u);
        }
    }
    return std::min(count, kMaxLightClusters);
}

void BuildQ3ClusterTree(const Q3BspDataView &view, EditorScene &scene, Q3BspClusterState &state)
{
    EditorWorldClusters &clusters = scene.worldClusters;
    clusters = EditorWorldClusters{};
    scene.nodes.clear();

    if (view.nodeCount <= 0 || view.leafCount <= 0 || view.planeCount <= 0)
    {
        return;
    }

    state.clusterCount = ComputeClusterCount(view);
    scene.nodes.resize(static_cast<size_t>(view.nodeCount));
    for (int i = 0; i < view.nodeCount; i++)
    {
        const Q3DNode &src = view.nodes[i];
        EditorBspNode &dst = scene.nodes[static_cast<size_t>(i)];
        if (src.planeNum >= 0 && src.planeNum < view.planeCount)
        {
            const Q3DPlane &p = view.planes[src.planeNum];
            std::memcpy(dst.normal, p.normal, sizeof(dst.normal));
            dst.distance = p.dist;
        }
        dst.children[0] = src.children[0] >= 0 ? src.children[0] : (src.children[0] - 1);
        dst.children[1] = src.children[1] >= 0 ? src.children[1] : (src.children[1] - 1);
    }

    const size_t totalLeafs = static_cast<size_t>(view.leafCount) + 1u;
    clusters.numLeafs = static_cast<uint32_t>(totalLeafs);
    clusters.contents.assign(totalLeafs, kBspContentsSolid);
    clusters.leafCluster.assign(totalLeafs, 0u);
    for (int i = 0; i < view.leafCount; i++)
    {
        const int32_t rawCluster = view.leafs[i].cluster;
        const size_t leafSlot = static_cast<size_t>(i) + 1u;
        if (rawCluster >= 0 && uint32_t(rawCluster) + 1u < state.clusterCount)
        {
            clusters.contents[leafSlot] = kBspContentsEmpty;
            clusters.leafCluster[leafSlot] = static_cast<uint32_t>(rawCluster) + 1u;
        }
    }

    clusters.mins.assign(state.clusterCount, QrFloat3D{ { 1e30f, 1e30f, 1e30f } });
    clusters.maxs.assign(state.clusterCount, QrFloat3D{ { -1e30f, -1e30f, -1e30f } });
    clusters.flags.assign(state.clusterCount, 0u);
    state.bounded.assign(state.clusterCount, 0u);
    state.skyFlags.assign(state.clusterCount, 0u);
}

uint32_t WalkQ3LeafAtPoint(const EditorScene &scene, const float point[3])
{
    if (scene.nodes.empty())
    {
        return 0u;
    }

    int32_t nodeIdx = 0;
    for (size_t step = 0; step <= scene.nodes.size(); step++)
    {
        if (nodeIdx < 0 || size_t(nodeIdx) >= scene.nodes.size())
        {
            return 0u;
        }

        const EditorBspNode &node = scene.nodes[size_t(nodeIdx)];
        const float dist = point[0] * node.normal[0] + point[1] * node.normal[1] +
                           point[2] * node.normal[2] - node.distance;
        const int32_t child = dist >= 0.0f ? node.children[0] : node.children[1];
        if (child >= 0)
        {
            nodeIdx = child;
            continue;
        }

        const int32_t leafIdx = -(child + 1);
        if (leafIdx <= 0 || size_t(leafIdx) >= scene.worldClusters.leafCluster.size())
        {
            return 0u;
        }
        return scene.worldClusters.leafCluster[size_t(leafIdx)];
    }

    return 0u;
}

uint32_t ClusterForQ3Point(const EditorScene &scene, const float point[3])
{
    uint32_t cluster = WalkQ3LeafAtPoint(scene, point);
    if (cluster > 0u)
    {
        return cluster;
    }

    static constexpr float kProbes[] = { 0.25f, 1.0f, 4.0f, 16.0f, 48.0f, 128.0f };
    for (float offset : kProbes)
    {
        for (int axis = 0; axis < 3; axis++)
        {
            for (int sign = -1; sign <= 1; sign += 2)
            {
                float probed[3] = { point[0], point[1], point[2] };
                probed[axis] += offset * static_cast<float>(sign);
                cluster = WalkQ3LeafAtPoint(scene, probed);
                if (cluster > 0u)
                {
                    return cluster;
                }
            }
        }
    }

    static constexpr float kDiagOffsets[] = { 16.0f, 48.0f, 96.0f };
    for (float d : kDiagOffsets)
    {
        for (int sx = -1; sx <= 1; sx += 2)
        {
            for (int sy = -1; sy <= 1; sy += 2)
            {
                for (int sz = -1; sz <= 1; sz += 2)
                {
                    const float probed[3] = {
                        point[0] + d * float(sx),
                        point[1] + d * float(sy),
                        point[2] + d * float(sz),
                    };
                    cluster = WalkQ3LeafAtPoint(scene, probed);
                    if (cluster > 0u)
                    {
                        return cluster;
                    }
                }
            }
        }
    }

    return 0u;
}

void AccumulateClusterPoint(EditorScene &scene, Q3BspClusterState &state, uint32_t cluster,
                            const float pos[3])
{
    if (cluster >= state.clusterCount)
    {
        return;
    }

    QrFloat3D &cmin = scene.worldClusters.mins[cluster];
    QrFloat3D &cmax = scene.worldClusters.maxs[cluster];
    if (state.bounded[cluster] == 0u)
    {
        state.bounded[cluster] = 1u;
        for (int axis = 0; axis < 3; axis++)
        {
            cmin.data[axis] = pos[axis];
            cmax.data[axis] = pos[axis];
        }
        return;
    }

    for (int axis = 0; axis < 3; axis++)
    {
        cmin.data[axis] = std::min(cmin.data[axis], pos[axis]);
        cmax.data[axis] = std::max(cmax.data[axis], pos[axis]);
    }
}

void OrSkyVisRow(const uint8_t *source, uint32_t visRowBytes, uint32_t rowBytes,
                 std::vector<uint8_t> &skyVis)
{
    for (uint32_t b = 0; b < rowBytes; b++)
    {
        uint32_t bits = 0;
        if (b < visRowBytes)
        {
            bits |= uint32_t(source[b]) << 1u;
        }
        if (b > 0 && b - 1u < visRowBytes)
        {
            bits |= uint32_t(source[b - 1u]) >> 7u;
        }
        skyVis[b] |= uint8_t(bits & 0xFFu);
    }
}

void EncodeRleVisRow(const std::vector<uint8_t> &row, std::vector<uint8_t> &visData)
{
    const uint32_t rowBytes = uint32_t(row.size());
    for (uint32_t b = 0; b < rowBytes;)
    {
        if (row[b] != 0u)
        {
            visData.push_back(row[b]);
            b++;
            continue;
        }
        uint32_t run = 0;
        while (b < rowBytes && row[b] == 0u && run < 255u)
        {
            b++;
            run++;
        }
        visData.push_back(0u);
        visData.push_back(uint8_t(run));
    }
}

void FillUnboundedClusterBoxes(EditorWorldClusters &wc, const EditorScene &scene,
                               const Q3BspClusterState &state)
{
    for (uint32_t c = 0; c < state.clusterCount; c++)
    {
        if (state.bounded[c] != 0u)
        {
            continue;
        }
        for (int axis = 0; axis < 3; axis++)
        {
            wc.mins[c].data[axis] = scene.hasBounds ? scene.boundsMin[axis] : -4096.0f;
            wc.maxs[c].data[axis] = scene.hasBounds ? scene.boundsMax[axis] : 4096.0f;
        }
    }
}

void FinalizeQ3ClustersAndPvs(const Q3BspDataView &view, EditorScene &scene,
                              const Q3BspClusterState &state)
{
    EditorWorldClusters &wc = scene.worldClusters;
    if (wc.mins.empty())
    {
        return;
    }

    FillUnboundedClusterBoxes(wc, scene, state);

    if (view.visibility == nullptr || view.visibilityBytes < 8)
    {
        return;
    }

    int32_t visClusters = 0;
    int32_t visRowBytes = 0;
    std::memcpy(&visClusters, view.visibility + 0, 4);
    std::memcpy(&visRowBytes, view.visibility + 4, 4);
    if (visClusters <= 0 || visRowBytes <= 0 ||
        8u + size_t(visClusters) * size_t(visRowBytes) > view.visibilityBytes)
    {
        return;
    }

    const uint32_t rowBytes = (state.clusterCount + 7u) / 8u;
    wc.pvsRowBytes = rowBytes;
    wc.visOffsets.assign(state.clusterCount, -1);
    wc.skyVis.assign(rowBytes, 0u);
    wc.visData.clear();

    std::vector<uint8_t> row(rowBytes, 0u);
    for (uint32_t c = 0; c + 1u < state.clusterCount && int32_t(c) < visClusters; c++)
    {
        const uint32_t sdkCluster = c + 1u;
        const uint8_t *source = view.visibility + 8 + size_t(c) * size_t(visRowBytes);
        if (sdkCluster < state.skyFlags.size() && state.skyFlags[sdkCluster] != 0u)
        {
            OrSkyVisRow(source, uint32_t(visRowBytes), rowBytes, wc.skyVis);
        }

        std::fill(row.begin(), row.end(), 0u);
        std::memcpy(row.data(), source, std::min(rowBytes, uint32_t(visRowBytes)));
        wc.visOffsets[sdkCluster] = int32_t(wc.visData.size());
        EncodeRleVisRow(row, wc.visData);
    }
}

void BuildFallbackTileRgba(const std::string &name, uint32_t &outW, uint32_t &outH,
                           std::vector<uint8_t> &outRgba)
{
    outW = 16;
    outH = 16;
    outRgba.resize(size_t(outW) * outH * 4u);

    uint32_t hash = 2166136261u;
    for (char ch : name)
    {
        hash ^= uint8_t(ch);
        hash *= 16777619u;
    }

    const uint8_t baseR = uint8_t(80u + (hash & 0x5Fu));
    const uint8_t baseG = uint8_t(80u + ((hash >> 8u) & 0x5Fu));
    const uint8_t baseB = uint8_t(80u + ((hash >> 16u) & 0x5Fu));

    for (uint32_t y = 0; y < outH; y++)
    {
        for (uint32_t x = 0; x < outW; x++)
        {
            const bool checker = ((x >> 2u) ^ (y >> 2u)) & 1u;
            const float shade = checker ? 1.0f : 0.72f;
            const size_t px = (size_t(y) * outW + x) * 4u;
            outRgba[px + 0] = uint8_t(float(baseR) * shade);
            outRgba[px + 1] = uint8_t(float(baseG) * shade);
            outRgba[px + 2] = uint8_t(float(baseB) * shade);
            outRgba[px + 3] = 255u;
        }
    }
}

EditorSurfaceKind ClassifyQ3SurfaceKind(const Q3DShader &dshader, const Q3ShaderDef *shaderDef)
{
    if ((dshader.surfaceFlags & kSurfSky) != 0 || (shaderDef != nullptr && shaderDef->isSky))
    {
        return EditorSurfaceKind::Sky;
    }
    if ((dshader.contentFlags & kContentsLava) != 0 || (shaderDef != nullptr && shaderDef->isLava))
    {
        return EditorSurfaceKind::Lava;
    }
    if ((dshader.contentFlags & kContentsSlime) != 0 || (shaderDef != nullptr && shaderDef->isSlime))
    {
        return EditorSurfaceKind::Slime;
    }
    if ((dshader.contentFlags & kContentsWater) != 0 || (shaderDef != nullptr && shaderDef->isWater))
    {
        return EditorSurfaceKind::Water;
    }
    if (shaderDef != nullptr && shaderDef->alphaTested)
    {
        return EditorSurfaceKind::Fence;
    }
    return EditorSurfaceKind::Opaque;
}

void EvaluatePatchControlPoint(const Q3DrawVert *cp[3][3], float u, float v, float outPos[3],
                               float outSt[2], float outEvalNormal[3])
{
    const float bu[3] = { (1.0f - u) * (1.0f - u), 2.0f * (1.0f - u) * u, u * u };
    const float bv[3] = { (1.0f - v) * (1.0f - v), 2.0f * (1.0f - v) * v, v * v };
    const float dbu[3] = { -2.0f * (1.0f - u), 2.0f - 4.0f * u, 2.0f * u };
    const float dbv[3] = { -2.0f * (1.0f - v), 2.0f - 4.0f * v, 2.0f * v };

    std::fill(outPos, outPos + 3, 0.0f);
    std::fill(outSt, outSt + 2, 0.0f);
    float du[3] = {};
    float dv[3] = {};

    for (int py = 0; py < 3; py++)
    {
        for (int px = 0; px < 3; px++)
        {
            const Q3DrawVert &p = *cp[py][px];
            const float w = bu[px] * bv[py];
            const float wu = dbu[px] * bv[py];
            const float wv = bu[px] * dbv[py];

            for (int c = 0; c < 3; c++)
            {
                outPos[c] += p.xyz[c] * w;
                du[c] += p.xyz[c] * wu;
                dv[c] += p.xyz[c] * wv;
            }
            outSt[0] += p.st[0] * w;
            outSt[1] += p.st[1] * w;
        }
    }

    outEvalNormal[0] = du[1] * dv[2] - du[2] * dv[1];
    outEvalNormal[1] = du[2] * dv[0] - du[0] * dv[2];
    outEvalNormal[2] = du[0] * dv[1] - du[1] * dv[0];
    NormalizeVec3(outEvalNormal);
}

void BuildPatchGrid(const Q3DSurface &surface, const Q3DrawVert *verts, const float baseScale[2],
                    std::vector<QrVertex> &outVerts, std::vector<float> &evalNormals)
{
    const int pw = surface.patchWidth;
    const int subX = (pw - 1) / 2;
    const int subY = (surface.patchHeight - 1) / 2;
    const int cols = subX * kPatchTessLevel + 1;
    const int rows = subY * kPatchTessLevel + 1;

    outVerts.resize(size_t(cols) * size_t(rows));
    evalNormals.assign(size_t(cols) * size_t(rows) * 3u, 0.0f);

    for (int sy = 0; sy < subY; sy++)
    {
        for (int sx = 0; sx < subX; sx++)
        {
            const Q3DrawVert *cp[3][3] = {};
            for (int py = 0; py < 3; py++)
            {
                for (int px = 0; px < 3; px++)
                {
                    cp[py][px] = &verts[surface.firstVert + (sy * 2 + py) * pw + (sx * 2 + px)];
                }
            }
            for (int iy = (sy == 0 ? 0 : 1); iy <= kPatchTessLevel; iy++)
            {
                const float v = float(iy) / float(kPatchTessLevel);
                const int gy = sy * kPatchTessLevel + iy;
                for (int ix = (sx == 0 ? 0 : 1); ix <= kPatchTessLevel; ix++)
                {
                    const float u = float(ix) / float(kPatchTessLevel);
                    const int gx = sx * kPatchTessLevel + ix;
                    const size_t vi = size_t(gy) * size_t(cols) + size_t(gx);
                    QrVertex &dst = outVerts[vi];
                    dst = {};
                    EvaluatePatchControlPoint(cp, u, v, dst.position, dst.texCoord,
                                              &evalNormals[vi * 3u]);
                    dst.texCoord[0] = dst.texCoord[0] * baseScale[0];
                    dst.texCoord[1] = dst.texCoord[1] * baseScale[1];
                    dst.packedColor = 0xFFFFFFFFu;
                }
            }
        }
    }
}

void WeldPatchSeamPair(std::vector<QrVertex> &verts, size_t a, size_t b)
{
    const float dx = verts[a].position[0] - verts[b].position[0];
    const float dy = verts[a].position[1] - verts[b].position[1];
    const float dz = verts[a].position[2] - verts[b].position[2];
    if (dx * dx + dy * dy + dz * dz < 1.0e-4f)
    {
        for (int c = 0; c < 3; c++)
        {
            const float sum = verts[a].normal[c] + verts[b].normal[c];
            verts[a].normal[c] = sum;
            verts[b].normal[c] = sum;
        }
    }
}

void FinalizePatchNormalsAndWinding(int rows, int cols, const std::vector<float> &evalNormals,
                                    std::vector<QrVertex> &outVerts,
                                    std::vector<uint32_t> &outIndices)
{
    for (size_t k = 0; k + 2 < outIndices.size(); k += 3)
    {
        float tn[3] = {};
        TriangleCross(outVerts[outIndices[k]].position, outVerts[outIndices[k + 1]].position,
                      outVerts[outIndices[k + 2]].position, tn);
        for (size_t j = 0; j < 3; j++)
        {
            QrVertex &pv = outVerts[outIndices[k + j]];
            pv.normal[0] += tn[0];
            pv.normal[1] += tn[1];
            pv.normal[2] += tn[2];
        }
    }

    for (int y = 0; y < rows; y++)
    {
        WeldPatchSeamPair(outVerts, size_t(y) * cols, size_t(y) * cols + size_t(cols - 1));
    }
    for (int x = 0; x < cols; x++)
    {
        WeldPatchSeamPair(outVerts, size_t(x), size_t(rows - 1) * cols + size_t(x));
    }

    float dot = 0.0f;
    for (size_t i = 0; i < outVerts.size(); i++)
    {
        dot += outVerts[i].normal[0] * evalNormals[i * 3u + 0] +
               outVerts[i].normal[1] * evalNormals[i * 3u + 1] +
               outVerts[i].normal[2] * evalNormals[i * 3u + 2];
    }
    if (dot < 0.0f)
    {
        for (size_t k = 0; k + 2 < outIndices.size(); k += 3)
        {
            std::swap(outIndices[k + 1], outIndices[k + 2]);
        }
        for (QrVertex &pv : outVerts)
        {
            pv.normal[0] = -pv.normal[0];
            pv.normal[1] = -pv.normal[1];
            pv.normal[2] = -pv.normal[2];
        }
    }
    for (QrVertex &pv : outVerts)
    {
        NormalizeVec3(pv.normal);
    }
}

void TessellateQ3PatchMesh(const Q3DSurface &surface, const Q3DrawVert *verts,
                           const float baseScale[2], std::vector<QrVertex> &outVerts,
                           std::vector<uint32_t> &outIndices, int &outCols, int &outRows)
{
    const int pw = surface.patchWidth;
    const int ph = surface.patchHeight;
    if (pw < 3 || ph < 3 || (pw % 2) == 0 || (ph % 2) == 0 || pw * ph > surface.numVerts)
    {
        return;
    }

    outCols = ((pw - 1) / 2) * kPatchTessLevel + 1;
    outRows = ((ph - 1) / 2) * kPatchTessLevel + 1;

    std::vector<float> evalNormals;
    BuildPatchGrid(surface, verts, baseScale, outVerts, evalNormals);

    outIndices.reserve(size_t(outRows - 1) * size_t(outCols - 1) * 6u);
    for (int y = 0; y < outRows - 1; y++)
    {
        for (int x = 0; x < outCols - 1; x++)
        {
            const uint32_t v00 = uint32_t(y * outCols + x);
            const uint32_t v10 = uint32_t(y * outCols + x + 1);
            const uint32_t v01 = uint32_t((y + 1) * outCols + x);
            const uint32_t v11 = uint32_t((y + 1) * outCols + x + 1);
            outIndices.push_back(v00);
            outIndices.push_back(v01);
            outIndices.push_back(v10);
            outIndices.push_back(v10);
            outIndices.push_back(v01);
            outIndices.push_back(v11);
        }
    }

    FinalizePatchNormalsAndWinding(outRows, outCols, evalNormals, outVerts, outIndices);
}

void BuildQ3PolygonMesh(const Q3DSurface &surface, const Q3DrawVert *verts, const int32_t *indices,
                        const float baseScale[2], std::vector<QrVertex> &outVerts,
                        std::vector<uint32_t> &outIndices)
{
    float faceNormal[3] = {
        surface.lightmapVecs[2][0], surface.lightmapVecs[2][1], surface.lightmapVecs[2][2]
    };
    if (faceNormal[0] * faceNormal[0] + faceNormal[1] * faceNormal[1] +
            faceNormal[2] * faceNormal[2] <= 1.0e-6f &&
        surface.numIndexes >= 3)
    {
        const Q3DrawVert &p0 = verts[surface.firstVert + indices[surface.firstIndex + 0]];
        const Q3DrawVert &p1 = verts[surface.firstVert + indices[surface.firstIndex + 1]];
        const Q3DrawVert &p2 = verts[surface.firstVert + indices[surface.firstIndex + 2]];
        TriangleCross(p0.xyz, p2.xyz, p1.xyz, faceNormal);
    }
    NormalizeVec3(faceNormal);

    outVerts.resize(size_t(surface.numVerts));
    for (int i = 0; i < surface.numVerts; i++)
    {
        const Q3DrawVert &src = verts[surface.firstVert + i];
        QrVertex &dst = outVerts[size_t(i)];
        dst = {};
        std::memcpy(dst.position, src.xyz, sizeof(dst.position));
        if (surface.surfaceType == kMstTriangleSoup &&
            (src.normal[0] * src.normal[0] + src.normal[1] * src.normal[1] +
             src.normal[2] * src.normal[2]) > 1.0e-6f)
        {
            std::memcpy(dst.normal, src.normal, sizeof(dst.normal));
            NormalizeVec3(dst.normal);
        }
        else
        {
            std::memcpy(dst.normal, faceNormal, sizeof(dst.normal));
        }
        dst.texCoord[0] = src.st[0] * baseScale[0];
        dst.texCoord[1] = src.st[1] * baseScale[1];
        dst.packedColor = uint32_t(src.color[0]) | (uint32_t(src.color[1]) << 8u) |
                          (uint32_t(src.color[2]) << 16u) | (uint32_t(src.color[3]) << 24u);
    }

    outIndices.resize(size_t(surface.numIndexes));
    for (int i = 0; i < surface.numIndexes; i++)
    {
        const int32_t idx = indices[surface.firstIndex + i];
        outIndices[size_t(i)] = (idx >= 0 && idx < surface.numVerts) ? uint32_t(idx) : 0u;
    }

    bool flipWinding = true;
    if (surface.surfaceType == kMstPlanar && outIndices.size() >= 3)
    {
        float windingNormal[3] = {};
        TriangleCross(outVerts[outIndices[0]].position, outVerts[outIndices[1]].position,
                      outVerts[outIndices[2]].position, windingNormal);
        const float dot = windingNormal[0] * faceNormal[0] + windingNormal[1] * faceNormal[1] +
                          windingNormal[2] * faceNormal[2];
        if (dot > 0.0f)
        {
            flipWinding = false;
        }
    }

    if (flipWinding)
    {
        for (size_t i = 0; i + 2 < outIndices.size(); i += 3)
        {
            std::swap(outIndices[i + 1], outIndices[i + 2]);
        }
    }
}

void AppendTwoSidedGeometry(std::vector<QrVertex> &verts, std::vector<uint32_t> &indices)
{
    const uint32_t baseVert = uint32_t(verts.size());
    const size_t baseIndexCount = indices.size();
    verts.reserve(verts.size() * 2u);
    for (uint32_t i = 0; i < baseVert; i++)
    {
        QrVertex flipped = verts[i];
        flipped.normal[0] = -flipped.normal[0];
        flipped.normal[1] = -flipped.normal[1];
        flipped.normal[2] = -flipped.normal[2];
        verts.push_back(flipped);
    }

    indices.reserve(baseIndexCount * 2u);
    for (size_t i = 0; i + 2 < baseIndexCount; i += 3)
    {
        indices.push_back(baseVert + indices[i]);
        indices.push_back(baseVert + indices[i + 2]);
        indices.push_back(baseVert + indices[i + 1]);
    }
}

void PushTriangleFace(const std::vector<QrVertex> &verts, uint32_t i0, uint32_t i1, uint32_t i2,
                      EditorSurface &batch, uint32_t aliasInstanceId = 0)
{
    const QrVertex &v0 = verts[i0];
    const QrVertex &v1 = verts[i1];
    const QrVertex &v2 = verts[i2];

    EditorSurfaceFace face;
    TriangleCross(v0.position, v1.position, v2.position, face.normal);
    NormalizeVec3(face.normal);
    face.cluster = v0.cluster != 0u ? v0.cluster : (v1.cluster != 0u ? v1.cluster : v2.cluster);
    face.aliasInstanceId = aliasInstanceId;
    face.positions = {
        QrFloat3D{ { v0.position[0], v0.position[1], v0.position[2] } },
        QrFloat3D{ { v1.position[0], v1.position[1], v1.position[2] } },
        QrFloat3D{ { v2.position[0], v2.position[1], v2.position[2] } },
    };
    face.texCoords = {
        QrFloat2D{ { v0.texCoord[0], v0.texCoord[1] } },
        QrFloat2D{ { v1.texCoord[0], v1.texCoord[1] } },
        QrFloat2D{ { v2.texCoord[0], v2.texCoord[1] } },
    };
    batch.faces.push_back(std::move(face));
}

bool TryBuildQuadFromTriPair(size_t vertCount, const uint32_t triPair[6], uint32_t outQuad[4])
{
    const uint32_t a[3] = { triPair[0], triPair[1], triPair[2] };
    const uint32_t b[3] = { triPair[3], triPair[4], triPair[5] };
    int shared = 0;
    uint32_t fourth = UINT32_MAX;
    int fourthPos = -1;

    for (int ib = 0; ib < 3; ib++)
    {
        const bool inA = (b[ib] == a[0] || b[ib] == a[1] || b[ib] == a[2]);
        if (inA)
        {
            shared++;
        }
        else
        {
            fourth = b[ib];
            fourthPos = ib;
        }
    }
    if (shared != 2 || fourth == UINT32_MAX)
    {
        return false;
    }

    const uint32_t prev = b[(fourthPos + 2) % 3];
    const uint32_t next = b[(fourthPos + 1) % 3];
    for (int ia = 0; ia < 3; ia++)
    {
        if (a[ia] == prev && a[(ia + 1) % 3] == next)
        {
            outQuad[0] = a[ia];
            outQuad[1] = fourth;
            outQuad[2] = a[(ia + 1) % 3];
            outQuad[3] = a[(ia + 2) % 3];
            return *std::max_element(outQuad, outQuad + 4) < vertCount;
        }
    }
    return false;
}

bool TryBuildQuadFromTwoTris(const std::vector<QrVertex> &verts,
                             const std::vector<uint32_t> &indices, uint32_t outQuad[4])
{
    return indices.size() == 6 && TryBuildQuadFromTriPair(verts.size(), indices.data(), outQuad);
}

void AppendCoarsePatchFaces(const std::vector<QrVertex> &verts, int cols, int rows,
                            EditorSurface &batch, uint32_t aliasInstanceId = 0)
{
    for (int y = 0; y + 1 < rows; y += kPatchFaceStride)
    {
        const int yNext = std::min(rows - 1, y + kPatchFaceStride);
        for (int x = 0; x + 1 < cols; x += kPatchFaceStride)
        {
            const int xNext = std::min(cols - 1, x + kPatchFaceStride);
            const uint32_t v00 = uint32_t(y * cols + x);
            const uint32_t v10 = uint32_t(y * cols + xNext);
            const uint32_t v11 = uint32_t(yNext * cols + xNext);
            const uint32_t v01 = uint32_t(yNext * cols + x);

            EditorSurfaceFace face;
            TriangleCross(verts[v00].position, verts[v01].position, verts[v10].position,
                          face.normal);
            NormalizeVec3(face.normal);
            const float normalDot =
                face.normal[0] * (verts[v00].normal[0] + verts[v11].normal[0]) +
                face.normal[1] * (verts[v00].normal[1] + verts[v11].normal[1]) +
                face.normal[2] * (verts[v00].normal[2] + verts[v11].normal[2]);
            const bool flipQuad = normalDot < 0.0f;
            if (flipQuad)
            {
                face.normal[0] = -face.normal[0];
                face.normal[1] = -face.normal[1];
                face.normal[2] = -face.normal[2];
            }
            face.cluster = verts[v00].cluster != 0u ? verts[v00].cluster : verts[v11].cluster;
            face.aliasInstanceId = aliasInstanceId;
            const uint32_t quadVerts[4] = {
                v00,
                flipQuad ? v10 : v01,
                v11,
                flipQuad ? v01 : v10,
            };
            for (uint32_t idx : quadVerts)
            {
                face.positions.push_back(
                    QrFloat3D{ { verts[idx].position[0], verts[idx].position[1],
                                 verts[idx].position[2] } });
                face.texCoords.push_back(
                    QrFloat2D{ { verts[idx].texCoord[0], verts[idx].texCoord[1] } });
            }
            batch.faces.push_back(std::move(face));
        }
    }
}

void AppendRepresentativeFaces(int32_t surfaceType, int patchCols, int patchRows,
                               const std::vector<QrVertex> &frontVerts,
                               const std::vector<uint32_t> &frontIndices, EditorSurface &batch,
                               uint32_t aliasInstanceId = 0)
{
    if (surfaceType == kMstPatch && patchCols > 1 && patchRows > 1)
    {
        AppendCoarsePatchFaces(frontVerts, patchCols, patchRows, batch, aliasInstanceId);
        return;
    }

    uint32_t quad[4] = {};
    if (surfaceType == kMstPlanar && TryBuildQuadFromTwoTris(frontVerts, frontIndices, quad))
    {
        EditorSurfaceFace face;
        TriangleCross(frontVerts[quad[0]].position, frontVerts[quad[1]].position,
                      frontVerts[quad[2]].position, face.normal);
        NormalizeVec3(face.normal);
        face.cluster = frontVerts[quad[0]].cluster != 0u ? frontVerts[quad[0]].cluster
                                                         : frontVerts[quad[1]].cluster;
        face.aliasInstanceId = aliasInstanceId;
        for (uint32_t idx : quad)
        {
            face.positions.push_back(QrFloat3D{ { frontVerts[idx].position[0],
                                                  frontVerts[idx].position[1],
                                                  frontVerts[idx].position[2] } });
            face.texCoords.push_back(
                QrFloat2D{ { frontVerts[idx].texCoord[0], frontVerts[idx].texCoord[1] } });
        }
        batch.faces.push_back(std::move(face));
        return;
    }

    for (size_t i = 0; i + 2 < frontIndices.size(); i += 3)
    {
        PushTriangleFace(frontVerts, frontIndices[i], frontIndices[i + 1], frontIndices[i + 2],
                         batch, aliasInstanceId);
    }
}

float EdgeDistSq(const float a[3], const float b[3])
{
    const float dx = b[0] - a[0];
    const float dy = b[1] - a[1];
    const float dz = b[2] - a[2];
    return dx * dx + dy * dy + dz * dz;
}

void ComputeAutoSprite2MajorAxis(const std::vector<QrVertex> &verts, const uint32_t quad[4],
                                 float outAxis[3])
{
    const float *p0 = verts[quad[0]].position;
    const float *p1 = verts[quad[1]].position;
    const float *p2 = verts[quad[2]].position;
    const float *p3 = verts[quad[3]].position;
    const float sum01 = EdgeDistSq(p0, p1) + EdgeDistSq(p3, p2);
    const float sum12 = EdgeDistSq(p1, p2) + EdgeDistSq(p0, p3);
    const float *e0a = (sum01 <= sum12) ? p0 : p1;
    const float *e0b = (sum01 <= sum12) ? p1 : p2;
    const float *e1a = (sum01 <= sum12) ? p3 : p0;
    const float *e1b = (sum01 <= sum12) ? p2 : p3;
    for (int axis = 0; axis < 3; axis++)
    {
        outAxis[axis] = 0.5f * (e1a[axis] + e1b[axis]) - 0.5f * (e0a[axis] + e0b[axis]);
    }
    NormalizeVec3(outAxis);
    if (outAxis[0] * outAxis[0] + outAxis[1] * outAxis[1] + outAxis[2] * outAxis[2] < 1e-6f)
    {
        outAxis[0] = 0.0f;
        outAxis[1] = 0.0f;
        outAxis[2] = 1.0f;
    }
}

void ChooseAutoSpriteRotationAxis(const std::vector<QrVertex> &verts, const uint32_t quad[4],
                                  bool autoSprite2, float outAxis[3])
{
    if (autoSprite2)
    {
        ComputeAutoSprite2MajorAxis(verts, quad, outAxis);
        return;
    }

    float minP[3] = {};
    float maxP[3] = {};
    std::copy_n(verts[quad[0]].position, 3, minP);
    std::copy_n(verts[quad[0]].position, 3, maxP);
    for (int k = 1; k < 4; k++)
    {
        for (int axis = 0; axis < 3; axis++)
        {
            minP[axis] = std::min(minP[axis], verts[quad[k]].position[axis]);
            maxP[axis] = std::max(maxP[axis], verts[quad[k]].position[axis]);
        }
    }
    const bool flatInZ =
        (maxP[2] - minP[2] < 1.0f) && (maxP[0] - minP[0] > 1.0f || maxP[1] - minP[1] > 1.0f);
    outAxis[0] = flatInZ ? 1.0f : 0.0f;
    outAxis[1] = 0.0f;
    outAxis[2] = flatInZ ? 0.0f : 1.0f;
}

void RotatePoint90AroundAxis(const float p[3], const float center[3], const float axis[3],
                             float out[3])
{
    const float rel[3] = { p[0] - center[0], p[1] - center[1], p[2] - center[2] };
    const float proj = rel[0] * axis[0] + rel[1] * axis[1] + rel[2] * axis[2];
    const float perp[3] = {
        rel[0] - proj * axis[0],
        rel[1] - proj * axis[1],
        rel[2] - proj * axis[2],
    };
    const float cr[3] = {
        axis[1] * perp[2] - axis[2] * perp[1],
        axis[2] * perp[0] - axis[0] * perp[2],
        axis[0] * perp[1] - axis[1] * perp[0],
    };
    for (int c = 0; c < 3; c++)
    {
        out[c] = center[c] + proj * axis[c] + cr[c];
    }
}

uint32_t FirstNonZeroQuadCluster(const std::vector<QrVertex> &verts, const uint32_t quad[4])
{
    for (int k = 0; k < 4; k++)
    {
        if (verts[quad[k]].cluster != 0u)
        {
            return verts[quad[k]].cluster;
        }
    }
    return 0u;
}

void AppendRotatedAutoSpriteQuad(const std::vector<QrVertex> &verts, const uint32_t quad[4],
                                 bool autoSprite2, EditorSurface &batch, uint32_t aliasInstanceId)
{
    float center[3] = {};
    for (int k = 0; k < 4; k++)
    {
        for (int axis = 0; axis < 3; axis++)
        {
            center[axis] += verts[quad[k]].position[axis] * 0.25f;
        }
    }
    float rotAxis[3] = {};
    ChooseAutoSpriteRotationAxis(verts, quad, autoSprite2, rotAxis);

    EditorSurfaceFace crossed;
    crossed.aliasInstanceId = aliasInstanceId;
    crossed.cluster = FirstNonZeroQuadCluster(verts, quad);
    for (int k = 0; k < 4; k++)
    {
        const QrVertex &v = verts[quad[k]];
        float rotated[3] = {};
        RotatePoint90AroundAxis(v.position, center, rotAxis, rotated);
        crossed.positions.push_back(QrFloat3D{ { rotated[0], rotated[1], rotated[2] } });
        crossed.texCoords.push_back(QrFloat2D{ { v.texCoord[0], v.texCoord[1] } });
    }
    TriangleCross(crossed.positions[0].data, crossed.positions[1].data,
                  crossed.positions[2].data, crossed.normal);
    NormalizeVec3(crossed.normal);
    batch.faces.push_back(std::move(crossed));
}

void AppendRotatedAutoSpriteTris(const std::vector<QrVertex> &verts,
                                 const std::vector<uint32_t> &indices, EditorSurface &batch,
                                 uint32_t aliasInstanceId)
{
    float center[3] = {};
    const float inv = 1.0f / float(verts.size());
    for (const QrVertex &v : verts)
    {
        for (int axis = 0; axis < 3; axis++)
        {
            center[axis] += v.position[axis] * inv;
        }
    }
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        EditorSurfaceFace triFace;
        triFace.aliasInstanceId = aliasInstanceId;
        const uint32_t c0 = verts[indices[i]].cluster;
        const uint32_t c1 = verts[indices[i + 1]].cluster;
        triFace.cluster = c0 != 0u ? c0 : (c1 != 0u ? c1 : verts[indices[i + 2]].cluster);
        for (size_t k = 0; k < 3; k++)
        {
            const QrVertex &v = verts[indices[i + k]];
            const float dx = v.position[0] - center[0];
            const float dy = v.position[1] - center[1];
            triFace.positions.push_back(QrFloat3D{ {
                center[0] - dy,
                center[1] + dx,
                v.position[2],
            } });
            triFace.texCoords.push_back(QrFloat2D{ { v.texCoord[0], v.texCoord[1] } });
        }
        TriangleCross(triFace.positions[0].data, triFace.positions[1].data,
                      triFace.positions[2].data, triFace.normal);
        NormalizeVec3(triFace.normal);
        batch.faces.push_back(std::move(triFace));
    }
}

void AppendCrossedAutoSpritePickingFace(const std::vector<QrVertex> &verts,
                                        const std::vector<uint32_t> &indices, bool autoSprite2,
                                        EditorSurface &batch, uint32_t aliasInstanceId)
{
    if (verts.size() < 3 || indices.size() < 3)
    {
        return;
    }

    if (indices.size() >= 6u && (indices.size() % 6u) == 0u)
    {
        std::vector<std::array<uint32_t, 4>> quads;
        quads.reserve(indices.size() / 6u);
        bool allQuads = true;
        for (size_t i = 0; i < indices.size(); i += 6u)
        {
            std::array<uint32_t, 4> quad{};
            if (!TryBuildQuadFromTriPair(verts.size(), &indices[i], quad.data()))
            {
                allQuads = false;
                break;
            }
            quads.push_back(quad);
        }
        if (allQuads)
        {
            for (const auto &quad : quads)
            {
                AppendRotatedAutoSpriteQuad(verts, quad.data(), autoSprite2, batch,
                                            aliasInstanceId);
            }
            return;
        }
    }

    AppendRotatedAutoSpriteTris(verts, indices, batch, aliasInstanceId);
}

void AssignVertexClustersAndBounds(std::vector<QrVertex> &verts, EditorScene &scene,
                                   Q3BspClusterState &clusterState, bool isSkySurface)
{
    for (QrVertex &v : verts)
    {
        const float nudged[3] = {
            v.position[0] + v.normal[0] * 0.5f,
            v.position[1] + v.normal[1] * 0.5f,
            v.position[2] + v.normal[2] * 0.5f,
        };
        uint32_t c = ClusterForQ3Point(scene, nudged);
        if (c == 0u)
        {
            c = ClusterForQ3Point(scene, v.position);
        }
        v.cluster = c;
        if (isSkySurface && c > 0u && c < clusterState.skyFlags.size())
        {
            clusterState.skyFlags[c] = 1u;
        }
        AccumulateClusterPoint(scene, clusterState, c, v.position);

        for (int axis = 0; axis < 3; axis++)
        {
            scene.boundsMin[axis] =
                scene.hasBounds ? std::min(scene.boundsMin[axis], v.position[axis]) : v.position[axis];
            scene.boundsMax[axis] =
                scene.hasBounds ? std::max(scene.boundsMax[axis], v.position[axis]) : v.position[axis];
        }
        scene.hasBounds = true;
    }
}

bool ValidateQ3Surface(const Q3DSurface &s, const Q3BspDataView &view)
{
    if (s.shaderNum < 0 || s.shaderNum >= view.shaderCount)
    {
        return false;
    }
    if (s.firstVert < 0 || s.numVerts <= 0 || s.firstVert + s.numVerts > view.vertCount)
    {
        return false;
    }
    if (s.surfaceType == kMstPatch)
    {
        return s.patchWidth >= 3 && s.patchHeight >= 3;
    }
    if (s.surfaceType != kMstPlanar && s.surfaceType != kMstTriangleSoup)
    {
        return false;
    }
    return s.firstIndex >= 0 && s.numIndexes >= 3 && s.firstIndex + s.numIndexes <= view.indexCount;
}

std::vector<std::string> TokenizeQ3Entities(const char *data, size_t size)
{
    std::vector<std::string> tokens;
    size_t i = 0;

    while (i < size && data[i] != '\0')
    {
        if (std::isspace(static_cast<unsigned char>(data[i])))
        {
            i++;
            continue;
        }
        if (data[i] == '{' || data[i] == '}')
        {
            tokens.emplace_back(1, data[i]);
            i++;
            continue;
        }
        if (data[i] == '"')
        {
            i++;
            const size_t start = i;
            while (i < size && data[i] != '"')
            {
                i++;
            }
            tokens.emplace_back(data + start, i - start);
            if (i < size)
            {
                i++;
            }
            continue;
        }
        const size_t start = i;
        while (i < size && !std::isspace(static_cast<unsigned char>(data[i])) &&
               data[i] != '{' && data[i] != '}')
        {
            i++;
        }
        tokens.emplace_back(data + start, i - start);
    }

    return tokens;
}

int ParseThreeFloats(const std::string &text, float out[3])
{
    const char *p = text.c_str();
    int count = 0;
    while (count < 3)
    {
        while (*p != '\0' && std::isspace(static_cast<unsigned char>(*p)))
        {
            p++;
        }
        if (*p == '\0')
        {
            break;
        }
        char *end = nullptr;
        const float value = std::strtof(p, &end);
        if (end == p)
        {
            break;
        }
        out[count++] = value;
        p = end;
    }
    return count;
}

void ParseLightIntensityAndTint(const std::unordered_map<std::string, std::string> &keys,
                               float &outIntensity, float outTint[3])
{
    outIntensity = 300.0f;
    outTint[0] = 1.0f;
    outTint[1] = 1.0f;
    outTint[2] = 1.0f;

    float lightValues[3] = {};
    auto lightIt = keys.find("light");
    if (lightIt == keys.end())
    {
        lightIt = keys.find("_light");
    }
    const int lightCount = lightIt != keys.end() ? ParseThreeFloats(lightIt->second, lightValues) : 0;

    if (lightCount >= 3)
    {
        outIntensity = std::max({ 1.0f, lightValues[0], lightValues[1], lightValues[2] });
        for (int c = 0; c < 3; c++)
        {
            outTint[c] = std::max(0.0f, lightValues[c]) / outIntensity;
        }
    }
    else if (lightCount >= 1 && lightValues[0] > 0.0f)
    {
        outIntensity = lightValues[0];
    }

    const auto scaleIt = keys.find("scale");
    if (scaleIt != keys.end())
    {
        const float scale = float(std::atof(scaleIt->second.c_str()));
        if (scale > 0.0f)
        {
            outIntensity *= scale;
        }
    }

    auto colorIt = keys.find("_color");
    if (colorIt == keys.end())
    {
        colorIt = keys.find("color");
    }
    float parsedColor[3] = {};
    if (colorIt != keys.end() && ParseThreeFloats(colorIt->second, parsedColor) >= 3)
    {
        for (int c = 0; c < 3; c++)
        {
            outTint[c] = std::max(0.0f, parsedColor[c]);
        }
    }
}

void TryApplyQ3SunEntity(const float origin[3], const std::array<float, 3> &targetOrigin,
                         float intensity, const float tint[3], EditorScene &scene)
{
    const float dx = origin[0] - targetOrigin[0];
    const float dy = origin[1] - targetOrigin[1];
    const float dz = origin[2] - targetOrigin[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist <= 1.0f || scene.hasSun)
    {
        return;
    }

    const float inv = 1.0f / dist;
    const float toSun[3] = { dx * inv, dy * inv, dz * inv };
    const float xy = std::sqrt(toSun[0] * toSun[0] + toSun[1] * toSun[1]);
    scene.hasSun = true;
    std::memcpy(scene.sunColor, tint, sizeof(scene.sunColor));
    scene.sunIntensity = intensity;
    scene.sunDegrees = std::atan2(toSun[1], toSun[0]) * (180.0f / 3.14159265358979323846f);
    scene.sunElevation = std::atan2(toSun[2], xy) * (180.0f / 3.14159265358979323846f);
}

void ConfigureQ3Spotlight(const std::unordered_map<std::string, std::string> &keys,
                          const float origin[3], const float targetPos[3], EditorLight &light)
{
    float dir[3] = {
        targetPos[0] - origin[0],
        targetPos[1] - origin[1],
        targetPos[2] - origin[2],
    };
    const float dist = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (dist <= 1.0f)
    {
        return;
    }

    dir[0] /= dist;
    dir[1] /= dist;
    dir[2] /= dist;
    light.spot = true;
    std::memcpy(light.direction, dir, sizeof(light.direction));

    float coneRadius = 64.0f;
    const auto radiusIt = keys.find("radius");
    if (radiusIt != keys.end())
    {
        const float parsedRadius = float(std::atof(radiusIt->second.c_str()));
        if (parsedRadius > 0.0f)
        {
            coneRadius = parsedRadius;
        }
    }
    const float outerDeg = std::clamp(
        std::atan2(coneRadius, dist) * (180.0f / 3.14159265358979323846f), 1.0f, 89.0f);
    light.angleOuter = outerDeg;
    light.angleInner = outerDeg * 0.7f;
}

void ParseQ3LightEntity(const std::string &cls,
                        const std::unordered_map<std::string, std::string> &keys,
                        const std::unordered_map<std::string, std::array<float, 3>> &targetOrigins,
                        EditorScene &scene)
{
    const auto originIt = keys.find("origin");
    float origin[3] = {};
    if (originIt == keys.end() || ParseThreeFloats(originIt->second, origin) < 3)
    {
        return;
    }

    float intensity = 300.0f;
    float tint[3] = { 1.0f, 1.0f, 1.0f };
    ParseLightIntensityAndTint(keys, intensity, tint);

    const auto sunIt = keys.find("_sun");
    const auto targetIt = keys.find("target");
    const auto targetPosIt =
        targetIt != keys.end() ? targetOrigins.find(targetIt->second) : targetOrigins.end();

    if (sunIt != keys.end() && std::atoi(sunIt->second.c_str()) != 0 &&
        targetPosIt != targetOrigins.end())
    {
        TryApplyQ3SunEntity(origin, targetPosIt->second, intensity, tint, scene);
        return;
    }

    EditorLight light;
    std::memcpy(light.position, origin, sizeof(light.position));
    light.radius = 16.0f;
    light.className = cls;
    light.hasColor = true;
    const auto styleIt = keys.find("style");
    if (styleIt != keys.end())
    {
        light.style = std::max(0, std::atoi(styleIt->second.c_str()));
    }

    if (targetPosIt != targetOrigins.end())
    {
        ConfigureQ3Spotlight(keys, origin, targetPosIt->second.data(), light);
    }

    const uint32_t resolvedCluster = ClusterForQ3Point(scene, light.position);
    light.cluster = resolvedCluster == 0u ? QR_CLUSTER_LIGHT_NO_CLUSTER : resolvedCluster;
    SetEditorLightTint(light, tint);
    SetEditorLightIntensity(light, intensity);
    scene.lights.push_back(light);
    scene.bspLights.push_back(light);
}

int SpawnPriorityForClass(const std::string &cls)
{
    if (cls == "info_player_deathmatch")
    {
        return 4;
    }
    if (cls == "info_player_start")
    {
        return 3;
    }
    if (cls == "info_player_coop")
    {
        return 2;
    }
    return cls == "info_player_intermission" ? 1 : 0;
}

void ParseQ3EntityBlock(const std::unordered_map<std::string, std::string> &keys,
                        const std::unordered_map<std::string, std::array<float, 3>> &targetOrigins,
                        EditorScene &scene, int &cameraPriority,
                        std::unordered_set<int> &hiddenSubmodels)
{
    const auto clsIt = keys.find("classname");
    if (clsIt == keys.end())
    {
        return;
    }
    const std::string &cls = clsIt->second;

    const auto modelIt = keys.find("model");
    if (modelIt != keys.end() && modelIt->second.size() >= 2 && modelIt->second[0] == '*' &&
        cls.rfind("trigger_", 0) == 0)
    {
        const int idx = std::atoi(modelIt->second.c_str() + 1);
        if (idx > 0)
        {
            hiddenSubmodels.insert(idx);
        }
    }

    if (cls == "light" || cls == "lightJunior" || cls.rfind("light_", 0) == 0)
    {
        ParseQ3LightEntity(cls, keys, targetOrigins, scene);
        return;
    }

    const int prio = SpawnPriorityForClass(cls);
    const auto originIt = keys.find("origin");
    float origin[3] = {};
    if (prio > cameraPriority && originIt != keys.end() &&
        ParseThreeFloats(originIt->second, origin) >= 3)
    {
        cameraPriority = prio;
        scene.hasCamera = true;
        scene.cameraPosition[0] = origin[0];
        scene.cameraPosition[1] = origin[1];
        scene.cameraPosition[2] = origin[2] + 26.0f;

        float yaw = 0.0f;
        const auto anglesIt = keys.find("angles");
        const auto angleIt = keys.find("angle");
        float angles[3] = {};
        if (anglesIt != keys.end() && ParseThreeFloats(anglesIt->second, angles) >= 2)
        {
            yaw = angles[1];
        }
        else if (angleIt != keys.end())
        {
            yaw = float(std::atof(angleIt->second.c_str()));
        }
        scene.cameraYawDegrees = yaw;
    }
}

void ParseSingleQ3EntityBlock(const std::vector<std::string> &tokens, size_t &idx,
                              std::unordered_map<std::string, std::string> &outKeys,
                              std::unordered_map<std::string, std::array<float, 3>> &targetOrigins)
{
    idx++;
    while (idx < tokens.size() && tokens[idx] != "}")
    {
        const std::string &key = tokens[idx++];
        if (idx >= tokens.size() || tokens[idx] == "}")
        {
            break;
        }
        outKeys[key] = tokens[idx++];
    }
    if (idx < tokens.size() && tokens[idx] == "}")
    {
        idx++;
    }

    const auto targetNameIt = outKeys.find("targetname");
    const auto originIt = outKeys.find("origin");
    if (targetNameIt != outKeys.end() && !targetNameIt->second.empty() && originIt != outKeys.end())
    {
        float pt[3] = {};
        if (ParseThreeFloats(originIt->second, pt) >= 3)
        {
            targetOrigins.emplace(targetNameIt->second,
                                  std::array<float, 3>{ pt[0], pt[1], pt[2] });
        }
    }
}

void ParseAllQ3Entities(const Q3BspDataView &view, EditorScene &scene,
                        std::unordered_set<int> &hiddenSubmodels)
{
    if (view.entities == nullptr || view.entityBytes == 0)
    {
        return;
    }

    const std::vector<std::string> tokens = TokenizeQ3Entities(view.entities, view.entityBytes);
    std::vector<std::unordered_map<std::string, std::string>> blocks;
    std::unordered_map<std::string, std::array<float, 3>> targetOrigins;

    size_t idx = 0;
    while (idx < tokens.size())
    {
        if (tokens[idx] != "{")
        {
            idx++;
            continue;
        }
        std::unordered_map<std::string, std::string> keys;
        ParseSingleQ3EntityBlock(tokens, idx, keys, targetOrigins);
        blocks.push_back(std::move(keys));
    }

    int cameraPriority = 0;
    for (const auto &keys : blocks)
    {
        ParseQ3EntityBlock(keys, targetOrigins, scene, cameraPriority, hiddenSubmodels);
    }
}

void TryAppendExtraSceneTexture(const std::string &rawRef, const EditorAssetSource *assets,
                                std::unordered_set<std::string> &knownTextures, EditorScene &scene)
{
    const std::string cleanRef = StripQ3ImageExtension(NormalizeQ3AssetPath(rawRef));
    if (cleanRef.empty() || !knownTextures.insert(cleanRef).second)
    {
        return;
    }

    EditorTexture extra;
    extra.name = cleanRef;
    extra.isExternal = true;
    std::string resolved;
    if (LoadQ3ImageRgba(assets, rawRef, extra.rgba, extra.width, extra.height, resolved) &&
        extra.width > 0 && extra.height > 0)
    {
        scene.textures.push_back(std::move(extra));
    }
}

void RegisterShaderStageTextures(const EditorTexture &tex, const EditorAssetSource *assets,
                                 std::unordered_set<std::string> &knownTextures, EditorScene &scene)
{
    TryAppendExtraSceneTexture(tex.defaultBaseTexture, assets, knownTextures, scene);
    TryAppendExtraSceneTexture(tex.defaultEmissiveTexture, assets, knownTextures, scene);
    TryAppendExtraSceneTexture(tex.defaultOverlayTexture, assets, knownTextures, scene);

    for (const EditorOverlayStage &stage : tex.shaderStages)
    {
        if (!stage.isLightmap && !stage.isWhite && !stage.map.empty())
        {
            TryAppendExtraSceneTexture(stage.map, assets, knownTextures, scene);
        }
        for (const std::string &frameMap : stage.animationMaps)
        {
            TryAppendExtraSceneTexture(frameMap, assets, knownTextures, scene);
        }
    }
}

void AppendDefaultEmissiveTextures(int shaderCount, const EditorAssetSource *assets, EditorScene &scene)
{
    std::unordered_set<std::string> knownTextures;
    knownTextures.reserve(scene.textures.size() * 2u);
    for (const EditorTexture &tex : scene.textures)
    {
        knownTextures.insert(StripQ3ImageExtension(NormalizeQ3AssetPath(tex.name)));
    }

    for (int i = 0; i < shaderCount; i++)
    {
        RegisterShaderStageTextures(scene.textures[size_t(i)], assets, knownTextures, scene);
    }
}

void BuildQ3TexturesAndSky(const Q3BspDataView &view, const Q3ShaderDatabase &shaders,
                           const EditorAssetSource *assets, EditorScene &scene,
                           std::vector<const Q3ShaderDef *> &shaderDefs)
{
    scene.textures.resize(size_t(view.shaderCount));
    shaderDefs.assign(size_t(view.shaderCount), nullptr);

    for (int i = 0; i < view.shaderCount; i++)
    {
        const Q3DShader &src = view.shaders[i];
        const std::string shaderName = NormalizeQ3AssetPath(src.shader);
        const Q3ShaderDef *def = shaders.Find(shaderName);
        shaderDefs[size_t(i)] = def;

        EditorTexture &tex = scene.textures[size_t(i)];
        tex.name = shaderName.empty() ? ("shader_" + std::to_string(i)) : shaderName;

        const std::string imageRef = (def != nullptr && !def->map.empty()) ? def->map : tex.name;
        std::string resolved;
        if (!LoadQ3ImageRgba(assets, imageRef, tex.rgba, tex.width, tex.height, resolved) &&
            !(def != nullptr && !def->editorImage.empty() &&
              LoadQ3ImageRgba(assets, def->editorImage, tex.rgba, tex.width, tex.height, resolved)))
        {
            BuildFallbackTileRgba(tex.name, tex.width, tex.height, tex.rgba);
        }

        if (def != nullptr)
        {
            const bool isSky = (src.surfaceFlags & kSurfSky) != 0 || def->isSky;
            ApplyQ3ShaderToEditorTexture(*def, assets, isSky, tex);
            if (def->isPortal && tex.name.find("mirror") != std::string::npos)
            {
                tex.isMirrorShader = true;
            }
        }
    }

    AppendDefaultEmissiveTextures(view.shaderCount, assets, scene);
}

void SkyAxisDirection(int axis, float s, float t, float outDir[3])
{
    static constexpr int kStToVec[6][3] = {
        { 3, -1, 2 },
        { -3, 1, 2 },
        { 1, 3, 2 },
        { -1, -3, 2 },
        { -2, -1, 3 },
        { 2, -1, -3 },
    };
    const float b[3] = { s, t, 1.0f };
    for (int j = 0; j < 3; j++)
    {
        const int k = kStToVec[axis][j];
        const int idx = std::clamp((k < 0 ? -k : k) - 1, 0, 2);
        outDir[j] = (k < 0) ? -b[idx] : b[idx];
    }
}

void SampleCloudStagePixel(const std::vector<uint8_t> &pixels, uint32_t w, uint32_t h,
                           const Q3ShaderStage &stage, const float dir[3], float cloudHeight,
                           uint8_t outPx[3])
{
    const float denom = std::max(std::fabs(dir[2]), 0.05f);
    const float scale = cloudHeight / denom / 4096.0f;
    float u = (0.5f + dir[0] * scale) * stage.scale[0];
    float v = (0.5f + dir[1] * scale) * stage.scale[1];
    u -= std::floor(u);
    v -= std::floor(v);
    const uint32_t sx = std::min(w - 1u, uint32_t(u * float(w)));
    const uint32_t sy = std::min(h - 1u, uint32_t(v * float(h)));
    const size_t idx = (size_t(sy) * w + sx) * 4u;
    outPx[0] = pixels[idx + 0];
    outPx[1] = pixels[idx + 1];
    outPx[2] = pixels[idx + 2];
}

struct SkyStageLayer
{
    Q3ShaderStage stage;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

void CollectCloudSkyLayers(const Q3ShaderDef &skyDef, const EditorAssetSource *assets,
                           std::vector<SkyStageLayer> &outLayers)
{
    for (const Q3ShaderStage &st : skyDef.stages)
    {
        if (st.isLightmap || st.map.empty())
        {
            continue;
        }
        SkyStageLayer layer;
        layer.stage = st;
        std::string resolved;
        if (LoadQ3ImageRgba(assets, st.map, layer.pixels, layer.width, layer.height, resolved) &&
            layer.width > 0 && layer.height > 0)
        {
            outLayers.push_back(std::move(layer));
        }
    }
}

void SampleCloudSkyFace(int axis, uint32_t faceSize, float cloudHeight,
                        const std::vector<SkyStageLayer> &layers, std::vector<uint8_t> &face,
                        double sum[3], uint64_t &sampleCount)
{
    face.resize(size_t(faceSize) * faceSize * 4u, 255u);
    for (uint32_t y = 0; y < faceSize; y++)
    {
        const float t = 1.0f - 2.0f * ((float(y) + 0.5f) / float(faceSize));
        for (uint32_t x = 0; x < faceSize; x++)
        {
            const float s = 2.0f * ((float(x) + 0.5f) / float(faceSize)) - 1.0f;
            float dir[3] = {};
            SkyAxisDirection(axis, s, t, dir);
            int accum[3] = { 0, 0, 0 };
            for (size_t li = 0; li < layers.size(); li++)
            {
                uint8_t px[3] = {};
                SampleCloudStagePixel(layers[li].pixels, layers[li].width, layers[li].height,
                                      layers[li].stage, dir, cloudHeight, px);
                for (int c = 0; c < 3; c++)
                {
                    accum[c] = (li == 0) ? int(px[c]) : std::min(255, accum[c] + int(px[c]));
                }
            }
            const size_t dst = (size_t(y) * faceSize + x) * 4u;
            for (int c = 0; c < 3; c++)
            {
                face[dst + c] = uint8_t(accum[c]);
                sum[c] += accum[c];
            }
            sampleCount++;
        }
    }
}

void BuildQ3CloudSkybox(const Q3ShaderDef &skyDef, const EditorAssetSource *assets,
                        EditorScene &scene)
{
    std::vector<SkyStageLayer> layers;
    CollectCloudSkyLayers(skyDef, assets, layers);
    if (layers.empty())
    {
        return;
    }

    static constexpr int kSkyTexOrder[6] = { 0, 2, 1, 3, 4, 5 };
    constexpr uint32_t kFaceSize = 64;
    scene.generatedSkyboxSize = kFaceSize;
    double sum[3] = { 0.0, 0.0, 0.0 };
    uint64_t sampleCount = 0;

    for (int axis = 0; axis < 6; axis++)
    {
        SampleCloudSkyFace(axis, kFaceSize, skyDef.cloudHeight, layers,
                           scene.generatedSkyboxFaces[size_t(kSkyTexOrder[axis])], sum,
                           sampleCount);
    }

    if (sampleCount > 0 && !scene.hasSkyFlatColor)
    {
        const double inv = 1.0 / (double(sampleCount) * 255.0);
        scene.skyFlatColor[0] = float(sum[0] * inv);
        scene.skyFlatColor[1] = float(sum[1] * inv);
        scene.skyFlatColor[2] = float(sum[2] * inv);
        scene.hasSkyFlatColor = true;
    }
}

void ApplyQ3SkyShaderInfo(const Q3ShaderDef *skyDef, const EditorAssetSource *assets,
                          EditorScene &scene)
{
    if (skyDef == nullptr)
    {
        return;
    }

    if (!skyDef->outerBox.empty())
    {
        scene.skyboxName = skyDef->outerBox;
    }
    else
    {
        BuildQ3CloudSkybox(*skyDef, assets, scene);
    }

    if (skyDef->hasSun)
    {
        scene.hasSun = true;
        scene.sunColor[0] = skyDef->sunColor[0];
        scene.sunColor[1] = skyDef->sunColor[1];
        scene.sunColor[2] = skyDef->sunColor[2];
        scene.sunIntensity = skyDef->sunIntensity;
        scene.sunDegrees = skyDef->sunDegrees;
        scene.sunElevation = skyDef->sunElevation;
    }
}

struct Q3SurfaceCollectContext
{
    const Q3BspDataView &view;
    const std::vector<const Q3ShaderDef *> &shaderDefs;
    std::vector<EditorSurface> &batches;
    std::vector<EditorOverlaySurface> &overlayBatches;
    std::vector<EditorOverlaySurface> &postOverlayBatches;
    EditorScene &scene;
    Q3BspClusterState &clusterState;
    const Q3ShaderDef *firstSkyDef = nullptr;
    uint32_t nextPatchOverlayGroup = 2u;
};

bool BuildSurfaceMeshData(const Q3DSurface &surf, const Q3BspDataView &view,
                          const float baseScale[2], std::vector<QrVertex> &verts,
                          std::vector<uint32_t> &indices, int patchDims[2])
{
    if (surf.surfaceType == kMstPatch)
    {
        TessellateQ3PatchMesh(surf, view.verts, baseScale, verts, indices, patchDims[0],
                              patchDims[1]);
    }
    else
    {
        BuildQ3PolygonMesh(surf, view.verts, view.indices, baseScale, verts, indices);
    }
    return !verts.empty() && !indices.empty();
}

void ComputeOverlayLightTint(const EditorTexture &tex, float outTint[3])
{
    if (tex.hasSurfaceLightColor)
    {
        outTint[0] = tex.surfaceLightColor[0];
        outTint[1] = tex.surfaceLightColor[1];
        outTint[2] = tex.surfaceLightColor[2];
        return;
    }

    const std::vector<uint8_t> &pixels = tex.rgba;
    double sum[3] = {};
    for (size_t i = 0; i + 3 < pixels.size(); i += 4)
    {
        const double alpha = double(pixels[i + 3]) / 255.0;
        sum[0] += (double(pixels[i + 0]) / 255.0) * alpha;
        sum[1] += (double(pixels[i + 1]) / 255.0) * alpha;
        sum[2] += (double(pixels[i + 2]) / 255.0) * alpha;
    }

    const double peak = std::max({ sum[0], sum[1], sum[2] });
    if (peak > 1e-4)
    {
        outTint[0] = float(sum[0] / peak);
        outTint[1] = float(sum[1] / peak);
        outTint[2] = float(sum[2] / peak);
        return;
    }

    outTint[0] = 1.0f;
    outTint[1] = 1.0f;
    outTint[2] = 1.0f;
}

struct Q3SurfaceTri
{
    uint32_t idx[3] = {};
    double area = 0.0;
    double centroid[3] = {};
    double cross[3] = {};
};

struct Q3SurfaceBucket
{
    double areaSum = 0.0;
    double centerAccum[3] = {};
    double normAccum[3] = {};
};

bool CollectQ3SurfaceTriangles(const std::vector<QrVertex> &verts,
                               const std::vector<uint32_t> &indices,
                               std::vector<Q3SurfaceTri> &outTris, double &outTotalArea,
                               double outCenterAccum[3], double outNormAccum[3])
{
    outTris.clear();
    outTotalArea = 0.0;
    std::fill_n(outCenterAccum, 3, 0.0);
    std::fill_n(outNormAccum, 3, 0.0);

    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const uint32_t i0 = indices[i + 0];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];
        if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
        {
            continue;
        }
        const float *p0 = verts[i0].position;
        const float *p1 = verts[i1].position;
        const float *p2 = verts[i2].position;
        const double e1[3] = {
            double(p1[0]) - double(p0[0]),
            double(p1[1]) - double(p0[1]),
            double(p1[2]) - double(p0[2]),
        };
        const double e2[3] = {
            double(p2[0]) - double(p0[0]),
            double(p2[1]) - double(p0[1]),
            double(p2[2]) - double(p0[2]),
        };
        const double cr[3] = {
            e1[1] * e2[2] - e1[2] * e2[1],
            e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0],
        };
        const double area = 0.5 * std::sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
        if (!std::isfinite(area) || area < 0.5)
        {
            continue;
        }

        Q3SurfaceTri tri;
        tri.idx[0] = i0;
        tri.idx[1] = i1;
        tri.idx[2] = i2;
        tri.area = area;
        for (int c = 0; c < 3; c++)
        {
            tri.centroid[c] = (double(p0[c]) + double(p1[c]) + double(p2[c])) / 3.0;
            tri.cross[c] = cr[c];
            outCenterAccum[c] += area * tri.centroid[c];
            outNormAccum[c] += cr[c];
        }
        outTotalArea += area;
        outTris.push_back(tri);
    }

    return !outTris.empty() && outTotalArea >= 0.5;
}

void NormalizeAccumulatedNormal(const double normAccum[3], const float fallback[3],
                                float outNormal[3])
{
    const double len = std::sqrt(normAccum[0] * normAccum[0] + normAccum[1] * normAccum[1] +
                                 normAccum[2] * normAccum[2]);
    if (len > 1e-4)
    {
        outNormal[0] = float(normAccum[0] / len);
        outNormal[1] = float(normAccum[1] / len);
        outNormal[2] = float(normAccum[2] / len);
        return;
    }
    if (fallback != nullptr)
    {
        std::copy_n(fallback, 3, outNormal);
        return;
    }
    outNormal[0] = 0.0f;
    outNormal[1] = 0.0f;
    outNormal[2] = 1.0f;
}

float ComputeQ3SurfaceBoundingRadius(const std::vector<QrVertex> &verts,
                                     const std::vector<Q3SurfaceTri> &tris,
                                     const double weightedCentroid[3])
{
    double maxDistSq = 0.0;
    for (const Q3SurfaceTri &tri : tris)
    {
        for (int k = 0; k < 3; k++)
        {
            const float *p = verts[tri.idx[k]].position;
            const double dx = double(p[0]) - weightedCentroid[0];
            const double dy = double(p[1]) - weightedCentroid[1];
            const double dz = double(p[2]) - weightedCentroid[2];
            maxDistSq = std::max(maxDistSq, dx * dx + dy * dy + dz * dz);
        }
    }
    return float(std::sqrt(maxDistSq));
}

bool HasDuplicateQ3SurfaceLight(const EditorScene &scene, const float pos[3],
                                 const std::string &targetClassName, bool matchAnyLight)
{
    constexpr float kMergeDistSq = 24.0f * 24.0f;
    for (const EditorLight &existing : scene.bspLights)
    {
        if (!matchAnyLight && existing.className != targetClassName)
        {
            continue;
        }
        const float dx = existing.position[0] - pos[0];
        const float dy = existing.position[1] - pos[1];
        const float dz = existing.position[2] - pos[2];
        if ((dx * dx + dy * dy + dz * dz) <= kMergeDistSq)
        {
            return true;
        }
    }
    return false;
}

void ComputeQ3SurfaceLightTint(const Q3ShaderDef &def, const EditorTexture &tex, float outTint[3])
{
    if (def.hasLightRGB)
    {
        const float r = std::max(0.0f, def.lightRGB[0]);
        const float g = std::max(0.0f, def.lightRGB[1]);
        const float b = std::max(0.0f, def.lightRGB[2]);
        const float peak = std::max({ r, g, b });
        if (peak > 1e-4f)
        {
            outTint[0] = r / peak;
            outTint[1] = g / peak;
            outTint[2] = b / peak;
        }
        else
        {
            outTint[0] = 1.0f;
            outTint[1] = 1.0f;
            outTint[2] = 1.0f;
        }
        return;
    }
    ComputeOverlayLightTint(tex, outTint);
}

float ComputeOverlayMeanTextureFill(const EditorTexture &tex)
{
    if (tex.rgba.empty())
    {
        return 0.35f;
    }
    double fillAccum = 0.0;
    const size_t pxCount = tex.rgba.size() / 4u;
    for (size_t i = 0; i < pxCount; i++)
    {
        const uint8_t peak =
            std::max({ tex.rgba[i * 4u + 0u], tex.rgba[i * 4u + 1u], tex.rgba[i * 4u + 2u] });
        const float linPeak = float(peak) / 255.0f;
        const float alpha = float(tex.rgba[i * 4u + 3u]) / 255.0f;
        fillAccum += double(linPeak * linPeak * alpha);
    }
    return pxCount > 0u ? std::clamp(float(fillAccum / double(pxCount)), 0.14f, 1.0f) : 0.35f;
}

float ComputeNearbyLightScale(const EditorScene &scene, const float pos[3])
{
    for (const EditorLight &existing : scene.bspLights)
    {
        const float dx = existing.position[0] - pos[0];
        const float dy = existing.position[1] - pos[1];
        const float dz = existing.position[2] - pos[2];
        if ((dx * dx + dy * dy + dz * dz) <= (48.0f * 48.0f))
        {
            return 0.65f;
        }
    }
    return 1.0f;
}

void AppendQ3SurfacePointLight(const Q3ShaderDef &def, const EditorTexture &tex, const float pos[3],
                               double areaSum, float effectiveSurfaceLight, EditorScene &scene)
{
    const std::string targetClassName = !tex.name.empty() ? tex.name : std::string("light");
    const bool matchAnyLight = !(def.surfaceLight > 0.0f);
    if (HasDuplicateQ3SurfaceLight(scene, pos, targetClassName, matchAnyLight))
    {
        return;
    }

    constexpr float kQ3Pi = 3.14159265358979323846f;
    constexpr float kQ3OverlayToPoint = 0.5f / (7500.0f * kQ3Pi);

    EditorLight light;
    std::copy_n(pos, 3, light.position);
    light.radius = std::clamp(std::sqrt(float(areaSum) / (4.0f * kQ3Pi)), 6.0f, 24.0f);
    light.className = targetClassName;
    light.hasColor = true;
    const uint32_t resolvedCluster = ClusterForQ3Point(scene, light.position);
    light.cluster = resolvedCluster == 0u ? QR_CLUSTER_LIGHT_NO_CLUSTER : resolvedCluster;

    const float meanTexFill = ComputeOverlayMeanTextureFill(tex);
    const float nearbyScale = ComputeNearbyLightScale(scene, light.position);
    float tint[3] = {};
    ComputeQ3SurfaceLightTint(def, tex, tint);
    const float rawIntensity =
        effectiveSurfaceLight * float(areaSum) * kQ3OverlayToPoint * meanTexFill * nearbyScale;
    const float intensity = std::clamp(rawIntensity, 16.0f, 300.0f);
    SetEditorLightTint(light, tint);
    SetEditorLightIntensity(light, intensity);
    scene.lights.push_back(light);
    scene.bspLights.push_back(light);
}

void EmitQ3SurfacePointLights(const Q3DSurface &surf, const Q3ShaderDef &def,
                              const EditorTexture &tex, const std::vector<QrVertex> &verts,
                              const std::vector<uint32_t> &indices, EditorScene &scene)
{
    const bool isAutoSpriteOverlay = def.isNonTracedOverlay && (def.autoSprite || def.autoSprite2);
    if ((!(def.surfaceLight > 0.0f) && !isAutoSpriteOverlay) || verts.empty() ||
        indices.size() < 3)
    {
        return;
    }

    const float effectiveSurfaceLight = (def.surfaceLight > 0.0f) ? def.surfaceLight : 1500.0f;
    std::vector<Q3SurfaceTri> tris;
    double totalArea = 0.0;
    double centerAccum[3] = {};
    double normAccum[3] = {};
    if (!CollectQ3SurfaceTriangles(verts, indices, tris, totalArea, centerAccum, normAccum))
    {
        return;
    }

    const double weightedCentroid[3] = {
        centerAccum[0] / totalArea,
        centerAccum[1] / totalArea,
        centerAccum[2] / totalArea,
    };
    float weightedNormal[3] = {};
    NormalizeAccumulatedNormal(normAccum, nullptr, weightedNormal);
    const float boundingRadius = ComputeQ3SurfaceBoundingRadius(verts, tris, weightedCentroid);
    const float push = (surf.surfaceType != kMstTriangleSoup) ? 4.0f : 0.0f;

    if (boundingRadius <= 96.0f || surf.surfaceType == kMstTriangleSoup)
    {
        const float pos[3] = {
            float(weightedCentroid[0]) + weightedNormal[0] * push,
            float(weightedCentroid[1]) + weightedNormal[1] * push,
            float(weightedCentroid[2]) + weightedNormal[2] * push,
        };
        AppendQ3SurfacePointLight(def, tex, pos, totalArea, effectiveSurfaceLight, scene);
        return;
    }

    constexpr double kBucketSize = 128.0;
    std::map<std::array<int32_t, 3>, Q3SurfaceBucket> buckets;
    for (const Q3SurfaceTri &tri : tris)
    {
        const std::array<int32_t, 3> key{
            int32_t(std::floor(tri.centroid[0] / kBucketSize)),
            int32_t(std::floor(tri.centroid[1] / kBucketSize)),
            int32_t(std::floor(tri.centroid[2] / kBucketSize)),
        };
        Q3SurfaceBucket &bucket = buckets[key];
        bucket.areaSum += tri.area;
        for (int c = 0; c < 3; c++)
        {
            bucket.centerAccum[c] += tri.area * tri.centroid[c];
            bucket.normAccum[c] += tri.cross[c];
        }
    }

    for (const auto &[key, bucket] : buckets)
    {
        (void)key;
        if (bucket.areaSum < 0.5)
        {
            continue;
        }
        float bucketNormal[3] = {};
        NormalizeAccumulatedNormal(bucket.normAccum, weightedNormal, bucketNormal);
        const float pos[3] = {
            float(bucket.centerAccum[0] / bucket.areaSum) + bucketNormal[0] * push,
            float(bucket.centerAccum[1] / bucket.areaSum) + bucketNormal[1] * push,
            float(bucket.centerAccum[2] / bucket.areaSum) + bucketNormal[2] * push,
        };
        AppendQ3SurfacePointLight(def, tex, pos, bucket.areaSum, effectiveSurfaceLight, scene);
    }
}

bool ShouldSkipQ3Surface(const Q3DShader &dshader, const Q3ShaderDef *def)
{
    const bool isFog =
        (dshader.contentFlags & kContentsFog) != 0 || (def != nullptr && def->isFog);
    return (dshader.surfaceFlags & kSurfNoDraw) != 0 || isFog ||
           (def != nullptr && def->isNoDraw);
}

struct Q3OverlayStageSlice
{
    bool cutoutUnderlay = false;
    size_t firstStage = 0;
    size_t endStage = 0;
    float normalOffset = 0.0f;
};

void InitOverlayBatchMeta(const Q3ShaderDef &def, const std::string &shaderName,
                          const Q3OverlayStageSlice &slice, EditorOverlaySurface &dst)
{
    if (!dst.stages.empty())
    {
        return;
    }
    dst.shaderName = shaderName;
    dst.tracedBase = def.tracedBase;
    dst.cutoutUnderlay = slice.cutoutUnderlay;
    dst.metallicEnvBase = def.metallicEnvBase;
    dst.twoSided = def.twoSided;
    dst.autoSprite = def.autoSprite;
    dst.autoSprite2 = def.autoSprite2;
    dst.deformWave = def.deformWave;
    dst.deformSpread = def.deformSpread;
    dst.deformBase = def.deformBase;
    dst.deformAmplitude = def.deformAmplitude;
    dst.deformPhase = def.deformPhase;
    dst.deformFrequency = def.deformFrequency;
    dst.surfaceLight = def.surfaceLight;
    dst.baseStageIndex = def.baseStageIndex;
    dst.firstOverlayStage = slice.firstStage;
    dst.endOverlayStage = std::min(slice.endStage, def.stages.size());
    dst.stages = def.stages;
}

void AppendQ3OverlayMesh(const std::vector<QrVertex> &verts, const std::vector<uint32_t> &indices,
                         const Q3ShaderDef &def, const std::string &shaderName,
                         const Q3OverlayStageSlice &slice, EditorOverlaySurface &dst)
{
    if (slice.firstStage >= std::min(slice.endStage, def.stages.size()))
    {
        return;
    }
    InitOverlayBatchMeta(def, shaderName, slice, dst);

    std::vector<QrVertex> stageVerts = verts;
    if (slice.normalOffset != 0.0f)
    {
        OffsetQ3VerticesAlongSmoothNormals(stageVerts, indices, slice.normalOffset);
    }

    const uint32_t baseVertex = uint32_t(dst.vertices.size());
    dst.vertices.insert(dst.vertices.end(), stageVerts.begin(), stageVerts.end());
    dst.indices.reserve(dst.indices.size() + indices.size());
    for (uint32_t idx : indices)
    {
        dst.indices.push_back(baseVertex + idx);
    }
}

void AppendQ3SurfaceToBatch(const Q3DSurface &surf, const Q3DShader &dshader,
                            const Q3ShaderDef *def, const int patchDims[2],
                            std::vector<QrVertex> &verts, const std::vector<uint32_t> &indices,
                            Q3SurfaceCollectContext &ctx)
{
    EditorSurface &batch = ctx.batches[size_t(surf.shaderNum)];
    if (batch.texture < 0)
    {
        batch.texture = surf.shaderNum;
        batch.textureName = ctx.scene.textures[size_t(surf.shaderNum)].name;
        batch.kind = ClassifyQ3SurfaceKind(dshader, def);
        batch.hasAuthoredNormals = true;
        batch.isCutoutUnderlay = def != nullptr && def->cutoutFrameUnderlay;
        batch.noMarks = ((dshader.surfaceFlags & (kSurfNoImpact | kSurfNoMarks)) != 0) ||
                        ((dshader.contentFlags & kContentsFog) != 0);
    }
    else if ((dshader.surfaceFlags & (kSurfNoImpact | kSurfNoMarks)) != 0 ||
             (dshader.contentFlags & kContentsFog) != 0)
    {
        batch.noMarks = true;
    }
    batch.isOverlayOnly = false;

    const bool isModelMesh =
        (surf.surfaceType == kMstTriangleSoup) || (batch.textureName.rfind("models/", 0) == 0);
    AppendRepresentativeFaces(surf.surfaceType, patchDims[0], patchDims[1], verts, indices, batch,
                              isModelMesh ? 1u : 0u);
    std::vector<uint32_t> appendedIndices = indices;
    if (def != nullptr && def->twoSided)
    {
        AppendTwoSidedGeometry(verts, appendedIndices);
    }

    const uint32_t baseVertex = uint32_t(batch.vertices.size());
    batch.vertices.insert(batch.vertices.end(), verts.begin(), verts.end());
    batch.indices.reserve(batch.indices.size() + appendedIndices.size());
    for (uint32_t idx : appendedIndices)
    {
        batch.indices.push_back(baseVertex + idx);
    }
}

void EmitNonTracedOverlaySurface(const Q3DSurface &surf, const Q3DShader &dshader,
                                 const Q3ShaderDef &def, const int patchDims[2],
                                 std::vector<QrVertex> &verts,
                                 const std::vector<uint32_t> &indices, Q3SurfaceCollectContext &ctx)
{
    const EditorTexture &tex = ctx.scene.textures[size_t(surf.shaderNum)];
    EmitQ3SurfacePointLights(surf, def, tex, verts, indices, ctx.scene);
    AssignVertexClustersAndBounds(verts, ctx.scene, ctx.clusterState, false);

    EditorOverlaySurface individualOverlay;
    AppendQ3OverlayMesh(verts, indices, def, tex.name,
                        { false, 0u, def.stages.size(), 0.0f }, individualOverlay);
    if (!individualOverlay.vertices.empty() && !individualOverlay.indices.empty())
    {
        ctx.scene.overlaySurfaces.push_back(std::move(individualOverlay));
    }

    EditorSurface &batch = ctx.batches[size_t(surf.shaderNum)];
    if (batch.texture < 0)
    {
        batch.texture = surf.shaderNum;
        batch.textureName = tex.name;
        batch.kind = ClassifyQ3SurfaceKind(dshader, &def);
        batch.hasAuthoredNormals = true;
        batch.isOverlayOnly = true;
    }
    const uint32_t overlayGroupTag =
        (surf.surfaceType == kMstPatch) ? ctx.nextPatchOverlayGroup++ : 1u;
    AppendRepresentativeFaces(surf.surfaceType, patchDims[0], patchDims[1], verts, indices, batch,
                              overlayGroupTag);
    if (def.autoSprite || def.autoSprite2)
    {
        AppendCrossedAutoSpritePickingFace(verts, indices, def.autoSprite2, batch,
                                           overlayGroupTag);
    }
    const uint32_t baseVertex = uint32_t(batch.vertices.size());
    batch.vertices.insert(batch.vertices.end(), verts.begin(), verts.end());
    batch.indices.reserve(batch.indices.size() + indices.size());
    for (uint32_t idx : indices)
    {
        batch.indices.push_back(baseVertex + idx);
    }
}

void AppendStandardQ3OverlayBatches(const Q3DSurface &surf, const Q3ShaderDef &def,
                                    const std::vector<QrVertex> &overlayVerts,
                                    const std::vector<uint32_t> &indices,
                                    Q3SurfaceCollectContext &ctx)
{
    const std::string &shaderName = ctx.scene.textures[size_t(surf.shaderNum)].name;
    if (def.cutoutFrameUnderlay && def.baseStageIndex > 0u &&
        def.baseStageIndex <= def.stages.size())
    {
        AppendQ3OverlayMesh(overlayVerts, indices, def, shaderName,
                            { true, 0u, def.baseStageIndex, -0.8f },
                            ctx.overlayBatches[size_t(surf.shaderNum)]);
        if (def.baseStageIndex + 1u < def.stages.size())
        {
            AppendQ3OverlayMesh(overlayVerts, indices, def, shaderName,
                                { false, def.baseStageIndex + 1u, def.stages.size(), 0.25f },
                                ctx.postOverlayBatches[size_t(surf.shaderNum)]);
        }
        return;
    }

    const size_t firstStage = (def.tracedBase && def.baseStageIndex < def.stages.size())
                                  ? (def.baseStageIndex + 1u)
                                  : 0u;
    const float normalOffset = (def.tracedBase || !def.isNonTracedOverlay) ? 0.25f : 0.0f;
    AppendQ3OverlayMesh(overlayVerts, indices, def, shaderName,
                        { false, firstStage, def.stages.size(), normalOffset },
                        ctx.overlayBatches[size_t(surf.shaderNum)]);
}

void ApplyBaseScaleAndAppendSurface(const Q3DSurface &surf, const Q3DShader &dshader,
                                    const Q3ShaderDef *def, const int patchDims[2],
                                    bool isSky, std::vector<QrVertex> &verts,
                                    const std::vector<uint32_t> &indices,
                                    Q3SurfaceCollectContext &ctx)
{
    if (def != nullptr && (def->baseScale[0] != 1.0f || def->baseScale[1] != 1.0f))
    {
        for (QrVertex &v : verts)
        {
            v.texCoord[0] *= def->baseScale[0];
            v.texCoord[1] *= def->baseScale[1];
        }
    }

    AssignVertexClustersAndBounds(verts, ctx.scene, ctx.clusterState, isSky);
    AppendQ3SurfaceToBatch(surf, dshader, def, patchDims, verts, indices, ctx);
}

void EmitTracedQ3Surface(const Q3DSurface &surf, const Q3DShader &dshader, const Q3ShaderDef *def,
                         const int patchDims[2], bool isSky, std::vector<QrVertex> &verts,
                         std::vector<uint32_t> &indices, Q3SurfaceCollectContext &ctx)
{
    if (def != nullptr && !isSky)
    {
        EmitQ3SurfacePointLights(surf, *def, ctx.scene.textures[size_t(surf.shaderNum)], verts,
                                 indices, ctx.scene);
    }

    const bool wantOverlay = def != nullptr && def->usesOverlay && !isSky;
    std::vector<QrVertex> overlayVerts = wantOverlay ? verts : std::vector<QrVertex>{};
    ApplyBaseScaleAndAppendSurface(surf, dshader, def, patchDims, isSky, verts, indices, ctx);

    if (wantOverlay && !overlayVerts.empty())
    {
        AppendStandardQ3OverlayBatches(surf, *def, overlayVerts, indices, ctx);
    }
}

void EmitSingleQ3Surface(const Q3DSurface &surf, Q3SurfaceCollectContext &ctx)
{
    const Q3DShader &dshader = ctx.view.shaders[surf.shaderNum];
    const Q3ShaderDef *def = ctx.shaderDefs[size_t(surf.shaderNum)];
    if (ShouldSkipQ3Surface(dshader, def))
    {
        return;
    }

    const bool isSky = (dshader.surfaceFlags & kSurfSky) != 0 || (def != nullptr && def->isSky);
    if (isSky && ctx.firstSkyDef == nullptr)
    {
        ctx.firstSkyDef = def;
    }

    static constexpr float kIdentityScale[2] = { 1.0f, 1.0f };
    std::vector<QrVertex> verts;
    std::vector<uint32_t> indices;
    int patchDims[2] = { 0, 0 };
    if (!BuildSurfaceMeshData(surf, ctx.view, kIdentityScale, verts, indices, patchDims))
    {
        return;
    }

    if (def != nullptr && def->isNonTracedOverlay && !isSky)
    {
        EmitNonTracedOverlaySurface(surf, dshader, *def, patchDims, verts, indices, ctx);
        return;
    }

    EmitTracedQ3Surface(surf, dshader, def, patchDims, isSky, verts, indices, ctx);
}

void CollectQ3SurfaceRange(int firstSurface, int numSurfaces, Q3SurfaceCollectContext &ctx)
{
    const int endSurface = std::min(ctx.view.surfaceCount, firstSurface + numSurfaces);
    for (int sIdx = std::max(0, firstSurface); sIdx < endSurface; sIdx++)
    {
        const Q3DSurface &surf = ctx.view.surfaces[sIdx];
        if (ValidateQ3Surface(surf, ctx.view))
        {
            EmitSingleQ3Surface(surf, ctx);
        }
    }
}

struct FaceAabb
{
    float mn[3] = {};
    float mx[3] = {};
};

FaceAabb ComputeFaceAabb(const EditorSurfaceFace &face)
{
    FaceAabb box;
    std::copy_n(face.positions[0].data, 3, box.mn);
    std::copy_n(face.positions[0].data, 3, box.mx);
    for (size_t i = 1; i < face.positions.size(); i++)
    {
        for (int axis = 0; axis < 3; axis++)
        {
            box.mn[axis] = std::min(box.mn[axis], face.positions[i].data[axis]);
            box.mx[axis] = std::max(box.mx[axis], face.positions[i].data[axis]);
        }
    }
    return box;
}

bool BoxesOverlapWithin(const FaceAabb &a, const FaceAabb &b, float pad)
{
    for (int axis = 0; axis < 3; axis++)
    {
        if (a.mn[axis] > b.mx[axis] + pad || b.mn[axis] > a.mx[axis] + pad)
        {
            return false;
        }
    }
    return true;
}

size_t FindClusterRoot(std::vector<size_t> &parent, size_t x)
{
    while (parent[x] != x)
    {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

void BuildFaceClustersUnionFind(const std::vector<FaceAabb> &boxes,
                                const std::vector<uint32_t> &groupTags, float expand,
                                std::vector<size_t> &parent)
{
    const size_t n = boxes.size();
    parent.resize(n);
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; i++)
    {
        parent[i] = i;
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return boxes[a].mn[0] < boxes[b].mn[0]; });

    for (size_t a = 0; a < n; a++)
    {
        const size_t ia = order[a];
        const float limitX = boxes[ia].mx[0] + expand;
        for (size_t b = a + 1; b < n; b++)
        {
            const size_t ib = order[b];
            if (boxes[ib].mn[0] > limitX)
            {
                break;
            }
            const size_t ra = FindClusterRoot(parent, ia);
            const size_t rb = FindClusterRoot(parent, ib);
            if (ra != rb && groupTags[ia] == groupTags[ib] &&
                BoxesOverlapWithin(boxes[ia], boxes[ib], expand))
            {
                parent[rb] = ra;
            }
        }
    }
}

void EmitClusteredAliasInstance(EditorSurface &batch, const std::vector<size_t> &clusteredFaces,
                                const std::vector<size_t> &members, const std::vector<FaceAabb> &boxes,
                                EditorScene &scene)
{
    FaceAabb box = boxes[members[0]];
    for (size_t k = 1; k < members.size(); k++)
    {
        const FaceAabb &mb = boxes[members[k]];
        for (int axis = 0; axis < 3; axis++)
        {
            box.mn[axis] = std::min(box.mn[axis], mb.mn[axis]);
            box.mx[axis] = std::max(box.mx[axis], mb.mx[axis]);
        }
    }

    float origin[3] = {};
    float diagSq = 0.0f;
    for (int axis = 0; axis < 3; axis++)
    {
        const float mid = 0.5f * (box.mn[axis] + box.mx[axis]);
        if (box.mx[axis] - box.mn[axis] < 4.0f)
        {
            box.mn[axis] = mid - 2.0f;
            box.mx[axis] = mid + 2.0f;
        }
        origin[axis] = mid;
        const float span = box.mx[axis] - box.mn[axis];
        diagSq += span * span;
    }

    const uint32_t instanceId = uint32_t(scene.aliasInstances.size()) + 1u;
    for (size_t m : members)
    {
        batch.faces[clusteredFaces[m]].aliasInstanceId = instanceId;
    }

    EditorAliasInstance inst;
    inst.modelName = batch.textureName;
    inst.textureName = batch.textureName;
    std::copy_n(origin, 3, inst.origin);
    inst.modelSizeMeters = std::max(0.5f * std::sqrt(diagSq) / kEditorUnitsPerMeter, 0.001f);
    inst.uniqueID = 0;
    inst.aliasInstanceId = instanceId;
    inst.hasBounds = true;
    std::copy_n(box.mn, 3, inst.boundsMin);
    std::copy_n(box.mx, 3, inst.boundsMax);
    scene.aliasInstances.push_back(std::move(inst));
}

void ClusterBatchAliasInstances(EditorSurface &batch, EditorScene &scene)
{
    std::vector<size_t> clusteredFaces;
    std::vector<FaceAabb> boxes;
    std::vector<uint32_t> groupTags;
    for (size_t i = 0; i < batch.faces.size(); i++)
    {
        if (batch.faces[i].aliasInstanceId != 0 && !batch.faces[i].positions.empty())
        {
            clusteredFaces.push_back(i);
            boxes.push_back(ComputeFaceAabb(batch.faces[i]));
            groupTags.push_back(batch.faces[i].aliasInstanceId);
        }
    }
    if (clusteredFaces.empty())
    {
        return;
    }

    std::vector<size_t> parent;
    BuildFaceClustersUnionFind(boxes, groupTags, 4.0f, parent);

    std::unordered_map<size_t, size_t> groupIndexByRoot;
    std::vector<std::vector<size_t>> groups;
    for (size_t i = 0; i < clusteredFaces.size(); i++)
    {
        const size_t root = FindClusterRoot(parent, i);
        const auto it = groupIndexByRoot.find(root);
        if (it == groupIndexByRoot.end())
        {
            groupIndexByRoot.emplace(root, groups.size());
            groups.push_back({ i });
        }
        else
        {
            groups[it->second].push_back(i);
        }
    }

    for (const std::vector<size_t> &members : groups)
    {
        EmitClusteredAliasInstance(batch, clusteredFaces, members, boxes, scene);
    }
}

void MergeCollectedQ3Surfaces(std::vector<EditorSurface> &batches,
                              std::vector<EditorOverlaySurface> &overlayBatches,
                              std::vector<EditorOverlaySurface> &postOverlayBatches,
                              EditorScene &scene)
{
    for (EditorSurface &batch : batches)
    {
        if (batch.vertices.empty() || batch.indices.empty())
        {
            continue;
        }
        ClusterBatchAliasInstances(batch, scene);
        const uint32_t baseVertex = uint32_t(scene.vertices.size());
        scene.vertices.insert(scene.vertices.end(), batch.vertices.begin(), batch.vertices.end());
        scene.indices.reserve(scene.indices.size() + batch.indices.size());
        for (uint32_t idx : batch.indices)
        {
            scene.indices.push_back(baseVertex + idx);
        }
        scene.surfaces.push_back(std::move(batch));
    }

    for (size_t i = 0; i < overlayBatches.size(); i++)
    {
        if (!overlayBatches[i].vertices.empty() && !overlayBatches[i].indices.empty())
        {
            scene.overlaySurfaces.push_back(std::move(overlayBatches[i]));
        }
        if (!postOverlayBatches[i].vertices.empty() && !postOverlayBatches[i].indices.empty())
        {
            scene.overlaySurfaces.push_back(std::move(postOverlayBatches[i]));
        }
    }
}

} // namespace

bool IsQuake3BspMemory(const std::vector<uint8_t> &bytes)
{
    if (bytes.size() < 8)
    {
        return false;
    }
    int32_t version = 0;
    std::memcpy(&version, bytes.data() + 4, 4);
    return bytes[0] == 'I' && bytes[1] == 'B' && bytes[2] == 'S' && bytes[3] == 'P' &&
           version == kQ3BspVersion;
}

bool LoadQ3BspSceneMemory(const std::vector<uint8_t> &bytes, const std::string &sourceName,
                          EditorScene &scene, std::string &error,
                          const EditorSceneLoadOptions &options)
{
    (void)sourceName;
    scene = EditorScene{};
    scene.usesExternalLightingModel = true;

    Q3BspDataView view;
    if (!ParseQ3BspView(bytes, view, error))
    {
        return false;
    }

    Q3ShaderDatabase shaderDb;
    if (options.assets != nullptr)
    {
        shaderDb.LoadFromAssets(*options.assets);
    }

    Q3BspClusterState clusterState;
    BuildQ3ClusterTree(view, scene, clusterState);

    std::vector<const Q3ShaderDef *> shaderDefs;
    BuildQ3TexturesAndSky(view, shaderDb, options.assets, scene, shaderDefs);

    std::unordered_set<int> hiddenSubmodels;
    ParseAllQ3Entities(view, scene, hiddenSubmodels);

    std::vector<EditorSurface> batches(size_t(view.shaderCount));
    std::vector<EditorOverlaySurface> overlayBatches(size_t(view.shaderCount));
    std::vector<EditorOverlaySurface> postOverlayBatches(size_t(view.shaderCount));
    Q3SurfaceCollectContext collectCtx{
        view, shaderDefs, batches, overlayBatches, postOverlayBatches, scene, clusterState, nullptr,
    };

    if (view.modelCount > 0)
    {
        CollectQ3SurfaceRange(view.models[0].firstSurface, view.models[0].numSurfaces, collectCtx);
        if (options.includeInlineSubmodels)
        {
            for (int m = 1; m < view.modelCount; m++)
            {
                if (hiddenSubmodels.find(m) == hiddenSubmodels.end())
                {
                    CollectQ3SurfaceRange(view.models[m].firstSurface, view.models[m].numSurfaces,
                                          collectCtx);
                }
            }
        }
    }
    else
    {
        CollectQ3SurfaceRange(0, view.surfaceCount, collectCtx);
    }

    MergeCollectedQ3Surfaces(batches, overlayBatches, postOverlayBatches, scene);

    if (scene.surfaces.empty())
    {
        error = "Q3 BSP contains no drawable surfaces";
        return false;
    }

    ApplyQ3SkyShaderInfo(collectCtx.firstSkyDef, options.assets, scene);
    FinalizeQ3ClustersAndPvs(view, scene, clusterState);
    return true;
}
