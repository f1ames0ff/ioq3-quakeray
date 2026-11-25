#include "Q3Model.h"

#include "Assets.h"
#include "Q3Shader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace
{

constexpr int32_t kMd3Version = 15;
constexpr size_t kMd3HeaderBytes = 108;
constexpr size_t kMd3TagBytes = 112;
constexpr size_t kMd3SurfaceHeaderBytes = 108;
constexpr float kMd3XyzScale = 1.0f / 64.0f;
constexpr float kPi = 3.14159265358979323846f;

#pragma pack(push, 1)
struct Md3Header
{
    char ident[4];
    int32_t version;
    char name[64];
    int32_t flags;
    int32_t numFrames;
    int32_t numTags;
    int32_t numSurfaces;
    int32_t numSkins;
    int32_t ofsFrames;
    int32_t ofsTags;
    int32_t ofsSurfaces;
    int32_t ofsEnd;
};

struct Md3Tag
{
    char name[64];
    float origin[3];
    float axis[3][3];
};

struct Md3SurfaceHeader
{
    char ident[4];
    char name[64];
    int32_t flags;
    int32_t numFrames;
    int32_t numShaders;
    int32_t numVerts;
    int32_t numTriangles;
    int32_t ofsTriangles;
    int32_t ofsShaders;
    int32_t ofsSt;
    int32_t ofsXyzNormals;
    int32_t ofsEnd;
};

struct Md3ShaderRecord
{
    char name[64];
    int32_t shaderIndex;
};

struct Md3TriangleRecord
{
    int32_t indexes[3];
};

struct Md3StRecord
{
    float st[2];
};

struct Md3XyzNormalRecord
{
    int16_t xyz[3];
    int16_t normal;
};
#pragma pack(pop)

static_assert(sizeof(Md3Header) == kMd3HeaderBytes, "unexpected Md3Header size");
static_assert(sizeof(Md3Tag) == kMd3TagBytes, "unexpected Md3Tag size");
static_assert(sizeof(Md3SurfaceHeader) == kMd3SurfaceHeaderBytes, "unexpected Md3SurfaceHeader");

struct ParsedMd3Surface
{
    std::string surfaceName;
    std::string defaultShader;
    int numFrames = 0;
    int numVerts = 0;
    std::vector<uint32_t> indices;
    std::vector<float> texCoords;
    std::vector<float> positions;
    std::vector<float> normals;
};

struct ParsedMd3TagFrame
{
    float origin[3] = {};
    float axis[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
};

struct ParsedMd3Model
{
    int numFrames = 0;
    int numTags = 0;
    std::unordered_map<std::string, int> tagIndexByName;
    std::vector<ParsedMd3TagFrame> tags;
    std::vector<ParsedMd3Surface> surfaces;
};

struct Md3Transform
{
    float origin[3] = {};
    float axis[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
};

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

void DecodeMd3Normal(int16_t packed, float out[3])
{
    const uint32_t u = static_cast<uint16_t>(packed);
    const uint32_t lat = (u >> 8u) & 0xffu;
    const uint32_t lng = u & 0xffu;
    const float latRad = float(lat) * (2.0f * kPi / 256.0f);
    const float lngRad = float(lng) * (2.0f * kPi / 256.0f);
    out[0] = std::cos(latRad) * std::sin(lngRad);
    out[1] = std::sin(latRad) * std::sin(lngRad);
    out[2] = std::cos(lngRad);
    NormalizeVec3(out);
}

std::string FixedCString(const char *buf, size_t cap)
{
    size_t len = 0;
    while (len < cap && buf[len] != '\0')
    {
        len++;
    }
    return std::string(buf, len);
}

bool ValidRange(size_t total, int32_t ofs, size_t byteSize)
{
    if (ofs < 0)
    {
        return false;
    }
    const size_t start = static_cast<size_t>(ofs);
    return start <= total && byteSize <= total - start;
}

bool ParseMd3SurfaceTris(const uint8_t *surfBytes, const Md3SurfaceHeader &sh,
                         std::vector<uint32_t> &outIndices)
{
    outIndices.reserve(size_t(sh.numTriangles) * 3u);
    const auto *tris = reinterpret_cast<const Md3TriangleRecord *>(surfBytes + sh.ofsTriangles);
    for (int t = 0; t < sh.numTriangles; t++)
    {
        const int32_t i0 = tris[t].indexes[0];
        const int32_t i1 = tris[t].indexes[1];
        const int32_t i2 = tris[t].indexes[2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= sh.numVerts || i1 >= sh.numVerts ||
            i2 >= sh.numVerts)
        {
            return false;
        }
        outIndices.push_back(uint32_t(i0));
        outIndices.push_back(uint32_t(i2));
        outIndices.push_back(uint32_t(i1));
    }
    return true;
}

void ParseMd3SurfaceVertices(const uint8_t *surfBytes, const Md3SurfaceHeader &sh,
                             ParsedMd3Surface &out)
{
    const auto *sts = reinterpret_cast<const Md3StRecord *>(surfBytes + sh.ofsSt);
    out.texCoords.resize(size_t(sh.numVerts) * 2u);
    for (int v = 0; v < sh.numVerts; v++)
    {
        out.texCoords[size_t(v) * 2u + 0] = sts[v].st[0];
        out.texCoords[size_t(v) * 2u + 1] = sts[v].st[1];
    }

    const size_t totalVerts = size_t(sh.numFrames) * size_t(sh.numVerts);
    out.positions.resize(totalVerts * 3u);
    out.normals.resize(totalVerts * 3u);
    const auto *xyzNorm =
        reinterpret_cast<const Md3XyzNormalRecord *>(surfBytes + sh.ofsXyzNormals);
    for (size_t v = 0; v < totalVerts; v++)
    {
        out.positions[v * 3u + 0] = float(xyzNorm[v].xyz[0]) * kMd3XyzScale;
        out.positions[v * 3u + 1] = float(xyzNorm[v].xyz[1]) * kMd3XyzScale;
        out.positions[v * 3u + 2] = float(xyzNorm[v].xyz[2]) * kMd3XyzScale;
        DecodeMd3Normal(xyzNorm[v].normal, &out.normals[v * 3u]);
    }
}

bool ParseMd3Surface(const std::vector<uint8_t> &bytes, size_t surfOfs, size_t surfEnd,
                     ParsedMd3Surface &out)
{
    if (surfEnd - surfOfs < kMd3SurfaceHeaderBytes)
    {
        return false;
    }

    Md3SurfaceHeader sh = {};
    std::memcpy(&sh, bytes.data() + surfOfs, sizeof(sh));
    if (std::memcmp(sh.ident, "IDP3", 4) != 0 || sh.numFrames <= 0 || sh.numVerts <= 0 ||
        sh.numTriangles <= 0)
    {
        return false;
    }

    const size_t surfLen = surfEnd - surfOfs;
    const size_t triBytes = size_t(sh.numTriangles) * sizeof(Md3TriangleRecord);
    const size_t stBytes = size_t(sh.numVerts) * sizeof(Md3StRecord);
    const size_t vertBytes = size_t(sh.numFrames) * size_t(sh.numVerts) * sizeof(Md3XyzNormalRecord);
    if (!ValidRange(surfLen, sh.ofsTriangles, triBytes) ||
        !ValidRange(surfLen, sh.ofsSt, stBytes) ||
        !ValidRange(surfLen, sh.ofsXyzNormals, vertBytes))
    {
        return false;
    }

    out.surfaceName = NormalizeQ3AssetPath(FixedCString(sh.name, sizeof(sh.name)));
    out.numFrames = sh.numFrames;
    out.numVerts = sh.numVerts;

    if (sh.numShaders > 0 && ValidRange(surfLen, sh.ofsShaders, sizeof(Md3ShaderRecord)))
    {
        Md3ShaderRecord sr = {};
        std::memcpy(&sr, bytes.data() + surfOfs + sh.ofsShaders, sizeof(sr));
        out.defaultShader = NormalizeQ3AssetPath(FixedCString(sr.name, sizeof(sr.name)));
    }

    if (!ParseMd3SurfaceTris(bytes.data() + surfOfs, sh, out.indices))
    {
        return false;
    }
    ParseMd3SurfaceVertices(bytes.data() + surfOfs, sh, out);
    return true;
}

void ParseMd3TagTable(const std::vector<uint8_t> &bytes, const Md3Header &hdr, ParsedMd3Model &out)
{
    if (hdr.numTags <= 0)
    {
        return;
    }

    const size_t totalTags = size_t(hdr.numFrames) * size_t(hdr.numTags);
    out.tags.resize(totalTags);
    const auto *rawTags = reinterpret_cast<const Md3Tag *>(bytes.data() + hdr.ofsTags);
    for (int t = 0; t < hdr.numTags; t++)
    {
        const std::string tagName = NormalizeQ3AssetPath(FixedCString(rawTags[t].name, 64));
        if (!tagName.empty())
        {
            out.tagIndexByName.try_emplace(tagName, t);
        }
    }
    for (size_t i = 0; i < totalTags; i++)
    {
        std::memcpy(out.tags[i].origin, rawTags[i].origin, sizeof(out.tags[i].origin));
        std::memcpy(out.tags[i].axis, rawTags[i].axis, sizeof(out.tags[i].axis));
    }
}

bool ParseMd3Surfaces(const std::vector<uint8_t> &bytes, const Md3Header &hdr, ParsedMd3Model &out,
                      std::string &error)
{
    int32_t cursor = hdr.ofsSurfaces;
    for (int s = 0; s < hdr.numSurfaces; s++)
    {
        if (cursor < 0 || size_t(cursor) + kMd3SurfaceHeaderBytes > bytes.size())
        {
            error = "MD3 surface offset out of bounds";
            return false;
        }
        Md3SurfaceHeader sh = {};
        std::memcpy(&sh, bytes.data() + cursor, sizeof(sh));
        if (sh.ofsEnd < int32_t(kMd3SurfaceHeaderBytes) ||
            !ValidRange(bytes.size(), cursor, size_t(sh.ofsEnd)))
        {
            error = "MD3 surface length out of bounds";
            return false;
        }
        ParsedMd3Surface parsedSurf;
        if (ParseMd3Surface(bytes, size_t(cursor), size_t(cursor) + size_t(sh.ofsEnd), parsedSurf))
        {
            out.surfaces.push_back(std::move(parsedSurf));
        }
        cursor += sh.ofsEnd;
    }

    if (out.surfaces.empty())
    {
        error = "MD3 contains no valid surfaces";
        return false;
    }
    return true;
}

bool ParseMd3Binary(const std::vector<uint8_t> &bytes, ParsedMd3Model &out, std::string &error)
{
    if (bytes.size() < kMd3HeaderBytes)
    {
        error = "MD3 file shorter than header";
        return false;
    }

    Md3Header hdr = {};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    if (std::memcmp(hdr.ident, "IDP3", 4) != 0 || hdr.version != kMd3Version ||
        hdr.numFrames <= 0 || hdr.numSurfaces <= 0 || hdr.numTags < 0)
    {
        error = "invalid MD3 header/version/counts";
        return false;
    }

    const size_t tagBytes = size_t(hdr.numFrames) * size_t(hdr.numTags) * kMd3TagBytes;
    if (hdr.numTags > 0 && !ValidRange(bytes.size(), hdr.ofsTags, tagBytes))
    {
        error = "MD3 tag table out of bounds";
        return false;
    }

    out = ParsedMd3Model{};
    out.numFrames = hdr.numFrames;
    out.numTags = hdr.numTags;
    ParseMd3TagTable(bytes, hdr, out);
    return ParseMd3Surfaces(bytes, hdr, out, error);
}

Md3Transform ComputeChildTagTransform(const ParsedMd3Model &parent, int parentFrame,
                                      const char *tagName, const Md3Transform &parentXf)
{
    Md3Transform out = parentXf;
    const auto it = parent.tagIndexByName.find(NormalizeQ3AssetPath(tagName));
    if (it == parent.tagIndexByName.end() || parent.numTags <= 0)
    {
        return out;
    }

    const int clampedFrame = std::clamp(parentFrame, 0, std::max(0, parent.numFrames - 1));
    const ParsedMd3TagFrame &tag =
        parent.tags[size_t(clampedFrame) * size_t(parent.numTags) + size_t(it->second)];

    for (int m = 0; m < 3; m++)
    {
        out.origin[m] = parentXf.origin[m] + tag.origin[0] * parentXf.axis[0][m] +
                        tag.origin[1] * parentXf.axis[1][m] + tag.origin[2] * parentXf.axis[2][m];
    }

    for (int r = 0; r < 3; r++)
    {
        for (int m = 0; m < 3; m++)
        {
            out.axis[r][m] = tag.axis[r][0] * parentXf.axis[0][m] +
                             tag.axis[r][1] * parentXf.axis[1][m] +
                             tag.axis[r][2] * parentXf.axis[2][m];
        }
    }
    return out;
}

void AppendTwoSidedSection(EditorPreviewModelSection &sec)
{
    const uint32_t baseVert = uint32_t(sec.vertices.size());
    const size_t baseIdxCount = sec.indices.size();
    sec.vertices.reserve(sec.vertices.size() * 2u);
    for (uint32_t i = 0; i < baseVert; i++)
    {
        QrVertex v = sec.vertices[i];
        v.normal[0] = -v.normal[0];
        v.normal[1] = -v.normal[1];
        v.normal[2] = -v.normal[2];
        sec.vertices.push_back(v);
    }
    sec.indices.reserve(baseIdxCount * 2u);
    for (size_t i = 0; i + 2 < baseIdxCount; i += 3)
    {
        sec.indices.push_back(baseVert + sec.indices[i]);
        sec.indices.push_back(baseVert + sec.indices[i + 2]);
        sec.indices.push_back(baseVert + sec.indices[i + 1]);
    }
}

void AppendTransformedMd3Part(const ParsedMd3Model &model, int frameIndex, const Md3Transform &xf,
                              const std::unordered_map<std::string, std::string> &defaultSkinMap,
                              const Q3ShaderDatabase &shaders, EditorPreviewModelFrame &outFrame)
{
    const int safeModelFrame = std::clamp(frameIndex, 0, std::max(0, model.numFrames - 1));
    for (const ParsedMd3Surface &surf : model.surfaces)
    {
        const int surfFrame = std::min(safeModelFrame, std::max(0, surf.numFrames - 1));
        const size_t frameBase = size_t(surfFrame) * size_t(surf.numVerts) * 3u;

        std::string matName = surf.defaultShader;
        const auto skinIt = defaultSkinMap.find(surf.surfaceName);
        if (skinIt != defaultSkinMap.end() && !skinIt->second.empty())
        {
            matName = skinIt->second;
        }
        if (matName.empty())
        {
            matName = surf.surfaceName;
        }

        const Q3ShaderDef *def = shaders.Find(matName);

        EditorPreviewModelSection sec;
        sec.surfaceName = surf.surfaceName;
        sec.materialName = matName;
        sec.alphaTested = def != nullptr && def->alphaTested;
        sec.twoSided = def != nullptr && def->twoSided;
        sec.hasAuthoredNormals = true;
        if (def != nullptr)
        {
            sec.shaderName = matName;
            sec.isNonTracedOverlay = def->isNonTracedOverlay;
            sec.cutoutFrameUnderlay = def->cutoutFrameUnderlay;
            sec.usesOverlay = def->usesOverlay;
            sec.tracedBase = def->tracedBase;
            sec.metallicEnvBase = def->metallicEnvBase;
            sec.autoSprite = def->autoSprite;
            sec.autoSprite2 = def->autoSprite2;
            sec.deformWave = def->deformWave;
            sec.deformSpread = def->deformSpread;
            sec.deformBase = def->deformBase;
            sec.deformAmplitude = def->deformAmplitude;
            sec.deformPhase = def->deformPhase;
            sec.deformFrequency = def->deformFrequency;
            sec.surfaceLight = def->surfaceLight;
            sec.baseStageIndex = def->baseStageIndex;
            sec.stages = def->stages;
        }
        sec.vertices.resize(size_t(surf.numVerts));
        sec.indices = surf.indices;

        for (int v = 0; v < surf.numVerts; v++)
        {
            const float lx = surf.positions[frameBase + size_t(v) * 3u + 0];
            const float ly = surf.positions[frameBase + size_t(v) * 3u + 1];
            const float lz = surf.positions[frameBase + size_t(v) * 3u + 2];
            const float nx = surf.normals[frameBase + size_t(v) * 3u + 0];
            const float ny = surf.normals[frameBase + size_t(v) * 3u + 1];
            const float nz = surf.normals[frameBase + size_t(v) * 3u + 2];

            QrVertex &dst = sec.vertices[size_t(v)];
            dst = {};
            dst.position[0] = xf.origin[0] + xf.axis[0][0] * lx + xf.axis[1][0] * ly +
                              xf.axis[2][0] * lz;
            dst.position[1] = xf.origin[1] + xf.axis[0][1] * lx + xf.axis[1][1] * ly +
                              xf.axis[2][1] * lz;
            dst.position[2] = xf.origin[2] + xf.axis[0][2] * lx + xf.axis[1][2] * ly +
                              xf.axis[2][2] * lz;
            dst.normal[0] = xf.axis[0][0] * nx + xf.axis[1][0] * ny + xf.axis[2][0] * nz;
            dst.normal[1] = xf.axis[0][1] * nx + xf.axis[1][1] * ny + xf.axis[2][1] * nz;
            dst.normal[2] = xf.axis[0][2] * nx + xf.axis[1][2] * ny + xf.axis[2][2] * nz;
            NormalizeVec3(dst.normal);
            dst.texCoord[0] = surf.texCoords[size_t(v) * 2u + 0];
            dst.texCoord[1] = surf.texCoords[size_t(v) * 2u + 1];
            dst.packedColor = 0xFFFFFFFFu;
        }

        if (sec.twoSided && !sec.isNonTracedOverlay)
        {
            AppendTwoSidedSection(sec);
        }

        outFrame.sections.push_back(std::move(sec));
    }
}

void FinalizePreviewFrameBounds(EditorPreviewModelFrame &frame)
{
    if (frame.sections.empty())
    {
        return;
    }

    float mins[3] = { 1e30f, 1e30f, 1e30f };
    float maxs[3] = { -1e30f, -1e30f, -1e30f };
    bool any = false;

    for (const EditorPreviewModelSection &sec : frame.sections)
    {
        for (const QrVertex &v : sec.vertices)
        {
            any = true;
            for (int c = 0; c < 3; c++)
            {
                mins[c] = std::min(mins[c], v.position[c]);
                maxs[c] = std::max(maxs[c], v.position[c]);
            }
        }
    }

    if (!any)
    {
        return;
    }

    float center[3] = {};
    for (int c = 0; c < 3; c++)
    {
        center[c] = (mins[c] + maxs[c]) * 0.5f;
        frame.center[c] = center[c];
    }

    float maxSq = 0.0f;
    for (EditorPreviewModelSection &sec : frame.sections)
    {
        for (QrVertex &v : sec.vertices)
        {
            for (int c = 0; c < 3; c++)
            {
                v.position[c] -= center[c];
            }
            const float sq = v.position[0] * v.position[0] + v.position[1] * v.position[1] +
                             v.position[2] * v.position[2];
            maxSq = std::max(maxSq, sq);
        }
    }

    frame.radius = std::max(std::sqrt(maxSq), 1.0f);
    frame.vertices = frame.sections.front().vertices;
    frame.indices = frame.sections.front().indices;
}

void BuildPreviewFallbackRgba(const std::string &name, uint32_t &outW, uint32_t &outH,
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
    const uint8_t r = uint8_t(90u + (hash & 0x5Fu));
    const uint8_t g = uint8_t(90u + ((hash >> 8u) & 0x5Fu));
    const uint8_t b = uint8_t(90u + ((hash >> 16u) & 0x5Fu));
    for (size_t i = 0; i < outW * outH; i++)
    {
        outRgba[i * 4u + 0] = r;
        outRgba[i * 4u + 1] = g;
        outRgba[i * 4u + 2] = b;
        outRgba[i * 4u + 3] = 255u;
    }
}

bool IsHiddenMd3Material(const std::string &name, const Q3ShaderDef *def)
{
    if (def != nullptr && def->isNoDraw)
    {
        return true;
    }
    const std::string norm = NormalizeQ3AssetPath(name);
    const std::string stem = StripQ3ImageExtension(norm);
    return stem == "null" ||
           (stem.size() >= 5u && stem.compare(stem.size() - 5u, 5u, "/null") == 0);
}

std::vector<std::string> CollectOrderedMd3Materials(const EditorPreviewModel &model)
{
    std::vector<std::string> orderedNames;
    std::unordered_set<std::string> seen;
    auto addName = [&](const std::string &raw) {
        const std::string norm = NormalizeQ3AssetPath(raw);
        if (!norm.empty() && seen.insert(norm).second)
        {
            orderedNames.push_back(norm);
        }
    };

    for (const EditorPreviewModelFrame &frame : model.frames)
    {
        for (const EditorPreviewModelSection &sec : frame.sections)
        {
            addName(sec.materialName);
        }
    }
    for (const EditorPreviewModelSkinPreset &preset : model.skinPresets)
    {
        for (const auto &kv : preset.surfaceMaterials)
        {
            addName(kv.second);
        }
    }
    return orderedNames;
}

EditorTexture LoadSingleMd3Texture(const std::string &name, const Q3ShaderDef *def,
                                   const EditorAssetSource &assets)
{
    const std::string imgRef = (def != nullptr && !def->map.empty()) ? def->map : name;
    EditorTexture tex;
    tex.name = name;
    std::string resolved;
    bool loaded = LoadQ3ImageRgba(&assets, imgRef, tex.rgba, tex.width, tex.height, resolved);
    if (!loaded && def != nullptr && !def->editorImage.empty())
    {
        loaded = LoadQ3ImageRgba(&assets, def->editorImage, tex.rgba, tex.width, tex.height,
                                 resolved);
    }
    if (!loaded)
    {
        BuildPreviewFallbackRgba(name, tex.width, tex.height, tex.rgba);
    }
    if (def != nullptr)
    {
        ApplyQ3ShaderToEditorTexture(*def, &assets, def->isSky, tex);
    }
    return tex;
}

void PopulateMd3ModelTextures(const EditorAssetSource &assets, const Q3ShaderDatabase &shaders,
                              EditorPreviewModel &outModel)
{
    for (const std::string &name : CollectOrderedMd3Materials(outModel))
    {
        const Q3ShaderDef *def = shaders.Find(name);
        if (IsHiddenMd3Material(name, def))
        {
            outModel.hiddenMaterials.insert(NormalizeQ3AssetPath(name));
            outModel.hiddenMaterials.insert(StripQ3ImageExtension(name));
            continue;
        }
        outModel.surfaceTextures.push_back(LoadSingleMd3Texture(name, def, assets));
    }

    if (!outModel.surfaceTextures.empty())
    {
        outModel.skins.push_back(outModel.surfaceTextures.front());
    }
    else
    {
        EditorTexture placeholder;
        placeholder.name = outModel.path;
        BuildPreviewFallbackRgba(placeholder.name, placeholder.width, placeholder.height,
                                 placeholder.rgba);
        outModel.surfaceTextures.push_back(placeholder);
        outModel.skins.push_back(placeholder);
    }
}

struct Q3AnimSpan
{
    int firstFrame = 0;
    int numFrames = 1;
};

struct Q3CharacterPoseSpec
{
    std::string label;
    int legsFrame = 0;
    int torsoFrame = 0;
};

constexpr int kAnimBothDeath1 = 0;
constexpr int kAnimTorsoGesture = 6;
constexpr int kAnimTorsoAttack = 7;
constexpr int kAnimTorsoStand = 11;
constexpr int kAnimTorsoStand2 = 12;
constexpr int kAnimLegsWalkCr = 13;
constexpr int kAnimLegsWalk = 14;
constexpr int kAnimLegsRun = 15;
constexpr int kAnimLegsJump = 18;
constexpr int kAnimLegsIdle = 22;
constexpr int kAnimLegsIdleCr = 23;
constexpr int kAnimMaxCount = 31;

bool ParseAnimConfigLine(std::string line, Q3AnimSpan &out)
{
    const size_t slash = line.find("//");
    if (slash != std::string::npos)
    {
        line.resize(slash);
    }

    const char *p = line.c_str();
    while (*p != '\0' && std::isspace(static_cast<unsigned char>(*p)))
    {
        p++;
    }
    if (*p == '\0' || (!std::isdigit(static_cast<unsigned char>(*p)) && *p != '-'))
    {
        return false;
    }

    int vals[4] = {};
    int count = 0;
    while (count < 4 && *p != '\0')
    {
        char *next = nullptr;
        vals[count] = int(std::strtol(p, &next, 10));
        if (next == p)
        {
            break;
        }
        count++;
        p = next;
    }
    if (count < 2)
    {
        return false;
    }

    out = { vals[0], std::max(1, std::abs(vals[1])) };
    return true;
}

bool ParseQ3AnimationCfg(const std::vector<uint8_t> &bytes,
                         std::array<Q3AnimSpan, kAnimMaxCount> &out)
{
    const std::string text(bytes.begin(), bytes.end());
    std::vector<Q3AnimSpan> parsed;
    size_t pos = 0;

    while (pos < text.size() && int(parsed.size()) < kAnimMaxCount)
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        Q3AnimSpan span;
        if (ParseAnimConfigLine(text.substr(pos, end - pos), span))
        {
            parsed.push_back(span);
        }
        pos = end + 1;
    }

    if (parsed.size() < 25u)
    {
        return false;
    }

    int skip = 0;
    for (size_t a = 0; a < parsed.size(); a++)
    {
        out[a] = parsed[a];
        if (a == size_t(kAnimLegsWalkCr))
        {
            skip = out[a].firstFrame - out[kAnimTorsoGesture].firstFrame;
        }
        if (a >= size_t(kAnimLegsWalkCr) && a <= 24u)
        {
            out[a].firstFrame -= skip;
        }
    }
    return true;
}

std::vector<Q3CharacterPoseSpec> BuildQ3CharacterPoses(const EditorAssetSource &assets,
                                                       const std::string &charDir,
                                                       int maxFrames)
{
    std::vector<Q3CharacterPoseSpec> poses;
    std::vector<uint8_t> cfgBytes;
    std::array<Q3AnimSpan, kAnimMaxCount> anims = {};

    if (assets.ReadFile(charDir + "/animation.cfg", cfgBytes) &&
        ParseQ3AnimationCfg(cfgBytes, anims))
    {
        const int standLegs = std::max(0, anims[kAnimLegsIdle].firstFrame);
        const int standTorso = std::max(0, anims[kAnimTorsoStand].firstFrame);
        poses.push_back({ "Stand", standLegs, standTorso });

        const int stand2Torso = std::max(0, anims[kAnimTorsoStand2].firstFrame);
        if (stand2Torso != standTorso)
        {
            poses.push_back({ "Stand Alt", standLegs, stand2Torso });
        }

        auto appendAnimCycle = [&](const char *label, int legsAnim, int torsoAnim, bool varyLegs,
                                   int maxSamples) {
            const int count = varyLegs ? anims[size_t(legsAnim)].numFrames
                                       : anims[size_t(torsoAnim)].numFrames;
            const int step = std::max(1, count / maxSamples);
            for (int k = 0; k < count; k += step)
            {
                const int lf = varyLegs ? (anims[size_t(legsAnim)].firstFrame + k)
                                        : anims[size_t(legsAnim)].firstFrame;
                const int tf = !varyLegs ? (anims[size_t(torsoAnim)].firstFrame + k)
                                         : anims[size_t(torsoAnim)].firstFrame;
                poses.push_back({ std::string(label) + " " + std::to_string(k),
                                  std::max(0, lf), std::max(0, tf) });
            }
        };

        appendAnimCycle("Walk", kAnimLegsWalk, kAnimTorsoStand, true, 6);
        appendAnimCycle("Run", kAnimLegsRun, kAnimTorsoStand, true, 6);
        appendAnimCycle("Crouch", kAnimLegsIdleCr, kAnimTorsoStand, true, 4);
        appendAnimCycle("Jump", kAnimLegsJump, kAnimTorsoStand, true, 4);
        appendAnimCycle("Attack", kAnimLegsIdle, kAnimTorsoAttack, false, 6);
        appendAnimCycle("Gesture", kAnimLegsIdle, kAnimTorsoGesture, false, 6);

        const int deathCount = anims[kAnimBothDeath1].numFrames;
        const int deathStep = std::max(1, deathCount / 4);
        for (int k = 0; k < deathCount; k += deathStep)
        {
            const int f = std::max(0, anims[kAnimBothDeath1].firstFrame + k);
            poses.push_back({ "Death " + std::to_string(k), f, f });
        }
        return poses;
    }

    const int count = std::min(std::max(1, maxFrames), 32);
    for (int f = 0; f < count; f++)
    {
        poses.push_back({ "Frame " + std::to_string(f), f, f });
    }
    return poses;
}

using SkinSurfaceMap = std::unordered_map<std::string, std::string>;

bool ParseCharacterSkinFilename(const std::string &path, const std::string &charDir,
                                std::string &outPart, std::string &outSkinName)
{
    const std::string lower = NormalizeQ3AssetPath(path);
    const size_t slash = lower.rfind('/');
    const std::string base = slash == std::string::npos ? lower : lower.substr(slash + 1);
    if (slash != std::string::npos && lower.substr(0, slash) != charDir)
    {
        return false;
    }

    const size_t underscore = base.find('_');
    const size_t dot = base.rfind('.');
    if (underscore == std::string::npos || dot == std::string::npos || dot <= underscore + 1)
    {
        return false;
    }

    outPart = base.substr(0, underscore);
    if (outPart != "lower" && outPart != "upper" && outPart != "head")
    {
        return false;
    }
    outSkinName = base.substr(underscore + 1, dot - underscore - 1);
    return true;
}

std::vector<std::string> OrderSkinNames(const std::set<std::string> &skinNames)
{
    std::vector<std::string> ordered;
    ordered.push_back("default");
    if (skinNames.count("red") != 0u)
    {
        ordered.push_back("red");
    }
    if (skinNames.count("blue") != 0u)
    {
        ordered.push_back("blue");
    }
    for (const std::string &s : skinNames)
    {
        if (s != "default" && s != "red" && s != "blue")
        {
            ordered.push_back(s);
        }
    }
    return ordered;
}

std::vector<EditorPreviewModelSkinPreset> DiscoverCharacterSkins(const EditorAssetSource &assets,
                                                                 const std::string &charDir)
{
    std::vector<EditorAssetCatalogEntry> skinFiles;
    assets.ListFilesWithPrefixAndExtension(charDir + "/", ".skin", skinFiles);

    std::unordered_map<std::string, SkinSurfaceMap> perPartSkins;
    std::set<std::string> skinNames;
    skinNames.insert("default");

    std::vector<uint8_t> bytes;
    for (const EditorAssetCatalogEntry &entry : skinFiles)
    {
        std::string part;
        std::string skinName;
        if (!ParseCharacterSkinFilename(entry.path, charDir, part, skinName))
        {
            continue;
        }
        skinNames.insert(skinName);
        if (assets.ReadFile(entry.path, bytes))
        {
            std::string mapKey = part;
            mapKey += '_';
            mapKey += skinName;
            perPartSkins[mapKey] = ParseQ3SkinBytes(bytes);
        }
    }

    const std::vector<std::string> ordered = OrderSkinNames(skinNames);
    std::vector<EditorPreviewModelSkinPreset> presets;
    presets.reserve(ordered.size());

    for (const std::string &skin : ordered)
    {
        EditorPreviewModelSkinPreset preset;
        preset.name = skin;
        for (const char *part : { "lower", "upper", "head" })
        {
            std::string partSkinKey = part;
            partSkinKey += '_';
            partSkinKey += skin;
            auto it = perPartSkins.find(partSkinKey);
            if (it == perPartSkins.end())
            {
                std::string fallbackKey = part;
                fallbackKey += "_default";
                it = perPartSkins.find(fallbackKey);
            }
            if (it != perPartSkins.end())
            {
                for (const auto &kv : it->second)
                {
                    preset.surfaceMaterials[kv.first] = kv.second;
                }
            }
        }
        presets.push_back(std::move(preset));
    }
    return presets;
}

struct Q3CharacterParts
{
    ParsedMd3Model lower;
    ParsedMd3Model upper;
    ParsedMd3Model head;
    bool hasLower = false;
    bool hasUpper = false;
    bool hasHead = false;
};

void BuildCharacterFrame(const Q3CharacterParts &parts, const Q3CharacterPoseSpec &pose,
                         const SkinSurfaceMap &defaultMap, const Q3ShaderDatabase &shaders,
                         EditorPreviewModelFrame &frame)
{
    frame.name = pose.label;
    const Md3Transform rootXf{};
    Md3Transform upperXf = rootXf;
    Md3Transform headXf = rootXf;

    if (parts.hasLower)
    {
        AppendTransformedMd3Part(parts.lower, pose.legsFrame, rootXf, defaultMap, shaders, frame);
        upperXf = ComputeChildTagTransform(parts.lower, pose.legsFrame, "tag_torso", rootXf);
        headXf = upperXf;
    }
    if (parts.hasUpper)
    {
        AppendTransformedMd3Part(parts.upper, pose.torsoFrame, upperXf, defaultMap, shaders, frame);
        headXf = ComputeChildTagTransform(parts.upper, pose.torsoFrame, "tag_head", upperXf);
    }
    if (parts.hasHead)
    {
        AppendTransformedMd3Part(parts.head, 0, headXf, defaultMap, shaders, frame);
    }

    FinalizePreviewFrameBounds(frame);
}

bool LoadQ3CharacterModel(const std::string &modelPath, const std::string &charDir,
                          const EditorAssetSource &assets, const Q3ShaderDatabase &shaders,
                          EditorPreviewModel &outModel, std::string &error)
{
    Q3CharacterParts parts;
    std::string dummyErr;
    std::vector<uint8_t> bytes;

    parts.hasLower = assets.ReadFile(charDir + "/lower.md3", bytes) &&
                     ParseMd3Binary(bytes, parts.lower, dummyErr);
    parts.hasUpper = assets.ReadFile(charDir + "/upper.md3", bytes) &&
                     ParseMd3Binary(bytes, parts.upper, dummyErr);
    parts.hasHead =
        assets.ReadFile(charDir + "/head.md3", bytes) && ParseMd3Binary(bytes, parts.head, dummyErr);

    if (!parts.hasLower && !parts.hasUpper && !parts.hasHead)
    {
        error = "no lower/upper/head .md3 found in " + charDir;
        return false;
    }

    outModel = EditorPreviewModel{};
    outModel.path = modelPath;
    outModel.hasMultiSections = true;
    outModel.skinPresets = DiscoverCharacterSkins(assets, charDir);

    const SkinSurfaceMap &defaultMap = outModel.skinPresets.front().surfaceMaterials;
    const int maxFrames = std::max({ parts.hasLower ? parts.lower.numFrames : 1,
                                     parts.hasUpper ? parts.upper.numFrames : 1,
                                     parts.hasHead ? parts.head.numFrames : 1 });
    const std::vector<Q3CharacterPoseSpec> poses = BuildQ3CharacterPoses(assets, charDir, maxFrames);

    outModel.frames.reserve(poses.size());
    for (const Q3CharacterPoseSpec &pose : poses)
    {
        EditorPreviewModelFrame frame;
        BuildCharacterFrame(parts, pose, defaultMap, shaders, frame);
        if (!frame.sections.empty())
        {
            outModel.frames.push_back(std::move(frame));
        }
    }

    if (outModel.frames.empty())
    {
        error = "Q3 character produced no frames";
        return false;
    }

    PopulateMd3ModelTextures(assets, shaders, outModel);
    return true;
}

bool ExtractSingleMd3SkinName(const std::string &entryPath, const std::string &dir,
                              const std::string &prefix, std::string &outSkinName)
{
    const std::string low = NormalizeQ3AssetPath(entryPath);
    const size_t es = low.rfind('/');
    const std::string eDir = es == std::string::npos ? "" : low.substr(0, es);
    const std::string eFile = es == std::string::npos ? low : low.substr(es + 1);
    if (eDir != dir || eFile.compare(0, prefix.size(), prefix) != 0)
    {
        return false;
    }
    const size_t edot = eFile.rfind('.');
    if (edot == std::string::npos || edot <= prefix.size())
    {
        return false;
    }
    outSkinName = eFile.substr(prefix.size(), edot - prefix.size());
    return true;
}

std::vector<EditorPreviewModelSkinPreset> BuildSortedSkinPresets(
    const SkinSurfaceMap &baseDefaults, const std::unordered_map<std::string, SkinSurfaceMap> &namedSkins)
{
    std::vector<EditorPreviewModelSkinPreset> presets;
    EditorPreviewModelSkinPreset defPreset{ "default", baseDefaults };
    if (const auto defIt = namedSkins.find("default"); defIt != namedSkins.end())
    {
        for (const auto &kv : defIt->second)
        {
            defPreset.surfaceMaterials[kv.first] = kv.second;
        }
    }
    presets.push_back(std::move(defPreset));

    std::vector<std::string> extraNames;
    for (const auto &kv : namedSkins)
    {
        if (kv.first != "default")
        {
            extraNames.push_back(kv.first);
        }
    }
    std::sort(extraNames.begin(), extraNames.end());
    for (const std::string &skin : extraNames)
    {
        EditorPreviewModelSkinPreset preset{ skin, baseDefaults };
        const auto skinIt = namedSkins.find(skin);
        if (skinIt != namedSkins.end())
        {
            for (const auto &kv : skinIt->second)
            {
                preset.surfaceMaterials[kv.first] = kv.second;
            }
        }
        presets.push_back(std::move(preset));
    }

    return presets;
}

std::vector<EditorPreviewModelSkinPreset> DiscoverSingleMd3Skins(const EditorAssetSource &assets,
                                                                 const std::string &modelPath,
                                                                 const ParsedMd3Model &model)
{
    const std::string norm = NormalizeQ3AssetPath(modelPath);
    const size_t slash = norm.rfind('/');
    const std::string dir = slash == std::string::npos ? "" : norm.substr(0, slash);
    const std::string file = slash == std::string::npos ? norm : norm.substr(slash + 1);
    const size_t dot = file.rfind('.');
    const std::string prefix = (dot == std::string::npos ? file : file.substr(0, dot)) + "_";

    SkinSurfaceMap baseDefaults;
    for (const ParsedMd3Surface &s : model.surfaces)
    {
        if (!s.defaultShader.empty())
        {
            baseDefaults[s.surfaceName] = s.defaultShader;
        }
    }

    std::vector<EditorAssetCatalogEntry> skinFiles;
    assets.ListFilesWithPrefixAndExtension(dir.empty() ? "" : (dir + "/"), ".skin", skinFiles);

    std::unordered_map<std::string, SkinSurfaceMap> namedSkins;
    std::vector<uint8_t> bytes;
    for (const EditorAssetCatalogEntry &entry : skinFiles)
    {
        std::string skinName;
        if (ExtractSingleMd3SkinName(entry.path, dir, prefix, skinName) &&
            assets.ReadFile(entry.path, bytes))
        {
            namedSkins[skinName] = ParseQ3SkinBytes(bytes);
        }
    }

    return BuildSortedSkinPresets(baseDefaults, namedSkins);
}

bool LoadSingleMd3Model(const std::string &modelPath, const EditorAssetSource &assets,
                        const Q3ShaderDatabase &shaders, EditorPreviewModel &outModel,
                        std::string &error)
{
    std::vector<uint8_t> bytes;
    if (!assets.ReadFile(modelPath, bytes))
    {
        error = "cannot read MD3 asset " + modelPath;
        return false;
    }

    ParsedMd3Model md3;
    if (!ParseMd3Binary(bytes, md3, error))
    {
        return false;
    }

    outModel = EditorPreviewModel{};
    outModel.path = modelPath;
    outModel.hasMultiSections = true;
    outModel.skinPresets = DiscoverSingleMd3Skins(assets, modelPath, md3);

    const SkinSurfaceMap &defaultMap = outModel.skinPresets.front().surfaceMaterials;
    const Md3Transform rootXf{};
    const int frameCount = std::min(md3.numFrames, 128);
    outModel.frames.reserve(size_t(frameCount));

    for (int f = 0; f < frameCount; f++)
    {
        EditorPreviewModelFrame frame;
        frame.name = "Frame " + std::to_string(f);
        AppendTransformedMd3Part(md3, f, rootXf, defaultMap, shaders, frame);
        FinalizePreviewFrameBounds(frame);
        if (!frame.sections.empty())
        {
            outModel.frames.push_back(std::move(frame));
        }
    }

    if (outModel.frames.empty())
    {
        error = "MD3 model has no valid frames";
        return false;
    }

    PopulateMd3ModelTextures(assets, shaders, outModel);
    return true;
}

} // namespace

bool LoadQ3PreviewModel(const std::string &modelPath, const EditorAssetSource &assets,
                        EditorPreviewModel &outModel, std::string &error)
{
    Q3ShaderDatabase shaders;
    shaders.LoadFromAssets(assets);

    const std::string norm = NormalizeQ3AssetPath(modelPath);
    static constexpr const char *kCharExt = ".character";
    if (norm.size() > 10u && norm.compare(norm.size() - 10u, 10u, kCharExt) == 0)
    {
        const std::string charDir = norm.substr(0, norm.size() - 10u);
        return LoadQ3CharacterModel(modelPath, charDir, assets, shaders, outModel, error);
    }

    if (norm.size() >= 4u && norm.compare(norm.size() - 4u, 4u, ".md3") == 0)
    {
        return LoadSingleMd3Model(modelPath, assets, shaders, outModel, error);
    }

    if (assets.Exists(norm + "/lower.md3") || assets.Exists(norm + "/upper.md3") ||
        assets.Exists(norm + "/head.md3"))
    {
        return LoadQ3CharacterModel(modelPath, norm, assets, shaders, outModel, error);
    }

    error = "unsupported Q3 model path: " + modelPath;
    return false;
}
