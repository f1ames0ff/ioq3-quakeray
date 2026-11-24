#include "Assets.h"
#include "EditorSession.h"
#include "Q3Bsp.h"
#include "Q3EditorAdapter.h"
#include "Q3Model.h"
#include "Q3Shader.h"
#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace
{

bool Check(bool cond, const char *msg)
{
    if (!cond)
    {
        std::fprintf(stderr, "Q3AdapterTests failed: %s\n", msg);
    }
    return cond;
}

void AppendLe16(std::vector<uint8_t> &out, uint16_t v)
{
    out.push_back(uint8_t(v & 0xFFu));
    out.push_back(uint8_t((v >> 8u) & 0xFFu));
}

void AppendLe32(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back(uint8_t(v & 0xFFu));
    out.push_back(uint8_t((v >> 8u) & 0xFFu));
    out.push_back(uint8_t((v >> 16u) & 0xFFu));
    out.push_back(uint8_t((v >> 24u) & 0xFFu));
}

void AppendStructBytes(std::vector<uint8_t> &out, const void *ptr, size_t len)
{
    const auto *p = static_cast<const uint8_t *>(ptr);
    out.insert(out.end(), p, p + len);
}

struct StoredZipInput
{
    std::string name;
    std::vector<uint8_t> data;
};

bool WriteStoredZip(const std::filesystem::path &path, const std::vector<StoredZipInput> &files)
{
    std::vector<uint8_t> out;
    struct CdRecord
    {
        std::string name;
        uint32_t offset = 0;
        uint32_t size = 0;
    };
    std::vector<CdRecord> records;

    for (const StoredZipInput &entry : files)
    {
        CdRecord rec{ entry.name, uint32_t(out.size()), uint32_t(entry.data.size()) };
        AppendLe32(out, 0x04034b50u);
        AppendLe16(out, 20u);
        AppendLe16(out, 0u);
        AppendLe16(out, 0u);
        AppendLe16(out, 0u);
        AppendLe16(out, 0u);
        AppendLe32(out, 0u);
        AppendLe32(out, rec.size);
        AppendLe32(out, rec.size);
        AppendLe16(out, uint16_t(entry.name.size()));
        AppendLe16(out, 0u);
        out.insert(out.end(), entry.name.begin(), entry.name.end());
        out.insert(out.end(), entry.data.begin(), entry.data.end());
        records.push_back(std::move(rec));
    }

    const uint32_t cdOffset = uint32_t(out.size());
    for (const CdRecord &rec : records)
    {
        AppendLe32(out, 0x02014b50u);
        AppendLe16(out, 20u);
        AppendLe16(out, 20u);
        AppendLe16(out, 0u);
        AppendLe16(out, 0u);
        AppendLe16(out, 0u);
        AppendLe16(out, 0u);
        AppendLe32(out, 0u);
        AppendLe32(out, rec.size);
        AppendLe32(out, rec.size);
        AppendLe16(out, uint16_t(rec.name.size()));
        AppendLe16(out, 0u);
        AppendLe16(out, 0u);
        AppendLe16(out, 0u);
        AppendLe16(out, 0u);
        AppendLe32(out, 0u);
        AppendLe32(out, rec.offset);
        out.insert(out.end(), rec.name.begin(), rec.name.end());
    }
    const uint32_t cdSize = uint32_t(out.size()) - cdOffset;

    AppendLe32(out, 0x06054b50u);
    AppendLe16(out, 0u);
    AppendLe16(out, 0u);
    AppendLe16(out, uint16_t(records.size()));
    AppendLe16(out, uint16_t(records.size()));
    AppendLe32(out, cdSize);
    AppendLe32(out, cdOffset);
    AppendLe16(out, 0u);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char *>(out.data()), std::streamsize(out.size()));
    return bool(file);
}

#pragma pack(push, 1)
struct TestQ3Lump
{
    int32_t fileofs;
    int32_t filelen;
};

struct TestQ3Shader
{
    char shader[64];
    int32_t surfaceFlags;
    int32_t contentFlags;
};

struct TestQ3Plane
{
    float normal[3];
    float dist;
};

struct TestQ3Node
{
    int32_t planeNum;
    int32_t children[2];
    int32_t mins[3];
    int32_t maxs[3];
};

struct TestQ3Leaf
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

struct TestQ3Model
{
    float mins[3];
    float maxs[3];
    int32_t firstSurface;
    int32_t numSurfaces;
    int32_t firstBrush;
    int32_t numBrushes;
};

struct TestQ3DrawVert
{
    float xyz[3];
    float st[2];
    float lightmap[2];
    float normal[3];
    uint8_t color[4];
};

struct TestQ3Surface
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

std::vector<uint8_t> BuildSyntheticQ3Bsp()
{
    std::vector<uint8_t> bytes(8 + 17 * 8, 0);
    bytes[0] = 'I';
    bytes[1] = 'B';
    bytes[2] = 'S';
    bytes[3] = 'P';
    const int32_t version = 46;
    std::memcpy(bytes.data() + 4, &version, 4);

    auto writeLump = [&](int idx, const void *data, size_t len) {
        TestQ3Lump lump{ int32_t(bytes.size()), int32_t(len) };
        std::memcpy(bytes.data() + 8 + idx * 8, &lump, sizeof(lump));
        AppendStructBytes(bytes, data, len);
    };

    const std::string ents =
        "{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"32 0 16\"\n\"angle\" \"90\"\n}\n"
        "{\n\"classname\" \"light\"\n\"origin\" \"40 0 64\"\n\"light\" \"400\"\n\"_color\" \"1 0.5 0.25\"\n}\n"
        "{\n\"classname\" \"trigger_hurt\"\n\"model\" \"*1\"\n}\n";
    writeLump(0, ents.data(), ents.size() + 1);

    TestQ3Shader shaders[4] = {};
    std::snprintf(shaders[0].shader, 64, "textures/test/wall");
    std::snprintf(shaders[1].shader, 64, "textures/test/sky");
    shaders[1].surfaceFlags = 0x4;
    std::snprintf(shaders[2].shader, 64, "textures/test/lamp");
    std::snprintf(shaders[3].shader, 64, "textures/test/nodraw");
    shaders[3].surfaceFlags = 0x80;
    writeLump(1, shaders, sizeof(shaders));

    TestQ3Plane plane{ { 1.0f, 0.0f, 0.0f }, 0.0f };
    writeLump(2, &plane, sizeof(plane));

    TestQ3Node node{};
    node.planeNum = 0;
    node.children[0] = -1;
    node.children[1] = -2;
    writeLump(3, &node, sizeof(node));

    TestQ3Leaf leafs[2] = {};
    leafs[0].cluster = 0;
    leafs[1].cluster = -1;
    writeLump(4, leafs, sizeof(leafs));

    TestQ3Model models[2] = {};
    models[0].firstSurface = 0;
    models[0].numSurfaces = 3;
    models[1].firstSurface = 3;
    models[1].numSurfaces = 1;
    writeLump(7, models, sizeof(models));

    std::vector<TestQ3DrawVert> verts(13);
    verts[0] = { { 10, 0, 0 }, { 0, 0 }, {}, { 1, 0, 0 }, { 255, 255, 255, 255 } };
    verts[1] = { { 10, 64, 0 }, { 1, 0 }, {}, { 1, 0, 0 }, { 255, 255, 255, 255 } };
    verts[2] = { { 10, 64, 64 }, { 1, 1 }, {}, { 1, 0, 0 }, { 255, 255, 255, 255 } };
    verts[3] = { { 10, 0, 64 }, { 0, 1 }, {}, { 1, 0, 0 }, { 255, 255, 255, 255 } };
    for (int py = 0; py < 3; py++)
    {
        for (int px = 0; px < 3; px++)
        {
            const float x = float(px) * 16.0f + 16.0f;
            const float y = float(py) * 16.0f;
            const float z = (px == 1) ? 12.0f : 0.0f;
            const size_t slot = size_t(4) + size_t(py) * 3u + size_t(px);
            verts[slot] = { { x, y, z }, { float(px) * 0.5f, float(py) * 0.5f }, {}, { 0, 0, 1 }, {} };
        }
    }
    writeLump(10, verts.data(), verts.size() * sizeof(TestQ3DrawVert));

    int32_t idxData[9] = { 0, 2, 1, 0, 3, 2, 0, 1, 2 };
    writeLump(11, idxData, sizeof(idxData));

    TestQ3Surface surfs[4] = {};
    surfs[0].shaderNum = 0;
    surfs[0].surfaceType = 1;
    surfs[0].firstVert = 0;
    surfs[0].numVerts = 4;
    surfs[0].firstIndex = 0;
    surfs[0].numIndexes = 6;
    surfs[0].lightmapVecs[2][0] = 1.0f;

    surfs[1].shaderNum = 2;
    surfs[1].surfaceType = 2;
    surfs[1].firstVert = 4;
    surfs[1].numVerts = 9;
    surfs[1].patchWidth = 3;
    surfs[1].patchHeight = 3;

    surfs[2].shaderNum = 1;
    surfs[2].surfaceType = 1;
    surfs[2].firstVert = 0;
    surfs[2].numVerts = 3;
    surfs[2].firstIndex = 6;
    surfs[2].numIndexes = 3;
    surfs[2].lightmapVecs[2][0] = 1.0f;

    surfs[3].shaderNum = 3;
    surfs[3].surfaceType = 1;
    surfs[3].firstVert = 0;
    surfs[3].numVerts = 3;
    surfs[3].firstIndex = 6;
    surfs[3].numIndexes = 3;
    writeLump(13, surfs, sizeof(surfs));

    uint8_t vis[9] = {};
    const int32_t visClusters = 1;
    const int32_t rowBytes = 1;
    std::memcpy(vis + 0, &visClusters, 4);
    std::memcpy(vis + 4, &rowBytes, 4);
    vis[8] = 0x01u;
    writeLump(16, vis, sizeof(vis));

    return bytes;
}

void WriteIdentIdp3(uint8_t *dst)
{
    dst[0] = 'I';
    dst[1] = 'D';
    dst[2] = 'P';
    dst[3] = '3';
}

std::vector<uint8_t> BuildSyntheticMd3(const char *surfaceName, const char *shaderName,
                                       const char *tagName, const float tagOrigin[3],
                                       bool rotateTag90Z, int numFrames, const int16_t xyzBase[3])
{
    std::vector<uint8_t> bytes;
    const int32_t ofsFrames = 108;
    const int32_t ofsTags = ofsFrames + numFrames * 56;
    const int32_t ofsSurfaces = ofsTags + (tagName ? numFrames * 112 : 0);

    bytes.resize(size_t(ofsSurfaces), 0);
    WriteIdentIdp3(bytes.data());
    const int32_t version = 15;
    std::memcpy(bytes.data() + 4, &version, 4);
    const int32_t numTags = tagName ? 1 : 0;
    const int32_t numSurfs = 1;
    std::memcpy(bytes.data() + 76, &numFrames, 4);
    std::memcpy(bytes.data() + 80, &numTags, 4);
    std::memcpy(bytes.data() + 84, &numSurfs, 4);
    std::memcpy(bytes.data() + 92, &ofsFrames, 4);
    std::memcpy(bytes.data() + 96, &ofsTags, 4);
    std::memcpy(bytes.data() + 100, &ofsSurfaces, 4);

    if (tagName != nullptr)
    {
        for (int f = 0; f < numFrames; f++)
        {
            uint8_t *t = bytes.data() + ofsTags + size_t(f) * 112u;
            std::snprintf(reinterpret_cast<char *>(t), 64, "%s", tagName);
            std::memcpy(t + 64, tagOrigin, 12);
            float axis[3][3] = {
                { rotateTag90Z ? 0.0f : 1.0f, rotateTag90Z ? 1.0f : 0.0f, 0.0f },
                { rotateTag90Z ? -1.0f : 0.0f, rotateTag90Z ? 0.0f : 1.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f },
            };
            std::memcpy(t + 76, axis, sizeof(axis));
        }
    }

    const size_t surfStart = bytes.size();
    const int32_t numShaders = 1;
    const int32_t numVerts = 3;
    const int32_t numTris = 1;
    const int32_t ofsTris = 108;
    const int32_t ofsShaders = ofsTris + 12;
    const int32_t ofsSt = ofsShaders + 68;
    const int32_t ofsXyz = ofsSt + numVerts * 8;
    const int32_t ofsEnd = ofsXyz + numFrames * numVerts * 8;

    bytes.resize(surfStart + size_t(ofsEnd), 0);
    uint8_t *sp = bytes.data() + surfStart;
    WriteIdentIdp3(sp);
    std::snprintf(reinterpret_cast<char *>(sp + 4), 64, "%s", surfaceName);
    std::memcpy(sp + 72, &numFrames, 4);
    std::memcpy(sp + 76, &numShaders, 4);
    std::memcpy(sp + 80, &numVerts, 4);
    std::memcpy(sp + 84, &numTris, 4);
    std::memcpy(sp + 88, &ofsTris, 4);
    std::memcpy(sp + 92, &ofsShaders, 4);
    std::memcpy(sp + 96, &ofsSt, 4);
    std::memcpy(sp + 100, &ofsXyz, 4);
    std::memcpy(sp + 104, &ofsEnd, 4);

    const int32_t triIdx[3] = { 0, 1, 2 };
    std::memcpy(sp + ofsTris, triIdx, sizeof(triIdx));
    std::snprintf(reinterpret_cast<char *>(sp + ofsShaders), 64, "%s", shaderName);

    const float st[6] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
    std::memcpy(sp + ofsSt, st, sizeof(st));

    for (int f = 0; f < numFrames; f++)
    {
        for (int v = 0; v < 3; v++)
        {
            int16_t rec[4] = {
                int16_t(xyzBase[0] + (v == 1 ? 640 : 0) + f * 64),
                int16_t(xyzBase[1] + (v == 2 ? 640 : 0)),
                xyzBase[2],
                0,
            };
            std::memcpy(sp + ofsXyz + (size_t(f) * 3u + size_t(v)) * 8u, rec, sizeof(rec));
        }
    }

    const int32_t totalEnd = int32_t(bytes.size());
    std::memcpy(bytes.data() + 104, &totalEnd, 4);
    return bytes;
}

bool VerifyParsedWallShader(const Q3ShaderDef *wall)
{
    return Check(wall != nullptr, "missing wall shader") &&
           Check(wall->map == "textures/test/wall_base.tga", "wall map mismatch") &&
           Check(wall->alphaTested && wall->twoSided, "wall alpha/twoSided mismatch") &&
           Check(std::fabs(wall->baseScale[0] - 2.0f) < 1e-4f, "wall tcMod scale mismatch");
}

bool VerifyParsedSkyAndLamp(const Q3ShaderDef *sky, const Q3ShaderDef *lamp)
{
    const bool skyOk = Check(sky != nullptr && sky->isSky && sky->outerBox == "env/night",
                             "sky shader mismatch") &&
                       Check(sky->hasSun && std::fabs(sky->sunIntensity - 120.0f) < 1e-4f &&
                                 std::fabs(sky->sunDegrees - 45.0f) < 1e-4f,
                             "sun parameters mismatch");
    const bool lampOk = Check(lamp != nullptr && lamp->surfaceLight == 800.0f && lamp->hasLightRGB,
                              "lamp surfaceLight mismatch") &&
                        Check(lamp->emissionMaskMap == "textures/test/lamp_glow.tga",
                              "lamp emission mask mismatch");
    return skyOk && lampOk;
}

bool TestShaderAndSkinParsing()
{
    const char *script = R"(
// test shader comment
textures/test/wall {
    cull disable
    surfaceparm trans
    { map $lightmap }
    { map textures/test/wall_base.tga alphaFunc GE128 tcMod scale 2 4 }
}
textures/test/sky
{
    surfaceparm sky
    skyparms env/night 256 -
    q3map_sunExt 1.0 0.8 0.5 120 45 60 3 16
}
textures/test/lamp {
    q3map_surfacelight 800
    q3map_lightRGB 1.0 0.5 0.25
    { map textures/test/lamp_base.tga }
    { map textures/test/lamp_glow.tga blendFunc add }
}
)";

    Q3ShaderDatabase db;
    if (!Check(db.ParseScript(script, std::strlen(script)) == 3, "expected 3 shaders parsed"))
    {
        return false;
    }

    if (!VerifyParsedWallShader(db.Find("textures/test/wall.tga")) ||
        !VerifyParsedSkyAndLamp(db.Find("textures/test/sky"), db.Find("textures/test/lamp")))
    {
        return false;
    }

    const std::string skinText =
        "tag_head,\r\nh_cigar, models/players/sarge/cigar.tga\r\nh_head,models/players/sarge/band.tga\n";
    const auto skinMap = ParseQ3SkinBytes(std::vector<uint8_t>(skinText.begin(), skinText.end()));
    return Check(skinMap.size() == 2, "skin entry count mismatch") &&
           Check(skinMap.at("h_head") == "models/players/sarge/band.tga", "skin head mismatch") &&
           Check(skinMap.at("h_cigar") == "models/players/sarge/cigar.tga", "skin cigar mismatch");
}

std::vector<uint8_t> StringToBytes(const std::string &text)
{
    return std::vector<uint8_t>(text.begin(), text.end());
}

bool BuildSyntheticTestPk3(const std::filesystem::path &pk3Path)
{
    const std::string animCfg =
        "sex m\nheadoffset 4 0 0\n"
        "0 1 0 20\n1 1 0 20\n1 1 0 20\n1 1 0 20\n1 1 0 20\n1 1 0 20\n"
        "2 1 0 20\n3 1 0 20\n3 1 0 20\n3 1 0 20\n3 1 0 20\n4 1 0 15\n4 1 0 15\n"
        "5 1 1 20\n5 1 1 20\n5 1 1 20\n5 1 1 20\n5 1 1 20\n5 1 0 20\n"
        "5 1 0 20\n5 1 0 20\n5 1 0 20\n6 1 1 15\n6 1 1 15\n6 1 1 15\n";
    const int16_t originZero[3] = { 0, 0, 0 };
    const float torsoTag[3] = { 0.0f, 0.0f, 16.0f };
    const float headTag[3] = { 0.0f, 0.0f, 20.0f };
    const std::string q3Shader =
        "textures/test/sky { surfaceparm sky skyparms env/night 128 - q3map_sun 1 1 0.8 100 30 50 }\n"
        "textures/test/wall { cull disable { map textures/test/wall.tga } }\n"
        "textures/test/lamp { q3map_surfacelight 600 { map textures/test/lamp.tga } }\n";

    const std::vector<StoredZipInput> archiveFiles = {
        { "scripts/test.shader", StringToBytes(q3Shader) },
        { "maps/q3synth.bsp", BuildSyntheticQ3Bsp() },
        { "models/players/synth/animation.cfg", StringToBytes(animCfg) },
        { "models/players/synth/lower.md3",
          BuildSyntheticMd3("l_legs", "", "tag_torso", torsoTag, true, 5, originZero) },
        { "models/players/synth/upper.md3",
          BuildSyntheticMd3("u_torso", "", "tag_head", headTag, false, 5, originZero) },
        { "models/players/synth/head.md3",
          BuildSyntheticMd3("h_head", "", nullptr, headTag, false, 1, originZero) },
        { "models/players/synth/lower_default.skin",
          StringToBytes("l_legs,models/players/synth/legs.tga\n") },
        { "models/players/synth/upper_default.skin",
          StringToBytes("u_torso,models/players/synth/body.tga\n") },
        { "models/players/synth/upper_red.skin",
          StringToBytes("u_torso,models/players/synth/red.tga\n") },
        { "models/players/synth/head_default.skin",
          StringToBytes("h_head,models/players/synth/head.tga\n") },
    };
    return Check(WriteStoredZip(pk3Path, archiveFiles), "failed writing synthetic pk3");
}

bool VerifySyntheticFaces(const EditorScene &scene)
{
    if (!Check(scene.surfaces[0].faces.size() == 1 && scene.surfaces[2].faces.size() == 4,
               "representative quad/patch face reduction failed"))
    {
        return false;
    }
    const EditorSurfaceFace &quadFace = scene.surfaces[0].faces[0];
    return Check(quadFace.positions.size() == 4, "quad face must have 4 vertices") &&
           Check(quadFace.normal[0] > 0.99f, "quad face normal must point +X");
}

bool VerifySyntheticBsp(const EditorAssetSource &assets)
{
    std::vector<uint8_t> bspBytes;
    EditorScene scene;
    std::string error;
    if (!Check(assets.ReadFile("maps/q3synth.bsp", bspBytes), "ReadFile BSP failed") ||
        !Check(LoadEditorSceneBspMemory(bspBytes, "maps/q3synth.bsp", nullptr, scene, error,
                                        EditorSceneLoadOptions{ &assets, false }),
               error.c_str()))
    {
        return false;
    }

    const float openPt[3] = { 20.0f, 0.0f, 10.0f };
    const float solidPt[3] = { -20.0f, 0.0f, 10.0f };
    const bool hasEntityLight = std::any_of(scene.lights.begin(), scene.lights.end(), [](const EditorLight &l) {
        return l.className == "light";
    });
    const bool hasLampSurfaceLight = std::any_of(scene.lights.begin(), scene.lights.end(), [](const EditorLight &l) {
        return l.className == "textures/test/lamp";
    });
    const bool sceneOk =
        Check(scene.usesExternalLightingModel && scene.surfaces.size() == 3, "surface count") &&
                         Check(scene.hasCamera && scene.hasSun && scene.skyboxName == "env/night",
                               "camera/sky/sun missing") &&
                         Check(scene.lights.size() == 2u && scene.bspLights.size() == 2u &&
                                   hasEntityLight && hasLampSurfaceLight,
                               "entity or traced surface light missing") &&
                         Check(ResolveEditorLightCluster(scene, openPt) == 1u &&
                                   ResolveEditorLightCluster(scene, solidPt) == 1u,
                               "Q3 cluster walk failed");
    const bool pvsOk =
        Check(scene.worldClusters.skyVis.size() == 1 && scene.worldClusters.skyVis[0] == 0x02u,
              "Q3 skyVis bitmask mismatch") &&
        Check(scene.textures[2].isEmissiveShader && scene.textures[2].surfaceLightValue == 600.0f,
              "Q3 emissive shader metadata missing") &&
        VerifySyntheticFaces(scene);
    return sceneOk && pvsOk;
}

bool VerifySyntheticCharacter(const EditorAssetSource &assets)
{
    std::vector<EditorAssetCatalogEntry> models;
    assets.ListModelAssets(models);
    const bool foundChar = std::any_of(models.begin(), models.end(), [](const EditorAssetCatalogEntry &e) {
        return e.path == "models/players/synth.character";
    });
    if (!Check(foundChar, "synthesized character entry missing"))
    {
        return false;
    }

    EditorPreviewModel charModel;
    std::string error;
    if (!Check(LoadEditorPreviewModel("models/players/synth.character", assets, nullptr, charModel,
                                      error),
               error.c_str()))
    {
        return false;
    }

    const bool shapeOk =
        Check(charModel.hasMultiSections && !charModel.frames.empty(), "char model frames missing") &&
        Check(charModel.frames[0].name == "Stand" && charModel.frames[0].sections.size() == 3,
              "Stand pose or sections mismatch") &&
        Check(charModel.skinPresets.size() == 2 && charModel.skinPresets[1].name == "red",
              "skin presets mismatch") &&
        Check(charModel.skinPresets[1].surfaceMaterials.at("u_torso") ==
                  "models/players/synth/red.tga",
              "red skin override mismatch");
    if (!shapeOk)
    {
        return false;
    }

    const float legZ = charModel.frames[0].sections[0].vertices[0].position[2];
    const float headZ = charModel.frames[0].sections[2].vertices[0].position[2];
    return Check(headZ - legZ > 30.0f, "MD3 tag hierarchy height mismatch");
}

bool VerifyMaterialYamlRoundTrip(const std::filesystem::path &tempDir)
{
    EditorMaterialsDocument matDoc;
    EditorMaterialParams mp;
    mp.roughnessOverride = 0.25f;
    const std::string yaml = matDoc.WriteMerged({
        { "textures/test/wall", mp },
        { "models/players/synth/body.tga", mp },
    });

    const std::filesystem::path yamlPath = tempDir / "qray.materials.yaml";
    {
        std::ofstream yf(yamlPath, std::ios::trunc);
        yf << yaml;
    }

    std::string error;
    EditorMaterialsDocument reloaded;
    EditorMaterialParams readWall;
    EditorMaterialParams readModel;
    return Check(reloaded.Load(yamlPath.string(), error), error.c_str()) &&
           Check(reloaded.ReadMaterial("textures/test/wall", readWall), "wall yaml read failed") &&
           Check(reloaded.ReadMaterial("models/players/synth/body.tga", readModel),
                 "model yaml read failed") &&
           Check(yaml.find("textures/models/players") == std::string::npos,
                 "unexpected textures/ prefix on models/ path") &&
           Check(std::fabs(readModel.roughnessOverride - 0.25f) < 1e-4f,
                 "roughness round-trip mismatch");
}

bool TestSyntheticQ3BspAndCharacter(const std::filesystem::path &tempDir)
{
    const std::filesystem::path pk3Path = tempDir / "pak0.pk3";
    if (!BuildSyntheticTestPk3(pk3Path))
    {
        return false;
    }

    EditorAssetSource assets;
    if (!Check(assets.AddZip(pk3Path.string()), "AddZip failed") ||
        !Check(assets.Exists("maps/q3synth.bsp"), "Exists failed on pk3 BSP"))
    {
        return false;
    }

    return VerifySyntheticBsp(assets) && VerifySyntheticCharacter(assets) &&
           VerifyMaterialYamlRoundTrip(tempDir);
}

bool HasOverlaySurface(const EditorScene &scene, const char *shaderName,
                       bool requireCutout = false)
{
    for (const EditorOverlaySurface &ov : scene.overlaySurfaces)
    {
        if (ov.shaderName == shaderName && !ov.vertices.empty() && !ov.indices.empty() &&
            (!requireCutout || ov.cutoutUnderlay))
        {
            return true;
        }
    }
    return false;
}

const EditorTexture *FindSceneTexture(const EditorScene &scene, const char *name)
{
    for (const EditorTexture &tex : scene.textures)
    {
        if (tex.name == name)
        {
            return &tex;
        }
    }
    return nullptr;
}

const EditorSurface *FindSceneSurface(const EditorScene &scene, const char *name)
{
    for (const EditorSurface &surf : scene.surfaces)
    {
        if (surf.textureName == name)
        {
            return &surf;
        }
    }
    return nullptr;
}

bool CheckOverlaysPresent(const EditorScene &scene, const char *const *names, size_t count,
                          bool underlay = false)
{
    for (size_t i = 0; i < count; i++)
    {
        if (!Check(HasOverlaySurface(scene, names[i], underlay), names[i]))
        {
            return false;
        }
    }
    return true;
}

bool VerifyTourney1NoLeakedSurfaces(const EditorScene &scene)
{
    static constexpr const char *kForbidden[] = {
        "textures/sfx/beam_dusty2",
        "textures/sfx/fog_timdm1",
        "textures/sfx/teslacoil",
    };
    for (const EditorSurface &surf : scene.surfaces)
    {
        for (const char *name : kForbidden)
        {
            if (!Check(surf.textureName != name, "q3tourney1 leaked non-traced overlay/fog surface"))
            {
                return false;
            }
        }
    }
    return true;
}

bool VerifyTourney1SkyAndTextures(const EditorScene &scene)
{
    const EditorTexture *sky = FindSceneTexture(scene, "textures/skies/nightsky_xian_dm1");
    const EditorTexture *ceil50k = FindSceneTexture(scene, "textures/base_light/xceil1_39_50k");
    const EditorTexture *demonlt = FindSceneTexture(scene, "textures/sfx/demonltblackfinal");

    const bool skyOk = sky != nullptr && !sky->isEmissiveShader && sky->fullbright.empty();
    const bool emissivesOk = ceil50k != nullptr && ceil50k->isEmissiveShader &&
                             !ceil50k->fullbright.empty() && demonlt != nullptr &&
                             demonlt->isEmissiveShader && !demonlt->fullbright.empty();
    return Check(skyOk, "q3tourney1 sky must not emit DTAL light") &&
           Check(emissivesOk, "q3tourney1 DTAL emission textures missing");
}

bool VerifyTourney1EmissionsAndLights(const EditorScene &scene)
{
    if (!VerifyTourney1SkyAndTextures(scene))
    {
        return false;
    }

    const EditorSurface *atech = FindSceneSurface(scene, "textures/base_wall/atech1_alpha");
    const EditorSurface *lavaXdm1 = FindSceneSurface(scene, "textures/liquids/lavahell_xdm1");
    const EditorTexture *lavaXdm1Tex = FindSceneTexture(scene, "textures/liquids/lavahell_xdm1");

    bool hasCeil50kLight = false;
    bool hasDemonLight = false;
    bool hasFlame2Light = false;
    float maxLightIntensity = 0.0f;
    for (const EditorLight &l : scene.lights)
    {
        maxLightIntensity = std::max(maxLightIntensity, l.intensity);
        hasCeil50kLight = hasCeil50kLight || (l.className == "textures/base_light/xceil1_39_50k");
        hasDemonLight = hasDemonLight || (l.className == "textures/sfx/demonltblackfinal");
        hasFlame2Light = hasFlame2Light || (l.className == "textures/sfx/flame2");
    }

    const bool lavaOk = lavaXdm1 != nullptr && lavaXdm1->kind == EditorSurfaceKind::Lava &&
                        lavaXdm1Tex != nullptr && lavaXdm1Tex->isEmissiveShader &&
                        hasCeil50kLight && hasDemonLight && hasFlame2Light &&
                        scene.lights.size() == scene.bspLights.size();
    const bool cutoutAndCapOk =
        atech != nullptr && atech->isCutoutUnderlay && maxLightIntensity <= 300.0f;
    return Check(lavaOk, "q3tourney1 lavahell_xdm1 traced lava or fixture light mismatch") &&
           Check(cutoutAndCapOk, "q3tourney1 atech1_alpha cutout or surface light cap failed");
}

bool VerifyTourney1FlameAndOrbOverlays(const EditorScene &scene)
{
    bool orbAutoSprite = false;
    bool flameAnimated = false;
    size_t flame2OverlayCount = 0;
    for (const EditorOverlaySurface &ov : scene.overlaySurfaces)
    {
        orbAutoSprite =
            orbAutoSprite || (ov.shaderName == "models/mapobjects/bitch/orb" && ov.autoSprite);
        if (ov.shaderName == "textures/sfx/flame2")
        {
            flame2OverlayCount++;
            flameAnimated =
                flameAnimated || (!ov.stages.empty() && ov.stages[0].animationMaps.size() == 8u);
        }
    }

    static constexpr const char *kWorldOverlays[] = {
        "models/mapobjects/flares/electric",
        "textures/sfx/beam_dusty2",
        "textures/sfx/teslacoil",
        "textures/sfx/jacobs_x",
        "models/mapobjects/bitch/orb",
        "textures/sfx/flame2",
    };
    return Check(HasOverlaySurface(scene, "textures/base_wall/atech1_alpha", true),
                 "q3tourney1 atech1_alpha overlay underlay missing") &&
           Check(flameAnimated && flame2OverlayCount == 3u,
                 "q3tourney1 flame2 per-surface overlay missing") &&
           Check(orbAutoSprite, "q3tourney1 bitch/orb autosprite overlay missing") &&
           CheckOverlaysPresent(scene, kWorldOverlays, std::size(kWorldOverlays));
}

bool VerifyTourney1RetailScene(const EditorScene &scene)
{
    const bool headerOk = scene.skyboxName == "env/xnight2" && scene.hasSun &&
                          scene.sunDegrees < 0.0f && scene.lights.size() >= 15u;
    return Check(headerOk, "q3tourney1 sky/sun/lights mismatch") &&
           VerifyTourney1NoLeakedSurfaces(scene) &&
           VerifyTourney1EmissionsAndLights(scene) &&
           VerifyTourney1FlameAndOrbOverlays(scene);
}

bool VerifyTourney2RetailScene(const EditorScene &scene)
{
    const size_t spotCount = size_t(std::count_if(
        scene.lights.begin(), scene.lights.end(), [](const EditorLight &l) { return l.spot; }));
    const EditorTexture *sky = FindSceneTexture(scene, "textures/skies/xtoxicsky_tourney");
    const EditorTexture *pewter = FindSceneTexture(scene, "textures/base_trim/pewter_shiney");
    const EditorSurface *atech = FindSceneSurface(scene, "textures/base_wall/atech1_alpha");
    const EditorSurface *noblight = FindSceneSurface(scene, "textures/gothic_trim/x_noblight");

    static constexpr const char *kUnderlays[] = {
        "textures/base_wall/atech1_alpha",
        "textures/gothic_trim/x_noblight",
    };
    static constexpr const char *kOverlays[] = {
        "textures/sfx/flame1side",
        "models/mapobjects/teleporter/energy",
        "models/mapobjects/teleporter/transparency",
        "models/mapobjects/teleporter/widget",
    };

    bool ok = Check(spotCount >= 8u, "q3tourney2 spotlights missing");
    ok = ok && Check(sky != nullptr && !sky->isEmissiveShader, "q3tourney2 sky must not emit");
    ok = ok && Check(pewter != nullptr && pewter->isMetallicEnv && !pewter->metallicMask.empty() &&
                         !pewter->roughnessMask.empty(),
                     "q3tourney2 pewter_shiney metallic env mask missing");
    ok = ok && Check(atech != nullptr && atech->isCutoutUnderlay,
                     "q3tourney2 atech1_alpha cutout flag missing");
    ok = ok && Check(noblight != nullptr && noblight->isCutoutUnderlay,
                     "q3tourney2 x_noblight cutout flag missing");
    return ok && CheckOverlaysPresent(scene, kUnderlays, std::size(kUnderlays), true) &&
           CheckOverlaysPresent(scene, kOverlays, std::size(kOverlays), false);
}

bool VerifyDm1RetailScene(const EditorScene &scene)
{
    const size_t spotCount = size_t(std::count_if(
        scene.lights.begin(), scene.lights.end(), [](const EditorLight &l) { return l.spot; }));
    const EditorTexture *sky = FindSceneTexture(scene, "textures/skies/tim_hell");
    static constexpr const char *kCutouts[] = {
        "textures/gothic_block/demon_block15fx",
        "textures/gothic_floor/largerblock3b_ow",
        "textures/gothic_block/killblockgeomtrn",
        "textures/gothic_floor/center2trn",
        "textures/gothic_door/skullarch_b",
        "textures/skin/chapthroatooz",
    };

    for (const char *name : kCutouts)
    {
        const EditorSurface *surf = FindSceneSurface(scene, name);
        if (!Check(surf != nullptr && surf->isCutoutUnderlay &&
                       HasOverlaySurface(scene, name, true),
                   name))
        {
            return false;
        }
    }

    const bool flameHellCalibrated =
        std::any_of(scene.lights.begin(), scene.lights.end(), [](const EditorLight &l) {
            const float dx = l.position[0] - 672.0f;
            const float dy = l.position[1] - 1136.0f;
            const float dz = l.position[2] - 152.0f;
            return l.className == "textures/sfx/flame1_hell" &&
                   (dx * dx + dy * dy + dz * dz) < (64.0f * 64.0f) && l.intensity >= 16.0f &&
                   l.intensity <= 300.0f;
        });
    const bool pentagramFixturePresent =
        std::any_of(scene.lights.begin(), scene.lights.end(), [](const EditorLight &l) {
            return l.className == "textures/gothic_light/pentagram_light1_1k";
        });

    static constexpr const char *kFlames[] = {
        "textures/sfx/flame1_hell",
        "textures/sfx/flame2",
        "textures/sfx/flame1side",
    };
    return Check(spotCount >= 10u, "q3dm1 spotlights missing") &&
           Check(sky != nullptr && !sky->isEmissiveShader, "q3dm1 sky must not emit") &&
           Check(flameHellCalibrated && pentagramFixturePresent,
                 "q3dm1 flame1_hell or pentagram_light1_1k fixture lights failed") &&
           CheckOverlaysPresent(scene, kFlames, std::size(kFlames), false);
}

bool VerifyGeneratedCloudSkyScene(const EditorScene &scene, const char *mapPath)
{
    if (!Check(scene.skyboxName.empty() && scene.generatedSkyboxSize == 64u &&
                   scene.hasSkyFlatColor,
               mapPath))
    {
        return false;
    }
    for (const auto &face : scene.generatedSkyboxFaces)
    {
        if (!Check(face.size() == 64u * 64u * 4u, "generated skybox face size mismatch"))
        {
            return false;
        }
    }
    return true;
}

bool VerifyRetailMapSpecificChecks(const std::string &mapPath, const EditorScene &scene)
{
    if (mapPath == "maps/q3tourney1.bsp")
    {
        return VerifyTourney1RetailScene(scene);
    }
    if (mapPath == "maps/q3tourney2.bsp")
    {
        return VerifyTourney2RetailScene(scene);
    }
    if (mapPath == "maps/q3dm1.bsp")
    {
        return VerifyDm1RetailScene(scene) &&
               VerifyGeneratedCloudSkyScene(scene, mapPath.c_str());
    }
    if (mapPath == "maps/q3dm0.bsp")
    {
        return Check(FindSceneSurface(scene, "models/mapobjects/console/sphere2") == nullptr &&
                         HasOverlaySurface(scene, "models/mapobjects/console/sphere2"),
                     "q3dm0 sphere2 autoSprite must be raster overlay");
    }
    if (mapPath == "maps/q3dm10.bsp")
    {
        const EditorSurface *clang =
            FindSceneSurface(scene, "textures/base_floor/clang_floor3blava");
        return Check(FindSceneSurface(scene, "textures/base_trim/wires01") == nullptr &&
                         HasOverlaySurface(scene, "textures/base_trim/wires01") &&
                         HasOverlaySurface(scene, "textures/base_trim/wires01_ass") &&
                         clang != nullptr && !clang->isCutoutUnderlay,
                     "q3dm10 wires01 autoSprite2 or clang_floor3blava classification failed");
    }
    if (mapPath == "maps/q3dm6.bsp")
    {
        const bool hasStyle32 = std::any_of(
            scene.lights.begin(), scene.lights.end(),
            [](const EditorLight &l) { return l.style == 32; });
        return Check(hasStyle32, "q3dm6 light style 32 parsing failed");
    }
    if (mapPath == "maps/q3dm17.bsp" || mapPath == "maps/q3ctf1.bsp")
    {
        return VerifyGeneratedCloudSkyScene(scene, mapPath.c_str());
    }
    return true;
}

bool VerifyRetailMaps(const EditorAssetSource &assets)
{
    std::vector<EditorAssetCatalogEntry> maps;
    assets.ListMapAssets(maps);
    if (!Check(maps.size() >= 25, "expected retail Q3 maps"))
    {
        return false;
    }

    for (const EditorAssetCatalogEntry &m : maps)
    {
        std::vector<uint8_t> bspBytes;
        EditorScene scene;
        std::string err;
        if (!Check(assets.ReadFile(m.path, bspBytes), m.path.c_str()) ||
            !Check(LoadEditorSceneBspMemory(bspBytes, m.path, nullptr, scene, err,
                                            EditorSceneLoadOptions{ &assets, false }),
                   err.c_str()) ||
            !Check(scene.usesExternalLightingModel && !scene.surfaces.empty() && !scene.vertices.empty(),
                   m.path.c_str()) ||
            !VerifyRetailMapSpecificChecks(m.path, scene))
        {
            return false;
        }
    }
    return true;
}

bool EndsWithLowerAscii(std::string_view text, std::string_view suffix)
{
    if (text.size() < suffix.size())
    {
        return false;
    }
    const size_t offset = text.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); i++)
    {
        const unsigned char a = static_cast<unsigned char>(text[offset + i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b))
        {
            return false;
        }
    }
    return true;
}

bool VerifySpecialCharacterDetails(const EditorAssetSource &assets)
{
    EditorPreviewModel tankjr;
    EditorPreviewModel hunter;
    EditorPreviewModel sarge;
    std::string err;
    if (!Check(LoadEditorPreviewModel("models/players/tankjr.character", assets, nullptr, tankjr, err),
               err.c_str()) ||
        !Check(LoadEditorPreviewModel("models/players/hunter.character", assets, nullptr, hunter, err),
               err.c_str()) ||
        !Check(LoadEditorPreviewModel("models/players/sarge.character", assets, nullptr, sarge, err),
               err.c_str()))
    {
        return false;
    }

    bool foundTankJrTex = false;
    for (const EditorTexture &tex : tankjr.surfaceTextures)
    {
        if (tex.name.find("tankjr") != std::string::npos && tex.width >= 16)
        {
            foundTankJrTex = true;
            break;
        }
    }
    return Check(foundTankJrTex, "tankjr $whiteimage stage did not resolve real texture") &&
           Check(hunter.hiddenMaterials.count("models/players/hunter/harpy_f") != 0u,
                 "hunter harpy_f nodraw material must be hidden") &&
           Check(sarge.hiddenMaterials.count("models/players/sarge/null") != 0u,
                 "sarge null placeholder material must be hidden");
}

bool HasExcludedModelSuffix(std::string_view path)
{
    return EndsWithLowerAscii(path, "_hand.md3") || EndsWithLowerAscii(path, "_1.md3") ||
           EndsWithLowerAscii(path, "_2.md3");
}

bool VerifyRetailModels(const EditorAssetSource &assets)
{
    std::vector<EditorAssetCatalogEntry> models;
    assets.ListModelAssets(models);
    size_t charTested = 0;
    size_t totalTested = 0;

    for (const EditorAssetCatalogEntry &entry : models)
    {
        if (!Check(!HasExcludedModelSuffix(entry.path), entry.path.c_str()))
        {
            return false;
        }

        EditorPreviewModel model;
        std::string err;
        const bool loaded = LoadEditorPreviewModel(entry.path, assets, nullptr, model, err);
        if (!Check(loaded, err.c_str()) ||
            !Check(model.hasMultiSections && !model.frames.empty() && !model.frames[0].sections.empty(),
                   entry.path.c_str()))
        {
            return false;
        }
        totalTested++;
        if (EndsWithLowerAscii(entry.path, ".character"))
        {
            charTested++;
        }
    }

    EditorPreviewModel railgun;
    std::string err;
    return Check(charTested >= 20 && totalTested >= 180, "expected retail Q3 characters and models") &&
           Check(LoadEditorPreviewModel("models/weapons2/railgun/railgun.md3", assets, nullptr,
                                        railgun, err),
                 err.c_str()) &&
           Check(railgun.hasMultiSections && !railgun.frames.empty() && !railgun.frames[0].sections.empty(),
                 "retail standalone MD3 railgun failed") &&
           VerifySpecialCharacterDetails(assets);
}

bool TestRetailBaseQ3IfPresent()
{
    const std::filesystem::path baseq3 =
        "/home/user/.local/share/Steam/steamapps/common/Quake 3 Arena/baseq3";
    if (!std::filesystem::is_directory(baseq3))
    {
        return true;
    }

    EditorAssetSource assets;
    std::vector<std::string> pk3Files;
    for (const auto &entry : std::filesystem::directory_iterator(baseq3))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".pk3")
        {
            pk3Files.push_back(entry.path().string());
        }
    }
    std::sort(pk3Files.rbegin(), pk3Files.rend());
    for (const std::string &pk3 : pk3Files)
    {
        if (!Check(assets.AddZip(pk3), "retail pk3 mount failed"))
        {
            return false;
        }
    }

    return VerifyRetailMaps(assets) && VerifyRetailModels(assets);
}

} // namespace

std::vector<uint8_t> MakeUncompressedOrangeTga()
{
    std::vector<uint8_t> tga(18 + 2 * 2 * 4, 0);
    tga[2] = 2;
    tga[12] = 2;
    tga[14] = 2;
    tga[16] = 32;
    tga[17] = 8;
    for (size_t i = 18; i + 3 < tga.size(); i += 4)
    {
        tga[i + 0] = 32;  // B
        tga[i + 1] = 128; // G
        tga[i + 2] = 255; // R
        tga[i + 3] = 255; // A
    }
    return tga;
}

std::vector<uint8_t> BuildEdgeCaseQ3Bsp()
{
    std::vector<uint8_t> bytes(8 + 17 * 8, 0);
    bytes[0] = 'I';
    bytes[1] = 'B';
    bytes[2] = 'S';
    bytes[3] = 'P';
    const int32_t version = 46;
    std::memcpy(bytes.data() + 4, &version, 4);

    auto writeLump = [&](int idx, const void *data, size_t len) {
        TestQ3Lump lump{ int32_t(bytes.size()), int32_t(len) };
        std::memcpy(bytes.data() + 8 + idx * 8, &lump, sizeof(lump));
        AppendStructBytes(bytes, data, len);
    };

    const std::string ents =
        "{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"16 0 24\"\n}\n";
    writeLump(0, ents.data(), ents.size() + 1);

    TestQ3Shader shaders[4] = {};
    std::snprintf(shaders[0].shader, 64, "textures/test/overlay_flame");
    std::snprintf(shaders[1].shader, 64, "textures/test/fog_volume");
    shaders[1].contentFlags = 0x20000;
    std::snprintf(shaders[2].shader, 64, "textures/test/mirror_surface");
    std::snprintf(shaders[3].shader, 64, "textures/test/cloud_sky");
    shaders[3].surfaceFlags = 0x4;
    writeLump(1, shaders, sizeof(shaders));

    TestQ3Plane plane{ { 1.0f, 0.0f, 0.0f }, 0.0f };
    writeLump(2, &plane, sizeof(plane));

    TestQ3Node node{};
    node.children[0] = -1;
    node.children[1] = -2;
    writeLump(3, &node, sizeof(node));

    TestQ3Leaf leafs[2] = {};
    leafs[0].cluster = 0;
    leafs[1].cluster = -1;
    writeLump(4, leafs, sizeof(leafs));

    TestQ3Model model{};
    model.numSurfaces = 4;
    writeLump(7, &model, sizeof(model));

    std::vector<TestQ3DrawVert> verts(4);
    const float ys[4] = { -16.0f, 16.0f, 16.0f, -16.0f };
    const float zs[4] = { 0.0f, 0.0f, 32.0f, 32.0f };
    for (int i = 0; i < 4; i++)
    {
        verts[i].xyz[0] = 32.0f;
        verts[i].xyz[1] = ys[i];
        verts[i].xyz[2] = zs[i];
        verts[i].normal[0] = -1.0f;
        verts[i].color[0] = verts[i].color[1] = verts[i].color[2] = verts[i].color[3] = 255;
    }
    writeLump(10, verts.data(), verts.size() * sizeof(TestQ3DrawVert));

    const int32_t idxData[6] = { 0, 2, 1, 0, 3, 2 };
    writeLump(11, idxData, sizeof(idxData));

    TestQ3Surface surfs[4] = {};
    for (int i = 0; i < 4; i++)
    {
        surfs[i].shaderNum = i;
        surfs[i].surfaceType = 1;
        surfs[i].numVerts = 4;
        surfs[i].numIndexes = 6;
        surfs[i].lightmapNum = -1;
    }
    writeLump(13, surfs, sizeof(surfs));

    uint8_t vis[9] = {};
    const int32_t nClusters = 1;
    const int32_t rowBytes = 1;
    std::memcpy(vis + 0, &nClusters, 4);
    std::memcpy(vis + 4, &rowBytes, 4);
    vis[8] = 0x01u;
    writeLump(16, vis, sizeof(vis));

    return bytes;
}

bool VerifyShaderCutoutsAndSkinComments(const Q3ShaderDatabase &db)
{
    const Q3ShaderDef *flame = db.Find("textures/sfx/flame_test");
    const Q3ShaderDef *tesla = db.Find("textures/sfx/tesla_test");
    const Q3ShaderDef *fan = db.Find("textures/alpha/fan_test");
    const std::string skinText = "h_head,models/a.tga // comment\r\ntag_head,\r\n// full comment\r\n";
    const auto skin = ParseQ3SkinBytes(StringToBytes(skinText));

    const bool flameOk = flame != nullptr && flame->map == "tex1.tga" && flame->isNonTracedOverlay;
    const bool teslaOk = tesla != nullptr && tesla->isNonTracedOverlay;
    const bool fanOk = fan != nullptr && fan->alphaTested && !fan->isNonTracedOverlay;
    const bool skinOk = skin.size() == 1u && skin.at("h_head") == "models/a.tga";
    return Check(flameOk, "animMap blendFunc parsing failed") &&
           Check(teslaOk, "teslacoil mixed blendFunc not flagged overlay") &&
           Check(fanOk, "alphaBlend cutout shader classification failed") &&
           Check(skinOk, "ParseQ3SkinBytes comment stripping failed");
}

bool TestShaderEdgeCases()
{
    Q3ShaderDatabase db;
    const char *script = R"(
models/players/tankjr/tankjr
{
    { map $whiteimage }
    { map models/players/tankjr/tankjr.tga blendFunc GL_DST_COLOR GL_SRC_ALPHA }
}
models/players/razor/patriot_gogs
{
    { map models/players/razor/gogs.tga tcGen environment }
}
textures/sfx/flame_test
{
    { animMap 10 tex1.tga tex2.tga tex3.tga blendFunc GL_ONE GL_ONE }
}
textures/sfx/tesla_test
{
    cull none
    { map $lightmap tcgen environment blendfunc filter }
    { map textures/sfx/tesla1.tga blendfunc add }
    { map textures/sfx/cabletest2.tga blendfunc blend }
}
textures/alpha/fan_test
{
    { map textures/alpha/fan.tga blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA }
}
)";
    if (!Check(db.ParseScript(script, std::strlen(script)) == 5u, "edge case shader count"))
    {
        return false;
    }
    const Q3ShaderDef *tj = db.Find("models/players/tankjr/tankjr");
    const Q3ShaderDef *gogs = db.Find("models/players/razor/patriot_gogs");
    const bool tjOk =
        tj != nullptr && tj->map == "models/players/tankjr/tankjr.tga" && !tj->isNonTracedOverlay;
    const bool gogsOk =
        gogs != nullptr && gogs->map == "models/players/razor/gogs.tga" && gogs->hasEnvironment;

    return Check(tjOk, "$whiteimage shader fallback failed") &&
           Check(gogsOk, "tcGen environment fallback failed") &&
           VerifyShaderCutoutsAndSkinComments(db);
}

bool VerifyPrefixAndSessionPaths(const std::filesystem::path &tempDir)
{
    const std::filesystem::path prefixYaml = tempDir / "prefix.yaml";
    EditorMaterialEntry entry;
    entry.name = "models/players/sarge/band.tga";
    entry.params.roughnessOverride = 0.3f;
    EditorMaterialsDocument writer;
    const std::string yamlText = writer.WriteMerged({ entry });
    {
        std::ofstream out(prefixYaml, std::ios::binary | std::ios::trunc);
        out.write(yamlText.data(), std::streamsize(yamlText.size()));
    }
    std::string err;
    EditorMaterialsDocument doc;
    const bool docLoaded = doc.Load(prefixYaml.string(), err);
    const bool prefixOk = docLoaded && doc.HasMaterialWithPrefix("models/players/sarge") &&
                          !doc.HasMaterialWithPrefix("models/players/sarge2");

    const std::filesystem::path modMat = tempDir / "mod" / "qray.materials.yaml";
    const EditorSessionPaths sessPaths =
        BuildEditorSessionPaths(modMat.string(), "", "maps/nonexistent_in_cwd.bsp");
    const bool sessionOk =
        sessPaths.level == "nonexistent_in_cwd" && sessPaths.materials == modMat.string();

    return Check(prefixOk, "HasMaterialWithPrefix failed") &&
           Check(sessionOk, "BuildEditorSessionPaths relative map failed");
}

bool VerifyEdgeCaseBspScene(const EditorScene &scene)
{
    const bool mirrorMarked = scene.textures.size() >= 3u && scene.textures[2].isMirrorShader &&
                              scene.surfaces.size() == 2u;
    const bool overlayLightTinted = scene.lights.size() == 1u && scene.bspLights.size() == 1u &&
                                    scene.lights[0].tint[0] > scene.lights[0].tint[2] + 0.3f;
    const bool skyOk = scene.skyboxName.empty() && scene.generatedSkyboxSize == 64u &&
                       scene.hasSkyFlatColor && scene.hasSun &&
                       std::fabs(scene.sunDegrees + 41.0f) < 1e-4f;

    return Check(mirrorMarked, "overlay/fog skip or mirror classification failed") &&
           Check(overlayLightTinted, "overlay emissive RGB light tint failed") &&
           Check(skyOk, "cloud skybox generation or negative sunDegrees failed");
}

bool TestOverlayCloudSkyAndSessionPaths(const std::filesystem::path &tempDir)
{
    const std::filesystem::path pk3Path = tempDir / "edge_cases.pk3";
    const std::string shaderScript = R"(
textures/test/overlay_flame
{
    q3map_surfacelight 5500
    { map textures/test/orange.tga blendFunc add }
}
textures/test/fog_volume
{
    surfaceparm fog
    { map textures/test/orange.tga }
}
textures/test/mirror_surface
{
    portal
    { map textures/test/orange.tga }
}
textures/test/cloud_sky
{
    surfaceparm sky
    q3map_sun 1 0.9 0.8 100 -41 58
    { map textures/test/orange.tga tcMod scale 2 2 }
}
)";
    const std::vector<StoredZipInput> files = {
        { "scripts/edge.shader", StringToBytes(shaderScript) },
        { "textures/test/orange.tga", MakeUncompressedOrangeTga() },
        { "maps/edge.bsp", BuildEdgeCaseQ3Bsp() },
    };
    EditorAssetSource assets;
    std::vector<uint8_t> bspBytes;
    EditorScene scene;
    std::string err;
    const bool loaded = WriteStoredZip(pk3Path, files) && assets.AddZip(pk3Path.string()) &&
                        assets.ReadFile("maps/edge.bsp", bspBytes) &&
                        LoadEditorSceneBspMemory(bspBytes, "maps/edge.bsp", nullptr, scene, err,
                                                 EditorSceneLoadOptions{ &assets, false });
    return Check(loaded, "edge BSP load failed") && VerifyEdgeCaseBspScene(scene) &&
           VerifyPrefixAndSessionPaths(tempDir);
}

std::vector<uint8_t> MakeSolidOrCutoutTga(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b,
                                        uint8_t a, uint32_t holePixels)
{
    std::vector<uint8_t> tga(18 + size_t(w) * size_t(h) * 4u, 0);
    tga[2] = 2;
    tga[12] = uint8_t(w & 0xFFu);
    tga[13] = uint8_t((w >> 8u) & 0xFFu);
    tga[14] = uint8_t(h & 0xFFu);
    tga[15] = uint8_t((h >> 8u) & 0xFFu);
    tga[16] = 32;
    tga[17] = 8;
    const uint32_t total = w * h;
    for (uint32_t p = 0; p < total; p++)
    {
        const size_t off = 18 + size_t(p) * 4u;
        tga[off + 0] = b;
        tga[off + 1] = g;
        tga[off + 2] = r;
        tga[off + 3] = (p < holePixels) ? 0u : a;
    }
    return tga;
}

std::vector<uint8_t> BuildMultiSurfaceMd3(const char *surfA, const char *shA, const char *surfB,
                                          const char *shB)
{
    const char *names[2] = { surfA, surfB };
    const char *shaders[2] = { shA, shB };
    std::vector<uint8_t> bytes(108, 0);
    std::memcpy(bytes.data() + 0, "IDP3", 4);
    const int32_t ver = 15;
    const int32_t one = 1;
    const int32_t two = 2;
    std::memcpy(bytes.data() + 4, &ver, 4);
    std::memcpy(bytes.data() + 76, &one, 4);
    std::memcpy(bytes.data() + 84, &two, 4);
    const int32_t ofsFrames = 108;
    const int32_t ofsTags = ofsFrames + 56;
    const int32_t ofsSurfs = ofsTags;
    std::memcpy(bytes.data() + 92, &ofsFrames, 4);
    std::memcpy(bytes.data() + 96, &ofsTags, 4);
    std::memcpy(bytes.data() + 100, &ofsSurfs, 4);
    bytes.resize(size_t(ofsSurfs), 0);

    for (int s = 0; s < 2; s++)
    {
        const size_t surfStart = bytes.size();
        const int32_t triCount = 1;
        const int32_t vertCount = 3;
        const int32_t ofsTris = 108;
        const int32_t ofsShader = ofsTris + 12;
        const int32_t ofsSt = ofsShader + 68;
        const int32_t ofsXyz = ofsSt + vertCount * 8;
        const int32_t surfEnd = ofsXyz + vertCount * 8;
        bytes.resize(surfStart + size_t(surfEnd), 0);
        uint8_t *sp = bytes.data() + surfStart;
        std::memcpy(sp + 0, "IDP3", 4);
        std::snprintf(reinterpret_cast<char *>(sp + 4), 64, "%s", names[s]);
        std::memcpy(sp + 72, &one, 4);
        std::memcpy(sp + 76, &one, 4);
        std::memcpy(sp + 80, &vertCount, 4);
        std::memcpy(sp + 84, &triCount, 4);
        std::memcpy(sp + 88, &ofsTris, 4);
        std::memcpy(sp + 92, &ofsShader, 4);
        std::memcpy(sp + 96, &ofsSt, 4);
        std::memcpy(sp + 100, &ofsXyz, 4);
        std::memcpy(sp + 104, &surfEnd, 4);
        const int32_t tri[3] = { 0, 1, 2 };
        std::memcpy(sp + ofsTris, tri, sizeof(tri));
        std::snprintf(reinterpret_cast<char *>(sp + ofsShader), 64, "%s", shaders[s]);
        const float st[6] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
        std::memcpy(sp + ofsSt, st, sizeof(st));
        for (int v = 0; v < 3; v++)
        {
            int16_t rec[4] = { int16_t(v == 1 ? 640 : 0), int16_t(v == 2 ? 640 : 0), 0, 0 };
            std::memcpy(sp + ofsXyz + size_t(v) * 8u, rec, sizeof(rec));
        }
    }
    const int32_t totalEnd = int32_t(bytes.size());
    std::memcpy(bytes.data() + 104, &totalEnd, 4);
    return bytes;
}

std::vector<uint8_t> BuildCutoutLightAndOverlayQ3Bsp()
{
    std::vector<uint8_t> bytes(8 + 17 * 8, 0);
    bytes[0] = 'I';
    bytes[1] = 'B';
    bytes[2] = 'S';
    bytes[3] = 'P';
    const int32_t version = 46;
    std::memcpy(bytes.data() + 4, &version, 4);

    auto writeLump = [&](int idx, const void *data, size_t len) {
        TestQ3Lump lump{ int32_t(bytes.size()), int32_t(len) };
        std::memcpy(bytes.data() + 8 + idx * 8, &lump, sizeof(lump));
        AppendStructBytes(bytes, data, len);
    };

    const std::string ents =
        "{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"16 0 24\"\n}\n"
        "{\n\"classname\" \"target_position\"\n\"targetname\" \"spot_t\"\n\"origin\" \"0 0 0\"\n}\n"
        "{\n\"classname\" \"light\"\n\"origin\" \"0 0 128\"\n\"target\" \"spot_t\"\n"
        "\"light\" \"1000\"\n\"scale\" \"2\"\n\"radius\" \"32\"\n\"_color\" \"0.2 0.6 1.0\"\n}\n"
        "{\n\"classname\" \"light\"\n\"origin\" \"16 16 16\"\n\"_light\" \"120 60 30\"\n"
        "\"color\" \"1 0 0\"\n}\n"
        "{\n\"classname\" \"target_position\"\n\"targetname\" \"sun_t\"\n\"origin\" \"0 0 0\"\n}\n"
        "{\n\"classname\" \"light\"\n\"origin\" \"100 0 100\"\n\"target\" \"sun_t\"\n"
        "\"_sun\" \"1\"\n\"light\" \"150\"\n\"_color\" \"1 0.9 0.7\"\n}\n";
    writeLump(0, ents.data(), ents.size() + 1);

    TestQ3Shader shaders[4] = {};
    std::snprintf(shaders[0].shader, 64, "textures/test/cutout_glow");
    std::snprintf(shaders[1].shader, 64, "textures/test/torch_flame");
    std::snprintf(shaders[2].shader, 64, "textures/test/water_pool");
    shaders[2].contentFlags = 0x20;
    std::snprintf(shaders[3].shader, 64, "textures/test/sky_emit");
    shaders[3].surfaceFlags = 0x4;
    writeLump(1, shaders, sizeof(shaders));

    TestQ3Plane plane{ { 1.0f, 0.0f, 0.0f }, 0.0f };
    writeLump(2, &plane, sizeof(plane));
    TestQ3Node node{};
    node.children[0] = -1;
    node.children[1] = -2;
    writeLump(3, &node, sizeof(node));
    TestQ3Leaf leafs[2] = {};
    leafs[0].cluster = 0;
    leafs[1].cluster = -1;
    writeLump(4, leafs, sizeof(leafs));
    TestQ3Model model{};
    model.numSurfaces = 4;
    writeLump(7, &model, sizeof(model));

    std::vector<TestQ3DrawVert> verts(4);
    const float ys[4] = { -16.0f, 16.0f, 16.0f, -16.0f };
    const float zs[4] = { 0.0f, 0.0f, 32.0f, 32.0f };
    const float us[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
    const float vs[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    for (int i = 0; i < 4; i++)
    {
        verts[i].xyz[0] = 32.0f;
        verts[i].xyz[1] = ys[i];
        verts[i].xyz[2] = zs[i];
        verts[i].st[0] = us[i];
        verts[i].st[1] = vs[i];
        verts[i].normal[0] = -1.0f;
        verts[i].color[0] = 200;
        verts[i].color[1] = 100;
        verts[i].color[2] = 50;
        verts[i].color[3] = 255;
    }
    writeLump(10, verts.data(), verts.size() * sizeof(TestQ3DrawVert));
    const int32_t idxData[6] = { 0, 2, 1, 0, 3, 2 };
    writeLump(11, idxData, sizeof(idxData));

    TestQ3Surface surfs[4] = {};
    for (int i = 0; i < 4; i++)
    {
        surfs[i].shaderNum = i;
        surfs[i].surfaceType = 1;
        surfs[i].numVerts = 4;
        surfs[i].numIndexes = 6;
        surfs[i].lightmapNum = -1;
    }
    writeLump(13, surfs, sizeof(surfs));

    uint8_t vis[9] = {};
    const int32_t nClusters = 1;
    const int32_t rowBytes = 1;
    std::memcpy(vis + 0, &nClusters, 4);
    std::memcpy(vis + 4, &rowBytes, 4);
    vis[8] = 0x01u;
    writeLump(16, vis, sizeof(vis));
    return bytes;
}

bool VerifyCutoutAuxShadersAndWave(const Q3ShaderDatabase &db)
{
    const Q3ShaderDef *rotCutout = db.Find("textures/test/rot_cutout");
    const Q3ShaderDef *pewter = db.Find("textures/test/pewter_env");
    const Q3ShaderDef *tallFlame = db.Find("textures/test/torch_tall");

    const bool auxOk = rotCutout != nullptr && rotCutout->cutoutFrameUnderlay &&
                       rotCutout->baseStageIndex == 2u && pewter != nullptr &&
                       pewter->metallicEnvBase && pewter->tracedBase;
    const bool deformOk =
        tallFlame != nullptr && tallFlame->autoSprite2 && tallFlame->deformWave == Q3ShaderWaveType::Sin;
    const bool waveOk =
        std::fabs(EvaluateQ3ShaderWave(Q3ShaderWaveType::Square, 0.5f, 0.5f, 0.0f, 1.0f, 0.25f) -
                  1.0f) < 1e-4f;
    return Check(auxOk, "rot_cutout stage selection or pewter_env classification failed") &&
           Check(deformOk && waveOk, "autoSprite2, deformWave, or Square wave evaluation failed");
}

bool VerifyCutoutShaderDatabaseChecks(const std::string &shaderScript,
                                      const EditorAssetSource &assets)
{
    Q3ShaderDatabase db;
    db.ParseScript(shaderScript.data(), shaderScript.size(), true, &assets);
    const Q3ShaderDef *cutout = db.Find("textures/test/cutout_glow");

    const bool cutoutOk = cutout != nullptr && cutout->cutoutFrameUnderlay &&
                          cutout->baseStageIndex == 1u &&
                          std::fabs(cutout->baseScale[0] - 1.0f) < 1e-4f &&
                          cutout->emissionMaskMap == "textures/test/glow.tga" && cutout->usesOverlay;
    return Check(cutoutOk, "cutout_glow classification, baseScale, or emissionMaskMap failed") &&
           VerifyCutoutAuxShadersAndWave(db);
}

bool VerifyCutoutSpotlightAndSun(const EditorScene &scene)
{
    const EditorTexture *skyTex = FindSceneTexture(scene, "textures/test/sky_emit");
    const auto spotIt = std::find_if(scene.lights.begin(), scene.lights.end(),
                                     [](const EditorLight &l) { return l.spot; });
    const EditorLight *spot = spotIt != scene.lights.end() ? &*spotIt : nullptr;

    const bool hasCutoutLight =
        std::any_of(scene.lights.begin(), scene.lights.end(),
                    [](const EditorLight &l) { return l.className == "textures/test/cutout_glow"; });
    const bool hasTorchLight =
        std::any_of(scene.lights.begin(), scene.lights.end(),
                    [](const EditorLight &l) { return l.className == "textures/test/torch_flame"; });
    const bool spotOk = spot != nullptr && spot->direction[2] < -0.99f &&
                        std::fabs(spot->intensity - 2000.0f) < 1e-2f && spot->angleOuter > 13.5f &&
                        spot->angleOuter < 14.5f;
    const bool skyOk = scene.lights.size() == 4u && scene.bspLights.size() == 4u &&
                       hasCutoutLight && hasTorchLight && scene.hasSun && skyTex != nullptr &&
                       !skyTex->isEmissiveShader;
    return Check(spotOk, "spotlight target/scale/radius parse failed") &&
           Check(skyOk, "spotlight/_light/_sun/sky emission isolation failed");
}

bool VerifyCutoutSceneLightsAndOverlays(const EditorScene &scene)
{
    if (!VerifyCutoutSpotlightAndSun(scene))
    {
        return false;
    }

    const EditorSurface *tracedCutout = FindSceneSurface(scene, "textures/test/cutout_glow");
    size_t cutoutOvCount = 0;
    size_t postOvCount = 0;
    for (const EditorOverlaySurface &ov : scene.overlaySurfaces)
    {
        if (ov.shaderName == "textures/test/cutout_glow")
        {
            cutoutOvCount += ov.cutoutUnderlay ? 1u : 0u;
            postOvCount += ov.cutoutUnderlay ? 0u : 1u;
        }
    }

    const bool uvIsolated = tracedCutout != nullptr && tracedCutout->isCutoutUnderlay &&
                            !tracedCutout->vertices.empty() &&
                            std::fabs(tracedCutout->vertices[1].texCoord[0] - 1.0f) < 1e-4f;
    const bool torchSeparated = FindSceneSurface(scene, "textures/test/torch_flame") == nullptr &&
                                HasOverlaySurface(scene, "textures/test/torch_flame");
    const bool waterTracedOnly = FindSceneSurface(scene, "textures/test/water_pool") != nullptr &&
                                 !HasOverlaySurface(scene, "textures/test/water_pool");
    return Check(uvIsolated && cutoutOvCount == 1u && postOvCount == 1u,
                 "cutout underlay + post glow overlay split failed") &&
           Check(torchSeparated, "torch_flame overlay separation failed") &&
           Check(waterTracedOnly, "water_pool must not duplicate onto overlay");
}

bool VerifyCutoutMd3Metadata(const EditorPreviewModel &md3Model)
{
    if (!Check(md3Model.frames.size() == 1u && md3Model.frames[0].sections.size() == 2u,
               "MD3 frame/section count mismatch"))
    {
        return false;
    }
    const EditorPreviewModelSection &sec0 = md3Model.frames[0].sections[0];
    const EditorPreviewModelSection &sec1 = md3Model.frames[0].sections[1];
    return Check(sec0.tracedBase && sec0.usesOverlay && sec1.isNonTracedOverlay && sec1.autoSprite,
                 "MD3 preview section overlay/autosprite metadata failed");
}

bool TestCutoutOverlaysLightsAndMd3(const std::filesystem::path &tempDir)
{
    const std::string shaderScript = R"(
textures/test/cutout_glow
{
    q3map_surfacelight 100
    { map textures/sfx/firegorre.tga tcMod scale 4 4 tcMod scroll 0 1 blendFunc GL_ONE GL_ZERO }
    { map textures/test/frame_holes.tga blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA }
    { map $lightmap blendFunc GL_DST_COLOR GL_ZERO }
    { map textures/test/glow.tga blendFunc GL_ONE GL_ONE rgbGen wave sin 0.5 0.5 0 0.1 }
}
textures/test/rot_cutout
{
    { map textures/sfx/firegorre.tga tcMod rotate 333 }
    { map textures/test/frame_holes.tga blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA tcMod rotate 30 }
    { map textures/test/frame_holes.tga blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA }
    { map $lightmap blendFunc GL_DST_COLOR GL_ZERO }
}
textures/test/pewter_env
{
    { map textures/test/glow.tga tcGen environment }
    { map textures/test/env_mask.tga blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA }
    { map $lightmap blendFunc GL_DST_COLOR GL_ZERO }
}
textures/test/torch_flame
{
    deformVertexes autoSprite
    q3map_surfacelight 5500
    { animMap 10 textures/test/f1.tga textures/test/f2.tga blendFunc GL_ONE GL_ONE }
}
textures/test/torch_tall
{
    deformVertexes autoSprite2
    deformVertexes wave 100 sin 0 2 0 5
    { map textures/test/f1.tga blendFunc GL_ONE GL_ONE rgbGen vertex alphaGen vertex }
}
textures/test/water_pool
{
    surfaceparm water
    { map textures/test/f1.tga tcMod turb 0 0.1 0 0.5 }
}
textures/test/sky_emit
{
    surfaceparm sky
    q3map_surfacelight 500
    { map textures/test/f1.tga }
}
models/test/forearm
{
    { map textures/test/env_mask.tga rgbGen vertex }
    { map textures/test/glow.tga blendFunc GL_ONE GL_ONE tcMod scroll 2 0 }
}
models/test/orb
{
    deformVertexes autoSprite
    surfaceparm trans
    { clampmap textures/test/glow.tga blendFunc GL_ONE GL_ONE tcMod rotate 30 }
}
)";

    const std::filesystem::path pk3Path = tempDir / "cutout_lights.pk3";
    const std::vector<StoredZipInput> files = {
        { "scripts/cutout.shader", StringToBytes(shaderScript) },
        { "textures/sfx/firegorre.tga", MakeSolidOrCutoutTga(4, 4, 255, 120, 20, 255, 0) },
        { "textures/test/frame_holes.tga", MakeSolidOrCutoutTga(4, 4, 160, 160, 160, 255, 4) },
        { "textures/test/glow.tga", MakeSolidOrCutoutTga(4, 4, 255, 200, 40, 255, 0) },
        { "textures/test/env_mask.tga", MakeSolidOrCutoutTga(4, 4, 90, 90, 90, 140, 0) },
        { "textures/test/f1.tga", MakeSolidOrCutoutTga(4, 4, 255, 128, 32, 255, 0) },
        { "textures/test/f2.tga", MakeSolidOrCutoutTga(4, 4, 255, 160, 64, 255, 0) },
        { "maps/cutouts.bsp", BuildCutoutLightAndOverlayQ3Bsp() },
        { "models/test/widget.md3",
          BuildMultiSurfaceMd3("s_arm", "models/test/forearm", "s_orb", "models/test/orb") },
    };

    EditorAssetSource assets;
    std::vector<uint8_t> bspBytes;
    EditorScene scene;
    EditorPreviewModel md3Model;
    std::string err;
    if (!Check(WriteStoredZip(pk3Path, files) && assets.AddZip(pk3Path.string()),
               "cutout pk3 mount") ||
        !Check(assets.ReadFile("maps/cutouts.bsp", bspBytes), "read cutouts.bsp") ||
        !Check(LoadEditorSceneBspMemory(bspBytes, "maps/cutouts.bsp", nullptr, scene, err,
                                        EditorSceneLoadOptions{ &assets, false }),
               err.c_str()) ||
        !Check(LoadEditorPreviewModel("models/test/widget.md3", assets, nullptr, md3Model, err),
               err.c_str()))
    {
        return false;
    }

    return VerifyCutoutShaderDatabaseChecks(shaderScript, assets) &&
           VerifyCutoutSceneLightsAndOverlays(scene) && VerifyCutoutMd3Metadata(md3Model);
}

std::vector<uint8_t> BuildRegressionTestBspBytes()
{
    std::vector<uint8_t> bspBytes(8 + 17 * 8, 0);
    bspBytes[0] = 'I';
    bspBytes[1] = 'B';
    bspBytes[2] = 'S';
    bspBytes[3] = 'P';
    const int32_t version = 46;
    std::memcpy(bspBytes.data() + 4, &version, 4);
    auto writeLump = [&](int idx, const void *data, size_t len) {
        TestQ3Lump lump{ int32_t(bspBytes.size()), int32_t(len) };
        std::memcpy(bspBytes.data() + 8 + idx * 8, &lump, sizeof(lump));
        AppendStructBytes(bspBytes, data, len);
    };

    const std::string ents =
        "{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"0 0 24\"\n}\n"
        "{\n\"classname\" \"light\"\n\"origin\" \"32 0 16\"\n\"light\" \"140\"\n\"style\" \"32\"\n}\n";
    writeLump(0, ents.data(), ents.size() + 1);

    TestQ3Shader shaders[2] = {};
    std::snprintf(shaders[0].shader, 64, "textures/test/clang_translucent");
    std::snprintf(shaders[1].shader, 64, "textures/test/flame_pair");
    writeLump(1, shaders, sizeof(shaders));

    TestQ3Plane plane{ { 1.0f, 0.0f, 0.0f }, 0.0f };
    writeLump(2, &plane, sizeof(plane));
    TestQ3Node node{};
    node.children[0] = -1;
    node.children[1] = -2;
    writeLump(3, &node, sizeof(node));
    TestQ3Leaf leafs[2] = {};
    leafs[0].cluster = 0;
    leafs[1].cluster = -1;
    writeLump(4, leafs, sizeof(leafs));
    TestQ3Model model{};
    model.numSurfaces = 3;
    writeLump(7, &model, sizeof(model));

    std::vector<TestQ3DrawVert> verts(12);
    for (int s = 0; s < 3; s++)
    {
        const float x = (s == 2) ? 200.0f : 32.0f;
        const float ys[4] = { -20.0f, 20.0f, 20.0f, -20.0f };
        const float zs[4] = { 0.0f, 0.0f, 40.0f, 40.0f };
        for (int v = 0; v < 4; v++)
        {
            TestQ3DrawVert &dv = verts[size_t(s) * 4u + size_t(v)];
            dv.xyz[0] = x;
            dv.xyz[1] = ys[v];
            dv.xyz[2] = zs[v];
            dv.st[0] = (v == 1 || v == 2) ? 1.0f : 0.0f;
            dv.st[1] = (v >= 2) ? 1.0f : 0.0f;
            dv.normal[0] = -1.0f;
            dv.color[3] = 255;
        }
    }
    writeLump(10, verts.data(), verts.size() * sizeof(TestQ3DrawVert));
    const int32_t idxData[6] = { 0, 2, 1, 0, 3, 2 };
    writeLump(11, idxData, sizeof(idxData));

    TestQ3Surface surfs[3] = {};
    const int32_t sNums[3] = { 0, 1, 1 };
    for (int i = 0; i < 3; i++)
    {
        surfs[i].shaderNum = sNums[i];
        surfs[i].surfaceType = 1;
        surfs[i].firstVert = i * 4;
        surfs[i].numVerts = 4;
        surfs[i].firstIndex = 0;
        surfs[i].numIndexes = 6;
        surfs[i].lightmapNum = -1;
    }
    writeLump(13, surfs, sizeof(surfs));

    uint8_t vis[9] = {};
    const int32_t nClusters = 1;
    const int32_t rowBytes = 1;
    std::memcpy(vis + 0, &nClusters, 4);
    std::memcpy(vis + 4, &rowBytes, 4);
    vis[8] = 0x01u;
    writeLump(16, vis, sizeof(vis));
    return bspBytes;
}

bool CheckNonTracedOverlayShader(const Q3ShaderDatabase &db, const char *name, const char *msg)
{
    const Q3ShaderDef *def = db.Find(name);
    return Check(def != nullptr && def->isNonTracedOverlay && !def->tracedBase && def->usesOverlay,
                 msg);
}

bool VerifyRegressionShaderClassifications(const Q3ShaderDatabase &db)
{
    const Q3ShaderDef *noParmSky = db.Find("textures/skies/no_parm_sky");
    const Q3ShaderDef *clangTrans = db.Find("textures/test/clang_translucent");
    const Q3ShaderDef *clangBin = db.Find("textures/test/clang_binary");

    const bool skyOk = noParmSky != nullptr && noParmSky->isSky;
    const bool clangTransOk = clangTrans != nullptr && clangTrans->compositeUnderlayBase &&
                              !clangTrans->cutoutFrameUnderlay;
    const bool clangBinOk =
        clangBin != nullptr && clangBin->cutoutFrameUnderlay && clangBin->usesOverlay;

    return Check(skyOk, "skyparms must mark isSky") &&
           CheckNonTracedOverlayShader(db, "textures/test/sprite_alpha",
                                       "autoSprite + alphaFunc GE128 must be raster overlay") &&
           CheckNonTracedOverlayShader(db, "textures/test/wires_sprite2",
                                       "autoSprite2 + alphaFunc GE128 must be raster overlay") &&
           CheckNonTracedOverlayShader(
               db, "textures/test/shiny_inverted_glow",
               "animated opaque + invertedAlphaBlend + additive must be raster overlay") &&
           Check(clangTransOk,
                 "translucent grate without solid alpha texels must stay compositeUnderlayBase") &&
           Check(clangBinOk,
                 "binary cutout grate over emissive underlay without lightmap must be cutout");
}

bool VerifyRegressionLoadedScene(const std::vector<uint8_t> &bspBytes,
                                 const EditorAssetSource &assets)
{
    EditorScene scene;
    std::string err;
    if (!Check(LoadQ3BspSceneMemory(bspBytes, "maps/reg.bsp", scene, err,
                                    EditorSceneLoadOptions{ &assets, false }),
               err.c_str()))
    {
        return false;
    }

    const size_t flameCount = size_t(std::count_if(
        scene.overlaySurfaces.begin(), scene.overlaySurfaces.end(),
        [](const EditorOverlaySurface &ov) { return ov.shaderName == "textures/test/flame_pair"; }));

    bool hasStyle32 = false;
    bool calibratedOverlayLights = true;
    for (const EditorLight &l : scene.lights)
    {
        if (l.style == 32)
        {
            hasStyle32 = true;
        }
        else if (l.intensity < 16.0f || l.intensity > 300.0f)
        {
            calibratedOverlayLights = false;
        }
    }

    return Check(flameCount == 2u, "isNonTracedOverlay surfaces must stay unbatched per surface") &&
           Check(hasStyle32, "Q3 light style attribute parsing failed") &&
           Check(calibratedOverlayLights, "EmitQ3SurfacePointLights exceeded calibrated cap");
}

bool TestRegressionShadersAndPerSurfaceOverlays(const std::filesystem::path &tempDir)
{
    const std::string shaderScript = R"(
textures/skies/no_parm_sky
{
    skyparms - 128 -
    { map textures/test/f1.tga }
}
textures/test/sprite_alpha
{
    cull disable
    deformVertexes autoSprite
    { map textures/test/glow.tga blendFunc add }
    { clampmap textures/test/frame_holes.tga alphaFunc GE128 tcMod rotate 40 }
}
textures/test/wires_sprite2
{
    cull disable
    deformVertexes autoSprite2
    surfaceparm trans
    { map textures/test/frame_holes.tga alphaFunc GE128 rgbGen vertex }
    { map $lightmap blendFunc filter }
}
textures/test/shiny_inverted_glow
{
    q3map_surfacelight 100
    { map textures/sfx/firegorre.tga tcMod scroll -10 -1 }
    { map textures/test/frame_holes.tga blendFunc GL_ONE_MINUS_SRC_ALPHA GL_SRC_ALPHA }
    { map $lightmap blendFunc GL_DST_COLOR GL_ZERO }
    { map textures/test/glow.tga blendFunc GL_ONE GL_ONE }
}
textures/test/clang_translucent
{
    surfaceparm nolightmap
    { map textures/liquids/protolava2.tga tcMod scroll 0.04 0.03 }
    { map textures/test/translucent_grate.tga blendFunc blend }
}
textures/test/clang_binary
{
    surfaceparm nolightmap
    { map textures/liquids/protolava2.tga tcMod scroll 0.04 0.03 }
    { map textures/test/frame_holes.tga blendFunc blend }
}
textures/test/flame_pair
{
    q3map_surfacelight 5500
    { map textures/test/f1.tga blendFunc GL_ONE GL_ONE }
}
)";

    const std::vector<uint8_t> bspBytes = BuildRegressionTestBspBytes();
    const std::filesystem::path pk3Path = tempDir / "regression_checks.pk3";
    const std::vector<StoredZipInput> files = {
        { "scripts/reg.shader", StringToBytes(shaderScript) },
        { "textures/sfx/firegorre.tga", MakeSolidOrCutoutTga(4, 4, 255, 120, 20, 255, 0) },
        { "textures/liquids/protolava2.tga", MakeSolidOrCutoutTga(4, 4, 255, 80, 10, 255, 0) },
        { "textures/test/frame_holes.tga", MakeSolidOrCutoutTga(4, 4, 160, 160, 160, 255, 4) },
        { "textures/test/translucent_grate.tga", MakeSolidOrCutoutTga(4, 4, 140, 140, 140, 96, 0) },
        { "textures/test/glow.tga", MakeSolidOrCutoutTga(4, 4, 255, 200, 40, 255, 0) },
        { "textures/test/f1.tga", MakeSolidOrCutoutTga(4, 4, 255, 128, 32, 255, 0) },
        { "maps/reg.bsp", bspBytes },
    };
    EditorAssetSource assets;
    if (!Check(WriteStoredZip(pk3Path, files) && assets.AddZip(pk3Path.string()),
               "failed to mount regression pk3"))
    {
        return false;
    }

    Q3ShaderDatabase db;
    db.LoadFromAssets(assets);
    return VerifyRegressionShaderClassifications(db) &&
           VerifyRegressionLoadedScene(bspBytes, assets);
}

bool TestAdapterStaticRegistrarAndDotSlashCatalog()
{
    const EditorFormatAdapter *adapter = GetEditorFormatAdapter();
    if (!Check(adapter != nullptr && adapter->canLoadBspMemory != nullptr &&
                   adapter->loadBspSceneMemory != nullptr &&
                   adapter->canLoadPreviewModel != nullptr &&
                   adapter->loadPreviewModel != nullptr &&
                   adapter->shouldHideModelCatalogEntry != nullptr &&
                   adapter->postProcessModelCatalog != nullptr &&
                   adapter->resolveModelMaterialPrefix != nullptr &&
                   adapter->findDefaultPreviewModelIndex != nullptr &&
                   adapter->loadExternalImageRgba != nullptr,
               "Q3 static registrar failed to register format adapter before main()"))
    {
        return false;
    }

    std::vector<EditorAssetCatalogEntry> entries = {
        { "./models/players/Sarge/lower.md3", "pak0.pk3", "./models/players/Sarge" },
        { "./models/players/Sarge/upper.md3", "pak0.pk3", "./models/players/Sarge" },
    };
    adapter->postProcessModelCatalog(entries);
    const bool hasCleanChar =
        !entries.empty() && entries.front().path == "models/players/Sarge.character";
    return Check(hasCleanChar, "dot-slash character path corrupted in PostProcessQ3ModelCatalog");
}

void AppendTestQuad(std::vector<TestQ3DrawVert> &verts, std::vector<int32_t> &indices,
                    std::vector<TestQ3Surface> &surfs, int32_t shaderNum, int32_t surfaceType,
                    float x0, float x1, float y0, float y1, float z0, float z1)
{
    const int32_t firstVert = int32_t(verts.size());
    const int32_t firstIndex = int32_t(indices.size());
    TestQ3DrawVert q[4] = {};
    q[0].xyz[0] = x0; q[0].xyz[1] = y0; q[0].xyz[2] = z0;
    q[1].xyz[0] = x1; q[1].xyz[1] = y0; q[1].xyz[2] = z0;
    q[2].xyz[0] = x1; q[2].xyz[1] = y1; q[2].xyz[2] = z1;
    q[3].xyz[0] = x0; q[3].xyz[1] = y1; q[3].xyz[2] = z1;
    for (TestQ3DrawVert &v : q)
    {
        v.normal[2] = (z0 == z1) ? -1.0f : 0.0f;
        v.normal[1] = (z0 == z1) ? 0.0f : -1.0f;
        v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
        verts.push_back(v);
    }
    const int32_t quadIdx[6] = { 0, 1, 2, 0, 2, 3 };
    indices.insert(indices.end(), std::begin(quadIdx), std::end(quadIdx));

    TestQ3Surface s{};
    s.shaderNum = shaderNum;
    s.surfaceType = surfaceType;
    s.firstVert = firstVert;
    s.numVerts = 4;
    s.firstIndex = firstIndex;
    s.numIndexes = 6;
    s.lightmapNum = -1;
    s.lightmapVecs[2][1] = q[0].normal[1];
    s.lightmapVecs[2][2] = q[0].normal[2];
    surfs.push_back(s);
}

std::vector<uint8_t> BuildSurfaceLightBucketingAndDedupBsp()
{
    std::vector<uint8_t> bytes(8 + 17 * 8, 0);
    bytes[0] = 'I';
    bytes[1] = 'B';
    bytes[2] = 'S';
    bytes[3] = 'P';
    const int32_t version = 46;
    std::memcpy(bytes.data() + 4, &version, 4);

    auto writeLump = [&](int idx, const void *data, size_t len) {
        TestQ3Lump lump{ int32_t(bytes.size()), int32_t(len) };
        std::memcpy(bytes.data() + 8 + idx * 8, &lump, sizeof(lump));
        AppendStructBytes(bytes, data, len);
    };

    const std::string ents =
        "{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"0 0 24\"\n}\n"
        "{\n\"classname\" \"light_spot\"\n\"origin\" \"500 500 64\"\n\"light\" \"350\"\n}\n";
    writeLump(0, ents.data(), ents.size() + 1);

    TestQ3Shader shaders[6] = {};
    std::snprintf(shaders[0].shader, 64, "textures/test/overlay_flame");
    std::snprintf(shaders[1].shader, 64, "textures/test/ceil_lamp");
    std::snprintf(shaders[2].shader, 64, "textures/test/sconce_model");
    std::snprintf(shaders[3].shader, 64, "textures/test/sprite_orb");
    std::snprintf(shaders[4].shader, 64, "textures/test/wide_trim");
    std::snprintf(shaders[5].shader, 64, "textures/test/multi_part");
    writeLump(1, shaders, sizeof(shaders));

    TestQ3Plane plane{ { 1.0f, 0.0f, 0.0f }, 0.0f };
    writeLump(2, &plane, sizeof(plane));
    TestQ3Node node{};
    node.children[0] = -1;
    node.children[1] = -2;
    writeLump(3, &node, sizeof(node));
    TestQ3Leaf leafs[2] = {};
    leafs[0].cluster = 0;
    leafs[1].cluster = -1;
    writeLump(4, leafs, sizeof(leafs));

    std::vector<TestQ3DrawVert> verts;
    std::vector<int32_t> indices;
    std::vector<TestQ3Surface> surfs;

    // 0: Non-traced overlay flame at (40, 40, 16)
    AppendTestQuad(verts, indices, surfs, 0, 1, 32.0f, 48.0f, 40.0f, 40.0f, 0.0f, 32.0f);
    // 1: Traced ceiling brush face at (100, 100, 128)
    AppendTestQuad(verts, indices, surfs, 1, 1, 88.0f, 112.0f, 88.0f, 112.0f, 128.0f, 128.0f);
    // 2: Traced misc_model triangle soup sconce at (200, 200, 64) (push == 0)
    AppendTestQuad(verts, indices, surfs, 2, 3, 192.0f, 208.0f, 200.0f, 200.0f, 56.0f, 72.0f);
    // 3: Isolated fallback autoSprite orb at (300, 300, 48)
    AppendTestQuad(verts, indices, surfs, 3, 1, 292.0f, 308.0f, 300.0f, 300.0f, 40.0f, 56.0f);
    // 4: Fallback autoSprite orb right on top of light_spot (500, 500, 64) -> skipped
    AppendTestQuad(verts, indices, surfs, 3, 1, 492.0f, 508.0f, 500.0f, 500.0f, 56.0f, 72.0f);

    // 5: Wide subdivided q3map_surfacelight trim spanning x = 0..384 (boundingRadius > 96)
    {
        const int32_t firstVert = int32_t(verts.size());
        const int32_t firstIndex = int32_t(indices.size());
        for (int seg = 0; seg < 6; seg++)
        {
            const float x0 = float(seg) * 64.0f;
            const float x1 = x0 + 64.0f;
            const int32_t base = seg * 4;
            const float xs[4] = { x0, x1, x1, x0 };
            const float ys[4] = { 700.0f, 700.0f, 716.0f, 716.0f };
            for (int v = 0; v < 4; v++)
            {
                TestQ3DrawVert dv{};
                dv.xyz[0] = xs[v];
                dv.xyz[1] = ys[v];
                dv.xyz[2] = 96.0f;
                dv.normal[2] = -1.0f;
                dv.color[0] = dv.color[1] = dv.color[2] = dv.color[3] = 255;
                verts.push_back(dv);
            }
            const int32_t qIdx[6] = { base + 0, base + 1, base + 2, base + 0, base + 2, base + 3 };
            indices.insert(indices.end(), std::begin(qIdx), std::end(qIdx));
        }
        TestQ3Surface wide{};
        wide.shaderNum = 4;
        wide.surfaceType = 1;
        wide.firstVert = firstVert;
        wide.numVerts = 24;
        wide.firstIndex = firstIndex;
        wide.numIndexes = 36;
        wide.lightmapNum = -1;
        wide.lightmapVecs[2][2] = -1.0f;
        surfs.push_back(wide);
    }

    // 6 & 7: Two nearby multi-part faces of same shader within 24 units -> deduplicated to 1
    AppendTestQuad(verts, indices, surfs, 5, 1, 900.0f, 916.0f, 900.0f, 916.0f, 64.0f, 64.0f);
    AppendTestQuad(verts, indices, surfs, 5, 1, 916.0f, 932.0f, 900.0f, 916.0f, 64.0f, 64.0f);

    TestQ3Model model{};
    model.numSurfaces = int32_t(surfs.size());
    writeLump(7, &model, sizeof(model));
    writeLump(10, verts.data(), verts.size() * sizeof(TestQ3DrawVert));
    writeLump(11, indices.data(), indices.size() * sizeof(int32_t));
    writeLump(13, surfs.data(), surfs.size() * sizeof(TestQ3Surface));

    uint8_t vis[9] = {};
    const int32_t nClusters = 1;
    const int32_t rowBytes = 1;
    std::memcpy(vis + 0, &nClusters, 4);
    std::memcpy(vis + 4, &rowBytes, 4);
    vis[8] = 0x01u;
    writeLump(16, vis, sizeof(vis));
    return bytes;
}

bool VerifySurfaceLightsCountsAndMetadata(const EditorScene &scene)
{
    size_t lightSpotCount = 0;
    size_t flameCount = 0;
    size_t ceilCount = 0;
    size_t modelCount = 0;
    size_t orbCount = 0;
    size_t wideCount = 0;
    size_t multiCount = 0;
    bool ceilTintOk = false;
    bool modelPosOk = false;

    for (const EditorLight &l : scene.lights)
    {
        if (l.className == "light_spot")
        {
            lightSpotCount++;
        }
        else if (l.className == "textures/test/overlay_flame")
        {
            flameCount++;
        }
        else if (l.className == "textures/test/ceil_lamp")
        {
            ceilCount++;
            ceilTintOk = std::fabs(l.tint[0] - 0.5f) < 1e-3f &&
                         std::fabs(l.tint[1] - 1.0f) < 1e-3f &&
                         std::fabs(l.tint[2] - 0.25f) < 1e-3f;
        }
        else if (l.className == "textures/test/sconce_model")
        {
            modelCount++;
            modelPosOk = std::fabs(l.position[0] - 200.0f) < 1e-2f &&
                         std::fabs(l.position[1] - 200.0f) < 1e-2f &&
                         std::fabs(l.position[2] - 64.0f) < 1e-2f;
        }
        else if (l.className == "textures/test/sprite_orb")
        {
            orbCount++;
        }
        else if (l.className == "textures/test/wide_trim")
        {
            wideCount++;
        }
        else if (l.className == "textures/test/multi_part")
        {
            multiCount++;
        }
    }

    return Check(scene.lights == scene.bspLights, "scene.lights and scene.bspLights must match") &&
           Check(lightSpotCount == 1u, "light_spot entity missing") &&
           Check(flameCount == 1u && ceilCount == 1u && ceilTintOk,
                 "overlay flame or traced ceil_lamp light/tint mismatch") &&
           Check(modelCount == 1u && modelPosOk,
                 "misc_model triangle soup sconce light missing or offset") &&
           Check(orbCount == 1u, "fallback autoSprite dedup near entity light failed") &&
           Check(wideCount == 3u, "wide q3map_surfacelight trim must split into 3 bucket lights") &&
           Check(multiCount == 1u, "nearby multi-part faces within 24u must deduplicate to 1");
}

bool TestQ3SurfaceLightsBucketingAndDedup(const std::filesystem::path &tempDir)
{
    const std::string shaderScript = R"(
textures/test/overlay_flame
{
    q3map_surfacelight 5500
    { map textures/test/f1.tga blendFunc GL_ONE GL_ONE }
}
textures/test/ceil_lamp
{
    q3map_surfacelight 2000
    q3map_lightRGB 0.5 1.0 0.25
    { map textures/test/f1.tga }
}
textures/test/sconce_model
{
    q3map_surfacelight 1800
    { map textures/test/f1.tga }
}
textures/test/sprite_orb
{
    deformVertexes autoSprite
    surfaceparm trans
    { map textures/test/f1.tga blendFunc GL_ONE GL_ONE }
}
textures/test/wide_trim
{
    q3map_surfacelight 1200
    { map textures/test/f1.tga }
}
textures/test/multi_part
{
    q3map_surfacelight 1200
    { map textures/test/f1.tga }
}
)";
    const std::filesystem::path pk3Path = tempDir / "surface_lights.pk3";
    const std::vector<StoredZipInput> files = {
        { "scripts/surf_lights.shader", StringToBytes(shaderScript) },
        { "textures/test/f1.tga", MakeSolidOrCutoutTga(4, 4, 255, 200, 128, 255, 0) },
        { "maps/surf_lights.bsp", BuildSurfaceLightBucketingAndDedupBsp() },
    };
    EditorAssetSource assets;
    std::vector<uint8_t> bspBytes;
    EditorScene scene;
    std::string err;
    if (!Check(WriteStoredZip(pk3Path, files) && assets.AddZip(pk3Path.string()),
               "surface lights pk3 mount") ||
        !Check(assets.ReadFile("maps/surf_lights.bsp", bspBytes), "read surf_lights.bsp") ||
        !Check(LoadQ3BspSceneMemory(bspBytes, "maps/surf_lights.bsp", scene, err,
                                    EditorSceneLoadOptions{ &assets, false }),
               err.c_str()))
    {
        return false;
    }

    return VerifySurfaceLightsCountsAndMetadata(scene);
}

int main()
{
    if (!TestAdapterStaticRegistrarAndDotSlashCatalog())
    {
        return 1;
    }
    RegisterQ3EditorFormatAdapter();
    const std::filesystem::path tempDir =
        std::filesystem::temp_directory_path() / "q3_editor_adapter_tests";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir, ec);

    const bool ok = TestShaderAndSkinParsing() && TestShaderEdgeCases() &&
                    TestCutoutOverlaysLightsAndMd3(tempDir) &&
                    TestRegressionShadersAndPerSurfaceOverlays(tempDir) &&
                    TestQ3SurfaceLightsBucketingAndDedup(tempDir) &&
                    TestSyntheticQ3BspAndCharacter(tempDir) &&
                    TestOverlayCloudSkyAndSessionPaths(tempDir) &&
                    TestRetailBaseQ3IfPresent();

    std::filesystem::remove_all(tempDir, ec);
    return ok ? 0 : 1;
}
