#include "EditorDtalPipeline.h"
#include "EditorLightSelection.h"
#include "EditorOverlayPipeline.h"
#include "EditorSceneRuntime.h"
#include "EditorSpatial.h"
#include "Q3EditorAdapter.h"
#include "Q3Shader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

struct StubCapturedState
{
    uint32_t nextMaterialHandle = 1;
    std::vector<QrMaterial> createdMaterials;
    std::vector<QrMaterial> destroyedMaterials;
    std::unordered_map<QrMaterial, uint8_t> materialMaxRmeEmission;
    std::vector<QrGeometryUploadInfo> geometryUploads;
    std::vector<QrRasterizedGeometryUploadInfo> rasterizedUploads;
    std::vector<QrDirectionalLightUploadInfo> dirLights;
    std::vector<QrSphericalLightUploadInfo> sphereLights;
    std::vector<QrSpotLightUploadInfo> spotLights;
    std::vector<QrTexturedAreaLightUploadInfo> areaLights;
    std::vector<QrClusterLightSource> clusterSources;
    uint32_t uploadedClusterCount = 0;
    uint32_t beginStaticCalls = 0;
    uint32_t submitStaticCalls = 0;
    bool hasDrawFrame = false;
    QrDrawFrameInfo lastDrawFrame = {};
    QrDrawFrameIlluminationParams lastIllum = {};
    QrDrawFrameVolumetricParams lastVolume = {};
    QrDrawFrameLevelFogParams lastFog = {};
    QrDrawFrameSkyParams lastSky = {};
    QrBool32 dlssAvailable = QR_FALSE;
    QrBool32 fsrAvailable = QR_TRUE;

    void Reset()
    {
        nextMaterialHandle = 1;
        createdMaterials.clear();
        destroyedMaterials.clear();
        materialMaxRmeEmission.clear();
        geometryUploads.clear();
        rasterizedUploads.clear();
        dirLights.clear();
        sphereLights.clear();
        spotLights.clear();
        areaLights.clear();
        clusterSources.clear();
        uploadedClusterCount = 0;
        beginStaticCalls = 0;
        submitStaticCalls = 0;
        hasDrawFrame = false;
        lastDrawFrame = {};
        lastIllum = {};
        lastVolume = {};
        lastFog = {};
        lastSky = {};
        dlssAvailable = QR_FALSE;
        fsrAvailable = QR_TRUE;
    }
};

StubCapturedState &Stub()
{
    static StubCapturedState state;
    return state;
}

bool Check(bool cond, const char *msg)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        return false;
    }
    return true;
}

bool Near(float a, float b, float eps = 1.0e-3f)
{
    return std::fabs(a - b) <= eps;
}

std::vector<uint8_t> MakeSolidRgba(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4u);
    for (size_t i = 0; i < px.size(); i += 4u)
    {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = a;
    }
    return px;
}

std::vector<uint8_t> MakeHalfGlowRgba(int w, int h)
{
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4u, 0u);
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            const size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4u;
            const bool lit = (x >= w / 4 && x < (w * 3) / 4 && y >= h / 4 && y < (h * 3) / 4);
            px[i + 0] = lit ? 255 : 0;
            px[i + 1] = lit ? 180 : 0;
            px[i + 2] = lit ? 80 : 0;
            px[i + 3] = 255;
        }
    }
    return px;
}

} // namespace

extern "C" {

QrResult QRCONV qrCreateMaterial(QrInstance, const QrMaterialCreateInfo *pCreateInfo,
                               QrMaterial *pResult)
{
    if (pResult == nullptr)
    {
        return QR_WRONG_ARGUMENT;
    }
    *pResult = Stub().nextMaterialHandle++;
    Stub().createdMaterials.push_back(*pResult);
    uint8_t maxEmiss = 0u;
    if (pCreateInfo != nullptr && pCreateInfo->textures.pDataRoughnessMetallicEmission != nullptr)
    {
        const size_t pixels = size_t(pCreateInfo->size.width) * size_t(pCreateInfo->size.height);
        const auto *rme = static_cast<const uint8_t *>(
            pCreateInfo->textures.pDataRoughnessMetallicEmission);
        for (size_t i = 0; i < pixels; i++)
        {
            maxEmiss = std::max(maxEmiss, rme[i * 4u + 2u]);
        }
    }
    Stub().materialMaxRmeEmission[*pResult] = maxEmiss;
    return QR_SUCCESS;
}

QrResult QRCONV qrDestroyMaterial(QrInstance, QrMaterial material)
{
    Stub().destroyedMaterials.push_back(material);
    return QR_SUCCESS;
}

QrResult QRCONV qrBeginStaticGeometries(QrInstance)
{
    Stub().beginStaticCalls++;
    return QR_SUCCESS;
}

QrResult QRCONV qrSubmitStaticGeometries(QrInstance)
{
    Stub().submitStaticCalls++;
    return QR_SUCCESS;
}

QrResult QRCONV qrUploadGeometry(QrInstance, const QrGeometryUploadInfo *pUploadInfo)
{
    if (pUploadInfo != nullptr)
    {
        Stub().geometryUploads.push_back(*pUploadInfo);
    }
    return QR_SUCCESS;
}

QrResult QRCONV qrUploadRasterizedGeometry(QrInstance,
                                           const QrRasterizedGeometryUploadInfo *pUploadInfo,
                                           const float *, const QrViewport *)
{
    if (pUploadInfo != nullptr)
    {
        Stub().rasterizedUploads.push_back(*pUploadInfo);
    }
    return QR_SUCCESS;
}

QrResult QRCONV qrUploadWorldLights(QrInstance, const QrWorldLightsUploadInfo *pUploadInfo)
{
    if (pUploadInfo != nullptr)
    {
        Stub().uploadedClusterCount = pUploadInfo->numClusters;
    }
    return QR_SUCCESS;
}

QrResult QRCONV qrUploadClusterLightSources(QrInstance,
                                            const QrClusterLightSourcesUploadInfo *pUploadInfo)
{
    if (pUploadInfo != nullptr && pUploadInfo->pLights != nullptr)
    {
        Stub().clusterSources.assign(pUploadInfo->pLights,
                                     pUploadInfo->pLights + pUploadInfo->numLights);
    }
    return QR_SUCCESS;
}

QrResult QRCONV qrUploadDirectionalLight(QrInstance, const QrDirectionalLightUploadInfo *pUploadInfo)
{
    if (pUploadInfo != nullptr)
    {
        Stub().dirLights.push_back(*pUploadInfo);
    }
    return QR_SUCCESS;
}

QrResult QRCONV qrUploadSphericalLight(QrInstance, const QrSphericalLightUploadInfo *pUploadInfo)
{
    if (pUploadInfo != nullptr)
    {
        Stub().sphereLights.push_back(*pUploadInfo);
    }
    return QR_SUCCESS;
}

QrResult QRCONV qrUploadSpotLight(QrInstance, const QrSpotLightUploadInfo *pUploadInfo)
{
    if (pUploadInfo != nullptr)
    {
        Stub().spotLights.push_back(*pUploadInfo);
    }
    return QR_SUCCESS;
}

QrResult QRCONV qrUploadTexturedAreaLights(QrInstance,
                                           const QrTexturedAreaLightUploadInfo *pUploadInfos,
                                           uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        Stub().areaLights.push_back(pUploadInfos[i]);
    }
    return QR_SUCCESS;
}

QrResult QRCONV qrDrawFrame(QrInstance, const QrDrawFrameInfo *pDrawInfo)
{
    if (pDrawInfo != nullptr)
    {
        Stub().hasDrawFrame = true;
        Stub().lastDrawFrame = *pDrawInfo;
        if (pDrawInfo->pIlluminationParams != nullptr)
        {
            Stub().lastIllum = *pDrawInfo->pIlluminationParams;
        }
        if (pDrawInfo->pVolumetricParams != nullptr)
        {
            Stub().lastVolume = *pDrawInfo->pVolumetricParams;
        }
        if (pDrawInfo->pLevelFogParams != nullptr)
        {
            Stub().lastFog = *pDrawInfo->pLevelFogParams;
        }
        if (pDrawInfo->pSkyParams != nullptr)
        {
            Stub().lastSky = *pDrawInfo->pSkyParams;
        }
    }
    return QR_SUCCESS;
}

QrBool32 QRCONV qrIsRenderUpscaleTechniqueAvailable(QrInstance, QrRenderUpscaleTechnique tech)
{
    if (tech == QR_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS)
    {
        return Stub().dlssAvailable;
    }
    if (tech == QR_RENDER_UPSCALE_TECHNIQUE_AMD_FSR3)
    {
        return Stub().fsrAvailable;
    }
    return QR_TRUE;
}

const char *QRCONV qrGetResultDescription(QrResult)
{
    return "OK";
}

} // extern "C"

namespace
{

const QrInstance kFakeInstance = reinterpret_cast<QrInstance>(uintptr_t(0x1234));

QrVertex MakeTestVertex(float x, float y, float z, float u, float v, uint32_t cluster)
{
    QrVertex vert{};
    vert.position[0] = x;
    vert.position[1] = y;
    vert.position[2] = z;
    vert.normal[2] = 1.0f;
    vert.texCoord[0] = u;
    vert.texCoord[1] = v;
    vert.packedColor = 0xFFFFFFFFu;
    vert.cluster = cluster;
    return vert;
}

void AttachMemoryVfs(EditorAssetSource &assets,
                     const std::unordered_map<std::string, std::vector<uint8_t>> &files)
{
    EditorAssetVfsCallbacks cb{};
    cb.exists = [&files](const std::string &path) {
        return files.find(NormalizeEditorAssetPath(path)) != files.end();
    };
    cb.readFile = [&files](const std::string &path, std::vector<uint8_t> &out) {
        const auto it = files.find(NormalizeEditorAssetPath(path));
        if (it == files.end())
        {
            out.clear();
            return false;
        }
        out = it->second;
        return true;
    };
    cb.listFiles = [&files](const std::string &prefix, const std::string &ext,
                            std::vector<EditorAssetCatalogEntry> &out) {
        const std::string pfx = NormalizeEditorAssetPath(prefix);
        for (const auto &kv : files)
        {
            if (kv.first.rfind(pfx, 0) == 0 &&
                kv.first.size() >= ext.size() &&
                kv.first.compare(kv.first.size() - ext.size(), ext.size(), ext) == 0)
            {
                out.push_back(EditorAssetCatalogEntry{ kv.first, "vfs-test", "" });
            }
        }
    };
    assets.SetVfsCallbacks(std::move(cb));
}

void BuildSyntheticRuntimeScene(EditorSceneRuntime &runtime)
{
    EditorTexture t0;
    t0.name = "textures/base/wall";
    t0.width = 4;
    t0.height = 4;
    t0.rgba = MakeSolidRgba(4, 4, 200, 180, 160, 255);

    EditorTexture t1;
    t1.name = "textures/base/floor";
    t1.width = 4;
    t1.height = 4;
    t1.rgba = MakeSolidRgba(4, 4, 120, 120, 120, 255);

    runtime.scene.textures.push_back(t0);
    runtime.scene.textures.push_back(t1);

    EditorSurface s0;
    s0.textureName = t0.name;
    s0.texture = 0;
    s0.vertices = { MakeTestVertex(0, 0, 0, 0, 0, 1), MakeTestVertex(40, 0, 0, 1, 0, 1),
                    MakeTestVertex(40, 40, 0, 1, 1, 1) };
    s0.indices = { 0, 1, 2 };

    EditorSurface s1;
    s1.textureName = t1.name;
    s1.texture = 1;
    s1.vertices = { MakeTestVertex(0, 0, 10, 0, 0, 1), MakeTestVertex(40, 0, 10, 1, 0, 1),
                    MakeTestVertex(0, 40, 10, 0, 1, 1) };
    s1.indices = { 0, 1, 2 };

    runtime.scene.surfaces = { s0, s1 };
    runtime.scene.hasBounds = true;
    runtime.scene.boundsMin[0] = 0.0f;
    runtime.scene.boundsMin[1] = 0.0f;
    runtime.scene.boundsMin[2] = 0.0f;
    runtime.scene.boundsMax[0] = 40.0f;
    runtime.scene.boundsMax[1] = 40.0f;
    runtime.scene.boundsMax[2] = 40.0f;

    runtime.scene.worldClusters.numLeafs = 2u;
    runtime.scene.worldClusters.mins = { { -100, -100, -100 }, { -100, -100, -100 } };
    runtime.scene.worldClusters.maxs = { { 100, 100, 100 }, { 100, 100, 100 } };
    runtime.scene.worldClusters.flags = { 0, 0 };
    runtime.scene.worldClusters.contents = { 0, 0 };
    runtime.scene.worldClusters.leafCluster = { 0, 1 };
    runtime.materials.Rebuild(runtime.scene);
}

bool TestVfsFallbackAssetResolution(const std::filesystem::path &tempDir)
{
    std::unordered_map<std::string, std::vector<uint8_t>> vfsFiles;
    const char *shaderBody = "textures/base/wall\n{\n  map textures/base/wall.tga\n}\n";
    vfsFiles["scripts/test.shader"].assign(shaderBody, shaderBody + std::strlen(shaderBody));
    vfsFiles["textures/base/wall.tga"] = MakeSolidRgba(4, 4, 255, 255, 255, 255);

    EditorAssetSource assets;
    assets.AddDirectory(tempDir.string());
    AttachMemoryVfs(assets, vfsFiles);

    std::vector<uint8_t> readOut;
    std::vector<EditorAssetCatalogEntry> shaders;
    assets.ListFilesWithPrefixAndExtension("scripts", ".shader", shaders);

    return Check(assets.Exists("scripts/test.shader"), "VFS exists check failed") &&
           Check(assets.ReadFile("scripts/test.shader", readOut) && readOut.size() > 10u,
                 "VFS readFile check failed") &&
           Check(shaders.size() == 1u && shaders[0].path == "scripts/test.shader",
                 "VFS listFiles failed");
}

bool VerifyAuthoredRuntimeAndFrame()
{
    const bool uploadsOk =
        Check(Stub().dirLights.size() == 1u, "Expected sun directional light upload") &&
        Check(Stub().sphereLights.size() == 1u, "Expected level custom sphere light upload") &&
        Check(Stub().clusterSources.size() == 1u, "Expected cluster light source upload") &&
        Check(Stub().hasDrawFrame, "Expected EditorDrawFrame to invoke qrDrawFrame") &&
        Check(Near(Stub().lastDrawFrame.cameraNear, 0.25f) &&
                  Near(Stub().lastDrawFrame.cameraFar, 100.0f) &&
                  Near(Stub().lastDrawFrame.rayLength, 80.0f),
              "Expected Quake-to-meters camera distances in EditorDrawFrame") &&
        Check(Stub().lastIllum.restirEnabled == 1u && Stub().lastIllum.restirCandidates == 16u,
              "Expected config ReSTIR params") &&
        Check(Stub().lastVolume.enable == 1 && Near(Stub().lastFog.density, (0.05f / 64.0f) / 0.025f),
              "Expected volumetric + level fog scaling");
    return uploadsOk;
}

bool TestAuthoredConfigMaterialsLightsAndFrame()
{
    Stub().Reset();
    EditorSceneRuntime runtime;
    BuildSyntheticRuntimeScene(runtime);

    EditorMaterialsDocument baseDoc;
    baseDoc.LoadFromText(
        "qray_materials:\n"
        "  - name: textures/base/floor\n"
        "    roughness: 0.45\n"
        "    metallic: 0.80\n");
    EditorMaterialsDocument activeDoc;
    activeDoc.LoadFromText(
        "qray_materials:\n"
        "  - name: textures/base/wall\n"
        "    roughness: 0.20\n"
        "    emissive: 1.50\n");

    size_t loaded = 0;
    size_t inherited = 0;
    runtime.ApplyMaterialsDocuments(activeDoc, &baseDoc, &loaded, &inherited);
    if (!Check(loaded == 1u && inherited == 1u, "Expected 1 active + 1 inherited material"))
    {
        return false;
    }

    const int cfgCount = runtime.ApplyConfigText(
        "rt_truelight \"1\"\n"
        "rt_restir \"1\"\n"
        "rt_restir_candidates \"16\"\n"
        "rt_gi_level \"1.0\"\n"
        "rt_renderscale \"100\"\n"
        "rt_upscale_fsr31 \"2\"\n");
    if (!Check(cfgCount == 6, "Expected 6 config keys applied"))
    {
        return false;
    }

    EditorLevelDocument levelDoc;
    levelDoc.LoadFromText(
        "q3tourney1:\n"
        "  sky:\n"
        "    rt_physical_sun: 1\n"
        "    rt_sky_sun: 2.0\n"
        "  fog:\n"
        "    color: 3366cc\n"
        "    density: 0.05\n"
        "    enabled: true\n"
        "  lights:\n"
        "    - origin: 10 10 10\n"
        "      class: custom\n"
        "      radius: 0.5\n"
        "      intensity: 3.0\n"
        "      color: ffcc99\n");
    const EditorLevelApplyResult lvl = runtime.ApplyLevelDocument(levelDoc, "q3tourney1");
    if (!Check(lvl.hasSky && lvl.hasFog && lvl.hasLights && runtime.customLights.size() == 1u,
               "Level document sky/fog/lights apply failed"))
    {
        return false;
    }

    EditorAssetSource emptyAssets;
    runtime.CreateTextureMaterials(kFakeInstance, emptyAssets);
    runtime.UploadWorldClusters(kFakeInstance);
    runtime.UploadSun(kFakeInstance);
    runtime.UploadLights(kFakeInstance, 0.5, true);
    runtime.UploadClusterLightSources(kFakeInstance);

    const float eye[3] = { 0.0f, 0.0f, 0.0f };
    EditorFrameParams fp;
    QrDrawFrameInfo draw{};
    draw.cameraNear = 10.0f;
    draw.cameraFar = 4000.0f;
    draw.rayLength = 3200.0f;
    draw.rayCullMaskWorld = QR_DRAW_FRAME_RAY_CULL_WORLD_0_BIT;
    runtime.PopulateDrawFrame(kFakeInstance, eye, 1280u, 720u, fp, draw);
    const bool drawOk = Check(EditorDrawFrame(kFakeInstance, &draw) == QR_SUCCESS,
                              "EditorDrawFrame failed");
    const bool ok = drawOk && VerifyAuthoredRuntimeAndFrame();
    runtime.DestroyGpuResources(kFakeInstance);
    return ok;
}

EditorOverlaySurface BuildThreeStageOverlaySurface(const std::string &shaderName)
{
    EditorOverlaySurface ov;
    ov.shaderName = shaderName;
    ov.vertices = { MakeTestVertex(0, 0, 0, 0, 0, 1), MakeTestVertex(16, 0, 0, 1, 0, 1),
                    MakeTestVertex(16, 16, 0, 1, 1, 1) };
    ov.indices = { 0, 1, 2 };
    ov.firstOverlayStage = 0u;
    ov.endOverlayStage = 3u;

    EditorOverlayStage st0;
    st0.map = "textures/base/stage0.tga";
    EditorOverlayStage st1;
    st1.map = "textures/base/stage1.tga";
    EditorOverlayStage st2;
    st2.map = "textures/base/stage2.tga";
    st2.additive = true;
    ov.stages = { st0, st1, st2 };
    return ov;
}

bool TestWorldOverlaysAndDisabledStages()
{
    Stub().Reset();
    EditorSceneRuntime runtime;
    runtime.scene.overlaySurfaces.push_back(BuildThreeStageOverlaySurface("textures/fx/active"));
    runtime.scene.overlaySurfaces.push_back(BuildThreeStageOverlaySurface("textures/fx/off"));

    EditorTexture tActive;
    tActive.name = "textures/fx/active";
    tActive.width = 4;
    tActive.height = 4;
    tActive.rgba = MakeSolidRgba(4, 4, 255, 255, 255, 255);
    EditorTexture tOff = tActive;
    tOff.name = "textures/fx/off";
    runtime.scene.textures = { tActive, tOff };

    EditorSurface surfActive;
    surfActive.textureName = "textures/fx/active";
    surfActive.isOverlayOnly = true;
    EditorSurface surfOff;
    surfOff.textureName = "textures/fx/off";
    surfOff.isOverlayOnly = true;
    runtime.scene.surfaces = { surfActive, surfOff };
    runtime.materials.Rebuild(runtime.scene);

    EditorMaterialsDocument matDoc;
    matDoc.LoadFromText(
        "qray_materials:\n"
        "  - name: textures/fx/active\n"
        "    disabled_stages: 2\n"
        "  - name: textures/fx/off\n"
        "    enabled: false\n");
    runtime.ApplyMaterialsDocuments(matDoc, nullptr);

    std::unordered_map<std::string, std::vector<uint8_t>> vfsFiles;
    const std::vector<uint8_t> whiteTex = MakeSolidRgba(4, 4, 255, 255, 255, 255);
    vfsFiles["textures/base/stage0.tga"] = whiteTex;
    vfsFiles["textures/base/stage1.tga"] = whiteTex;
    vfsFiles["textures/base/stage2.tga"] = whiteTex;
    EditorAssetSource assets;
    AttachMemoryVfs(assets, vfsFiles);
    runtime.scene.textures.push_back(EditorTexture{ "textures/base/stage0", 4, 4, whiteTex });
    runtime.scene.textures.push_back(EditorTexture{ "textures/base/stage1", 4, 4, whiteTex });
    runtime.scene.textures.push_back(EditorTexture{ "textures/base/stage2", 4, 4, whiteTex });

    EditorCameraBasis cam{};
    cam.forward[0] = 1.0f;
    cam.right[1] = -1.0f;
    cam.up[2] = 1.0f;
    cam.viewportWidth = 800.0f;
    cam.viewportHeight = 600.0f;
    runtime.UploadWorldOverlays(kFakeInstance, assets, cam, 1.0f);

    const bool ok = Check(Stub().rasterizedUploads.size() == 2u,
                          "Expected 2 overlay stages uploaded (stage 1 masked, off surface skipped)");
    runtime.DestroyGpuResources(kFakeInstance);
    return ok;
}

bool TestDynamicMeshDtalAndUpload()
{
    Stub().Reset();
    EditorSceneRuntime runtime;
    EditorTexture glowTex;
    glowTex.name = "models/glow_mat";
    glowTex.width = 8;
    glowTex.height = 8;
    glowTex.rgba = MakeHalfGlowRgba(8, 8);
    glowTex.fullbright.assign(64u, 255u);
    runtime.scene.textures.push_back(glowTex);

    EditorAssetSource emptyAssets;
    runtime.CreateTextureMaterials(kFakeInstance, emptyAssets);
    const QrMaterial glowMat = runtime.textureMaterials[0];
    const EditorMaterialEmission emission = runtime.MaterialEmissionOf(glowMat);

    EditorRenderSettings settings;
    settings.truelight = 1.0f;
    settings.modelLights = true;
    EditorMaterialParams params;
    params.luma = true;
    params.lightBrightness = 2.0f;
    params.lightMinArea = 0.0f;

    const std::vector<QrVertex> verts = {
        MakeTestVertex(0, 0, 0, 0, 0, 1),
        MakeTestVertex(32, 0, 0, 1, 0, 1),
        MakeTestVertex(32, 32, 0, 1, 1, 1),
        MakeTestVertex(0, 32, 0, 0, 1, 1),
    };
    const std::vector<uint32_t> indices = { 0, 1, 2, 0, 2, 3 };

    int32_t budget = 1;
    uint64_t nextId = 0x5000000000000000ull;
    std::vector<EditorEmissiveLight> dynLights;
    EditorDynamicMeshDtalRequest req{
        &settings, &params, glowMat, emission, &verts, &indices, 1u, &budget, &nextId,
    };
    BuildEditorDynamicMeshAreaLights(req, dynLights);
    if (!Check(dynLights.size() == 1u && budget == 0 && nextId == 0x5000000000000001ull,
               "Dynamic DTAL budget cap check failed"))
    {
        return false;
    }

    budget = 8;
    dynLights.clear();
    BuildEditorDynamicMeshAreaLights(req, dynLights);
    if (!Check(dynLights.size() == 2u, "Expected 2 dynamic DTAL lights when budget permits"))
    {
        return false;
    }

    runtime.UploadEmissiveLights(kFakeInstance, 0.25f, &dynLights);
    const bool ok = Check(Stub().areaLights.size() == 2u,
                          "Expected UploadEmissiveLights to submit extra dynamic DTAL lights");
    runtime.DestroyGpuResources(kFakeInstance);
    return ok;
}

bool LoadAndPrepareSteamRuntime(const std::vector<uint8_t> &bspBytes,
                                const std::filesystem::path &baseDir,
                                const EditorAssetSource &assets,
                                bool includeInlineSubmodels,
                                EditorSceneRuntime &runtime)
{
    EditorSceneLoadOptions opts;
    opts.assets = &assets;
    opts.includeInlineSubmodels = includeInlineSubmodels;
    std::string err;
    if (!Check(runtime.LoadBspSceneMemory(bspBytes, "maps/q3tourney1.bsp", nullptr, opts, err),
               ("LoadBspSceneMemory failed: " + err).c_str()))
    {
        return false;
    }
    runtime.materials.Rebuild(runtime.scene);
    runtime.ApplySceneRenderDefaults();

    EditorMaterialsDocument matDoc;
    if (matDoc.Load((baseDir / "qray.materials.yaml").string(), err))
    {
        runtime.ApplyMaterialsDocuments(matDoc, nullptr);
    }
    std::ifstream cfgFile(baseDir / "config.cfg", std::ios::binary);
    if (cfgFile)
    {
        const std::string cfg((std::istreambuf_iterator<char>(cfgFile)),
                              std::istreambuf_iterator<char>());
        runtime.ApplyConfigText(cfg);
    }
    EditorLevelDocument lvlDoc;
    if (lvlDoc.Load((baseDir / "qray.lights.yaml").string(), err))
    {
        runtime.ApplyLevelDocument(lvlDoc, "q3tourney1");
    }

    runtime.CreateTextureMaterials(kFakeInstance, assets);
    const bool uploaded = runtime.UploadStaticGeometry(kFakeInstance, assets, 1u);
    const bool clusters = runtime.UploadWorldClusters(kFakeInstance);
    return uploaded && clusters;
}

bool TestSteamQ3Tourney1Parity()
{
    const std::filesystem::path baseDir(
        "/home/user/.local/share/Steam/steamapps/common/Quake 3 Arena/baseq3");
    std::error_code ec;
    if (!std::filesystem::is_regular_file(baseDir / "pak0.pk3", ec))
    {
        std::printf("SKIP: Steam pak0.pk3 not found at %s\n", baseDir.string().c_str());
        return true;
    }

    RegisterQ3EditorFormatAdapter();
    EditorAssetSource assets;
    MountEditorGameDirectory(assets, baseDir.string());

    std::vector<uint8_t> bspBytes;
    if (!Check(assets.ReadFile("maps/q3tourney1.bsp", bspBytes),
               "Failed to read maps/q3tourney1.bsp from Steam baseq3"))
    {
        return false;
    }

    Stub().Reset();
    EditorSceneRuntime gameRuntime;
    if (!Check(LoadAndPrepareSteamRuntime(bspBytes, baseDir, assets, false, gameRuntime),
               "Failed to prepare gameRuntime on q3tourney1"))
    {
        return false;
    }
    gameRuntime.UploadLights(kFakeInstance, 0.5, true);
    gameRuntime.UploadEmissiveLights(kFakeInstance, 0.5f);
    gameRuntime.UploadClusterLightSources(kFakeInstance);
    const size_t gameAreaLights = gameRuntime.emissiveLights.size();
    const size_t gameClusterSources = Stub().clusterSources.size();

    Stub().Reset();
    EditorSceneRuntime editorRuntime;
    if (!Check(LoadAndPrepareSteamRuntime(bspBytes, baseDir, assets, true, editorRuntime),
               "Failed to prepare editorRuntime on q3tourney1"))
    {
        gameRuntime.DestroyGpuResources(kFakeInstance);
        return false;
    }

    const bool ok =
        Check(gameRuntime.scene.skyboxName == "env/xnight2" && gameRuntime.hasSkybox,
              "Expected q3tourney1 skybox env/xnight2 6/6 faces") &&
        Check(gameRuntime.scene.worldClusters.mins.size() == 732u &&
                  editorRuntime.scene.worldClusters.mins.size() == 732u,
              "Expected 732 clusters on q3tourney1") &&
        Check(gameAreaLights > 0u && gameAreaLights == editorRuntime.emissiveLights.size(),
              "Expected matching non-zero DTAL area light count") &&
        Check(gameClusterSources > 0u, "Expected cluster light sources uploaded even under restir") &&
        Check(gameRuntime.scene.surfaces.size() <= editorRuntime.scene.surfaces.size(),
              "Game runtime without inline submodels must not exceed editor surface count");

    gameRuntime.DestroyGpuResources(kFakeInstance);
    editorRuntime.DestroyGpuResources(kFakeInstance);
    return ok;
}

bool TestZeroPowerClusterSourcesFiltered()
{
    Stub().Reset();
    EditorSceneRuntime runtime;
    BuildSyntheticRuntimeScene(runtime);

    EditorLight zeroPoint;
    zeroPoint.enabled = true;
    zeroPoint.color[0] = 0.0f;
    zeroPoint.color[1] = 0.0f;
    zeroPoint.color[2] = 0.0f;
    zeroPoint.cluster = 0u;

    EditorLight activePoint;
    activePoint.enabled = true;
    activePoint.color[0] = 10.0f;
    activePoint.color[1] = 10.0f;
    activePoint.color[2] = 10.0f;
    activePoint.cluster = 0u;

    runtime.scene.lights = { zeroPoint, activePoint };

    EditorEmissiveLight zeroEmissive;
    zeroEmissive.info.uniqueID = kEditorFirstEmissiveLightUniqueID;
    zeroEmissive.power = 0.0f;
    zeroEmissive.cluster = 0u;

    EditorEmissiveLight activeEmissive;
    activeEmissive.info.uniqueID = kEditorFirstEmissiveLightUniqueID + 1u;
    activeEmissive.power = 5.0f;
    activeEmissive.cluster = 0u;

    runtime.emissiveLights = { zeroEmissive, activeEmissive };

    std::vector<QrClusterLightSource> extraSources(4);
    extraSources[0].uniqueID = 90001u;
    extraSources[0].power = 0.0f;
    extraSources[1].uniqueID = 90002u;
    extraSources[1].power = -5.0f;
    extraSources[2].uniqueID = 90003u;
    extraSources[2].power = std::numeric_limits<float>::quiet_NaN();
    extraSources[3].uniqueID = 90004u;
    extraSources[3].power = 12.5f;

    EditorClusterLightUploadExtras extras{};
    extras.extraQuakeUnitSources = &extraSources;

    if (!Check(runtime.UploadClusterLightSources(kFakeInstance, &extras) == QR_SUCCESS,
               "UploadClusterLightSources returned error"))
    {
        return false;
    }

    const auto &uploaded = Stub().clusterSources;
    const auto hasId = [&uploaded](uint64_t id) {
        return std::any_of(uploaded.begin(), uploaded.end(),
                           [id](const QrClusterLightSource &src) { return src.uniqueID == id; });
    };
    const bool hasOnlyExpected =
        Check(uploaded.size() == 3u, "Expected only 3 positive-power cluster sources") &&
        Check(hasId(EditorLightUniqueID(1)),
              "Expected active point light cluster source") &&
        Check(hasId(90004u),
              "Expected positive extra cluster source") &&
        Check(hasId(kEditorFirstEmissiveLightUniqueID + 1u),
              "Expected active emissive cluster source");

    for (const QrClusterLightSource &src : uploaded)
    {
        if (!Check(std::isfinite(src.power) && src.power > 0.0f,
                   "Non-positive or non-finite cluster source power slipped through"))
        {
            return false;
        }
    }

    return hasOnlyExpected;
}

bool TestAnimatedWaveAreaLightsStaySteady()
{
    EditorSceneRuntime runtime;
    BuildSyntheticRuntimeScene(runtime);

    EditorOverlayStage waveStage;
    waveStage.map = "textures/base_trim/spiderbit_fx.tga";
    waveStage.isLightmap = false;
    waveStage.rgbWave = EditorWaveType::Triangle;
    waveStage.rgbWaveBase = 0.1f;
    waveStage.rgbWaveAmplitude = 0.5f;
    waveStage.rgbWavePhase = 0.0f;
    waveStage.rgbWaveFrequency = 7.0f;
    runtime.scene.textures[0].baseStageIndex = 0u;
    runtime.scene.textures[0].shaderStages = { EditorOverlayStage{}, waveStage };

    EditorEmissiveLight areaLight;
    areaLight.surfaceIndex = 0u;
    areaLight.info.uniqueID = kEditorFirstEmissiveLightUniqueID;
    areaLight.info.color = { 4.0f, 3.0f, 2.0f };
    areaLight.info.area = 100.0f;
    areaLight.power = 10.0f;
    runtime.emissiveLights = { areaLight };

    for (const float timeSeconds : { 0.0f, 0.07f })
    {
        Stub().Reset();
        if (!Check(runtime.UploadEmissiveLights(kFakeInstance, timeSeconds) == QR_SUCCESS,
                   "UploadEmissiveLights returned error"))
        {
            return false;
        }
        if (!Check(Stub().areaLights.size() == 1u,
                   "Expected 1 uploaded area light for wave-animated surface"))
        {
            return false;
        }
        const QrFloat3D &color = Stub().areaLights[0].color;
        if (!Check(Near(color.data[0], 4.0f) && Near(color.data[1], 3.0f) &&
                       Near(color.data[2], 2.0f),
                   "Expected static DTAL area light color unaffected by rgbGen wave"))
        {
            return false;
        }
    }

    return true;
}

bool TestClusterSourcePowerAndRadiusScale()
{
    Stub().Reset();
    EditorSceneRuntime runtime;
    BuildSyntheticRuntimeScene(runtime);

    EditorLight point;
    point.enabled = true;
    point.color[0] = 160.0f;
    point.color[1] = 160.0f;
    point.color[2] = 160.0f;
    point.radius = 16.0f;
    point.cluster = 0u;
    runtime.scene.lights = { point };

    EditorEmissiveLight area;
    area.info.uniqueID = kEditorFirstEmissiveLightUniqueID;
    area.info.area = 100.0f;
    area.power = 160.0f;
    area.cluster = 0u;
    runtime.emissiveLights = { area };

    if (!Check(runtime.UploadClusterLightSources(kFakeInstance) == QR_SUCCESS,
               "UploadClusterLightSources returned error"))
    {
        return false;
    }

    const auto &uploaded = Stub().clusterSources;
    if (!Check(uploaded.size() == 2u, "Expected 2 uploaded cluster sources"))
    {
        return false;
    }

    constexpr float kTestPi = 3.14159265358979323846f;
    const float expectedAreaRadiusMeters = std::sqrt(100.0f / kTestPi) / 40.0f;
    return Check(Near(uploaded[0].power, 160.0f) && Near(uploaded[0].radius, 16.0f / 40.0f),
                 "Point light cluster power/radius must match (1600*flux) and meter radius") &&
           Check(Near(uploaded[1].power, 160.0f) &&
                     Near(uploaded[1].radius, expectedAreaRadiusMeters),
                 "Area light cluster power/radius must match point light scale");
}

bool TestSingleStageSurfaceLightClearedToNoneDoesNotGlowOrEmit(const std::filesystem::path &tempDir)
{
    Stub().Reset();
    static constexpr const char *kShaderName = "models/mapobjects/spotlamp/spotlamp_l";
    const std::string shaderScript = R"(
models/mapobjects/spotlamp/spotlamp_l
{
    cull disable
    q3map_surfacelight 200
    {
        map models/mapobjects/spotlamp/spotlamp_l.tga
        rgbGen identity
    }
}
)";
    EditorAssetSource emptyAssets;
    Q3ShaderDatabase db;
    db.ParseScript(shaderScript.data(), shaderScript.size(), true, &emptyAssets);
    const Q3ShaderDef *def = db.Find(kShaderName);
    if (!Check(def != nullptr, "spotlamp_l shader definition missing"))
    {
        return false;
    }

    EditorSceneRuntime runtime;
    runtime.scene.usesExternalLightingModel = true;
    EditorTexture spotTex;
    spotTex.name = kShaderName;
    spotTex.width = 8;
    spotTex.height = 8;
    spotTex.rgba = MakeSolidRgba(8, 8, 255, 255, 255, 255);
    ApplyQ3ShaderToEditorTexture(*def, &emptyAssets, false, spotTex);
    runtime.scene.textures.push_back(std::move(spotTex));

    EditorSurface surf;
    surf.textureName = kShaderName;
    surf.texture = 0;
    surf.vertices = {
        MakeTestVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1u),
        MakeTestVertex(32.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1u),
        MakeTestVertex(32.0f, 32.0f, 0.0f, 1.0f, 1.0f, 1u),
        MakeTestVertex(0.0f, 32.0f, 0.0f, 0.0f, 1.0f, 1u),
    };
    surf.indices = { 0u, 1u, 2u, 0u, 2u, 3u };
    EditorSurfaceFace face;
    face.normal[2] = 1.0f;
    face.cluster = 1u;
    face.positions = {
        { 0.0f, 0.0f, 0.0f }, { 32.0f, 0.0f, 0.0f }, { 32.0f, 32.0f, 0.0f }, { 0.0f, 32.0f, 0.0f }
    };
    face.texCoords = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
    surf.faces.push_back(std::move(face));
    runtime.scene.surfaces.push_back(std::move(surf));
    runtime.materials.Rebuild(runtime.scene);

    runtime.settings.truelight = 1.0f;
    runtime.settings.emissiveNee = true;

    if (!Check(runtime.CreateTextureMaterials(kFakeInstance, emptyAssets),
               "CreateTextureMaterials failed for spotlamp_l"))
    {
        return false;
    }

    const int32_t matIdx = runtime.materials.IndexOf(kShaderName);
    if (!Check(matIdx >= 0, "spotlamp_l material index missing"))
    {
        runtime.DestroyGpuResources(kFakeInstance);
        return false;
    }
    const EditorMaterialParams defaultParams = runtime.DefaultMaterialParamsFor(kShaderName);
    runtime.materials.At(size_t(matIdx)) = defaultParams;

    const QrMaterial fallbackMat = 9999u;
    runtime.BuildEmissiveLights(kFakeInstance, emptyAssets, fallbackMat);
    const bool defaultOk =
        Check(defaultParams.emissiveTexture == kShaderName && !defaultParams.luma,
              "spotlamp_l default params must expose self emissiveTexture with luma=false") &&
        Check(EditorSurfaceHasAutoEmissiveSource(runtime.scene, runtime.scene.surfaces[0],
                                                 defaultParams),
              "spotlamp_l must default to active auto-emissive source") &&
        Check(!runtime.emissiveLights.empty(),
              "spotlamp_l must emit area light by default when emissiveNee is enabled");
    if (!defaultOk)
    {
        runtime.DestroyGpuResources(kFakeInstance);
        return false;
    }

    const std::filesystem::path yamlPath = tempDir / "spotlamp.materials.yaml";
    const std::string clearedYaml =
        "qray_materials:\n"
        "  - name: models/mapobjects/spotlamp/spotlamp_l\n"
        "    texture_emissive: NONE\n";
    std::string ioErr;
    EditorMaterialsDocument doc;
    if (!Check(WriteEditorFile(yamlPath.string(), clearedYaml, ioErr) &&
                   doc.Load(yamlPath.string(), ioErr),
               ioErr.c_str()))
    {
        runtime.DestroyGpuResources(kFakeInstance);
        return false;
    }

    runtime.ApplyMaterialsDocuments(doc, nullptr);
    const EditorMaterialParams &clearedParams = runtime.materials.At(size_t(matIdx));
    Stub().geometryUploads.clear();
    const bool uploadOk = runtime.UploadStaticGeometry(kFakeInstance, emptyAssets, fallbackMat);
    const QrMaterial uploadedMat =
        !Stub().geometryUploads.empty() ? Stub().geometryUploads.back().geomMaterial.layerMaterials[0]
                                        : QR_NO_MATERIAL;
    const EditorMaterialEmission uploadedEmission = runtime.MaterialEmissionOf(uploadedMat);
    const auto rmeIt = Stub().materialMaxRmeEmission.find(uploadedMat);
    const uint8_t uploadedMaxRme = (rmeIt != Stub().materialMaxRmeEmission.end()) ? rmeIt->second : 255u;

    runtime.BuildEmissiveLights(kFakeInstance, emptyAssets, fallbackMat);

    EditorMaterialEntry saveEntry;
    saveEntry.name = kShaderName;
    saveEntry.params = clearedParams;
    saveEntry.clearDefaultEmissive =
        !defaultParams.emissiveTexture.empty() && clearedParams.emissiveTexture.empty();
    const std::string mergedYaml = doc.WriteMerged({ saveEntry });

    const bool clearedOk =
        Check(clearedParams.emissiveTexture.empty() && !clearedParams.luma,
              "Reloaded texture_emissive: NONE must clear emissiveTexture") &&
        Check(!EditorSurfaceHasAutoEmissiveSource(runtime.scene, runtime.scene.surfaces[0],
                                                  clearedParams),
              "Cleared texture_emissive: NONE must disable auto-emissive source") &&
        Check(uploadOk && uploadedMat != QR_NO_MATERIAL &&
                  Near(uploadedEmission.mean, 0.0f) && Near(uploadedEmission.meanBase, 0.0f) &&
                  !uploadedEmission.hasGlow && uploadedMaxRme == 0u,
              "Cleared texture_emissive: NONE must bake zero RME emission") &&
        Check(runtime.emissiveLights.empty(),
              "Cleared texture_emissive: NONE with is_light=false must produce 0 area lights") &&
        Check(saveEntry.clearDefaultEmissive &&
                  mergedYaml.find("texture_emissive: NONE") != std::string::npos,
              "Saving cleared spotlamp_l must persist texture_emissive: NONE");

    runtime.DestroyGpuResources(kFakeInstance);
    return clearedOk;
}

bool TestBaseStageDisabledCutoutBackingAndMirrorEnvPromotion()
{
    std::unordered_map<std::string, std::vector<uint8_t>> vfsFiles;
    const std::vector<uint8_t> whiteTex = MakeSolidRgba(4, 4, 255, 255, 255, 255);
    vfsFiles["textures/base/stage0.tga"] = whiteTex;
    vfsFiles["textures/base/stage1.tga"] = whiteTex;
    vfsFiles["textures/base/stage2.tga"] = whiteTex;
    EditorAssetSource assets;
    AttachMemoryVfs(assets, vfsFiles);
    const QrMaterial fallbackMat = QR_NO_MATERIAL;

    auto makeTriSurface = [](const std::string &name, bool overlayOnly, bool cutoutUnderlay) {
        EditorSurface s;
        s.textureName = name;
        s.isOverlayOnly = overlayOnly;
        s.isCutoutUnderlay = cutoutUnderlay;
        s.vertices = { MakeTestVertex(0, 0, 0, 0, 0, 1), MakeTestVertex(16, 0, 0, 1, 0, 1),
                       MakeTestVertex(16, 16, 0, 1, 1, 1) };
        s.indices = { 0, 1, 2 };
        return s;
    };

    // Case (a): Disabling the traced base stage skips static upload but keeps enabled non-base overlays.
    {
        Stub().Reset();
        EditorSceneRuntime runtime;
        EditorOverlayStage st0;
        st0.map = "textures/base/stage0.tga";
        EditorOverlayStage st1;
        st1.map = "textures/base/stage1.tga";
        EditorOverlayStage st2;
        st2.map = "textures/base/stage2.tga";
        st2.additive = true;

        EditorTexture tex;
        tex.name = "textures/test/traced_off";
        tex.width = 4;
        tex.height = 4;
        tex.rgba = whiteTex;
        tex.baseStageIndex = 1u;
        tex.shaderStages = { st0, st1, st2 };
        runtime.scene.textures = { tex, EditorTexture{ "textures/base/stage0", 4, 4, whiteTex },
                                   EditorTexture{ "textures/base/stage1", 4, 4, whiteTex },
                                   EditorTexture{ "textures/base/stage2", 4, 4, whiteTex } };
        runtime.scene.surfaces = { makeTriSurface("textures/test/traced_off", false, false) };
        EditorOverlaySurface ov = BuildThreeStageOverlaySurface("textures/test/traced_off");
        ov.tracedBase = true;
        ov.baseStageIndex = 1u;
        runtime.scene.overlaySurfaces = { ov };
        runtime.materials.Rebuild(runtime.scene);

        EditorMaterialsDocument doc;
        doc.LoadFromText("qray_materials:\n  - name: textures/test/traced_off\n    disabled_stages: 2\n");
        runtime.ApplyMaterialsDocuments(doc, nullptr);
        runtime.UploadStaticGeometry(kFakeInstance, assets, fallbackMat);

        EditorCameraBasis cam{};
        cam.forward[0] = 1.0f;
        cam.right[1] = -1.0f;
        cam.up[2] = 1.0f;
        cam.viewportWidth = 800.0f;
        cam.viewportHeight = 600.0f;
        runtime.UploadWorldOverlays(kFakeInstance, assets, cam, 1.0f);

        const bool ok = Check(Stub().geometryUploads.empty(),
                              "disabled base stage must skip traced static geometry upload") &&
                        Check(Stub().rasterizedUploads.size() == 2u,
                              "enabled non-base stages must still upload as overlays");
        runtime.DestroyGpuResources(kFakeInstance);
        if (!ok)
        {
            return false;
        }
    }

    // Case (b): Disabling stage 0 underlay skips cutout backing geometry while keeping base surface.
    {
        Stub().Reset();
        EditorSceneRuntime runtime;
        EditorOverlayStage st0;
        st0.map = "textures/base/stage0.tga";
        EditorOverlayStage st1;
        st1.map = "textures/base/stage1.tga";
        EditorTexture tex;
        tex.name = "textures/test/cutout_no_underlay";
        tex.width = 4;
        tex.height = 4;
        tex.rgba = whiteTex;
        tex.isCutoutUnderlay = true;
        tex.baseStageIndex = 1u;
        tex.shaderStages = { st0, st1 };
        runtime.scene.textures = { tex, EditorTexture{ "textures/base/stage0", 4, 4, whiteTex },
                                   EditorTexture{ "textures/base/stage1", 4, 4, whiteTex } };
        runtime.scene.surfaces = { makeTriSurface("textures/test/cutout_no_underlay", false, true) };
        runtime.materials.Rebuild(runtime.scene);

        EditorMaterialsDocument doc;
        doc.LoadFromText(
            "qray_materials:\n  - name: textures/test/cutout_no_underlay\n    disabled_stages: 1\n");
        runtime.ApplyMaterialsDocuments(doc, nullptr);
        runtime.UploadStaticGeometry(kFakeInstance, assets, fallbackMat);

        const bool ok = Check(Stub().geometryUploads.size() == 1u,
                              "disabled underlay stage 0 must skip cutout backing upload");
        runtime.DestroyGpuResources(kFakeInstance);
        if (!ok)
        {
            return false;
        }
    }

    // Case (c): mirror:true promotes isOverlayOnly env surface to traced MIRROR and suppresses env overlay.
    {
        Stub().Reset();
        EditorSceneRuntime runtime;
        EditorOverlayStage stEnv;
        stEnv.map = "textures/base/stage0.tga";
        stEnv.isEnvironment = true;
        EditorOverlayStage stGlow;
        stGlow.map = "textures/base/stage2.tga";
        stGlow.additive = true;

        EditorTexture tex;
        tex.name = "textures/test/env_mirror";
        tex.width = 4;
        tex.height = 4;
        tex.rgba = MakeSolidRgba(4, 4, 180, 90, 220, 255);
        tex.isNonTracedOverlay = true;
        tex.baseStageIndex = 0u;
        tex.shaderStages = { stEnv, stGlow };
        runtime.scene.textures = { tex, EditorTexture{ "textures/base/stage0", 4, 4, whiteTex },
                                   EditorTexture{ "textures/base/stage2", 4, 4, whiteTex } };
        runtime.scene.surfaces = { makeTriSurface("textures/test/env_mirror", true, false) };

        EditorOverlaySurface ov;
        ov.shaderName = "textures/test/env_mirror";
        ov.vertices = runtime.scene.surfaces[0].vertices;
        ov.indices = runtime.scene.surfaces[0].indices;
        ov.baseStageIndex = 0u;
        ov.firstOverlayStage = 0u;
        ov.endOverlayStage = 2u;
        ov.stages = { stEnv, stGlow };
        runtime.scene.overlaySurfaces = { ov };
        runtime.materials.Rebuild(runtime.scene);

        EditorMaterialsDocument doc;
        doc.LoadFromText("qray_materials:\n  - name: textures/test/env_mirror\n    mirror: true\n");
        runtime.ApplyMaterialsDocuments(doc, nullptr);
        runtime.UploadStaticGeometry(kFakeInstance, assets, fallbackMat);
        const bool promotedOk = Stub().geometryUploads.size() == 1u &&
                                Stub().geometryUploads[0].passThroughType ==
                                    QR_GEOMETRY_PASS_THROUGH_TYPE_MIRROR;

        EditorCameraBasis cam{};
        cam.forward[0] = 1.0f;
        cam.right[1] = -1.0f;
        cam.up[2] = 1.0f;
        cam.viewportWidth = 800.0f;
        cam.viewportHeight = 600.0f;
        runtime.UploadWorldOverlays(kFakeInstance, assets, cam, 1.0f);

        const bool envSuppressedOk = Stub().rasterizedUploads.size() == 1u;
        runtime.DestroyGpuResources(kFakeInstance);
        if (!Check(promotedOk && envSuppressedOk,
                   "mirror on overlay-only env surface must promote to MIRROR and suppress env overlay"))
        {
            return false;
        }
    }

    return true;
}

} // namespace

int main()
{
    const std::filesystem::path tempDir =
        std::filesystem::temp_directory_path() / "qray_authored_tests";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir, ec);

    const bool ok = TestVfsFallbackAssetResolution(tempDir) &&
                    TestAuthoredConfigMaterialsLightsAndFrame() &&
                    TestWorldOverlaysAndDisabledStages() &&
                    TestDynamicMeshDtalAndUpload() &&
                    TestSteamQ3Tourney1Parity() &&
                    TestZeroPowerClusterSourcesFiltered() &&
                    TestAnimatedWaveAreaLightsStaySteady() &&
                    TestClusterSourcePowerAndRadiusScale() &&
                    TestSingleStageSurfaceLightClearedToNoneDoesNotGlowOrEmit(tempDir) &&
                    TestBaseStageDisabledCutoutBackingAndMirrorEnvPromotion();

    std::filesystem::remove_all(tempDir, ec);
    if (!ok)
    {
        return 1;
    }
    std::printf("ALL_AUTHORED_TESTS_OK\n");
    return 0;
}
