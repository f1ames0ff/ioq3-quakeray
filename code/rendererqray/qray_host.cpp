#ifdef _WIN32
#include <windows.h>
#else
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" {
#include "q_shared.h"
#include "qfiles.h"
#include "surfaceflags.h"
#ifndef USE_RENDERER_DLOPEN
#define USE_RENDERER_DLOPEN
#endif
#include "tr_public.h"
}

#include "qray_window.h"
#include "EditorLightSelection.h"
#include "EditorSceneRuntime.h"
#include "EditorSpatial.h"
#include "Q3EditorAdapter.h"
#include "qray/qray.h"
#include "stb_image.h"

refimport_t ri;

#ifndef _WIN32
// The shader-script and asset code compares Quake 3 names case-insensitively the
// way the original MSVC build did.
static inline int _stricmp(const char *a, const char *b) { return strcasecmp(a, b); }
static inline int _strnicmp(const char *a, const char *b, size_t n) { return strncasecmp(a, b, n); }
#endif

namespace {

constexpr float kPi = 3.14159265358979323846f;
// QuakeRay's reference host uses 40 Quake units per metre.
constexpr float kQuakeUnitToMeter = 0.025f;
constexpr uint32_t kReadbackFormat = QR_FRAME_READBACK_FORMAT_RGBA8_UNORM;
constexpr uint64_t kDynamicLightUniqueIdBase = 0x100000000ull;

float QuakeUnitsToMeters(float value)
{
    return value * kQuakeUnitToMeter;
}

void QuakePositionToMeters(const float source[3], float destination[3])
{
    destination[0] = QuakeUnitsToMeters(source[0]);
    destination[1] = QuakeUnitsToMeters(source[1]);
    destination[2] = QuakeUnitsToMeters(source[2]);
}

void QuakePositionToMeters(const float source[3], QrFloat3D &destination)
{
    QuakePositionToMeters(source, destination.data);
}

std::vector<QrVertex> QuakeVerticesToMeters(const std::vector<QrVertex> &source)
{
    std::vector<QrVertex> result = source;
    for (QrVertex &vertex : result) {
        for (int axis = 0; axis < 3; ++axis) {
            vertex.position[axis] = QuakeUnitsToMeters(vertex.position[axis]);
        }
    }
    return result;
}

refexport_t re;
glconfig_t glConfig;

QrInstance qrayInstance = QR_NULL_HANDLE;

cvar_t *qrayShaderDir = nullptr;
cvar_t *qrayBlueNoise = nullptr;
cvar_t *qrayWaterNormal = nullptr;
cvar_t *qrayConfig = nullptr;
cvar_t *qraySunColor = nullptr;
cvar_t *qraySkybox = nullptr;
cvar_t *qraySkyboxRadius = nullptr;
cvar_t *qrayRayLength = nullptr;
cvar_t *qraySmoke = nullptr;
cvar_t *qraySmokeMax = nullptr;
cvar_t *qraySmokeLife = nullptr;
cvar_t *qraySmokeSize = nullptr;
cvar_t *qraySmokeGrowth = nullptr;
cvar_t *qraySmokeAlpha = nullptr;
cvar_t *qraySmokeDensity = nullptr;
cvar_t *qraySmokeColor = nullptr;
cvar_t *qrayTonemapType = nullptr;
cvar_t *qrayTonemapPower = nullptr;
cvar_t *qrayWhitePoint = nullptr;
cvar_t *qrayExposureBias = nullptr;
cvar_t *qrayEyeAdaptation = nullptr;
cvar_t *qrayEnvShellTrace = nullptr;
cvar_t *qrayEnvMapRaster = nullptr;
cvar_t *qrayEnvMapScale = nullptr;
cvar_t *qrayEnvMapSaturation = nullptr;
cvar_t *qrayEnvShellScale = nullptr;
cvar_t *qrayLightScale = nullptr;
cvar_t *qrayLightRadiusScale = nullptr;
cvar_t *qrayLightRadius = nullptr;
cvar_t *qrayTrueLight = nullptr;
cvar_t *qrayUpscale = nullptr;
cvar_t *qraySurfaceLightScale = nullptr;
cvar_t *qraySurfaceLightMax = nullptr;
cvar_t *qraySurfaceLightGlow = nullptr;
cvar_t *qrayOverlayScale = nullptr;
cvar_t *qrayOverlayAddScale = nullptr;
cvar_t *qrayLightTopUp = nullptr;
cvar_t *qrayDlightScale = nullptr;
cvar_t *qrayViewMode = nullptr;
cvar_t *qrayModelWinding = nullptr;
cvar_t *qrayCullBackFaces = nullptr;
cvar_t *qrayDecalOffset = nullptr;
cvar_t *qrayDebugEntities = nullptr;
cvar_t *qrayNormalMode = nullptr;
cvar_t *qrayTestQuad = nullptr;
cvar_t *qrayPolyDebug = nullptr;
cvar_t *qraySkipEntities = nullptr;
cvar_t *qrayAlphaTest = nullptr;
cvar_t *qrayGiLevel = nullptr;
cvar_t *qrayDenoiser = nullptr;
cvar_t *qrayNeeSamples = nullptr;
cvar_t *qrayRestir = nullptr;
cvar_t *qrayRestirCandidates = nullptr;
cvar_t *qrayCellWorldSize = nullptr;
cvar_t *qraySunBounceRange = nullptr;
cvar_t *qrayPhysicalSky = nullptr;
cvar_t *qrayPhysicalSkySun = nullptr;
cvar_t *qraySkyColor = nullptr;
cvar_t *qraySunDiscSize = nullptr;
cvar_t *qrayClouds = nullptr;
cvar_t *qrayCloudColor = nullptr;
cvar_t *qrayCloudCoverage = nullptr;
cvar_t *qrayCloudDensity = nullptr;
cvar_t *qrayCloudSpeed = nullptr;
cvar_t *qrayCloudHeight = nullptr;
cvar_t *qrayCloudThickness = nullptr;
cvar_t *qrayCloudOpacity = nullptr;
cvar_t *qrayCloudQuality = nullptr;
cvar_t *qrayGodRays = nullptr;
cvar_t *qrayGodRaysIntensity = nullptr;
cvar_t *qrayGodRaysQuality = nullptr;
cvar_t *qrayUncapped = nullptr;
cvar_t *qrayPhysicalSkySunPitch = nullptr;
cvar_t *qrayPhysicalSkyYaw = nullptr;
cvar_t *qrayPhysicalSkyPower = nullptr;
cvar_t *qraySkyAmbientLod = nullptr;
cvar_t *qraySkyLightMultiplier = nullptr;
cvar_t *qrayIndirectHalfRes = nullptr;
cvar_t *qrayAuthored = nullptr;
cvar_t *qrayMaterialsPath = nullptr;
cvar_t *qrayLightsPath = nullptr;
cvar_t *qrayEditorConfigPath = nullptr;

EditorAssetSource gameAssets;
EditorSceneRuntime sceneRuntime;
EditorMaterialsDocument activeMaterialsDoc;
EditorMaterialsDocument baseMaterialsDoc;
bool hasBaseMaterialsDoc = false;
EditorLevelDocument activeLevelDoc;
bool q3AdapterRegistered = false;
std::vector<EditorEmissiveLight> frameDynamicEmissiveLights;
int32_t frameDynamicDtalBudget = 256;
uint64_t frameDynamicDtalNextId = 0x4000000000000000ull;

QrMaterial defaultMaterial = QR_NO_MATERIAL;

struct DynamicLight
{
    float origin[3];
    float color[3];
    float radius;
    bool additive;
    bool linear;
};

std::vector<DynamicLight> dynamicLights;


std::unordered_map<qhandle_t, QrMaterial> uiMaterials;
std::unordered_map<qhandle_t, std::string> uiShaderNames;
std::unordered_map<std::string, qhandle_t> uiShaderHandles;
qhandle_t nextUiHandle = 50000;
float currentColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

struct ModelSurface
{
    std::string surfaceName;
    std::string shaderName;
    std::vector<uint32_t> indices;
    std::vector<float> texCoords;
    std::vector<float> positions;
    std::vector<float> normals;
    int numVerts = 0;
};

struct ModelAsset
{
    bool valid = false;
    bool isBspSubmodel = false;
    std::string name;
    int numFrames = 0;
    int numTags = 0;
    float bounds[2][3] = {};
    float radius = 0.0f;
    std::vector<ModelSurface> surfaces;
    std::vector<md3Tag_t> tags;
};

struct SkinAsset
{
    std::unordered_map<std::string, std::string> shaderBySurface;
};

enum ShaderWaveType
{
    SHADER_WAVE_NONE,
    SHADER_WAVE_SIN,
    SHADER_WAVE_SQUARE,
    SHADER_WAVE_TRIANGLE,
    SHADER_WAVE_SAWTOOTH,
    SHADER_WAVE_INVERSE_SAWTOOTH,
    SHADER_WAVE_NOISE
};

enum ShaderTcModType
{
    TCMOD_SCALE,
    TCMOD_SCROLL,
    TCMOD_ROTATE,
    TCMOD_STRETCH,
    TCMOD_TURB
};

enum ShaderColorGen
{
    COLORGEN_DEFAULT,
    COLORGEN_IDENTITY,
    COLORGEN_VERTEX,
    COLORGEN_ENTITY,
    COLORGEN_WAVE
};

enum ShaderAlphaGen
{
    ALPHAGEN_DEFAULT,
    ALPHAGEN_IDENTITY,
    ALPHAGEN_VERTEX,
    ALPHAGEN_ENTITY,
    ALPHAGEN_WAVE
};

struct ShaderTcMod
{
    ShaderTcModType type = TCMOD_SCALE;
    ShaderWaveType wave = SHADER_WAVE_NONE;
    float params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct ShaderStage
{
    std::string map;
    std::vector<std::string> animationMaps;
    float animationFps = 0.0f;
    QrGeometryMaterialBlendType blend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
    QrBlendFactor blendSrc = QR_BLEND_FACTOR_ONE;
    QrBlendFactor blendDst = QR_BLEND_FACTOR_ZERO;
    bool blended = false;
    bool darkenDecal = false;
    bool alphaTested = false;
    bool environmentMap = false;
    bool lightmap = false;
    bool clampMap = false;
    bool dynamic = false;
    bool animated = false;
    ShaderColorGen rgbGen = COLORGEN_DEFAULT;
    ShaderAlphaGen alphaGen = ALPHAGEN_DEFAULT;
    float scroll[2] = {0.0f, 0.0f};
    float scale[2] = {1.0f, 1.0f};
    std::vector<ShaderTcMod> tcMods;
    ShaderWaveType rgbWave = SHADER_WAVE_NONE;
    float rgbWaveBase = 1.0f;
    float rgbWaveAmplitude = 0.0f;
    float rgbWavePhase = 0.0f;
    float rgbWaveFrequency = 0.0f;
    ShaderWaveType alphaWave = SHADER_WAVE_NONE;
    float alphaWaveBase = 1.0f;
    float alphaWaveAmplitude = 0.0f;
    float alphaWavePhase = 0.0f;
    float alphaWaveFrequency = 0.0f;
};

struct ShaderDefinition
{
    std::string map;
    QrGeometryMaterialBlendType blend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
    QrBlendFactor blendSrc = QR_BLEND_FACTOR_ONE;
    QrBlendFactor blendDst = QR_BLEND_FACTOR_ZERO;
    bool darkenDecal = false;
    bool alphaTested = false;
    // The chosen stage is a tcGen environment pass, so its texcoords come from the eye
    // reflection vector instead of the surface UVs.
    bool environmentMap = false;
    bool usesOverlay = false;
    bool dynamic = false;
    bool transparentSurface = false;
    bool tracedBase = false;
    // World cutout frame over an animated under-layer (e.g., demon_block15fx, killblockgeomtrn):
    // the static stone frame is raytraced with alpha testing + opaque backing while the animated
    // under-layer(s) draw behind it on the raster overlay with depth test enabled.
    bool cutoutFrameUnderlay = false;
    // Stage 0 tcGen environment chrome followed by alpha-blended colored lacquer (e.g., armor, doors).
    bool metallicEnvBase = false;
    bool metallicEnvInvertedAlpha = false;
    // Sandwich-mask solid shaders whose alpha encodes wet/glossiness rather than transparency (wetwall).
    bool wetGlossAlphaMask = false;
    std::vector<std::string> decalOverlayMaps;
    std::vector<bool> decalOverlayInvertedAlpha;
    float baseScale[2] = {1.0f, 1.0f};
    // Opaque/animated Stage 0 underlay followed by SRC_ALPHA/ONE_MINUS_SRC_ALPHA diffuse skin
    // (e.g., models/players/doom/phobos, models/powerups/ammo/lighammo, bfgammo).
    bool compositeUnderlayBase = false;
    std::string underlayMap;
    float underlayScale[2] = {1.0f, 1.0f};
    bool underlayEmissive = false;
    ShaderWaveType deformWave = SHADER_WAVE_NONE;
    float deformSpread = 0.0f;
    float deformBase = 0.0f;
    float deformAmplitude = 0.0f;
    float deformPhase = 0.0f;
    float deformFrequency = 0.0f;
    // Every non-lightmap stage is blended (e.g., weapon impact explosions, armor energy shell).
    bool hasOnlyBlendedStages = false;
    size_t baseStageIndex = static_cast<size_t>(-1);
    std::vector<ShaderStage> stages;
    // q3map light emission. Retail Q3 bakes these into the lightmaps; the RT renderer turns
    // them into area lights (torch flames, light fixtures, lava).
    float surfaceLight = 0.0f;
    std::string lightImage;
    std::string editorImage;
    bool hasLightRGB = false;
    float lightRGB[3] = {1.0f, 1.0f, 1.0f};
    bool twoSided = false;
    bool isPortal = false;
};

std::unordered_map<qhandle_t, ModelAsset> modelAssets;
std::unordered_map<std::string, qhandle_t> modelHandles;
std::unordered_map<std::string, QrMaterial> textureMaterials;
std::unordered_map<std::string, QrMaterial> textureMaterialsRaw;
std::unordered_map<qhandle_t, QrMaterial> uiMaterialsRaw;
std::unordered_map<std::string, QrMaterial> scaledEnvMaterials;
std::unordered_map<std::string, QrMaterial> directImageMaterials;
std::unordered_map<std::string, bool> textureHasAlpha;
std::unordered_map<std::string, bool> binaryCutoutHoleCache;
std::unordered_map<std::string, ShaderDefinition> shaderDefinitions;
std::unordered_map<qhandle_t, SkinAsset> skinAssets;
std::unordered_map<std::string, qhandle_t> skinHandles;
qhandle_t nextModelHandle = 1000;
qhandle_t nextSkinHandle = 20000;
bool shaderScriptsLoaded = false;

struct PendingPoly
{
    qhandle_t shader = 0;
    std::vector<polyVert_t> verts;
};

// Impact marks are built by clipping a projected polygon against the world, so the host needs
// its own copy of the world triangles plus a coarse grid to keep the per-mark ray casts cheap.
struct MarkTriangle
{
    float points[3][3] = {};
    float normal[3] = {0.0f, 0.0f, 1.0f};
    float texCoords[3][2] = {};
    float vertexNormals[3][3] = {};
    int32_t sourceSurfaceIndex = -1;
    uint32_t sourceTriangleIndex = 0;
    bool isCurved = false;
    bool noMarks = false;
};

std::vector<MarkTriangle> markTriangles;
std::unordered_map<int64_t, std::vector<uint32_t>> markGrid;
constexpr float kMarkCellSize = 256.0f;

int64_t MarkCellKey(int x, int y, int z)
{
    return static_cast<int64_t>(x) * 73856093ll ^ static_cast<int64_t>(y) * 19349663ll ^
        static_cast<int64_t>(z) * 83492791ll;
}

struct PendingEntity
{
    refEntity_t entity{};
    float shaderTimeSeconds = 0.0f;
};

std::vector<PendingEntity> pendingEntities;
std::vector<PendingPoly> pendingPolys;
uint64_t dynamicGeometryId = 0x4000000000000000ull;

void NormalizeVector(float vector[3]);

bool frameStarted = false;
bool sceneLightsUploaded = false;
bool sceneValid = false;
bool sceneRendered = false;
bool frameHasWorldScene = false;
refdef_t sceneDef;
refdef_t probeSceneDef;
bool probeSceneValid = false;
std::string pendingWorldName;
bool worldLoaded = false;

std::vector<uint8_t> readbackBuffer;
std::vector<uint8_t> readbackReadyPixels;
uint32_t readbackWidth = 0;
uint32_t readbackHeight = 0;
uint32_t readbackRowPitch = 0;
uint64_t readbackBufferSize = 0;
uint64_t readbackRequest = 0;
bool readbackPending = false;
bool readbackValid = false;
// Full-frame readbacks cost a GPU copy plus a CPU copy every frame, so they only run
// while a consumer (AVI capture, qray_readback) asked for one recently.
constexpr int kReadbackKeepAliveFrames = 120;
int readbackWantedFrames = 0;

bool capturePending = false;
int captureWidth = 0;
int captureHeight = 0;
byte *captureBuffer = nullptr;
byte *captureEncodeBuffer = nullptr;
bool captureMotionJpeg = false;

bool commandsRegistered = false;
bool hostLogEnabled = false;

void QrayScreenshot_f();
void QrayReadbackDump_f();
void QrayProbe_f();
void ResetProceduralSmoke();
bool RayIntersectsTriangle(const MarkTriangle &triangle, const float origin[3],
    const float direction[3], float maxDistance, float &hitDistance,
    float *outU, float *outV);

// Directory for the diagnostic logs: %TEMP% on Windows, $TMPDIR or /tmp elsewhere.
bool TempFilePath(const char *fileName, char *out, size_t outSize)
{
    if (out == nullptr || outSize == 0) {
        return false;
    }
    out[0] = '\0';
#ifdef _WIN32
    char directory[MAX_PATH] = {};
    const DWORD length = GetTempPathA(MAX_PATH, directory);
    if (length == 0 || length >= MAX_PATH) {
        return false;
    }
    const int written = std::snprintf(out, outSize, "%s%s", directory, fileName);
#else
    const char *directory = std::getenv("TMPDIR");
    if (directory == nullptr || directory[0] == '\0') {
        directory = "/tmp";
    }
    const size_t directoryLength = std::strlen(directory);
    const bool needsSeparator = directoryLength > 0 && directory[directoryLength - 1] != '/';
    const int written = std::snprintf(out, outSize, "%s%s%s", directory, needsSeparator ? "/" : "", fileName);
#endif
    if (written <= 0 || static_cast<size_t>(written) >= outSize) {
        out[0] = '\0';
        return false;
    }
    return true;
}

void HostLog(const char *format, ...)
{
    if (!hostLogEnabled) {
        return;
    }

    static char path[1024] = {};
    if (path[0] == '\0' && !TempFilePath("qray_host.log", path, sizeof(path))) {
        return;
    }

    std::ofstream stream(path, std::ios::app);
    if (!stream.is_open()) {
        return;
    }

    char message[2048] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    stream << message << "\n";
}

const char *ProbeLogPath()
{
    static char path[1024] = {};
    if (path[0] == '\0') {
        TempFilePath("qray_probe.log", path, sizeof(path));
    }
    return path;
}

void ProbeLog(const char *format, ...)
{
    const char *path = ProbeLogPath();
    if (path[0] == '\0') {
        return;
    }

    std::ofstream stream(path, std::ios::app);
    if (!stream.is_open()) {
        return;
    }

    char message[4096] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    char timestamp[64] = {};
#ifdef _WIN32
    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02u.%03u",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
#else
    struct timeval now {};
    gettimeofday(&now, nullptr);
    struct tm local {};
    const time_t seconds = now.tv_sec;
    localtime_r(&seconds, &local);
    std::snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
        local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
        local.tm_sec, static_cast<int>(now.tv_usec / 1000));
#endif
    stream << timestamp << " " << message << "\n";
}

const char *SurfaceKindName(EditorSurfaceKind kind)
{
    switch (kind) {
    case EditorSurfaceKind::Opaque: return "OPAQUE";
    case EditorSurfaceKind::Fence: return "FENCE";
    case EditorSurfaceKind::Water: return "WATER";
    case EditorSurfaceKind::Slime: return "SLIME";
    case EditorSurfaceKind::Lava: return "LAVA";
    case EditorSurfaceKind::Teleport: return "TELEPORT";
    case EditorSurfaceKind::Sky: return "SKY";
    default: return "UNKNOWN";
    }
}

bool AuthoredModeEnabled()
{
    return qrayAuthored == nullptr || qrayAuthored->integer != 0;
}

void SceneRuntimeLog(const char *message)
{
    if (message != nullptr && message[0] != '\0') {
        HostLog("Runtime: %s", message);
    }
}

#ifdef _WIN32
int HostExceptionFilter(EXCEPTION_POINTERS *exceptionInfo)
{
    if (exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr) {
        HostLog("qray host exception 0x%08lx", static_cast<unsigned long>(exceptionInfo->ExceptionRecord->ExceptionCode));
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

QrResult CreateInstanceProtected(const QrInstanceCreateInfo *info, QrInstance *instance)
{
    __try {
        return qrCreateInstance(info, instance);
    } __except (HostExceptionFilter(GetExceptionInformation())) {
        *instance = QR_NULL_HANDLE;
        return QR_GRAPHICS_API_ERROR;
    }
}
#else
// There is no structured exception handling outside MSVC; C++ exceptions are still
// caught by the caller.
QrResult CreateInstanceProtected(const QrInstanceCreateInfo *info, QrInstance *instance)
{
    return qrCreateInstance(info, instance);
}
#endif

const char *CvarText(cvar_t *cvar, const char *fallback)
{
    if (cvar == nullptr || cvar->string == nullptr || cvar->string[0] == '\0') {
        return fallback;
    }
    return cvar->string;
}

float CvarFloat(cvar_t *cvar, float fallback)
{
    return cvar != nullptr ? cvar->value : fallback;
}

void CvarColor(cvar_t *cvar, const float fallback[3], QrFloat3D &color)
{
    color.data[0] = fallback[0];
    color.data[1] = fallback[1];
    color.data[2] = fallback[2];
    if (cvar == nullptr || cvar->string == nullptr || cvar->string[0] == '\0') {
        return;
    }

    float parsed[3] = {};
    if (std::sscanf(cvar->string, "%f %f %f", &parsed[0], &parsed[1], &parsed[2]) == 3) {
        const bool usesByteRange = parsed[0] > 1.0f || parsed[1] > 1.0f || parsed[2] > 1.0f;
        const float scale = usesByteRange ? 1.0f / 255.0f : 1.0f;
        color.data[0] = std::clamp(parsed[0] * scale, 0.0f, 1.0f);
        color.data[1] = std::clamp(parsed[1] * scale, 0.0f, 1.0f);
        color.data[2] = std::clamp(parsed[2] * scale, 0.0f, 1.0f);
    }
}

void CopyString(char *destination, const char *source, size_t size)
{
    if (destination == nullptr || size == 0) {
        return;
    }
    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }
    std::strncpy(destination, source, size - 1);
    destination[size - 1] = '\0';
}

void QrayPrint(const char *message, void *userData)
{
    (void)userData;
    if (message != nullptr) {
        HostLog("qray: %s", message);
        ri.Printf(PRINT_ALL, "%s", message);
    }
}

struct LoadedFile
{
    std::vector<uint8_t> data;
    void *q3Data;
};

void QrayOpenFile(const char *filePath, void *userData, const void **outData, uint32_t *outSize, void **outHandle)
{
    (void)userData;
    *outData = nullptr;
    *outSize = 0;
    *outHandle = nullptr;

    if (filePath == nullptr || filePath[0] == '\0') {
        return;
    }

    std::ifstream stream(filePath, std::ios::binary | std::ios::ate);
    if (stream.is_open()) {
        const std::streamsize length = stream.tellg();
        if (length > 0) {
            auto *file = new LoadedFile();
            file->q3Data = nullptr;
            file->data.resize(static_cast<size_t>(length));
            stream.seekg(0, std::ios::beg);
            stream.read(reinterpret_cast<char *>(file->data.data()), length);
            if (stream.good() || stream.eof()) {
                *outData = file->data.data();
                *outSize = static_cast<uint32_t>(file->data.size());
                *outHandle = file;
                return;
            }
            delete file;
        }
    }

    void *buffer = nullptr;
    const long length = ri.FS_ReadFile(filePath, &buffer);
    if (length > 0 && buffer != nullptr) {
        auto *file = new LoadedFile();
        file->q3Data = buffer;
        *outData = buffer;
        *outSize = static_cast<uint32_t>(length);
        *outHandle = file;
    }
}

void QrayCloseFile(void *fileHandle, void *userData)
{
    (void)userData;
    auto *file = static_cast<LoadedFile *>(fileHandle);
    if (file == nullptr) {
        return;
    }
    if (file->q3Data != nullptr) {
        ri.FS_FreeFile(file->q3Data);
    }
    delete file;
}

void RegisterCvars()
{
    cvar_t *debugLog = ri.Cvar_Get("qray_debugLog", "0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(debugLog, "Write QuakeRay host diagnostics to the system temporary directory.");
    hostLogEnabled = debugLog->integer != 0;

    qrayShaderDir = ri.Cvar_Get("qray_shaderDir", "qray/shaders/", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayShaderDir, "Directory containing the compiled QuakeRay SPIR-V shaders.");

    qrayBlueNoise = ri.Cvar_Get("qray_blueNoise", "qray/blue_noise.png", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayBlueNoise, "Path to the QuakeRay blue noise texture.");

    qrayWaterNormal = ri.Cvar_Get("qray_waterNormal", "", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayWaterNormal, "Optional path to the QuakeRay water normal texture.");

    qrayConfig = ri.Cvar_Get("qray_config", "qray.cfg", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayConfig, "Path to the QuakeRay renderer settings file.");

    qraySunColor = ri.Cvar_Get("qray_sunColor", "", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySunColor,
        "QuakeRay directional light color; empty uses the q3map_sun of the map.");

    // The SDK only applies a tonemap operator when the draw info carries the parameters, so
    // without these it falls back to a white point of one and clips every highlight.
    qrayTonemapType = ri.Cvar_Get("qray_tonemapType", "4", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayTonemapType,
        "QuakeRay tonemap operator: 0 none, 1 Reinhard, 2 extended Reinhard, 3 ACES, 4 AgX.");

    qrayTonemapPower = ri.Cvar_Get("qray_tonemapPower", "0.9", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayTonemapPower,
        "Blend towards the tonemap operator; 0 keeps the filmic curve alone.");

    qrayWhitePoint = ri.Cvar_Get("qray_whitePoint", "20.0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayWhitePoint,
        "Luminance mapped to white by the tonemap operator, higher values keep more highlights.");

    qrayExposureBias = ri.Cvar_Get("qray_exposureBias", "0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayExposureBias,
        "Exposure bias in stops applied by QuakeRay.");

    qrayEyeAdaptation = ri.Cvar_Get("qray_eyeAdaptation", "0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayEyeAdaptation,
        "Let QuakeRay adapt exposure to the frame; Quake 3 kept a fixed exposure, so automatic adaptation shifts the look.");

    qrayEnvShellTrace = ri.Cvar_Get("qray_envShellTrace", "0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayEnvShellTrace,
        "Draw additive environment shells (powerup glass) through the ray tracer instead of the unlit overlay; 2 skips them.");

    qrayEnvMapRaster = ri.Cvar_Get("qray_envMapRaster", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayEnvMapRaster,
        "Draw opaque environment map stages (powerup bodies) as unlit overlays, the way the fixed function pipeline did.");

    qrayEnvMapScale = ri.Cvar_Get("qray_envMapScale", "1.15", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayEnvMapScale,
        "Brightness of the opaque environment map stages; the baked maps read darker than in the retail frame buffer.");

    qrayEnvMapSaturation = ri.Cvar_Get("qray_envMapSaturation", "1.6", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayEnvMapSaturation,
        "Saturation of the opaque environment map stages; the additive shell desaturates the powerup bodies in the HDR pipeline.");

    qrayEnvShellScale = ri.Cvar_Get("qray_envShellScale", "1.0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayEnvShellScale,
        "Brightness of the additive glass shells; the fixed function blend overshoots in the HDR pipeline.");

    qraySkybox = ri.Cvar_Get("qray_skybox", "0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySkybox,
        "Draw the map's original Quake 3 sky as rasterized geometry over the physical sky (0 = physical sky and clouds only).");

    qraySkyboxRadius = ri.Cvar_Get("qray_skyboxRadius", "32768", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySkyboxRadius, "Distance of the rasterized sky box in map units.");

    qrayRayLength = ri.Cvar_Get("qray_rayLength", "65536", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayRayLength, "Maximum ray trace distance in Quake units.");

    qraySmoke = ri.Cvar_Get("qray_smoke", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySmoke,
        "Replace classic rocket and grenade smoke sprites with QuakeRay procedural smoke.");

    qraySmokeMax = ri.Cvar_Get("qray_smokeMax", "1024", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySmokeMax, "Maximum active procedural smoke puffs.");

    qraySmokeLife = ri.Cvar_Get("qray_smokeLife", "1.5", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySmokeLife, "Lifetime of procedural smoke puffs in seconds.");

    qraySmokeSize = ri.Cvar_Get("qray_smokeSize", "16", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySmokeSize, "Base radius of procedural smoke puffs in Quake units.");

    qraySmokeGrowth = ri.Cvar_Get("qray_smokeGrowth", "96", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySmokeGrowth, "Growth rate of procedural smoke puffs in Quake units/s.");

    qraySmokeAlpha = ri.Cvar_Get("qray_smokeAlpha", "1.0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySmokeAlpha, "Opacity multiplier of procedural smoke puffs.");

    qraySmokeDensity = ri.Cvar_Get("qray_smokeDensity", "2.0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySmokeDensity, "Density parameter of procedural smoke puffs.");

    qraySmokeColor = ri.Cvar_Get("qray_smokeColor", "0.4 0.4 0.4", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySmokeColor, "RGB tint of procedural smoke puffs (0..1 per channel).");

    qrayLightScale = ri.Cvar_Get("qray_lightScale", "0.25", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayLightScale, "Intensity scale applied to Quake 3 map light entities.");

    qrayLightRadiusScale = ri.Cvar_Get("qray_lightRadiusScale", "1.0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayLightRadiusScale, "Radius scale applied to Quake 3 map light entities.");

    qrayLightRadius = ri.Cvar_Get("qray_lightRadius", "16", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayLightRadius,
        "Emission sphere radius of a Quake 3 map light, in Quake units. Small values behave like point lights.");

    qrayUpscale = ri.Cvar_Get("qray_upscale", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayUpscale,
        "Upscaler: 0 = linear (no temporal AA), 1 = FSR 3 native AA, 2 = FSR quality, 3 = FSR balanced, 4 = FSR performance.");
    qrayTrueLight = ri.Cvar_Get("qray_truelight", "0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayTrueLight,
        "TrueLight mode (QuakeRay rt_truelight parity): 0 = legacy BSP light entities and gameplay dlights, "
        "1 = TrueLight physical area/surface lights, custom lights and sun with gameplay dlights, "
        "2 = strict TrueLight (physical area/surface lights, custom lights and sun only).");
    qraySurfaceLightScale = ri.Cvar_Get("qray_surfaceLightScale", "0.4", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySurfaceLightScale,
        "Intensity multiplier of q3map_surfacelight area lights, on top of qray_lightScale.");
    qraySurfaceLightMax = ri.Cvar_Get("qray_surfaceLightMax", "2048", CVAR_ARCHIVE | CVAR_LATCH);
    ri.Cvar_SetDescription(qraySurfaceLightMax,
        "Maximum number of surface light triangles per map; the dimmest are dropped above it. The SDK light array holds 4096 lights in total.");
    qraySurfaceLightGlow = ri.Cvar_Get("qray_surfaceLightGlow", "1", CVAR_ARCHIVE | CVAR_LATCH);
    ri.Cvar_SetDescription(qraySurfaceLightGlow,
        "Write an emission mask into the materials of q3map_surfacelight shaders so the light surfaces themselves glow.");
    qrayOverlayScale = ri.Cvar_Get("qray_overlayScale", "1.0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayOverlayScale,
        "HDR luminance multiplier for opaque and alpha-blended world shader overlays.");
    qrayOverlayAddScale = ri.Cvar_Get("qray_overlayAddScale", "1.5", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayOverlayAddScale,
        "HDR luminance multiplier for additive and emissive world shader overlays (flames, jump pads, lava).");

    qrayLightTopUp = ri.Cvar_Get("qray_lightTopUp", "1024", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayLightTopUp,
        "Distance in Quake units within which clusters receive lights their PVS row does not list.");

    qrayDlightScale = ri.Cvar_Get("qray_dlightScale", "0.25", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayDlightScale,
        "Intensity scale applied to per-frame Quake 3 dynamic lights.");

    qrayViewMode = ri.Cvar_Get("qray_viewMode", "7", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayViewMode, "Debug camera matrix layout selection for QuakeRay.");

    qrayModelWinding = ri.Cvar_Get("qray_modelWinding", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayModelWinding, "Reverse MD3 triangle winding for QuakeRay ray culling.");

    qrayCullBackFaces = ri.Cvar_Get("qray_cullBackFaces", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayCullBackFaces, "Cull back-facing triangles in QuakeRay.");

    qrayDecalOffset = ri.Cvar_Get("qray_decalOffset", "0.25", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayDecalOffset,
        "Distance polygon effects (bullet marks, scorches) are pushed off the surface they sit on.");

    qrayDebugEntities = ri.Cvar_Get("qray_debugEntities", "0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayDebugEntities,
        "Log entity material resolution to the host log (requires qray_log).");

    qrayNormalMode = ri.Cvar_Get("qray_normalMode", "0", CVAR_TEMP);
    ri.Cvar_SetDescription(qrayNormalMode,
        "Debug: 0 = flipped winding + smooth vertex normals, 1 = flipped winding + "
        "generated inverted normals, 2 = original winding + smooth vertex normals, "
        "3 = original winding + generated inverted normals.");

    qrayTestQuad = ri.Cvar_Get("qray_testQuad", "0", CVAR_TEMP);
    ri.Cvar_SetDescription(qrayTestQuad,
        "Debug: draw an unlit quad in front of the camera through the rasterized overlay.");

    qrayPolyDebug = ri.Cvar_Get("qray_polyDebug", "0", CVAR_TEMP);
    ri.Cvar_SetDescription(qrayPolyDebug,
        "Debug: 0 = normal decal path, 1 = decals as opaque default-material quads, "
        "2 = decals with blend but no depth test.");

    qraySkipEntities = ri.Cvar_Get("qray_skipEntities", "0", CVAR_TEMP);
    ri.Cvar_SetDescription(qraySkipEntities,
        "Debug: bit 1 = models, bit 2 = sprites, bit 4 = beams, bit 8 = polygons.");

    qrayAlphaTest = ri.Cvar_Get("qray_alphaTest", "1", CVAR_TEMP);
    ri.Cvar_SetDescription(qrayAlphaTest,
        "Debug: 1 = honour alphaFunc stages through the alpha tested hit group, 0 = opaque.");

    qrayGiLevel = ri.Cvar_Get("qray_giLevel", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayGiLevel,
        "Indirect lighting level: 0 disables GI, 1 uses one bounce, 2 enables the second bounce.");

    qrayDenoiser = ri.Cvar_Get("qray_denoiser", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayDenoiser, "Enable the QuakeRay temporal denoiser.");

    qrayNeeSamples = ri.Cvar_Get("qray_neeSamples", "2", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayNeeSamples, "Direct-light samples per pixel: 1 or 2.");

    qrayRestir = ri.Cvar_Get("qray_restir", "0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayRestir,
        "Use ReSTIR over the global light list instead of per-cluster light sampling.");

    qrayRestirCandidates = ri.Cvar_Get("qray_restirCandidates", "32", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayRestirCandidates, "ReSTIR candidates per light sample, from 1 to 64.");

    qrayCellWorldSize = ri.Cvar_Get("qray_cellWorldSize", "2.0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayCellWorldSize, "ReSTIR and GI spatial cell size in metres.");

    qraySunBounceRange = ri.Cvar_Get("qray_sunBounceRange", "50.8", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySunBounceRange,
        "Sun reach into an indirect bounce in metres; 0 disables indirect sunlight.");

    qrayPhysicalSky = ri.Cvar_Get("qray_physicalSky", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayPhysicalSky,
        "Use QuakeRay's procedural sky instead of the map's rasterized sky cubemap.");

    qrayPhysicalSkySun = ri.Cvar_Get("qray_physicalSkySun", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayPhysicalSkySun,
        "Upload the map sun as the physical directional light and procedural sun disc.");

    qraySkyColor = ri.Cvar_Get("qray_skyColor", "auto", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySkyColor,
        "Procedural sky colour as R G B in the 0..255 range; auto derives the tint from "
        "the map sky and uses QuakeRay's blue profile for black space skies.");

    qraySunDiscSize = ri.Cvar_Get("qray_sunDiscSize", "1.0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySunDiscSize, "Scale of the visible procedural sun disc.");

    qrayClouds = ri.Cvar_Get("qray_clouds", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayClouds, "Enable QuakeRay's volumetric cloud layer.");

    qrayCloudColor = ri.Cvar_Get("qray_cloudColor", "255 255 255", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayCloudColor,
        "Cloud colour as R G B in the 0..255 range; legacy 0..1 values are accepted.");

    qrayCloudCoverage = ri.Cvar_Get("qray_cloudCoverage", "0.2", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayCloudCoverage, "Volumetric cloud coverage in the range 0..1.");

    qrayCloudDensity = ri.Cvar_Get("qray_cloudDensity", "0.8", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayCloudDensity, "Volumetric cloud density.");

    qrayCloudSpeed = ri.Cvar_Get("qray_cloudSpeed", "0.3", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayCloudSpeed, "Volumetric cloud motion rate.");

    qrayCloudHeight = ri.Cvar_Get("qray_cloudHeight", "3500", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayCloudHeight, "Cloud layer base altitude in metres.");

    qrayCloudThickness = ri.Cvar_Get("qray_cloudThickness", "2250", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayCloudThickness, "Cloud layer thickness in metres.");

    qrayCloudOpacity = ri.Cvar_Get("qray_cloudOpacity", "1.0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayCloudOpacity, "Cloud compositing opacity in the range 0..1.");

    qrayCloudQuality = ri.Cvar_Get("qray_cloudQuality", "2", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayCloudQuality, "Cloud quality: 0 low through 3 ultra.");

    qrayGodRays = ri.Cvar_Get("qray_godRays", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayGodRays, "Enable procedural sun shafts.");

    qrayGodRaysIntensity = ri.Cvar_Get("qray_godRaysIntensity", "1.0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayGodRaysIntensity, "Procedural sun-shaft intensity.");

    qrayGodRaysQuality = ri.Cvar_Get("qray_godRaysQuality", "2", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayGodRaysQuality, "Procedural sun-shaft quality.");

    qrayUncapped = ri.Cvar_Get("qray_uncapped", "0", CVAR_TEMP);
    ri.Cvar_SetDescription(qrayUncapped,
        "Benchmark only: use IMMEDIATE present and zero present wait instead of FIFO with latency 1.");

    qrayPhysicalSkySunPitch = ri.Cvar_Get("qray_physicalSkySunPitch", "-1000", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayPhysicalSkySunPitch,
        "Physical sun elevation in degrees; -1000 uses the map q3map_sun.");

    qrayPhysicalSkyYaw = ri.Cvar_Get("qray_physicalSkyYaw", "-1000", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayPhysicalSkyYaw,
        "Physical sun yaw in degrees; -1000 uses the map q3map_sun.");

    qrayPhysicalSkyPower = ri.Cvar_Get("qray_physicalSkyPower", "2.5", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayPhysicalSkyPower,
        "Physical sun power from 0 to 100; 0 keeps the sun disabled.");

    qraySkyAmbientLod = ri.Cvar_Get("qray_skyAmbientLod", "4", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySkyAmbientLod,
        "Cubemap/procedural-sky mip level used for ambient lighting.");

    qraySkyLightMultiplier = ri.Cvar_Get("qray_skyLightMultiplier", "1.0", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qraySkyLightMultiplier,
        "Multiplier on the light physical sky contributes to the scene, from 0 to 8.");

    qrayIndirectHalfRes = ri.Cvar_Get("qray_indirectHalfRes", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayIndirectHalfRes,
        "Trace indirect lighting at half resolution while keeping the primary visibility pass full resolution.");

    qrayAuthored = ri.Cvar_Get("qray_authored", "1", CVAR_ARCHIVE);
    ri.Cvar_SetDescription(qrayAuthored,
        "Load QuakeRay Editor authored config.cfg, qray.materials.yaml, and qray.lights.yaml overrides.");

    qrayMaterialsPath = ri.Cvar_Get("qray_materialsPath", "", CVAR_TEMP);
    ri.Cvar_SetDescription(qrayMaterialsPath,
        "Optional explicit filesystem path to qray.materials.yaml.");

    qrayLightsPath = ri.Cvar_Get("qray_lightsPath", "", CVAR_TEMP);
    ri.Cvar_SetDescription(qrayLightsPath,
        "Optional explicit filesystem path to qray.lights.yaml.");

    qrayEditorConfigPath = ri.Cvar_Get("qray_editorConfigPath", "", CVAR_TEMP);
    ri.Cvar_SetDescription(qrayEditorConfigPath,
        "Optional explicit filesystem path to the editor System config.cfg.");
}

bool CreateDefaultMaterial()
{
    static uint8_t albedo[4] = {255, 255, 255, 255};
    static uint8_t roughMetalEmission[4] = {160, 0, 0, 255};
    static uint8_t normal[4] = {128, 128, 255, 255};

    QrMaterialCreateInfo info{};
    info.size = {1, 1};
    info.textures.pDataAlbedoAlpha = albedo;
    info.textures.pDataRoughnessMetallicEmission = roughMetalEmission;
    info.textures.pDataNormal = normal;
    info.filter = QR_SAMPLER_FILTER_LINEAR;
    info.addressModeU = QR_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeV = QR_SAMPLER_ADDRESS_MODE_REPEAT;

    QrResult result = qrCreateMaterial(qrayInstance, &info, &defaultMaterial);
    if (result != QR_SUCCESS) {
        ri.Printf(PRINT_WARNING, "rendererqray: qrCreateMaterial failed: %d\n", static_cast<int>(result));
        defaultMaterial = QR_NO_MATERIAL;
        return false;
    }
    return true;
}

bool LoadQ3Texture(const char *shaderName, int &width, int &height, std::vector<uint8_t> &pixels){
    width = 0;
    height = 0;
    pixels.clear();

    if (shaderName == nullptr || shaderName[0] == '\0' || shaderName[0] == '$') {
        return false;
    }

    std::vector<std::string> candidates;
    const char *lastDot = std::strrchr(shaderName, '.');
    const char *lastSlash = std::strrchr(shaderName, '/');
    if (lastDot != nullptr && (lastSlash == nullptr || lastDot > lastSlash)) {
        const std::string base(shaderName, static_cast<size_t>(lastDot - shaderName));
        candidates.emplace_back(shaderName);
        candidates.emplace_back(base + ".tga");
        candidates.emplace_back(base + ".jpg");
        candidates.emplace_back(base + ".jpeg");
        candidates.emplace_back(base + ".png");
    } else {
        candidates.emplace_back(std::string(shaderName) + ".tga");
        candidates.emplace_back(std::string(shaderName) + ".jpg");
        candidates.emplace_back(std::string(shaderName) + ".jpeg");
        candidates.emplace_back(std::string(shaderName) + ".png");
    }

    for (const std::string &candidate : candidates) {
        void *fileData = nullptr;
        const long fileLength = ri.FS_ReadFile(candidate.c_str(), &fileData);
        if (fileLength <= 0 || fileData == nullptr) {
            continue;
        }

        int decodedWidth = 0;
        int decodedHeight = 0;
        int channels = 0;
        stbi_uc *decoded = stbi_load_from_memory(
            static_cast<const stbi_uc *>(fileData), static_cast<int>(fileLength),
            &decodedWidth, &decodedHeight, &channels, 4);
        ri.FS_FreeFile(fileData);

        if (decoded != nullptr && decodedWidth > 0 && decodedHeight > 0) {
            width = decodedWidth;
            height = decodedHeight;
            pixels.assign(decoded, decoded + static_cast<size_t>(width) * height * 4);
            stbi_image_free(decoded);
            std::string key = candidate;
            for (char &character : key) {
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
            bool hasTransparentPixel = false;
            if (channels == 4 || channels == 2) {
                for (size_t index = 3; index < pixels.size(); index += 4) {
                    if (pixels[index] < 250) {
                        hasTransparentPixel = true;
                        break;
                    }
                }
            }
            // Several retail effect TGAs are 32-bit even though every texel is opaque. Treating
            // the mere presence of an alpha channel as a transparency mask leaves their black
            // background as a solid card instead of selecting the original additive blend.
            textureHasAlpha[key] = hasTransparentPixel;
            return true;
        }
        if (decoded != nullptr) {
            stbi_image_free(decoded);
        }
    }

    return false;
}

void CleanAdditiveBlackBackground(std::vector<uint8_t> &pixels)
{
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const uint8_t peak = std::max({pixels[i + 0], pixels[i + 1], pixels[i + 2]});
        if (peak <= 6) {
            pixels[i + 0] = 0;
            pixels[i + 1] = 0;
            pixels[i + 2] = 0;
        } else if (peak < 18) {
            const float scale = static_cast<float>(peak - 6) / 12.0f;
            for (int c = 0; c < 3; ++c) {
                pixels[i + c] = static_cast<uint8_t>(std::clamp(
                    static_cast<float>(pixels[i + c]) * scale, 0.0f, 255.0f));
            }
        }
    }
}

QrMaterial CreateMaterialFromPixels(const char *name, int width, int height,
    const std::vector<uint8_t> &albedo, QrSamplerAddressMode addressMode,
    const std::vector<uint8_t> *emission = nullptr, bool metallicEnvAlphaMask = false,
    bool metallicEnvInvertedAlpha = false, bool wetGlossAlphaMask = false,
    bool forceOpaqueAlpha = false)
{
    if (width <= 0 || height <= 0 || albedo.size() < static_cast<size_t>(width) * height * 4) {
        return QR_NO_MATERIAL;
    }

    const size_t pixelCount = static_cast<size_t>(width) * height;
    const bool needsRme = emission != nullptr || metallicEnvAlphaMask || wetGlossAlphaMask;
    bool needsOpaqueAlphaFix = false;
    if (!metallicEnvAlphaMask && !wetGlossAlphaMask && forceOpaqueAlpha) {
        for (size_t i = 0; i < pixelCount; ++i) {
            if (albedo[i * 4 + 3] != 255) {
                needsOpaqueAlphaFix = true;
                break;
            }
        }
    }
    const bool copyAlbedo = metallicEnvAlphaMask || wetGlossAlphaMask || needsOpaqueAlphaFix;
    std::vector<uint8_t> adjustedAlbedo;
    const std::vector<uint8_t> *albedoSource = &albedo;
    if (copyAlbedo) {
        adjustedAlbedo = albedo;
        albedoSource = &adjustedAlbedo;
    }
    std::vector<uint8_t> roughnessMetallicEmission;
    if (needsRme) {
        roughnessMetallicEmission.assign(pixelCount * 4, 0);
    }
    for (size_t i = 0; i < pixelCount; ++i) {
        uint8_t roughness = 200;
        uint8_t metallic = 0;
        if (metallicEnvAlphaMask) {
            const float rawA = static_cast<float>(albedo[i * 4 + 3]) / 255.0f;
            const float envMask = metallicEnvInvertedAlpha ? rawA : (1.0f - rawA);
            roughness = static_cast<uint8_t>(
                std::clamp(0.32f - 0.24f * envMask, 0.08f, 0.32f) * 255.0f);
            metallic = static_cast<uint8_t>(
                std::clamp(0.25f + 0.70f * envMask, 0.0f, 1.0f) * 255.0f);
            // Replace Stage 0 tcGen environment silver specular highlight in chrome regions
            // so bare metal edges are not dark where base lacquer does not cover them.
            const float chromeLift = envMask * 0.5f;
            for (int c = 0; c < 3; ++c) {
                const float v = static_cast<float>(adjustedAlbedo[i * 4 + c]);
                const float lifted = v + (215.0f - v) * chromeLift;
                adjustedAlbedo[i * 4 + c] = static_cast<uint8_t>(std::clamp(lifted, 0.0f, 255.0f));
            }
            adjustedAlbedo[i * 4 + 3] = 255;
        } else if (wetGlossAlphaMask) {
            const float wetMask = 1.0f - (static_cast<float>(albedo[i * 4 + 3]) / 255.0f);
            roughness = static_cast<uint8_t>(
                std::clamp(0.78f - 1.20f * wetMask, 0.14f, 0.78f) * 255.0f);
            adjustedAlbedo[i * 4 + 3] = 255;
        } else if (needsOpaqueAlphaFix) {
            adjustedAlbedo[i * 4 + 3] = 255;
        }
        if (needsRme) {
            roughnessMetallicEmission[i * 4 + 0] = roughness;
            roughnessMetallicEmission[i * 4 + 1] = metallic;
            roughnessMetallicEmission[i * 4 + 2] =
                (emission != nullptr && i < emission->size()) ? (*emission)[i] : 0;
            roughnessMetallicEmission[i * 4 + 3] = 0;
        }
    }

    QrMaterialCreateInfo info{};
    info.size.width = static_cast<uint32_t>(width);
    info.size.height = static_cast<uint32_t>(height);
    info.textures.pDataAlbedoAlpha = albedoSource->data();
    info.textures.pDataRoughnessMetallicEmission =
        needsRme ? roughnessMetallicEmission.data() : nullptr;
    info.textures.pDataNormal = nullptr;
    info.pRelativePath = name;
    info.filter = QR_SAMPLER_FILTER_LINEAR;
    info.addressModeU = addressMode;
    info.addressModeV = addressMode;

    QrMaterial material = QR_NO_MATERIAL;
    const QrResult result = qrCreateMaterial(qrayInstance, &info, &material);
    if (result != QR_SUCCESS) {
        HostLog("CreateMaterialFromPixels: %s failed result=%d", name != nullptr ? name : "(null)",
            static_cast<int>(result));
        return QR_NO_MATERIAL;
    }
    return material;
}

std::string NormalizeShaderName(const std::string &name)
{
    std::string result = name;
    for (char &character : result) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    const size_t slash = result.find_last_of('/');
    const size_t dot = result.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        result.resize(dot);
    }
    return result;
}

QrGeometryMaterialBlendType BlendTypeForFactors(const std::string &source, const std::string &destination)
{
    const bool sourceOne = _stricmp(source.c_str(), "GL_ONE") == 0;
    const bool sourceAlpha = _stricmp(source.c_str(), "GL_SRC_ALPHA") == 0;
    const bool sourceZero = _stricmp(source.c_str(), "GL_ZERO") == 0;
    const bool destinationOne = _stricmp(destination.c_str(), "GL_ONE") == 0;
    const bool destinationInverseColor = _stricmp(destination.c_str(), "GL_ONE_MINUS_SRC_COLOR") == 0;
    const bool destinationInverseAlpha = _stricmp(destination.c_str(), "GL_ONE_MINUS_SRC_ALPHA") == 0;
    if (sourceZero && destinationInverseColor) {
        return QR_GEOMETRY_MATERIAL_BLEND_TYPE_SHADE;
    }
    if (sourceOne && (destinationOne || destinationInverseColor || destinationInverseAlpha)) {
        return QR_GEOMETRY_MATERIAL_BLEND_TYPE_ADD;
    }
    // GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA is regular transparency, not additive glow.
    if (sourceAlpha) {
        return destinationOne ? QR_GEOMETRY_MATERIAL_BLEND_TYPE_ADD
                              : QR_GEOMETRY_MATERIAL_BLEND_TYPE_ALPHA;
    }
    return QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
}

QrGeometryMaterialBlendType BlendTypeForMnemonic(const std::string &mode)
{
    if (_stricmp(mode.c_str(), "add") == 0) {
        return QR_GEOMETRY_MATERIAL_BLEND_TYPE_ADD;
    }
    if (_stricmp(mode.c_str(), "blend") == 0) {
        return QR_GEOMETRY_MATERIAL_BLEND_TYPE_ALPHA;
    }
    if (_stricmp(mode.c_str(), "filter") == 0) {
        return QR_GEOMETRY_MATERIAL_BLEND_TYPE_SHADE;
    }
    return QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
}

// The rasterized overlay reproduces the GL blend equation itself, so the stage factors are kept
// verbatim instead of being collapsed into one of the four material blend classes.
QrBlendFactor BlendFactorForName(const std::string &name, QrBlendFactor fallback)
{
    if (_stricmp(name.c_str(), "GL_ONE") == 0) {
        return QR_BLEND_FACTOR_ONE;
    }
    if (_stricmp(name.c_str(), "GL_ZERO") == 0) {
        return QR_BLEND_FACTOR_ZERO;
    }
    if (_stricmp(name.c_str(), "GL_SRC_COLOR") == 0) {
        return QR_BLEND_FACTOR_SRC_COLOR;
    }
    if (_stricmp(name.c_str(), "GL_ONE_MINUS_SRC_COLOR") == 0) {
        return QR_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    }
    if (_stricmp(name.c_str(), "GL_DST_COLOR") == 0) {
        return QR_BLEND_FACTOR_DST_COLOR;
    }
    if (_stricmp(name.c_str(), "GL_ONE_MINUS_DST_COLOR") == 0) {
        return QR_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    }
    if (_stricmp(name.c_str(), "GL_SRC_ALPHA") == 0) {
        return QR_BLEND_FACTOR_SRC_ALPHA;
    }
    if (_stricmp(name.c_str(), "GL_ONE_MINUS_SRC_ALPHA") == 0) {
        return QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    return fallback;
}

QrBlendFactor BlendFactorForMnemonic(const std::string &mode, bool source)
{
    if (_stricmp(mode.c_str(), "add") == 0) {
        return QR_BLEND_FACTOR_ONE;
    }
    if (_stricmp(mode.c_str(), "blend") == 0) {
        return source ? QR_BLEND_FACTOR_SRC_ALPHA : QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    if (_stricmp(mode.c_str(), "filter") == 0) {
        return source ? QR_BLEND_FACTOR_DST_COLOR : QR_BLEND_FACTOR_ZERO;
    }
    return source ? QR_BLEND_FACTOR_ONE : QR_BLEND_FACTOR_ZERO;
}

// Q3 uses GL_ZERO/GL_ONE_MINUS_SRC_COLOR for impact decals: the result is dst*(1-src.rgb),
// which no QuakeRay blend mode expresses directly. Such textures are converted to a black
// image whose alpha is the source luminance, turning the decal into regular transparency.
bool BlendFactorsAreDarkenDecal(const std::string &source, const std::string &destination)
{
    return _stricmp(source.c_str(), "GL_ZERO") == 0 &&
        _stricmp(destination.c_str(), "GL_ONE_MINUS_SRC_COLOR") == 0;
}

bool IsShaderStageKeyword(const std::string &token)
{
    static const char *keywords[] = {
        "map",
        "clampmap",
        "animmap",
        "blendfunc",
        "alphafunc",
        "alphagen",
        "rgbgen",
        "tcmod",
        "tcgen",
        "depthwrite",
        "depthfunc",
        "detail",
        "deformvertexes",
        "sort",
        "nopicmip",
        "nomipmaps"
    };
    for (const char *keyword : keywords) {
        if (_stricmp(token.c_str(), keyword) == 0) {
            return true;
        }
    }
    return false;
}

ShaderWaveType ParseShaderWave(const std::string &name)
{
    if (_stricmp(name.c_str(), "sin") == 0) {
        return SHADER_WAVE_SIN;
    }
    if (_stricmp(name.c_str(), "square") == 0) {
        return SHADER_WAVE_SQUARE;
    }
    if (_stricmp(name.c_str(), "triangle") == 0) {
        return SHADER_WAVE_TRIANGLE;
    }
    if (_stricmp(name.c_str(), "sawtooth") == 0) {
        return SHADER_WAVE_SAWTOOTH;
    }
    if (_stricmp(name.c_str(), "inversesawtooth") == 0) {
        return SHADER_WAVE_INVERSE_SAWTOOTH;
    }
    if (_stricmp(name.c_str(), "noise") == 0) {
        return SHADER_WAVE_NOISE;
    }
    return SHADER_WAVE_NONE;
}

float EvaluateShaderWaveSigned(ShaderWaveType type, float base, float amplitude, float phase,
    float frequency, float timeSeconds)
{
    if (type == SHADER_WAVE_NONE) {
        return 0.0f;
    }

    const float argument = timeSeconds * frequency + phase;
    float value = 0.0f;
    switch (type) {
    case SHADER_WAVE_SIN:
        value = std::sin(argument * 2.0f * kPi);
        break;
    case SHADER_WAVE_SQUARE:
        value = std::sin(argument * 2.0f * kPi) >= 0.0f ? 1.0f : -1.0f;
        break;
    case SHADER_WAVE_TRIANGLE: {
        const float cycle = argument - std::floor(argument);
        value = cycle < 0.5f ? cycle * 4.0f - 1.0f : 3.0f - cycle * 4.0f;
        break;
    }
    case SHADER_WAVE_SAWTOOTH:
        value = argument - std::floor(argument);
        break;
    case SHADER_WAVE_INVERSE_SAWTOOTH:
        value = 1.0f - (argument - std::floor(argument));
        break;
    case SHADER_WAVE_NOISE: {
        const uint32_t bits = static_cast<uint32_t>(argument * 4096.0f);
        uint32_t hash = bits * 747796405u + 2891336453u;
        hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
        hash = (hash >> 22u) ^ hash;
        value = static_cast<float>(hash & 0x00FFFFFFu) / 16777215.0f;
        break;
    }
    case SHADER_WAVE_NONE:
    default:
        return 0.0f;
    }
    return base + amplitude * value;
}

float EvaluateShaderWave(ShaderWaveType type, float base, float amplitude, float phase,
    float frequency, float timeSeconds)
{
    if (type == SHADER_WAVE_NONE) {
        return 1.0f;
    }
    return std::clamp(
        EvaluateShaderWaveSigned(type, base, amplitude, phase, frequency, timeSeconds),
        0.0f, 8.0f);
}

bool IsWhitePlaceholderMap(const std::string &map)
{
    return _stricmp(map.c_str(), "$whiteimage") == 0 ||
        _stricmp(map.c_str(), "*white") == 0;
}

void ConvertToDarkenDecalTexture(std::vector<uint8_t> &pixels)
{
    for (size_t index = 0; index + 3 < pixels.size(); index += 4) {
        const float luminance = 0.299f * static_cast<float>(pixels[index + 0]) +
            0.587f * static_cast<float>(pixels[index + 1]) +
            0.114f * static_cast<float>(pixels[index + 2]);
        const float scaled = luminance * static_cast<float>(pixels[index + 3]) / 255.0f;
        pixels[index + 0] = 0;
        pixels[index + 1] = 0;
        pixels[index + 2] = 0;
        pixels[index + 3] = static_cast<uint8_t>(std::clamp(scaled, 0.0f, 255.0f));
    }
}

/// Returns true when a shader underlay stage is genuinely emissive/FX rather than static grime.
bool IsEmissiveUnderlayStage(const ShaderStage &stage, float surfaceLight)
{
    if (surfaceLight > 0.0f || stage.animationMaps.size() > 1u) {
        return true;
    }
    const std::string key = NormalizeShaderName(stage.map);
    static const char *kEmissiveTokens[] = {
        "sfx/",
        "effects/",
        "_glow",
        "glow",
        "comp3text",
        "proto_comp",
        "swirl_",
        "protolava",
        "lava",
        "slime",
        "teleport"
    };
    for (const char *token : kEmissiveTokens) {
        if (key.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/// Returns true when a stage mask texture contains binary cutout holes (at least 0.5% alpha < 128).
bool TextureHasBinaryCutoutHoles(const std::string &mapName)
{
    const std::string key = NormalizeShaderName(mapName);
    if (key.empty()) {
        return false;
    }
    const auto existing = binaryCutoutHoleCache.find(key);
    if (existing != binaryCutoutHoleCache.end()) {
        return existing->second;
    }

    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    bool hasHoles = false;
    if (LoadQ3Texture(mapName.c_str(), width, height, pixels) && width > 0 && height > 0) {
        const size_t total = static_cast<size_t>(width) * height;
        size_t lowAlphaCount = 0;
        for (size_t i = 0; i < total && (i * 4 + 3) < pixels.size(); ++i) {
            if (pixels[i * 4 + 3] < 128) {
                ++lowAlphaCount;
            }
        }
        hasHoles = lowAlphaCount * 200u >= total;
    }
    binaryCutoutHoleCache.emplace(key, hasHoles);
    return hasHoles;
}

void LoadShaderScripts()
{
    if (shaderScriptsLoaded) {
        return;
    }
    shaderScriptsLoaded = true;

    static const char *scriptNames[] = {
        "scripts/models.shader",
        "scripts/common.shader",
        "scripts/gfx.shader",
        "scripts/sfx.shader",
        "scripts/sky.shader",
        "scripts/skin.shader",
        "scripts/base.shader",
        "scripts/base_button.shader",
        "scripts/base_floor.shader",
        "scripts/base_light.shader",
        "scripts/base_object.shader",
        "scripts/base_support.shader",
        "scripts/base_trim.shader",
        "scripts/base_wall.shader",
        "scripts/ctf.shader",
        "scripts/eerie.shader",
        "scripts/gothic_block.shader",
        "scripts/gothic_floor.shader",
        "scripts/gothic_light.shader",
        "scripts/gothic_trim.shader",
        "scripts/gothic_wall.shader",
        "scripts/hell.shader",
        "scripts/liquid.shader",
        "scripts/menu.shader",
        "scripts/organics.shader",
        "scripts/shrine.shader",
        "scripts/explode1.shader",
        "scripts/lightningnew.shader",
        "scripts/tim.shader",
        "scripts/test.shader"
    };

    // Retail scripts first, in their established order, then every other scripts/*.shader
    // from the search path (custom maps, mods). Additional scripts only add definitions so
    // they cannot change how retail content resolves.
    std::vector<std::string> scriptList(std::begin(scriptNames), std::end(scriptNames));
    const size_t retailScriptCount = scriptList.size();
    {
        std::unordered_set<std::string> known;
        for (const std::string &name : scriptList) {
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            known.insert(lower);
        }
        int listed = 0;
        char **files = ri.FS_ListFiles != nullptr ? ri.FS_ListFiles("scripts", ".shader", &listed) : nullptr;
        for (int i = 0; files != nullptr && i < listed; ++i) {
            if (files[i] == nullptr || files[i][0] == '\0') {
                continue;
            }
            std::string path = std::string("scripts/") + files[i];
            std::string lower = path;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (known.insert(lower).second) {
                scriptList.push_back(std::move(path));
            }
        }
        if (files != nullptr && ri.FS_FreeFileList != nullptr) {
            ri.FS_FreeFileList(files);
        }
    }

    for (size_t scriptIndex = 0; scriptIndex < scriptList.size(); ++scriptIndex) {
        const char *scriptName = scriptList[scriptIndex].c_str();
        const bool overrideExisting = scriptIndex < retailScriptCount;
        void *fileData = nullptr;
        const long fileLength = ri.FS_ReadFile(scriptName, &fileData);
        if (fileLength <= 0 || fileData == nullptr) {
            continue;
        }

        std::string text(static_cast<const char *>(fileData), static_cast<size_t>(fileLength));
        ri.FS_FreeFile(fileData);

        std::vector<std::string> tokens;
        for (size_t index = 0; index < text.size();) {
            const char character = text[index];
            if (std::isspace(static_cast<unsigned char>(character))) {
                ++index;
                continue;
            }
            if (character == '/' && index + 1 < text.size() && text[index + 1] == '/') {
                while (index < text.size() && text[index] != '\n') {
                    ++index;
                }
                continue;
            }
            if (character == '/' && index + 1 < text.size() && text[index + 1] == '*') {
                index += 2;
                while (index + 1 < text.size() && !(text[index] == '*' && text[index + 1] == '/')) {
                    ++index;
                }
                index = std::min(text.size(), index + 2);
                continue;
            }
            if (character == '{' || character == '}') {
                tokens.emplace_back(1, character);
                ++index;
                continue;
            }
            const size_t start = index;
            while (index < text.size() && !std::isspace(static_cast<unsigned char>(text[index])) &&
                text[index] != '{' && text[index] != '}') {
                ++index;
            }
            tokens.emplace_back(text.substr(start, index - start));
        }

        std::string pendingName;
        std::string currentName;
        const size_t definitionsBefore = shaderDefinitions.size();
        std::string stageMap;
        std::string preferredMap;
        bool stageHasEnvironment = false;
        QrGeometryMaterialBlendType stageBlend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
        QrGeometryMaterialBlendType preferredBlend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
        QrBlendFactor stageBlendSrc = QR_BLEND_FACTOR_ONE;
        QrBlendFactor stageBlendDst = QR_BLEND_FACTOR_ZERO;
        QrBlendFactor preferredBlendSrc = QR_BLEND_FACTOR_ONE;
        QrBlendFactor preferredBlendDst = QR_BLEND_FACTOR_ZERO;
        bool stageDarkenDecal = false;
        bool preferredDarkenDecal = false;
        bool stageAlphaTested = false;
        bool preferredAlphaTested = false;
        bool preferredEnvironment = false;
        bool preferredStageChosen = false;
        size_t preferredStageIndex = static_cast<size_t>(-1);
        bool currentTransparentSurface = false;
        float currentSurfaceLight = 0.0f;
        std::string currentLightImage;
        std::string currentEditorImage;
        bool currentHasLightRGB = false;
        float currentLightRGB[3] = {1.0f, 1.0f, 1.0f};
        bool currentTwoSided = false;
        bool currentIsPortal = false;
        ShaderWaveType currentDeformWave = SHADER_WAVE_NONE;
        float currentDeformSpread = 0.0f;
        float currentDeformBase = 0.0f;
        float currentDeformAmplitude = 0.0f;
        float currentDeformPhase = 0.0f;
        float currentDeformFrequency = 0.0f;
        std::vector<ShaderStage> currentStages;
        ShaderStage currentStage;
        float stageScale[2] = {1.0f, 1.0f};
        int depth = 0;
        // Arguments never extend past a brace. Q3 parses shader keywords per line; this
        // token stream has no line breaks, so a short or malformed line must not consume
        // the closing '}' and desynchronise every later definition in the file.
        const auto argsAvailable = [&tokens](size_t at, size_t count) {
            for (size_t k = 1; k <= count; ++k) {
                if (at + k >= tokens.size() || tokens[at + k] == "{" || tokens[at + k] == "}") {
                    return false;
                }
            }
            return true;
        };
        for (size_t index = 0; index < tokens.size(); ++index) {
            const std::string &token = tokens[index];
            if (token == "{") {
                if (depth == 0) {
                    currentName = pendingName;
                    stageMap.clear();
                    preferredMap.clear();
                    preferredBlend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
                    preferredBlendSrc = QR_BLEND_FACTOR_ONE;
                    preferredBlendDst = QR_BLEND_FACTOR_ZERO;
                    preferredDarkenDecal = false;
                    preferredAlphaTested = false;
                    preferredEnvironment = false;
                    preferredStageChosen = false;
                    preferredStageIndex = static_cast<size_t>(-1);
                    currentTransparentSurface = false;
                    currentSurfaceLight = 0.0f;
                    currentLightImage.clear();
                    currentEditorImage.clear();
                    currentHasLightRGB = false;
                    currentLightRGB[0] = currentLightRGB[1] = currentLightRGB[2] = 1.0f;
                    currentTwoSided = false;
                    currentIsPortal = false;
                    currentDeformWave = SHADER_WAVE_NONE;
                    currentDeformSpread = 0.0f;
                    currentDeformBase = 0.0f;
                    currentDeformAmplitude = 0.0f;
                    currentDeformPhase = 0.0f;
                    currentDeformFrequency = 0.0f;
                    currentStages.clear();
                    depth = 1;
                } else {
                    if (depth == 1) {
                        stageMap.clear();
                        stageHasEnvironment = false;
                        stageBlend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
                        stageBlendSrc = QR_BLEND_FACTOR_ONE;
                        stageBlendDst = QR_BLEND_FACTOR_ZERO;
                        stageDarkenDecal = false;
                        stageAlphaTested = false;
                        currentStage = ShaderStage{};
                        stageScale[0] = 1.0f;
                        stageScale[1] = 1.0f;
                    }
                    ++depth;
                }
                continue;
            }
            if (token == "}") {
                if (depth == 2) {
                    if (!stageMap.empty()) {
                        currentStage.map = stageMap;
                        currentStage.blend = stageBlend;
                        currentStage.blendSrc = stageBlendSrc;
                        currentStage.blendDst = stageBlendDst;
                        currentStage.blended = stageBlend != QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE ||
                            stageBlendSrc != QR_BLEND_FACTOR_ONE ||
                            stageBlendDst != QR_BLEND_FACTOR_ZERO;
                        currentStage.darkenDecal = stageDarkenDecal;
                        currentStage.alphaTested = stageAlphaTested;
                        currentStage.environmentMap = stageHasEnvironment;
                        currentStage.lightmap = _stricmp(stageMap.c_str(), "$lightmap") == 0;
                        currentStage.scale[0] = stageScale[0];
                        currentStage.scale[1] = stageScale[1];
                        currentStage.dynamic = currentStage.dynamic ||
                            currentStage.blended || currentStage.animationMaps.size() > 1u;
                        currentStages.push_back(currentStage);
                        const size_t currentStageIndex = currentStages.size() - 1u;

                        const bool betterBaseStage = !preferredStageChosen ||
                            preferredEnvironment ||
                            (stageBlend == QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE &&
                             preferredBlend != QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE);
                        if (!stageHasEnvironment && !currentStage.lightmap &&
                            !IsWhitePlaceholderMap(stageMap) && betterBaseStage) {
                            preferredMap = stageMap;
                            preferredBlend = stageBlend;
                            preferredBlendSrc = stageBlendSrc;
                            preferredBlendDst = stageBlendDst;
                            preferredDarkenDecal = stageDarkenDecal;
                            preferredEnvironment = false;
                            preferredStageChosen = true;
                            preferredStageIndex = currentStageIndex;
                            // alphaFunc is usually on the base stage while the lightmap stage
                            // that follows clears the flag again, so it has to stick.
                            preferredAlphaTested = preferredAlphaTested || stageAlphaTested;
                        } else if (stageHasEnvironment && !preferredStageChosen) {
                            preferredMap = stageMap;
                            preferredBlend = stageBlend;
                            preferredBlendSrc = stageBlendSrc;
                            preferredBlendDst = stageBlendDst;
                            preferredDarkenDecal = stageDarkenDecal;
                            preferredEnvironment = true;
                            preferredStageChosen = true;
                            preferredStageIndex = currentStageIndex;
                            preferredAlphaTested = preferredAlphaTested || stageAlphaTested;
                        }
                    }
                    stageMap.clear();
                    stageHasEnvironment = false;
                    stageBlend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
                    stageBlendSrc = QR_BLEND_FACTOR_ONE;
                    stageBlendDst = QR_BLEND_FACTOR_ZERO;
                    stageDarkenDecal = false;
                    stageAlphaTested = false;
                    currentStage = ShaderStage{};
                    stageScale[0] = 1.0f;
                    stageScale[1] = 1.0f;
                    depth = 1;
                } else if (depth == 1) {
                    if (!currentName.empty() && !currentStages.empty()) {
                        const bool stage0WhiteOrLightmap = currentStages[0].lightmap ||
                            IsWhitePlaceholderMap(currentStages[0].map);
                        size_t lightmapPreferredIdx = static_cast<size_t>(-1);
                        if (!currentTransparentSurface && stage0WhiteOrLightmap &&
                            currentStages.size() >= 2) {
                            const ShaderStage &s1 = currentStages[1];
                            const bool s1DirectDiffuse = !s1.lightmap && !s1.animated &&
                                !s1.environmentMap && !IsWhitePlaceholderMap(s1.map) &&
                                s1.blendSrc == QR_BLEND_FACTOR_DST_COLOR &&
                                (s1.blendDst == QR_BLEND_FACTOR_ZERO ||
                                 s1.blendDst == QR_BLEND_FACTOR_SRC_ALPHA);
                            if (s1DirectDiffuse) {
                                lightmapPreferredIdx = 1;
                            } else if (currentStages.size() >= 3 &&
                                s1.blendSrc == QR_BLEND_FACTOR_DST_COLOR &&
                                s1.blendDst == QR_BLEND_FACTOR_SRC_COLOR) {
                                for (size_t si = 2; si < currentStages.size(); ++si) {
                                    const ShaderStage &s = currentStages[si];
                                    if (!s.lightmap && !s.animated && !s.environmentMap &&
                                        !IsWhitePlaceholderMap(s.map) &&
                                        s.blendSrc == QR_BLEND_FACTOR_DST_COLOR &&
                                        s.blendDst == QR_BLEND_FACTOR_ZERO) {
                                        lightmapPreferredIdx = si;
                                        break;
                                    }
                                }
                            } else if (currentStages[0].lightmap) {
                                for (size_t si = 1; si < currentStages.size(); ++si) {
                                    const ShaderStage &s = currentStages[si];
                                    if (!s.lightmap && !s.animated && !s.environmentMap &&
                                        !IsWhitePlaceholderMap(s.map) &&
                                        s.blendSrc == QR_BLEND_FACTOR_SRC_ALPHA &&
                                        s.blendDst == QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) {
                                        lightmapPreferredIdx = si;
                                        break;
                                    }
                                }
                            }
                        }
                        if (lightmapPreferredIdx != static_cast<size_t>(-1)) {
                            preferredMap = currentStages[lightmapPreferredIdx].map;
                            preferredBlend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
                            preferredBlendSrc = QR_BLEND_FACTOR_ONE;
                            preferredBlendDst = QR_BLEND_FACTOR_ZERO;
                            preferredEnvironment = false;
                            preferredStageChosen = true;
                            preferredStageIndex = lightmapPreferredIdx;
                            preferredAlphaTested = preferredAlphaTested ||
                                currentStages[lightmapPreferredIdx].alphaTested;
                        }

                        ShaderDefinition definition;
                        definition.map = preferredMap;
                        definition.blend = preferredBlend;
                        definition.blendSrc = preferredBlendSrc;
                        definition.blendDst = preferredBlendDst;
                        definition.darkenDecal = preferredDarkenDecal;
                        definition.alphaTested = preferredAlphaTested;
                        definition.environmentMap = preferredEnvironment;
                        definition.transparentSurface = currentTransparentSurface;
                        definition.surfaceLight = currentSurfaceLight;
                        definition.lightImage = currentLightImage;
                        definition.editorImage = currentEditorImage;
                        definition.hasLightRGB = currentHasLightRGB;
                        std::memcpy(definition.lightRGB, currentLightRGB, sizeof(currentLightRGB));
                        definition.twoSided = currentTwoSided;
                        definition.isPortal = currentIsPortal;
                        definition.deformWave = currentDeformWave;
                        definition.deformSpread = currentDeformSpread;
                        definition.deformBase = currentDeformBase;
                        definition.deformAmplitude = currentDeformAmplitude;
                        definition.deformPhase = currentDeformPhase;
                        definition.deformFrequency = currentDeformFrequency;
                        definition.baseStageIndex = preferredStageIndex;
                        const bool baseStageAnimated = preferredStageIndex < currentStages.size() &&
                            currentStages[preferredStageIndex].animated;
                        definition.tracedBase = preferredStageIndex < currentStages.size() &&
                            !currentTransparentSurface && !baseStageAnimated &&
                            (preferredBlend == QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE ||
                             preferredAlphaTested);
                        definition.stages = currentStages;

                        bool hasLightmapStage = false;
                        for (const ShaderStage &stage : currentStages) {
                            if (stage.lightmap) {
                                hasLightmapStage = true;
                                break;
                            }
                        }
                        if (!currentTransparentSurface &&
                            (hasLightmapStage ||
                             _strnicmp(currentName.c_str(), "models/mapobjects/", 18) == 0) &&
                            currentStages.size() >= 2 && !currentStages[0].blended &&
                            currentStages[0].animated && !currentStages[0].environmentMap &&
                            !currentStages[0].lightmap) {
                            size_t frameIdx = static_cast<size_t>(-1);
                            for (size_t si = 1; si < currentStages.size(); ++si) {
                                const ShaderStage &stage = currentStages[si];
                                if (!stage.lightmap && !stage.animated && !stage.environmentMap &&
                                    stage.blendSrc == QR_BLEND_FACTOR_SRC_ALPHA &&
                                    stage.blendDst == QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) {
                                    frameIdx = si;
                                }
                            }
                            if (frameIdx != static_cast<size_t>(-1)) {
                                if (IsEmissiveUnderlayStage(currentStages[0], currentSurfaceLight) &&
                                    TextureHasBinaryCutoutHoles(currentStages[frameIdx].map)) {
                                    definition.cutoutFrameUnderlay = true;
                                    definition.baseStageIndex = frameIdx;
                                    definition.map = currentStages[frameIdx].map;
                                    definition.blend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
                                    definition.blendSrc = QR_BLEND_FACTOR_ONE;
                                    definition.blendDst = QR_BLEND_FACTOR_ZERO;
                                    definition.alphaTested = true;
                                    definition.environmentMap = false;
                                    definition.tracedBase = true;
                                    definition.usesOverlay = true;
                                } else {
                                    definition.compositeUnderlayBase = true;
                                    definition.underlayMap = currentStages[0].map;
                                    definition.underlayScale[0] = currentStages[0].scale[0];
                                    definition.underlayScale[1] = currentStages[0].scale[1];
                                    definition.underlayEmissive = false;
                                    definition.map = currentStages[frameIdx].map;
                                    definition.blend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
                                    definition.blendSrc = QR_BLEND_FACTOR_ONE;
                                    definition.blendDst = QR_BLEND_FACTOR_ZERO;
                                    definition.alphaTested = false;
                                    definition.environmentMap = false;
                                    definition.baseStageIndex = frameIdx;
                                    definition.tracedBase = true;
                                    definition.usesOverlay = false;
                                }
                            }
                        }

                        if (!definition.cutoutFrameUnderlay && !definition.compositeUnderlayBase &&
                            !currentTransparentSurface && hasLightmapStage &&
                            currentStages.size() >= 3 && !currentStages[0].blended &&
                            !currentStages[0].animated && !currentStages[0].environmentMap &&
                            !currentStages[0].lightmap) {
                            const std::string baseKey = NormalizeShaderName(currentStages[0].map);
                            size_t maskIdx = static_cast<size_t>(-1);
                            bool allIntermediateAdditive = true;
                            for (size_t si = 1; si < currentStages.size(); ++si) {
                                const ShaderStage &stage = currentStages[si];
                                if (!stage.lightmap && !stage.animated && !stage.environmentMap &&
                                    stage.blendSrc == QR_BLEND_FACTOR_SRC_ALPHA &&
                                    stage.blendDst == QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA &&
                                    NormalizeShaderName(stage.map) == baseKey) {
                                    maskIdx = si;
                                    break;
                                }
                                const bool stageAdditive =
                                    stage.blend == QR_GEOMETRY_MATERIAL_BLEND_TYPE_ADD ||
                                    (stage.blendSrc == QR_BLEND_FACTOR_ONE &&
                                     stage.blendDst == QR_BLEND_FACTOR_ONE) ||
                                    (stage.blendSrc == QR_BLEND_FACTOR_SRC_ALPHA &&
                                     stage.blendDst == QR_BLEND_FACTOR_ONE);
                                if (!stageAdditive) {
                                    allIntermediateAdditive = false;
                                    break;
                                }
                            }
                            if (maskIdx != static_cast<size_t>(-1) && maskIdx >= 2 &&
                                allIntermediateAdditive) {
                                if (TextureHasBinaryCutoutHoles(currentStages[maskIdx].map)) {
                                    definition.cutoutFrameUnderlay = true;
                                    definition.baseStageIndex = maskIdx;
                                    definition.map = currentStages[maskIdx].map;
                                    definition.blend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
                                    definition.blendSrc = QR_BLEND_FACTOR_ONE;
                                    definition.blendDst = QR_BLEND_FACTOR_ZERO;
                                    definition.alphaTested = true;
                                    definition.environmentMap = false;
                                    definition.tracedBase = true;
                                    definition.usesOverlay = true;
                                } else {
                                    definition.wetGlossAlphaMask = true;
                                    definition.baseStageIndex = 0;
                                    definition.map = currentStages[0].map;
                                    definition.blend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
                                    definition.blendSrc = QR_BLEND_FACTOR_ONE;
                                    definition.blendDst = QR_BLEND_FACTOR_ZERO;
                                    definition.alphaTested = false;
                                    definition.environmentMap = false;
                                    definition.tracedBase = true;
                                    definition.usesOverlay = false;
                                }
                            }
                        }

                        if (!definition.cutoutFrameUnderlay && !definition.compositeUnderlayBase &&
                            !definition.wetGlossAlphaMask && currentStages.size() >= 2 &&
                            currentStages[0].environmentMap && !currentStages[0].blended) {
                            const auto isAlphaLayer = [](const ShaderStage &stage, bool &inverted) {
                                if (stage.lightmap || stage.environmentMap || stage.animated) {
                                    return false;
                                }
                                if (stage.blendSrc == QR_BLEND_FACTOR_SRC_ALPHA &&
                                    stage.blendDst == QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) {
                                    inverted = false;
                                    return true;
                                }
                                if (stage.blendSrc == QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA &&
                                    stage.blendDst == QR_BLEND_FACTOR_SRC_ALPHA) {
                                    inverted = true;
                                    return true;
                                }
                                return false;
                            };
                            size_t diffuseIdx = static_cast<size_t>(-1);
                            bool diffuseInverted = false;
                            for (size_t si = 1; si < currentStages.size(); ++si) {
                                if (isAlphaLayer(currentStages[si], diffuseInverted)) {
                                    diffuseIdx = si;
                                    break;
                                }
                            }
                            if (diffuseIdx != static_cast<size_t>(-1)) {
                                definition.metallicEnvBase = true;
                                definition.metallicEnvInvertedAlpha = diffuseInverted;
                                definition.map = currentStages[diffuseIdx].map;
                                definition.blend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
                                definition.blendSrc = QR_BLEND_FACTOR_ONE;
                                definition.blendDst = QR_BLEND_FACTOR_ZERO;
                                definition.environmentMap = false;
                                definition.baseStageIndex = diffuseIdx;
                                definition.tracedBase = !currentTransparentSurface;
                                definition.usesOverlay = false;
                                bool seenLightmap = false;
                                for (size_t si = diffuseIdx + 1; si < currentStages.size(); ++si) {
                                    const ShaderStage &stage = currentStages[si];
                                    if (stage.lightmap) {
                                        seenLightmap = true;
                                        continue;
                                    }
                                    bool decalInverted = false;
                                    if (!seenLightmap && isAlphaLayer(stage, decalInverted)) {
                                        definition.decalOverlayMaps.push_back(stage.map);
                                        definition.decalOverlayInvertedAlpha.push_back(decalInverted);
                                        continue;
                                    }
                                    const bool stageAdditive =
                                        stage.blend == QR_GEOMETRY_MATERIAL_BLEND_TYPE_ADD ||
                                        (stage.blendSrc == QR_BLEND_FACTOR_ONE &&
                                         stage.blendDst == QR_BLEND_FACTOR_ONE) ||
                                        (stage.blendSrc == QR_BLEND_FACTOR_SRC_ALPHA &&
                                         stage.blendDst == QR_BLEND_FACTOR_ONE);
                                    if (stageAdditive) {
                                        definition.usesOverlay = true;
                                    }
                                }
                            }
                        }

                        if (!definition.cutoutFrameUnderlay && !definition.metallicEnvBase &&
                            !definition.wetGlossAlphaMask &&
                            !hasLightmapStage && currentStages.size() >= 2 &&
                            !currentStages[0].blended && !currentStages[0].lightmap &&
                            !IsWhitePlaceholderMap(currentStages[0].map)) {
                            size_t diffuseIdx = static_cast<size_t>(-1);
                            for (size_t si = 1; si < currentStages.size(); ++si) {
                                const ShaderStage &stage = currentStages[si];
                                if (!stage.lightmap && !stage.environmentMap && !stage.animated &&
                                    stage.blendSrc == QR_BLEND_FACTOR_SRC_ALPHA &&
                                    stage.blendDst == QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) {
                                    diffuseIdx = si;
                                }
                            }
                            if (diffuseIdx != static_cast<size_t>(-1)) {
                                definition.compositeUnderlayBase = true;
                                definition.underlayMap = currentStages[0].map;
                                definition.underlayScale[0] = currentStages[0].scale[0];
                                definition.underlayScale[1] = currentStages[0].scale[1];
                                definition.underlayEmissive = currentStages[0].animated &&
                                    !currentStages[0].environmentMap;
                                definition.map = currentStages[diffuseIdx].map;
                                definition.blend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
                                definition.blendSrc = QR_BLEND_FACTOR_ONE;
                                definition.blendDst = QR_BLEND_FACTOR_ZERO;
                                definition.environmentMap = false;
                                definition.baseStageIndex = diffuseIdx;
                                definition.tracedBase = !currentTransparentSurface;
                                definition.usesOverlay = false;
                            }
                        }

                        if (!definition.metallicEnvBase && !definition.compositeUnderlayBase &&
                            !definition.wetGlossAlphaMask) {
                            for (size_t stageIndex = 0; stageIndex < currentStages.size(); ++stageIndex) {
                                const ShaderStage &stage = currentStages[stageIndex];
                                const bool baseStage = definition.tracedBase &&
                                    stageIndex == definition.baseStageIndex;
                                if (!baseStage && (stage.blended || stage.dynamic)) {
                                    definition.usesOverlay = true;
                                }
                                definition.dynamic = definition.dynamic || stage.dynamic;
                            }
                            if (!definition.tracedBase) {
                                definition.usesOverlay = true;
                            }
                        }

                        if (definition.tracedBase &&
                            definition.baseStageIndex < currentStages.size()) {
                            definition.baseScale[0] =
                                currentStages[definition.baseStageIndex].scale[0];
                            definition.baseScale[1] =
                                currentStages[definition.baseStageIndex].scale[1];
                        }

                        bool anyNonLightmapStage = false;
                        bool allNonLightmapBlended = true;
                        for (const ShaderStage &stage : currentStages) {
                            if (stage.lightmap) {
                                continue;
                            }
                            anyNonLightmapStage = true;
                            if (!stage.blended) {
                                allNonLightmapBlended = false;
                                break;
                            }
                        }
                        definition.hasOnlyBlendedStages = !definition.tracedBase &&
                            !definition.metallicEnvBase && !definition.compositeUnderlayBase &&
                            !definition.wetGlossAlphaMask && anyNonLightmapStage &&
                            allNonLightmapBlended;

                        if (overrideExisting) {
                            shaderDefinitions[NormalizeShaderName(currentName)] = std::move(definition);
                        } else {
                            shaderDefinitions.emplace(NormalizeShaderName(currentName), std::move(definition));
                        }
                    }
                    currentName.clear();
                    currentDeformWave = SHADER_WAVE_NONE;
                    currentDeformSpread = 0.0f;
                    currentDeformBase = 0.0f;
                    currentDeformAmplitude = 0.0f;
                    currentDeformPhase = 0.0f;
                    currentDeformFrequency = 0.0f;
                    currentStages.clear();
                    depth = 0;
                } else if (depth > 2) {
                    --depth;
                }
                continue;
            }
            if (depth == 0) {
                pendingName = token;
                continue;
            }
            if (depth == 1) {
                if (_strnicmp(token.c_str(), "q3map_sun", 9) == 0 && argsAvailable(index, 6)) {
                    index += 6;
                } else if (_stricmp(token.c_str(), "skyparms") == 0 && argsAvailable(index, 3)) {
                    index += 3;
                } else if (_stricmp(token.c_str(), "q3map_surfacelight") == 0 && argsAvailable(index, 1)) {
                    currentSurfaceLight = std::max(0.0f, static_cast<float>(std::atof(tokens[index + 1].c_str())));
                    index += 1;
                } else if (_stricmp(token.c_str(), "q3map_lightimage") == 0 && argsAvailable(index, 1)) {
                    currentLightImage = tokens[index + 1];
                    index += 1;
                } else if (_stricmp(token.c_str(), "qer_editorimage") == 0 && argsAvailable(index, 1)) {
                    currentEditorImage = tokens[index + 1];
                    index += 1;
                } else if (_stricmp(token.c_str(), "q3map_lightRGB") == 0 && argsAvailable(index, 3)) {
                    currentHasLightRGB = true;
                    for (int c = 0; c < 3; ++c) {
                        currentLightRGB[c] = std::max(0.0f, static_cast<float>(std::atof(tokens[index + 1 + c].c_str())));
                    }
                    index += 3;
                } else if (_stricmp(token.c_str(), "cull") == 0 && argsAvailable(index, 1)) {
                    const char *mode = tokens[index + 1].c_str();
                    currentTwoSided = _stricmp(mode, "none") == 0 || _stricmp(mode, "disable") == 0 ||
                        _stricmp(mode, "twosided") == 0;
                    index += 1;
                } else if (_stricmp(token.c_str(), "portal") == 0) {
                    currentIsPortal = true;
                } else if (_stricmp(token.c_str(), "surfaceparm") == 0 && argsAvailable(index, 1)) {
                    if (_stricmp(tokens[index + 1].c_str(), "trans") == 0) {
                        currentTransparentSurface = true;
                    }
                    index += 1;
                } else if (_stricmp(token.c_str(), "deformVertexes") == 0 && argsAvailable(index, 1)) {
                    const std::string &mode = tokens[index + 1];
                    if (_stricmp(mode.c_str(), "wave") == 0 && argsAvailable(index, 7)) {
                        const float div = static_cast<float>(std::atof(tokens[index + 2].c_str()));
                        const ShaderWaveType wave = ParseShaderWave(tokens[index + 3]);
                        if (wave != SHADER_WAVE_NONE) {
                            currentDeformWave = wave;
                            currentDeformSpread = div != 0.0f ? (1.0f / div) : 100.0f;
                            currentDeformBase = static_cast<float>(std::atof(tokens[index + 4].c_str()));
                            currentDeformAmplitude = static_cast<float>(std::atof(tokens[index + 5].c_str()));
                            currentDeformPhase = static_cast<float>(std::atof(tokens[index + 6].c_str()));
                            currentDeformFrequency = static_cast<float>(std::atof(tokens[index + 7].c_str()));
                        }
                        index += 7;
                    } else if (_stricmp(mode.c_str(), "move") == 0 && argsAvailable(index, 9)) {
                        index += 9;
                    } else if (_stricmp(mode.c_str(), "bulge") == 0 && argsAvailable(index, 4)) {
                        index += 4;
                    } else if (_stricmp(mode.c_str(), "normal") == 0 && argsAvailable(index, 3)) {
                        index += 3;
                    } else {
                        index += 1;
                    }
                }
                continue;
            }
            if (depth == 2) {
                if (_stricmp(token.c_str(), "tcGen") == 0 && argsAvailable(index, 1) &&
                    _stricmp(tokens[index + 1].c_str(), "environment") == 0) {
                    stageHasEnvironment = true;
                    currentStage.environmentMap = true;
                    ++index;
                } else if (_stricmp(token.c_str(), "blendFunc") == 0 && argsAvailable(index, 1)) {
                    const std::string &first = tokens[index + 1];
                    // blendFunc either takes a factor pair (GL_ONE GL_ONE) or a single
                    // mnemonic (add/blend/filter); consuming the wrong count eats a brace
                    // and desynchronises the rest of the file.
                    if (_strnicmp(first.c_str(), "GL_", 3) == 0 && argsAvailable(index, 2)) {
                        stageBlend = BlendTypeForFactors(first, tokens[index + 2]);
                        stageBlendSrc = BlendFactorForName(first, QR_BLEND_FACTOR_ONE);
                        stageBlendDst = BlendFactorForName(tokens[index + 2], QR_BLEND_FACTOR_ZERO);
                        stageDarkenDecal = BlendFactorsAreDarkenDecal(first, tokens[index + 2]);
                        currentStage.blend = stageBlend;
                        currentStage.blendSrc = stageBlendSrc;
                        currentStage.blendDst = stageBlendDst;
                        currentStage.blended = stageBlend != QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE ||
                            stageBlendSrc != QR_BLEND_FACTOR_ONE ||
                            stageBlendDst != QR_BLEND_FACTOR_ZERO;
                        currentStage.darkenDecal = stageDarkenDecal;
                        index += 2;
                    } else {
                        stageBlend = BlendTypeForMnemonic(first);
                        stageBlendSrc = BlendFactorForMnemonic(first, true);
                        stageBlendDst = BlendFactorForMnemonic(first, false);
                        currentStage.blend = stageBlend;
                        currentStage.blendSrc = stageBlendSrc;
                        currentStage.blendDst = stageBlendDst;
                        currentStage.blended = stageBlend != QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE ||
                            stageBlendSrc != QR_BLEND_FACTOR_ONE ||
                            stageBlendDst != QR_BLEND_FACTOR_ZERO;
                        ++index;
                    }
                } else if (_stricmp(token.c_str(), "alphaFunc") == 0 && argsAvailable(index, 1)) {
                    // Q3 cuts these stages out of the rasterized surface (grates, foliage,
                    // chains). QuakeRay has a dedicated alpha tested hit group for that.
                    stageAlphaTested = true;
                    currentStage.alphaTested = true;
                    // alphaFunc uses one comparison token (GT0, GE128, ...). Consuming two
                    // swallowed the stage's closing brace and desynchronised every following
                    // shader definition in the file.
                    ++index;
                } else if (_stricmp(token.c_str(), "map") == 0 || _stricmp(token.c_str(), "clampmap") == 0) {
                    if (argsAvailable(index, 1)) {
                        stageMap = tokens[index + 1];
                        currentStage.map = stageMap;
                        currentStage.clampMap = _stricmp(token.c_str(), "clampmap") == 0;
                        currentStage.lightmap = _stricmp(stageMap.c_str(), "$lightmap") == 0;
                        ++index;
                    }
                } else if (_stricmp(token.c_str(), "animMap") == 0 && argsAvailable(index, 2)) {
                    currentStage.animationFps = std::max(
                        static_cast<float>(std::atof(tokens[index + 1].c_str())), 0.001f);
                    currentStage.animationMaps.clear();
                    size_t frameIndex = index + 2;
                    while (frameIndex < tokens.size() && tokens[frameIndex] != "}" &&
                        !IsShaderStageKeyword(tokens[frameIndex])) {
                        currentStage.animationMaps.push_back(tokens[frameIndex]);
                        ++frameIndex;
                    }
                    if (!currentStage.animationMaps.empty()) {
                        stageMap = currentStage.animationMaps.front();
                        currentStage.map = stageMap;
                        currentStage.lightmap = _stricmp(stageMap.c_str(), "$lightmap") == 0;
                    }
                    currentStage.dynamic = currentStage.dynamic ||
                        currentStage.animationMaps.size() > 1u;
                    currentStage.animated = currentStage.animated ||
                        currentStage.animationMaps.size() > 1u;
                    index = frameIndex - 1u;
                } else if (_stricmp(token.c_str(), "tcMod") == 0 && argsAvailable(index, 1)) {
                    // tcMod sub-commands take a variable number of arguments, so only the
                    // ones that matter here are consumed; eating a fixed count would
                    // swallow the closing brace of the stage.
                    const char *subCommand = tokens[index + 1].c_str();
                    if (_stricmp(subCommand, "scale") == 0 && argsAvailable(index, 3)) {
                        float sx = static_cast<float>(std::atof(tokens[index + 2].c_str()));
                        float sy = static_cast<float>(std::atof(tokens[index + 3].c_str()));
                        if (sx == 0.0f) {
                            sx = 1.0f;
                        }
                        if (sy == 0.0f) {
                            sy = 1.0f;
                        }
                        stageScale[0] = sx;
                        stageScale[1] = sy;
                        ShaderTcMod mod{};
                        mod.type = TCMOD_SCALE;
                        mod.params[0] = sx;
                        mod.params[1] = sy;
                        currentStage.tcMods.push_back(mod);
                        index += 3;
                    } else if (_stricmp(subCommand, "scroll") == 0 && argsAvailable(index, 3)) {
                        const float su = static_cast<float>(std::atof(tokens[index + 2].c_str()));
                        const float sv = static_cast<float>(std::atof(tokens[index + 3].c_str()));
                        currentStage.scroll[0] += su;
                        currentStage.scroll[1] += sv;
                        ShaderTcMod mod{};
                        mod.type = TCMOD_SCROLL;
                        mod.params[0] = su;
                        mod.params[1] = sv;
                        currentStage.tcMods.push_back(mod);
                        currentStage.dynamic = true;
                        currentStage.animated = true;
                        index += 3;
                    } else if (_stricmp(subCommand, "rotate") == 0 && argsAvailable(index, 2)) {
                        ShaderTcMod mod{};
                        mod.type = TCMOD_ROTATE;
                        mod.params[0] = static_cast<float>(std::atof(tokens[index + 2].c_str()));
                        currentStage.tcMods.push_back(mod);
                        currentStage.dynamic = true;
                        currentStage.animated = true;
                        index += 2;
                    } else if (_stricmp(subCommand, "stretch") == 0 && argsAvailable(index, 6)) {
                        const ShaderWaveType wave = ParseShaderWave(tokens[index + 2]);
                        if (wave != SHADER_WAVE_NONE) {
                            ShaderTcMod mod{};
                            mod.type = TCMOD_STRETCH;
                            mod.wave = wave;
                            mod.params[0] = static_cast<float>(std::atof(tokens[index + 3].c_str()));
                            mod.params[1] = static_cast<float>(std::atof(tokens[index + 4].c_str()));
                            mod.params[2] = static_cast<float>(std::atof(tokens[index + 5].c_str()));
                            mod.params[3] = static_cast<float>(std::atof(tokens[index + 6].c_str()));
                            currentStage.tcMods.push_back(mod);
                            currentStage.dynamic = true;
                            currentStage.animated = true;
                        }
                        index += 6;
                    } else if (_stricmp(subCommand, "turb") == 0 && argsAvailable(index, 5)) {
                        ShaderTcMod mod{};
                        mod.type = TCMOD_TURB;
                        mod.params[0] = static_cast<float>(std::atof(tokens[index + 2].c_str()));
                        mod.params[1] = static_cast<float>(std::atof(tokens[index + 3].c_str()));
                        mod.params[2] = static_cast<float>(std::atof(tokens[index + 4].c_str()));
                        mod.params[3] = static_cast<float>(std::atof(tokens[index + 5].c_str()));
                        currentStage.tcMods.push_back(mod);
                        currentStage.dynamic = true;
                        currentStage.animated = true;
                        index += 5;
                    } else if (_stricmp(subCommand, "entityTranslate") == 0) {
                        currentStage.dynamic = true;
                        index += 1;
                    }
                } else if ((_stricmp(token.c_str(), "rgbGen") == 0 ||
                    _stricmp(token.c_str(), "alphaGen") == 0) &&
                    index + 1 < tokens.size() &&
                    tokens[index + 1] != "{" && tokens[index + 1] != "}") {
                    const bool isAlpha = _stricmp(token.c_str(), "alphaGen") == 0;
                    const std::string &mode = tokens[index + 1];
                    if (_stricmp(mode.c_str(), "wave") == 0 && argsAvailable(index, 6)) {
                        const ShaderWaveType wave = ParseShaderWave(tokens[index + 2]);
                        if (wave != SHADER_WAVE_NONE) {
                            ShaderWaveType &destination = isAlpha ? currentStage.alphaWave
                                                                  : currentStage.rgbWave;
                            float &base = isAlpha ? currentStage.alphaWaveBase
                                                  : currentStage.rgbWaveBase;
                            float &amplitude = isAlpha ? currentStage.alphaWaveAmplitude
                                                       : currentStage.rgbWaveAmplitude;
                            float &phase = isAlpha ? currentStage.alphaWavePhase
                                                   : currentStage.rgbWavePhase;
                            float &frequency = isAlpha ? currentStage.alphaWaveFrequency
                                                       : currentStage.rgbWaveFrequency;
                            destination = wave;
                            base = static_cast<float>(std::atof(tokens[index + 3].c_str()));
                            amplitude = static_cast<float>(std::atof(tokens[index + 4].c_str()));
                            phase = static_cast<float>(std::atof(tokens[index + 5].c_str()));
                            frequency = static_cast<float>(std::atof(tokens[index + 6].c_str()));
                            if (isAlpha) {
                                currentStage.alphaGen = ALPHAGEN_WAVE;
                            } else {
                                currentStage.rgbGen = COLORGEN_WAVE;
                            }
                            currentStage.dynamic = true;
                            currentStage.animated = true;
                        }
                        index += 6;
                    } else if (!isAlpha && (_stricmp(mode.c_str(), "vertex") == 0 ||
                                            _stricmp(mode.c_str(), "exactVertex") == 0)) {
                        currentStage.rgbGen = COLORGEN_VERTEX;
                        ++index;
                    } else if (!isAlpha && _stricmp(mode.c_str(), "entity") == 0) {
                        currentStage.rgbGen = COLORGEN_ENTITY;
                        ++index;
                    } else if (!isAlpha && (_stricmp(mode.c_str(), "identity") == 0 ||
                                            _stricmp(mode.c_str(), "identityLighting") == 0 ||
                                            _stricmp(mode.c_str(), "lightingDiffuse") == 0)) {
                        currentStage.rgbGen = COLORGEN_IDENTITY;
                        ++index;
                    } else if (isAlpha && _stricmp(mode.c_str(), "vertex") == 0) {
                        currentStage.alphaGen = ALPHAGEN_VERTEX;
                        ++index;
                    } else if (isAlpha && _stricmp(mode.c_str(), "entity") == 0) {
                        currentStage.alphaGen = ALPHAGEN_ENTITY;
                        ++index;
                    } else if (isAlpha && _stricmp(mode.c_str(), "identity") == 0) {
                        currentStage.alphaGen = ALPHAGEN_IDENTITY;
                        ++index;
                    } else if (_stricmp(mode.c_str(), "const") == 0) {
                        size_t skip = index + 2;
                        while (skip < tokens.size() && tokens[skip] != "}" &&
                            tokens[skip] != "{" && !IsShaderStageKeyword(tokens[skip])) {
                            if (tokens[skip].find(')') != std::string::npos) {
                                ++skip;
                                break;
                            }
                            ++skip;
                        }
                        index = skip - 1u;
                    } else {
                        ++index;
                    }
                }
            }
        }

        if (qrayDebugEntities != nullptr && qrayDebugEntities->integer != 0) {
            HostLog("LoadShaderScripts: %s definitions=%zu total=%zu", scriptName,
                shaderDefinitions.size() - definitionsBefore, shaderDefinitions.size());
        }
    }

    if (hostLogEnabled) {
        size_t alphaTestedCount = 0;
        std::string alphaTestedNames;
        for (const auto &entry : shaderDefinitions) {
            if (entry.second.alphaTested) {
                ++alphaTestedCount;
                if (alphaTestedCount <= 64) {
                    alphaTestedNames += entry.first;
                    alphaTestedNames += ' ';
                }
            }
        }
        HostLog("LoadShaderScripts: definitions=%zu alphaTested=%zu [%s]",
            shaderDefinitions.size(), alphaTestedCount, alphaTestedNames.c_str());
        static const char *reportedShaders[] = {
            "textures/sfx/launchpad_diamond",
            "textures/sfx/diamond2cjumppad",
            "models/mapobjects/spotlamp/beam",
        };
        for (const char *reportedShader : reportedShaders) {
            const auto definitionIt = shaderDefinitions.find(NormalizeShaderName(reportedShader));
            HostLog("LoadShaderScripts: probe shader='%s' found=%d map='%s' stages=%zu overlay=%d",
                reportedShader, definitionIt != shaderDefinitions.end() ? 1 : 0,
                definitionIt != shaderDefinitions.end() ? definitionIt->second.map.c_str() : "",
                definitionIt != shaderDefinitions.end() ? definitionIt->second.stages.size() : 0u,
                definitionIt != shaderDefinitions.end() && definitionIt->second.usesOverlay ? 1 : 0);
        }
    }

}

// q3map_surfacelight shaders carry the lit part of the fixture either in an additive stage
// (base_light "*.blend" textures) or in the base texture itself. That mask becomes the
// material's emission channel so the light surface glows in the traced image.
bool BuildSurfaceLightEmission(const ShaderDefinition &definition, int width, int height,
    const std::vector<uint8_t> &albedo, std::vector<uint8_t> &emission)
{
    if (definition.surfaceLight <= 0.0f || qraySurfaceLightGlow == nullptr ||
        qraySurfaceLightGlow->integer == 0 || width <= 0 || height <= 0) {
        return false;
    }
    const size_t pixelCount = static_cast<size_t>(width) * height;
    emission.assign(pixelCount, 0);

    std::string maskName;
    for (const ShaderStage &stage : definition.stages) {
        const bool additive = stage.blend == QR_GEOMETRY_MATERIAL_BLEND_TYPE_ADD ||
            (stage.blendSrc == QR_BLEND_FACTOR_ONE && stage.blendDst == QR_BLEND_FACTOR_ONE);
        if (additive && !stage.lightmap && !stage.environmentMap && !stage.map.empty() &&
            _stricmp(stage.map.c_str(), definition.map.c_str()) != 0) {
            maskName = stage.map;
            break;
        }
    }

    int maskWidth = 0;
    int maskHeight = 0;
    std::vector<uint8_t> mask;
    if (!maskName.empty() && LoadQ3Texture(maskName.c_str(), maskWidth, maskHeight, mask)) {
        for (int y = 0; y < height; ++y) {
            const int my = std::min(maskHeight - 1, y * maskHeight / height);
            for (int x = 0; x < width; ++x) {
                const int mx = std::min(maskWidth - 1, x * maskWidth / width);
                const uint8_t *texel = &mask[(static_cast<size_t>(my) * maskWidth + mx) * 4];
                emission[static_cast<size_t>(y) * width + x] = std::max({texel[0], texel[1], texel[2]});
            }
        }
        return true;
    }

    // No separate glow stage: the bright texels of the base texture are the light. The
    // squared response keeps dark frames and trims from glowing.
    for (size_t i = 0; i < pixelCount && i * 4 + 2 < albedo.size(); ++i) {
        const float value = std::max({albedo[i * 4 + 0], albedo[i * 4 + 1], albedo[i * 4 + 2]}) / 255.0f;
        emission[i] = static_cast<uint8_t>(std::clamp(value * value * 255.0f, 0.0f, 255.0f));
    }
    return true;
}

void CompositeUnderlayTexture(const ShaderDefinition &def, int width, int height,
    std::vector<uint8_t> &albedo, std::vector<uint8_t> &compositeEmission)
{
    if (!def.compositeUnderlayBase || width <= 0 || height <= 0) {
        return;
    }
    int uw = 0;
    int uh = 0;
    std::vector<uint8_t> underlayPixels;
    if (!LoadQ3Texture(def.underlayMap.c_str(), uw, uh, underlayPixels) || uw <= 0 || uh <= 0) {
        return;
    }

    float meanUnderlay[3] = {0.0f, 0.0f, 0.0f};
    const size_t uCount = static_cast<size_t>(uw) * uh;
    for (size_t ui = 0; ui < uCount && ui * 4 + 2 < underlayPixels.size(); ++ui) {
        meanUnderlay[0] += static_cast<float>(underlayPixels[ui * 4 + 0]);
        meanUnderlay[1] += static_cast<float>(underlayPixels[ui * 4 + 1]);
        meanUnderlay[2] += static_cast<float>(underlayPixels[ui * 4 + 2]);
    }
    if (uCount > 0) {
        const float invCount = 1.0f / static_cast<float>(uCount);
        meanUnderlay[0] *= invCount;
        meanUnderlay[1] *= invCount;
        meanUnderlay[2] *= invCount;
    }

    const float sx = def.underlayScale[0] != 0.0f ? def.underlayScale[0] : 1.0f;
    const float sy = def.underlayScale[1] != 0.0f ? def.underlayScale[1] : 1.0f;
    if (def.underlayEmissive) {
        compositeEmission.assign(static_cast<size_t>(width) * height, 0);
    }

    for (int y = 0; y < height; ++y) {
        const float vf = (static_cast<float>(y) + 0.5f) / static_cast<float>(height) * sy;
        const float wrappedV = vf - std::floor(vf);
        const int uy = std::clamp(
            static_cast<int>(std::floor(wrappedV * static_cast<float>(uh))), 0, uh - 1);
        for (int x = 0; x < width; ++x) {
            const float uf = (static_cast<float>(x) + 0.5f) / static_cast<float>(width) * sx;
            const float wrappedU = uf - std::floor(uf);
            const int ux = std::clamp(
                static_cast<int>(std::floor(wrappedU * static_cast<float>(uw))), 0, uw - 1);
            const size_t idx = static_cast<size_t>(y) * width + x;
            const size_t uidx = static_cast<size_t>(uy) * uw + ux;
            const float a = static_cast<float>(albedo[idx * 4 + 3]) / 255.0f;
            float bg[3] = {
                static_cast<float>(underlayPixels[uidx * 4 + 0]),
                static_cast<float>(underlayPixels[uidx * 4 + 1]),
                static_cast<float>(underlayPixels[uidx * 4 + 2])
            };
            if (def.underlayEmissive && a < 0.99f) {
                for (int c = 0; c < 3; ++c) {
                    bg[c] = std::max(bg[c], std::min(255.0f, meanUnderlay[c] * 1.6f));
                }
            }
            for (int c = 0; c < 3; ++c) {
                const float fg = static_cast<float>(albedo[idx * 4 + c]);
                albedo[idx * 4 + c] = static_cast<uint8_t>(std::clamp(
                    std::round(a * fg + (1.0f - a) * bg[c]), 0.0f, 255.0f));
            }
            albedo[idx * 4 + 3] = 255;
            if (def.underlayEmissive) {
                const float maxBg = std::max({bg[0], bg[1], bg[2]});
                compositeEmission[idx] = static_cast<uint8_t>(std::clamp(
                    std::round((1.0f - a) * maxBg), 0.0f, 255.0f));
            }
        }
    }
}

void CompositeDecalOverlays(const ShaderDefinition &def, int width, int height,
    std::vector<uint8_t> &albedo)
{
    if (def.decalOverlayMaps.empty() || width <= 0 || height <= 0) {
        return;
    }
    for (size_t di = 0; di < def.decalOverlayMaps.size(); ++di) {
        int dw = 0;
        int dh = 0;
        std::vector<uint8_t> decalPixels;
        if (!LoadQ3Texture(def.decalOverlayMaps[di].c_str(), dw, dh, decalPixels) ||
            dw <= 0 || dh <= 0) {
            continue;
        }
        const bool invAlpha = di < def.decalOverlayInvertedAlpha.size() &&
            def.decalOverlayInvertedAlpha[di];
        for (int y = 0; y < height; ++y) {
            const int dy = std::clamp(y * dh / height, 0, dh - 1);
            for (int x = 0; x < width; ++x) {
                const int dx = std::clamp(x * dw / width, 0, dw - 1);
                const size_t idx = static_cast<size_t>(y) * width + x;
                const size_t didx = static_cast<size_t>(dy) * dw + dx;
                const float da = static_cast<float>(decalPixels[didx * 4 + 3]) / 255.0f;
                const float coverage = std::clamp(invAlpha ? (1.0f - da) : da, 0.0f, 1.0f);
                for (int c = 0; c < 3; ++c) {
                    const float fg = static_cast<float>(decalPixels[didx * 4 + c]);
                    const float bg = static_cast<float>(albedo[idx * 4 + c]);
                    albedo[idx * 4 + c] = static_cast<uint8_t>(std::clamp(
                        std::round(coverage * fg + (1.0f - coverage) * bg), 0.0f, 255.0f));
                }
                float dstAlpha = static_cast<float>(albedo[idx * 4 + 3]) / 255.0f;
                if (def.metallicEnvInvertedAlpha) {
                    dstAlpha *= (1.0f - coverage);
                } else {
                    dstAlpha = std::max(dstAlpha, coverage);
                }
                albedo[idx * 4 + 3] = static_cast<uint8_t>(std::clamp(
                    std::round(dstAlpha * 255.0f), 0.0f, 255.0f));
            }
        }
    }
}

QrMaterial GetMaterialForTextureNameInternal(const char *name, bool rawTexture)
{
    if (name == nullptr || name[0] == '\0') {
        return defaultMaterial;
    }

    LoadShaderScripts();
    const std::string key = NormalizeShaderName(name);
    std::unordered_map<std::string, QrMaterial> &cache =
        rawTexture ? textureMaterialsRaw : textureMaterials;
    const auto existing = cache.find(key);
    if (existing != cache.end()) {
        return existing->second;
    }

    const auto shaderIt = shaderDefinitions.find(key);
    const char *textureName = shaderIt != shaderDefinitions.end() ? shaderIt->second.map.c_str() : name;
    const bool hasShader = !rawTexture && shaderIt != shaderDefinitions.end();
    const bool skipProceduralRme = !rawTexture && AuthoredModeEnabled() &&
        (activeMaterialsDoc.HasMaterial(name) ||
         (hasBaseMaterialsDoc && baseMaterialsDoc.HasMaterial(name)));
    const bool darkenDecal = hasShader && shaderIt->second.darkenDecal;
    const bool metallicEnvBase =
        hasShader && !skipProceduralRme && shaderIt->second.metallicEnvBase;
    const bool metallicEnvInvertedAlpha = hasShader && shaderIt->second.metallicEnvInvertedAlpha;
    const bool wetGlossAlphaMask =
        hasShader && !skipProceduralRme && shaderIt->second.wetGlossAlphaMask;
    const bool forceOpaqueAlpha = hasShader && shaderIt->second.tracedBase &&
        !shaderIt->second.alphaTested && !shaderIt->second.transparentSurface &&
        !shaderIt->second.hasOnlyBlendedStages;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> albedo;
    QrMaterial material = QR_NO_MATERIAL;
    if (LoadQ3Texture(textureName, width, height, albedo)) {
        if (darkenDecal) {
            ConvertToDarkenDecalTexture(albedo);
        }
        std::vector<uint8_t> compositeEmission;
        if (hasShader) {
            CompositeUnderlayTexture(shaderIt->second, width, height, albedo, compositeEmission);
            CompositeDecalOverlays(shaderIt->second, width, height, albedo);
        }
        std::vector<uint8_t> emission;
        const bool hasSurfaceEmission = hasShader && !skipProceduralRme &&
            BuildSurfaceLightEmission(shaderIt->second, width, height, albedo, emission);
        const std::vector<uint8_t> *activeEmission = hasSurfaceEmission
            ? &emission
            : (!compositeEmission.empty() && !skipProceduralRme ? &compositeEmission : nullptr);
        const char *materialRelPath = hasShader ? key.c_str() : textureName;
        material = CreateMaterialFromPixels(materialRelPath, width, height, albedo,
            QR_SAMPLER_ADDRESS_MODE_REPEAT, activeEmission, metallicEnvBase,
            metallicEnvInvertedAlpha, wetGlossAlphaMask, forceOpaqueAlpha);
    }
    if (material == QR_NO_MATERIAL) {
        if (hostLogEnabled) {
            HostLog("GetMaterialForTextureName: %s has no texture, using the default material",
                textureName);
        }
        material = defaultMaterial;
    }
    cache.emplace(key, material);
    return material;
}

QrMaterial GetMaterialForTextureName(const char *name)
{
    return GetMaterialForTextureNameInternal(name, false);
}

// The blend-equation path needs the untouched image: a darken decal is expressed by its
// ZERO/ONE_MINUS_SRC_COLOR factors instead of by a luminance-to-alpha conversion.
QrMaterial GetRawMaterialForTextureName(const char *name)
{
    return GetMaterialForTextureNameInternal(name, true);
}

// Loads a specific stage texture directly without redirecting through shaderDefinitions[name].map
// (which points to stage 0 when a stage image shares the parent shader's name, e.g. skullarch_b.tga).
QrMaterial GetDirectImageMaterial(const char *imageName, QrSamplerAddressMode addressMode,
    bool emissive = false, bool cleanAdditiveBlack = false)
{
    if (imageName == nullptr || imageName[0] == '\0') {
        return defaultMaterial;
    }

    std::string key = NormalizeShaderName(imageName);
    key += (addressMode == QR_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE) ? "|clamp" : "|repeat";
    if (emissive) {
        key += "|em";
    }
    if (cleanAdditiveBlack) {
        key += "|addclean";
    }
    const auto existing = directImageMaterials.find(key);
    if (existing != directImageMaterials.end()) {
        return existing->second;
    }

    int width = 0;
    int height = 0;
    std::vector<uint8_t> albedo;
    QrMaterial material = QR_NO_MATERIAL;
    if (LoadQ3Texture(imageName, width, height, albedo)) {
        if (cleanAdditiveBlack) {
            CleanAdditiveBlackBackground(albedo);
        }
        if (emissive && width > 0 && height > 0) {
            const size_t pixelCount = static_cast<size_t>(width) * height;
            std::vector<uint8_t> emission(pixelCount, 0);
            for (size_t i = 0; i < pixelCount && i * 4 + 2 < albedo.size(); ++i) {
                emission[i] = std::max({albedo[i * 4 + 0], albedo[i * 4 + 1], albedo[i * 4 + 2]});
            }
            material = CreateMaterialFromPixels(imageName, width, height, albedo, addressMode, &emission);
        } else {
            material = CreateMaterialFromPixels(imageName, width, height, albedo, addressMode);
        }
    }
    if (material == QR_NO_MATERIAL) {
        material = defaultMaterial;
    }
    directImageMaterials.emplace(std::move(key), material);
    return material;
}

// The fixed function blend adds the glass shell in an 8 bit frame buffer, where the bright pixels
// are already clamped. The HDR pipeline adds the same shell into an unclamped buffer, so the shell
// has to be dimmed before the tone mapper sees it. The same helper scales the opaque body stages,
// whose baked environment map ends up too dark and too washed out once the pipeline treats it as a
// lit albedo and the additive shell sits on top of it.
QrMaterial GetScaledEnvMaterialForTextureName(const char *name, float scale, float saturation = 1.0f)
{
    const float factor = std::clamp(scale, 0.0f, 8.0f);
    const float saturationFactor = std::clamp(saturation, 0.0f, 4.0f);
    if (name == nullptr || name[0] == '\0' ||
        (fabsf(factor - 1.0f) < 0.01f && fabsf(saturationFactor - 1.0f) < 0.01f)) {
        return GetMaterialForTextureName(name);
    }

    char key[540] = {};
    snprintf(key, sizeof(key), "%s|%.3f|%.3f",
        NormalizeShaderName(name == nullptr ? "" : name).c_str(), factor, saturationFactor);
    const auto existing = scaledEnvMaterials.find(key);
    if (existing != scaledEnvMaterials.end()) {
        return existing->second;
    }

    int width = 0;
    int height = 0;
    std::vector<uint8_t> albedo;
    QrMaterial material = QR_NO_MATERIAL;
    if (LoadQ3Texture(name, width, height, albedo)) {
        for (size_t i = 0; i + 3 < albedo.size(); i += 4) {
            float channels[3] = {
                albedo[i + 0] * factor,
                albedo[i + 1] * factor,
                albedo[i + 2] * factor};
            const float luminance =
                0.299f * channels[0] + 0.587f * channels[1] + 0.114f * channels[2];
            for (int channel = 0; channel < 3; ++channel) {
                const float value =
                    luminance + (channels[channel] - luminance) * saturationFactor;
                albedo[i + channel] =
                    static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
            }
        }
        material = CreateMaterialFromPixels(name, width, height, albedo,
            QR_SAMPLER_ADDRESS_MODE_REPEAT);
    }
    if (material == QR_NO_MATERIAL) {
        material = GetMaterialForTextureName(name);
    }
    scaledEnvMaterials.emplace(key, material);
    return material;
}

QrMaterial GetMaterialForHandle(qhandle_t handle)
{    const auto existing = uiMaterials.find(handle);
    if (existing != uiMaterials.end() && existing->second != QR_NO_MATERIAL) {
        return existing->second;
    }

    // Materials belong to the current QuakeRay instance, so a handle registered before the
    // renderer was re-initialized has to be re-resolved from its shader name.
    const auto nameIt = uiShaderNames.find(handle);
    if (nameIt != uiShaderNames.end() && !nameIt->second.empty()) {
        const QrMaterial material = GetMaterialForTextureName(nameIt->second.c_str());
        if (material != QR_NO_MATERIAL) {
            uiMaterials[handle] = material;
            if (qrayDebugEntities != nullptr && qrayDebugEntities->integer != 0) {
                HostLog("GetMaterialForHandle: handle=%d re-resolved from '%s'",
                    static_cast<int>(handle), nameIt->second.c_str());
            }
            return material;
        }
    }

    if (qrayDebugEntities != nullptr && qrayDebugEntities->integer != 0) {
        static std::unordered_set<qhandle_t> reported;
        if (reported.insert(handle).second) {
            HostLog("GetMaterialForHandle: handle=%d unknown, using default material",
                static_cast<int>(handle));
        }
    }
    return defaultMaterial;
}

uint32_t PackColor(const byte color[4])
{
    return static_cast<uint32_t>(color[0]) |
        (static_cast<uint32_t>(color[1]) << 8) |
        (static_cast<uint32_t>(color[2]) << 16) |
        (static_cast<uint32_t>(color[3]) << 24);
}

const char *ShaderNameForHandle(qhandle_t handle)
{
    const auto nameIt = uiShaderNames.find(handle);
    return nameIt != uiShaderNames.end() ? nameIt->second.c_str() : "";
}

int ShaderBlendTypeForHandle(qhandle_t handle)
{
    const auto nameIt = uiShaderNames.find(handle);
    if (nameIt == uiShaderNames.end()) {
        return QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
    }
    LoadShaderScripts();
    const auto definitionIt = shaderDefinitions.find(NormalizeShaderName(nameIt->second));
    return definitionIt != shaderDefinitions.end() ? definitionIt->second.blend
                                                   : QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
}

bool ShaderUsesAlphaTest(const char *shaderName)
{
    if (shaderName == nullptr || shaderName[0] == '\0') {
        return false;
    }
    LoadShaderScripts();
    const auto definitionIt = shaderDefinitions.find(NormalizeShaderName(shaderName));
    return definitionIt != shaderDefinitions.end() && definitionIt->second.alphaTested;
}

/// Returns true when a world shader uses a solid cutout frame over an animated underlay.
bool ShaderIsCutoutFrameUnderlay(const char *shaderName)
{
    if (shaderName == nullptr || shaderName[0] == '\0') {
        return false;
    }
    LoadShaderScripts();
    const auto definitionIt = shaderDefinitions.find(NormalizeShaderName(shaderName));
    return definitionIt != shaderDefinitions.end() && definitionIt->second.cutoutFrameUnderlay;
}

/// Applies static tcMod scale from a shader's traced base stage to texture coordinates.
void ApplyShaderBaseScale(const char *shaderName, float &u, float &v)
{
    if (shaderName == nullptr || shaderName[0] == '\0') {
        return;
    }
    LoadShaderScripts();
    const auto definitionIt = shaderDefinitions.find(NormalizeShaderName(shaderName));
    if (definitionIt != shaderDefinitions.end() && definitionIt->second.tracedBase &&
        !definitionIt->second.compositeUnderlayBase) {
        u *= definitionIt->second.baseScale[0];
        v *= definitionIt->second.baseScale[1];
    }
}

bool EffectTextureHasAlpha(const char *shaderName)
{
    if (shaderName == nullptr || shaderName[0] == '\0') {
        return true;
    }
    std::string base = shaderName;
    for (char &character : base) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    const size_t slash = base.find_last_of('/');
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        base.resize(dot);
    }

    static const char *extensions[] = {"", ".tga", ".jpg", ".jpeg", ".png"};
    for (const char *extension : extensions) {
        const auto entry = textureHasAlpha.find(base + extension);
        if (entry != textureHasAlpha.end()) {
            return entry->second;
        }
    }
    return true;
}

// Effect shaders usually have no script at all (decals), so the blending mode has to come from
// the image itself: Q3 stores impact marks and explosion frames as black-background glow maps
// without an alpha channel, which only look right when they are added to the surface.
QrGeometryMaterialBlendType EffectBlendForHandle(qhandle_t handle)
{
    const char *shaderName = ShaderNameForHandle(handle);
    LoadShaderScripts();
    const auto definitionIt = shaderDefinitions.find(NormalizeShaderName(shaderName));
    if (qrayDebugEntities != nullptr && qrayDebugEntities->integer != 0) {
        static std::unordered_set<std::string> reported;
        if (reported.insert(shaderName).second) {
            HostLog("EffectBlendForHandle: name='%s' found=%d map='%s' blend=%d darkenDecal=%d",
                shaderName, definitionIt != shaderDefinitions.end() ? 1 : 0,
                definitionIt != shaderDefinitions.end() ? definitionIt->second.map.c_str() : "",
                definitionIt != shaderDefinitions.end() ? static_cast<int>(definitionIt->second.blend) : -1,
                definitionIt != shaderDefinitions.end() ? (definitionIt->second.darkenDecal ? 1 : 0) : -1);
        }
    }
    if (definitionIt != shaderDefinitions.end() && definitionIt->second.darkenDecal) {
        return QR_GEOMETRY_MATERIAL_BLEND_TYPE_ALPHA;
    }
    const QrGeometryMaterialBlendType declared =
        static_cast<QrGeometryMaterialBlendType>(ShaderBlendTypeForHandle(handle));
    if (declared != QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE) {
        return declared;
    }
    return EffectTextureHasAlpha(ShaderNameForHandle(handle))
        ? QR_GEOMETRY_MATERIAL_BLEND_TYPE_ALPHA
        : QR_GEOMETRY_MATERIAL_BLEND_TYPE_ADD;
}

// Several impact marks are black-background images without an alpha channel and have no shader
// script at all, so the alpha blend they would inherit turns the black border into a solid card.
// Those marks are glow maps of the retail renderer and belong to the additive path.
bool EffectNeedsAdditiveFallback(qhandle_t handle)
{
    const char *shaderName = ShaderNameForHandle(handle);
    const auto definitionIt = shaderDefinitions.find(NormalizeShaderName(shaderName));
    if (definitionIt != shaderDefinitions.end() && !definitionIt->second.map.empty()) {
        return !EffectTextureHasAlpha(definitionIt->second.map.c_str());
    }
    return !EffectTextureHasAlpha(shaderName);
}

// The GL blend equation of an effect shader, as the rasterized overlay expects it. Returns false
// when the effect is opaque and must not take the blending path at all.
bool EffectBlendFactorsForHandle(qhandle_t handle, QrBlendFactor &source, QrBlendFactor &destination)
{
    const char *shaderName = ShaderNameForHandle(handle);
    LoadShaderScripts();
    const auto definitionIt = shaderDefinitions.find(NormalizeShaderName(shaderName));
    if (definitionIt != shaderDefinitions.end() &&
        definitionIt->second.blend != QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE) {
        source = definitionIt->second.blendSrc;
        destination = definitionIt->second.blendDst;
        if (source == QR_BLEND_FACTOR_SRC_ALPHA &&
            destination == QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA &&
            EffectNeedsAdditiveFallback(handle)) {
            source = QR_BLEND_FACTOR_ONE;
            destination = QR_BLEND_FACTOR_ONE;
        }
        return true;
    }

    switch (EffectBlendForHandle(handle)) {
    case QR_GEOMETRY_MATERIAL_BLEND_TYPE_ALPHA:
        if (EffectNeedsAdditiveFallback(handle)) {
            source = QR_BLEND_FACTOR_ONE;
            destination = QR_BLEND_FACTOR_ONE;
            return true;
        }
        source = QR_BLEND_FACTOR_SRC_ALPHA;
        destination = QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        return true;
    case QR_GEOMETRY_MATERIAL_BLEND_TYPE_ADD:
        source = QR_BLEND_FACTOR_ONE;
        destination = QR_BLEND_FACTOR_ONE;
        return true;
    case QR_GEOMETRY_MATERIAL_BLEND_TYPE_SHADE:
        source = QR_BLEND_FACTOR_ZERO;
        destination = QR_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        return true;
    default:
        return false;
    }
}

QrMaterial GetRawMaterialForHandle(qhandle_t handle)
{
    const auto existing = uiMaterialsRaw.find(handle);
    if (existing != uiMaterialsRaw.end()) {
        return existing->second;
    }

    QrMaterial material = QR_NO_MATERIAL;
    const auto nameIt = uiShaderNames.find(handle);
    if (nameIt != uiShaderNames.end() && !nameIt->second.empty()) {
        material = GetRawMaterialForTextureName(nameIt->second.c_str());
    }
    if (material == QR_NO_MATERIAL) {
        material = defaultMaterial;
    }
    uiMaterialsRaw.emplace(handle, material);
    return material;
}

uint32_t ClusterIndexForPoint(const float point[3])
{
    const uint32_t cluster = ResolveEditorLightCluster(sceneRuntime.scene, point);
    return cluster == QR_CLUSTER_LIGHT_NO_CLUSTER ? 0u : cluster;
}

std::string NormalizeDirectoryPath(const std::string &path)
{
    std::string out = path;
    while (!out.empty() && (out.back() == '/' || out.back() == '\\')) {
        out.pop_back();
    }
    return out;
}

void MountUniqueDirectory(std::vector<std::string> &seen, const std::string &dir)
{
    const std::string normalized = NormalizeDirectoryPath(dir);
    if (normalized.empty()) {
        return;
    }
    for (const std::string &existing : seen) {
        if (existing == normalized) {
            return;
        }
    }
    std::error_code ec;
    if (!std::filesystem::exists(normalized, ec)) {
        return;
    }
    seen.push_back(normalized);
    MountEditorGameDirectory(gameAssets, normalized);
}

void MountModAndBaseDirectories()
{
    std::vector<std::string> mountedDirs;
    const std::string materialsOverride = CvarText(qrayMaterialsPath, "");
    if (!materialsOverride.empty()) {
        MountUniqueDirectory(mountedDirs,
            std::filesystem::path(materialsOverride).parent_path().string());
    }

    cvar_t *fsHome = ri.Cvar_Get("fs_homepath", "", 0);
    cvar_t *fsBase = ri.Cvar_Get("fs_basepath", "", 0);
    cvar_t *fsGame = ri.Cvar_Get("fs_game", "", 0);
    const std::string homeRoot = CvarText(fsHome, "");
    const std::string baseRoot = CvarText(fsBase, "");
    const std::string modName = CvarText(fsGame, "");

    if (!modName.empty() && _stricmp(modName.c_str(), "baseq3") != 0) {
        if (!homeRoot.empty()) {
            MountUniqueDirectory(mountedDirs, (std::filesystem::path(homeRoot) / modName).string());
        }
        if (!baseRoot.empty()) {
            MountUniqueDirectory(mountedDirs, (std::filesystem::path(baseRoot) / modName).string());
        }
    }
    if (!homeRoot.empty()) {
        MountUniqueDirectory(mountedDirs, (std::filesystem::path(homeRoot) / "baseq3").string());
    }
    if (!baseRoot.empty()) {
        MountUniqueDirectory(mountedDirs, (std::filesystem::path(baseRoot) / "baseq3").string());
    }
}

bool VfsExistsCallback(const std::string &path)
{
    return !path.empty() && ri.FS_ReadFile(path.c_str(), nullptr) > 0;
}

bool VfsReadFileCallback(const std::string &path, std::vector<uint8_t> &out)
{
    out.clear();
    void *data = nullptr;
    const long len = !path.empty() ? ri.FS_ReadFile(path.c_str(), &data) : 0;
    if (len <= 0 || data == nullptr) {
        if (data != nullptr) {
            ri.FS_FreeFile(data);
        }
        return false;
    }
    const auto *bytes = static_cast<const uint8_t *>(data);
    out.assign(bytes, bytes + static_cast<size_t>(len));
    ri.FS_FreeFile(data);
    return true;
}

void VfsListFilesCallback(const std::string &prefix, const std::string &extension,
                          std::vector<EditorAssetCatalogEntry> &out)
{
    if (ri.FS_ListFiles == nullptr || ri.FS_FreeFileList == nullptr) {
        return;
    }
    std::string dir = prefix;
    while (!dir.empty() && dir.back() == '/') {
        dir.pop_back();
    }
    int listed = 0;
    char **files = ri.FS_ListFiles(dir.c_str(), extension.c_str(), &listed);
    if (files == nullptr) {
        return;
    }
    for (int i = 0; i < listed; ++i) {
        if (files[i] != nullptr && files[i][0] != '\0') {
            const std::string full = dir.empty()
                ? std::string(files[i])
                : (dir + "/" + files[i]);
            out.push_back(EditorAssetCatalogEntry{full, "ioq3-vfs", ""});
        }
    }
    ri.FS_FreeFileList(files);
}

EditorAssetVfsCallbacks BuildGameVfsCallbacks()
{
    EditorAssetVfsCallbacks vfs{};
    vfs.exists = VfsExistsCallback;
    vfs.readFile = VfsReadFileCallback;
    vfs.listFiles = VfsListFilesCallback;
    return vfs;
}

void ConfigureGameAssets()
{
    gameAssets.Clear();
    MountModAndBaseDirectories();
    gameAssets.SetVfsCallbacks(BuildGameVfsCallbacks());
}

bool ReadAuthoredTextFile(const std::string &explicitPath, const char *vfsRelName,
                          std::string &outText)
{
    outText.clear();
    if (!explicitPath.empty()) {
        std::ifstream file(explicitPath, std::ios::binary);
        if (file) {
            outText.assign((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
            return true;
        }
        return false;
    }

    std::vector<uint8_t> bytes;
    if (vfsRelName != nullptr && gameAssets.ReadFile(vfsRelName, bytes)) {
        outText.assign(bytes.begin(), bytes.end());
        return true;
    }

    void *data = nullptr;
    const long len = vfsRelName != nullptr ? ri.FS_ReadFile(vfsRelName, &data) : 0;
    if (len > 0 && data != nullptr) {
        outText.assign(static_cast<const char *>(data), static_cast<size_t>(len));
        ri.FS_FreeFile(data);
        return true;
    }
    if (data != nullptr) {
        ri.FS_FreeFile(data);
    }
    return false;
}

void LoadAuthoredSessionFilesForMap(const char *mapName)
{
    activeMaterialsDoc = EditorMaterialsDocument{};
    baseMaterialsDoc = EditorMaterialsDocument{};
    hasBaseMaterialsDoc = false;
    activeLevelDoc = EditorLevelDocument{};

    std::string matText;
    if (ReadAuthoredTextFile(CvarText(qrayMaterialsPath, ""), "qray.materials.yaml", matText)) {
        activeMaterialsDoc.LoadFromText(std::move(matText));
    }

    cvar_t *fsGame = ri.Cvar_Get("fs_game", "", 0);
    cvar_t *fsBase = ri.Cvar_Get("fs_basepath", "", 0);
    const std::string gameMod = CvarText(fsGame, "");
    const std::string baseRoot = CvarText(fsBase, "");
    if (!gameMod.empty() && _stricmp(gameMod.c_str(), "baseq3") != 0 && !baseRoot.empty()) {
        std::string baseErr;
        const std::string basePath =
            (std::filesystem::path(baseRoot) / "baseq3" / "qray.materials.yaml").string();
        if (baseMaterialsDoc.Load(basePath, baseErr) && baseMaterialsDoc.Exists()) {
            hasBaseMaterialsDoc = true;
        }
    }

    size_t matLoaded = 0;
    size_t matInherited = 0;
    sceneRuntime.ApplyMaterialsDocuments(activeMaterialsDoc,
        hasBaseMaterialsDoc ? &baseMaterialsDoc : nullptr, &matLoaded, &matInherited);

    int configApplied = 0;
    std::string cfgText;
    if (ReadAuthoredTextFile(CvarText(qrayEditorConfigPath, ""), "config.cfg", cfgText) &&
        !cfgText.empty()) {
        configApplied = sceneRuntime.ApplyConfigText(cfgText);
    }

    std::string lightsText;
    if (ReadAuthoredTextFile(CvarText(qrayLightsPath, ""), "qray.lights.yaml", lightsText)) {
        activeLevelDoc.LoadFromText(std::move(lightsText));
    }
    const std::string levelKey = BuildEditorSessionPaths("", "", mapName != nullptr ? mapName : "").level;
    const EditorLevelApplyResult lvl = sceneRuntime.ApplyLevelDocument(activeLevelDoc, levelKey);

    HostLog("Authored session '%s': cfg=%d mat=%zu (+%zu base) lights=%zu custom=%zu emitters=%zu sky=%d fog=%d",
        levelKey.c_str(), configApplied, matLoaded, matInherited,
        sceneRuntime.scene.lights.size(), sceneRuntime.customLights.size(),
        sceneRuntime.emitterLightOverrides.size(), lvl.hasSky ? 1 : 0, lvl.hasFog ? 1 : 0);
}

bool gHasTonemapPowerOverride = false;
float gTonemapPowerOverride = 0.9f;
bool gHasExposureBiasOverride = false;
float gExposureBiasOverride = 0.0f;
bool gHasEyeAdaptationOverride = false;
bool gEyeAdaptationOverride = true;

bool ShouldApplyCvar(cvar_t *cvar, bool onlyModified)
{
    if (cvar == nullptr) {
        return false;
    }
    if (!onlyModified) {
        return true;
    }
    if (cvar->modified == qfalse) {
        return false;
    }
    cvar->modified = qfalse;
    return true;
}

void ClearQrayCvarModifiedFlags()
{
    cvar_t *const cvars[] = {
        qrayTonemapType, qrayTonemapPower, qrayWhitePoint, qrayExposureBias, qrayEyeAdaptation,
        qrayUpscale, qrayTrueLight, qrayGiLevel, qrayDenoiser, qrayNeeSamples, qrayRestir,
        qrayRestirCandidates, qrayCellWorldSize, qraySunBounceRange, qrayPhysicalSky,
        qrayPhysicalSkySun, qraySkyColor, qraySunDiscSize, qrayClouds, qrayCloudColor,
        qrayCloudCoverage, qrayCloudDensity, qrayCloudSpeed, qrayCloudHeight,
        qrayCloudThickness, qrayCloudOpacity, qrayCloudQuality, qrayGodRays,
        qrayGodRaysIntensity, qrayGodRaysQuality, qrayPhysicalSkySunPitch, qrayPhysicalSkyYaw,
        qrayPhysicalSkyPower, qraySkyAmbientLod, qraySkyLightMultiplier, qrayIndirectHalfRes,
        qraySunColor,
    };
    for (cvar_t *cvar : cvars) {
        if (cvar != nullptr) {
            cvar->modified = qfalse;
        }
    }
}

void ApplyIlluminationCvars(bool onlyModified, EditorRenderSettings &settings)
{
    if (ShouldApplyCvar(qrayTrueLight, onlyModified)) {
        settings.truelight = static_cast<float>(std::clamp(qrayTrueLight->integer, 0, 2));
    }
    if (ShouldApplyCvar(qrayGiLevel, onlyModified)) {
        settings.giLevel = std::clamp(qrayGiLevel->value, 0.0f, 2.0f);
    }
    if (ShouldApplyCvar(qrayDenoiser, onlyModified)) {
        settings.denoiser = qrayDenoiser->integer != 0 ? 1.0f : 0.0f;
    }
    if (ShouldApplyCvar(qrayNeeSamples, onlyModified)) {
        settings.neeSamples = qrayNeeSamples->value < 1.5f ? 1.0f : 2.0f;
    }
    if (ShouldApplyCvar(qrayRestir, onlyModified)) {
        settings.restir = qrayRestir->integer != 0 ? 1.0f : 0.0f;
    }
    if (ShouldApplyCvar(qrayRestirCandidates, onlyModified)) {
        settings.restirCandidates = static_cast<float>(std::clamp(qrayRestirCandidates->integer, 1, 64));
    }
    if (ShouldApplyCvar(qrayCellWorldSize, onlyModified)) {
        settings.gradientCellMeters = std::max(qrayCellWorldSize->value, 0.001f);
    }
    if (ShouldApplyCvar(qraySunBounceRange, onlyModified)) {
        settings.sunBounceRange = std::max(qraySunBounceRange->value, 0.0f) / kQuakeUnitToMeter;
    }
}

void ApplySunCvars(bool onlyModified, EditorRenderSettings &settings, EditorSunState &sun)
{
    if (ShouldApplyCvar(qrayPhysicalSkySun, onlyModified)) {
        sun.enabled = qrayPhysicalSkySun->integer != 0;
    }
    if (ShouldApplyCvar(qrayPhysicalSkySunPitch, onlyModified) && qrayPhysicalSkySunPitch->value > -999.0f) {
        sun.pitch = std::clamp(qrayPhysicalSkySunPitch->value, -89.0f, 89.0f);
    }
    if (ShouldApplyCvar(qrayPhysicalSkyYaw, onlyModified) && qrayPhysicalSkyYaw->value > -999.0f) {
        float yaw = std::fmod(qrayPhysicalSkyYaw->value + 180.0f, 360.0f);
        sun.yaw = yaw < 0.0f ? (yaw + 360.0f) : yaw;
    }
    if (ShouldApplyCvar(qrayPhysicalSkyPower, onlyModified)) {
        sun.radiance = std::clamp(qrayPhysicalSkyPower->value, 0.0f, 100.0f);
    }
    if (ShouldApplyCvar(qraySunColor, onlyModified) && qraySunColor->string[0] != '\0') {
        QrFloat3D c{};
        CvarColor(qraySunColor, settings.sunColor, c);
        std::copy_n(c.data, 3, settings.sunColor);
    }
    if (ShouldApplyCvar(qraySunDiscSize, onlyModified)) {
        settings.sunDiscSize = std::max(qraySunDiscSize->value, 0.0f);
    }
}

void ApplySkyCvars(bool onlyModified, EditorRenderSettings &settings)
{
    if (ShouldApplyCvar(qrayPhysicalSky, onlyModified)) {
        settings.skyType = qrayPhysicalSky->integer != 0 ? QR_SKY_TYPE_PROCEDURAL : QR_SKY_TYPE_CUBEMAP;
    }
    if (ShouldApplyCvar(qraySkyColor, onlyModified) && qraySkyColor->string[0] != '\0' &&
        _stricmp(qraySkyColor->string, "auto") != 0) {
        QrFloat3D c{};
        CvarColor(qraySkyColor, settings.skyColor, c);
        std::copy_n(c.data, 3, settings.skyColor);
    }
    if (ShouldApplyCvar(qraySkyAmbientLod, onlyModified)) {
        settings.skyAmbientLod = std::clamp(qraySkyAmbientLod->integer, 0, 10);
    }
    if (ShouldApplyCvar(qraySkyLightMultiplier, onlyModified)) {
        settings.skyLightMultiplier = std::clamp(qraySkyLightMultiplier->value, 0.0f, 8.0f);
    }
    if (ShouldApplyCvar(qrayGodRays, onlyModified)) {
        settings.godRays = qrayGodRays->integer != 0;
    }
    if (ShouldApplyCvar(qrayGodRaysIntensity, onlyModified)) {
        settings.godRaysIntensity = std::max(qrayGodRaysIntensity->value, 0.0f);
    }
    if (ShouldApplyCvar(qrayGodRaysQuality, onlyModified)) {
        settings.godRaysQuality = std::max(qrayGodRaysQuality->integer, 0);
    }
}

void ApplyCloudCvars(bool onlyModified, EditorRenderSettings &settings)
{
    if (ShouldApplyCvar(qrayClouds, onlyModified)) {
        settings.clouds = qrayClouds->integer != 0;
    }
    if (ShouldApplyCvar(qrayCloudColor, onlyModified)) {
        QrFloat3D c{};
        CvarColor(qrayCloudColor, settings.cloudsColor, c);
        std::copy_n(c.data, 3, settings.cloudsColor);
    }
    if (ShouldApplyCvar(qrayCloudCoverage, onlyModified)) {
        settings.cloudsCoverage = std::clamp(qrayCloudCoverage->value, 0.0f, 1.0f);
    }
    if (ShouldApplyCvar(qrayCloudDensity, onlyModified)) {
        settings.cloudsDensity = std::max(qrayCloudDensity->value, 0.0f);
    }
    if (ShouldApplyCvar(qrayCloudSpeed, onlyModified)) {
        settings.cloudsSpeed = std::max(qrayCloudSpeed->value, 0.0f);
    }
    if (ShouldApplyCvar(qrayCloudHeight, onlyModified)) {
        settings.cloudsHeight = std::max(qrayCloudHeight->value, 1.0f) / kQuakeUnitToMeter;
    }
    if (ShouldApplyCvar(qrayCloudThickness, onlyModified)) {
        settings.cloudsThickness = std::max(qrayCloudThickness->value, 1.0f) / kQuakeUnitToMeter;
    }
    if (ShouldApplyCvar(qrayCloudOpacity, onlyModified)) {
        settings.cloudsAlpha = std::clamp(qrayCloudOpacity->value, 0.0f, 1.0f);
    }
    if (ShouldApplyCvar(qrayCloudQuality, onlyModified)) {
        settings.cloudsQuality = std::clamp(qrayCloudQuality->integer, 0,
            static_cast<int>(QR_SKY_CLOUDS_MAX_QUALITY));
    }
}

void ApplyTonemapAndResolutionCvars(bool onlyModified, EditorRenderSettings &settings)
{
    if (ShouldApplyCvar(qrayTonemapType, onlyModified)) {
        settings.tonemapType = std::clamp(qrayTonemapType->integer, 0, 4);
    }
    if (ShouldApplyCvar(qrayWhitePoint, onlyModified)) {
        settings.tonemapWhite = std::max(qrayWhitePoint->value, 1.0f);
    }
    if (ShouldApplyCvar(qrayUpscale, onlyModified)) {
        settings.upscaleFsr31 = std::clamp(qrayUpscale->integer, 0, 4);
    }
    if (ShouldApplyCvar(qrayTonemapPower, onlyModified)) {
        gHasTonemapPowerOverride = true;
        gTonemapPowerOverride = std::clamp(qrayTonemapPower->value, 0.0f, 1.0f);
    }
    if (ShouldApplyCvar(qrayExposureBias, onlyModified)) {
        gHasExposureBiasOverride = true;
        gExposureBiasOverride = qrayExposureBias->value;
    }
    if (ShouldApplyCvar(qrayEyeAdaptation, onlyModified)) {
        gHasEyeAdaptationOverride = true;
        gEyeAdaptationOverride = qrayEyeAdaptation->integer != 0;
    }
}

void ApplyQrayCvarsToRuntime(bool onlyModified)
{
    if (!onlyModified) {
        gHasTonemapPowerOverride = false;
        gHasExposureBiasOverride = false;
        gHasEyeAdaptationOverride = false;
    }
    ApplyIlluminationCvars(onlyModified, sceneRuntime.settings);
    ApplySunCvars(onlyModified, sceneRuntime.settings, sceneRuntime.sun);
    ApplySkyCvars(onlyModified, sceneRuntime.settings);
    ApplyCloudCvars(onlyModified, sceneRuntime.settings);
    ApplyTonemapAndResolutionCvars(onlyModified, sceneRuntime.settings);
    (void)ShouldApplyCvar(qrayIndirectHalfRes, onlyModified);
}

qhandle_t RegisterUiShader(const char *name)
{
    if (name == nullptr || name[0] == '\0') {
        return 0;
    }

    const auto existing = uiShaderHandles.find(name);
    if (existing != uiShaderHandles.end()) {
        return existing->second;
    }

    const qhandle_t handle = nextUiHandle++;
    uiShaderHandles.emplace(name, handle);
    uiShaderNames[handle] = name;

    const QrMaterial material = GetMaterialForTextureName(name);
    if (material != QR_NO_MATERIAL) {
        uiMaterials.emplace(handle, material);
    }
    return handle;
}

void ResetMaterialState()
{
    sceneRuntime.ResetGpuHandlesOnly();
    activeMaterialsDoc = EditorMaterialsDocument{};
    baseMaterialsDoc = EditorMaterialsDocument{};
    hasBaseMaterialsDoc = false;
    activeLevelDoc = EditorLevelDocument{};
    frameDynamicEmissiveLights.clear();
    // Handles stay valid because the shader names are owned by the host, but every
    // QrMaterial was destroyed together with the old QuakeRay instance.
    uiMaterials.clear();
    uiMaterialsRaw.clear();
    textureMaterials.clear();
    textureMaterialsRaw.clear();
    scaledEnvMaterials.clear();
    directImageMaterials.clear();
    binaryCutoutHoleCache.clear();
    defaultMaterial = QR_NO_MATERIAL;
    markTriangles.clear();
    markGrid.clear();
    ResetProceduralSmoke();
}

void RebuildUiMaterials()
{
    if (qrayInstance == QR_NULL_HANDLE) {
        return;
    }

    int rebuilt = 0;
    for (const auto &entry : uiShaderHandles) {
        const QrMaterial material = GetMaterialForTextureName(entry.first.c_str());
        if (material != QR_NO_MATERIAL) {
            uiMaterials[entry.second] = material;
            ++rebuilt;
        }
    }
    HostLog("RebuildUiMaterials: rebuilt=%d of %zu", rebuilt, uiShaderHandles.size());
}

bool CreateQrayInstance()
{
    HostLog("CreateQrayInstance: start");

    QrayNativeWindow nativeWindow{};
    const bool hasWindow = QrayWindow_GetNativeHandle(&nativeWindow);
    QrInstanceCreateInfo info{};
#if defined(QR_USE_SURFACE_WIN32)
    if (!hasWindow || nativeWindow.type != QrayNativeWindowType::Win32 || nativeWindow.window == 0) {
        HostLog("CreateQrayInstance: no Win32 window handle");
        ri.Printf(PRINT_WARNING, "rendererqray: no Win32 window handle available\n");
        return false;
    }
    HostLog("CreateQrayInstance: hwnd=0x%llx", nativeWindow.window);

    QrWin32SurfaceCreateInfo surfaceInfo{};
    surfaceInfo.hinstance = nativeWindow.display != nullptr
        ? static_cast<HINSTANCE>(nativeWindow.display)
        : GetModuleHandleA(nullptr);
    surfaceInfo.hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(nativeWindow.window));
    info.pWin32SurfaceInfo = &surfaceInfo;
#elif defined(QR_USE_SURFACE_XLIB)
    if (!hasWindow || nativeWindow.type != QrayNativeWindowType::X11 ||
        nativeWindow.display == nullptr || nativeWindow.window == 0) {
        HostLog("CreateQrayInstance: unsupported window system type=%d", static_cast<int>(nativeWindow.type));
        ri.Printf(PRINT_WARNING,
            "rendererqray: QuakeRay needs an X11 window (got window system %d). "
            "On Wayland run with SDL_VIDEODRIVER=x11 to use XWayland.\n",
            static_cast<int>(nativeWindow.type));
        return false;
    }
    HostLog("CreateQrayInstance: x11 display=%p window=0x%llx", nativeWindow.display, nativeWindow.window);

    QrXlibSurfaceCreateInfo surfaceInfo{};
    surfaceInfo.dpy = nativeWindow.display;
    surfaceInfo.window = static_cast<unsigned long>(nativeWindow.window);
    info.pXlibSurfaceCreateInfo = &surfaceInfo;
#else
#error "rendererqray: QuakeRay SDK was built without a supported surface type"
#endif

    info.pAppName = "QuakeRay";
    info.pAppGUID = "6f6f4e42-2f8b-4d5f-9c8b-4a7b1c2d3e4f";
    info.pConfigPath = CvarText(qrayConfig, "qray.cfg");
    info.pfnPrint = QrayPrint;
    info.pShaderFolderPath = CvarText(qrayShaderDir, "qray/shaders/");
    info.pBlueNoiseFilePath = CvarText(qrayBlueNoise, "qray/blue_noise.png");
    info.pfnOpenFile = QrayOpenFile;
    info.pfnCloseFile = QrayCloseFile;
    info.primaryRaysMaxAlbedoLayers = 2;
    info.indirectIlluminationMaxAlbedoLayers = 1;
    info.rayCullBackFacingTriangles = (qrayCullBackFaces != nullptr && qrayCullBackFaces->integer == 0)
        ? QR_FALSE : QR_TRUE;
    info.allowGeometryWithSkyFlag = QR_TRUE;
    info.rasterizedMaxVertexCount = 1u << 20;
    info.rasterizedMaxIndexCount = 1u << 21;
    info.rasterizedVertexColorGamma = QR_TRUE;
    info.rasterizedSkyCubemapSize = 256;
    info.maxTextureCount = 4096;
    info.textureSamplerForceMinificationFilterLinear = QR_TRUE;
    info.textureSamplerForceNormalMapFilterLinear = QR_TRUE;
    info.originalAlbedoAlphaTextureIsSRGB = QR_TRUE;
    info.originalRoughnessMetallicEmissionTextureIsSRGB = QR_FALSE;
    info.originalNormalTextureIsSRGB = QR_FALSE;
    info.overridenAlbedoAlphaTextureIsSRGB = QR_TRUE;
    info.overridenRoughnessMetallicEmissionTextureIsSRGB = QR_FALSE;
    info.overridenNormalTextureIsSRGB = QR_FALSE;
    info.pWaterNormalTexturePath = CvarText(qrayWaterNormal, "");
    info.pbrTextureSwizzling = QR_TEXTURE_SWIZZLING_ROUGHNESS_METALLIC_EMISSIVE;
    info.effectWipeIsUsed = QR_FALSE;
    info.godRaysQuality = 1;

    HostLog("CreateQrayInstance: shaders=%s blueNoise=%s water=%s config=%s",
        CvarText(qrayShaderDir, "qray/shaders/"), CvarText(qrayBlueNoise, "qray/blue_noise.png"),
        CvarText(qrayWaterNormal, ""), CvarText(qrayConfig, "qray.cfg"));

    QrResult result = QR_GRAPHICS_API_ERROR;
    try {
        result = CreateInstanceProtected(&info, &qrayInstance);
    } catch (const std::exception &exception) {
        HostLog("CreateQrayInstance: exception: %s", exception.what());
        qrayInstance = QR_NULL_HANDLE;
        return false;
    } catch (...) {
        HostLog("CreateQrayInstance: unknown exception");
        qrayInstance = QR_NULL_HANDLE;
        return false;
    }
    HostLog("CreateQrayInstance: qrCreateInstance result=%d instance=%p", static_cast<int>(result), qrayInstance);
    if (result != QR_SUCCESS) {
        qrayInstance = QR_NULL_HANDLE;
        ri.Printf(PRINT_WARNING, "rendererqray: qrCreateInstance failed: %d\n", static_cast<int>(result));
        return false;
    }

    return true;
}

bool ConfigureSpatialConvention()
{
    QrSpatialConvention spatial{};
    spatial.size = sizeof(spatial);
    spatial.version = QR_SPATIAL_CONVENTION_VERSION;
    spatial.sceneXAxis = QR_AXIS_POSITIVE_X;
    spatial.sceneYAxis = QR_AXIS_POSITIVE_Y;
    spatial.sceneZAxis = QR_AXIS_POSITIVE_Z;
    spatial.hostIsRightHanded = QR_TRUE;

    const QrResult result = qrSetSpatialConvention(qrayInstance, &spatial);
    HostLog("ConfigureSpatialConvention: identity Z-up right-handed result=%d scale=%f",
        static_cast<int>(result), kQuakeUnitToMeter);
    if (result != QR_SUCCESS) {
        ri.Printf(PRINT_WARNING, "rendererqray: qrSetSpatialConvention failed: %d\n",
            static_cast<int>(result));
        return false;
    }
    return true;
}

bool AllowDynamicLights()
{
    return static_cast<int>(std::lround(sceneRuntime.settings.truelight)) < 2;
}

constexpr float kMinDynamicLightColorSum = 0.0001f; // matches qray::LightManager::kLightMinColorSum

bool IsDynamicLightActive(const DynamicLight &source, float intensityScale)
{
    if (!std::isfinite(intensityScale) || intensityScale <= 0.0f) {
        return false;
    }
    const float r = std::max(0.0f, source.color[0] * intensityScale);
    const float g = std::max(0.0f, source.color[1] * intensityScale);
    const float b = std::max(0.0f, source.color[2] * intensityScale);
    const float sum = r + g + b;
    return std::isfinite(sum) && sum >= kMinDynamicLightColorSum;
}

void UploadDynamicLights()
{
    const bool allowed = AllowDynamicLights();
    const float intensityScale = CvarFloat(qrayDlightScale, 0.25f);
    size_t activeCount = 0;
    size_t additive = 0;
    size_t linear = 0;
    if (allowed) {
        for (const DynamicLight &light : dynamicLights) {
            if (!IsDynamicLightActive(light, intensityScale)) {
                continue;
            }
            ++activeCount;
            additive += light.additive ? 1u : 0u;
            linear += light.linear ? 1u : 0u;
        }
    }
    static size_t lastLightCount = 0;
    if (activeCount != lastLightCount) {
        lastLightCount = activeCount;
        HostLog("UploadDynamicLights: count=%zu additive=%zu linear=%zu",
            activeCount, additive, linear);
    }
    if (qrayInstance == QR_NULL_HANDLE || activeCount == 0) {
        return;
    }

    const float emissionRadius = QuakeUnitsToMeters(
        std::max(1.0f, CvarFloat(qrayLightRadius, 16.0f)));

    for (size_t i = 0; i < dynamicLights.size(); ++i) {
        const DynamicLight &source = dynamicLights[i];
        if (!IsDynamicLightActive(source, intensityScale)) {
            continue;
        }
        QrSphericalLightUploadInfo light{};
        light.uniqueID = kDynamicLightUniqueIdBase + i;
        light.color.data[0] = source.color[0] * intensityScale;
        light.color.data[1] = source.color[1] * intensityScale;
        light.color.data[2] = source.color[2] * intensityScale;
        QuakePositionToMeters(source.origin, light.position);
        light.radius = emissionRadius;

        const QrResult result = qrUploadSphericalLight(qrayInstance, &light);
        if (result != QR_SUCCESS) {
            HostLog("UploadDynamicLights: light %zu failed result=%d", i, static_cast<int>(result));
            break;
        }
    }
}

void AppendDynamicClusterSources(std::vector<QrClusterLightSource> &outSources)
{
    if (!AllowDynamicLights() || dynamicLights.empty()) {
        return;
    }
    const float dlightScale = CvarFloat(qrayDlightScale, 0.25f);
    const float dlightRadius = std::max(1.0f, CvarFloat(qrayLightRadius, 16.0f));
    outSources.reserve(dynamicLights.size());

    for (size_t i = 0; i < dynamicLights.size(); ++i) {
        const DynamicLight &source = dynamicLights[i];
        if (!IsDynamicLightActive(source, dlightScale)) {
            continue;
        }
        const uint32_t cluster = ResolveEditorLightCluster(sceneRuntime.scene, source.origin);
        QrClusterLightSource cls{};
        cls.uniqueID = kDynamicLightUniqueIdBase + i;
        cls.origin = {source.origin[0], source.origin[1], source.origin[2]};
        cls.cluster = cluster;
        cls.reach = std::max(source.radius, 256.0f);
        cls.radius = dlightRadius;
        const float lum = 0.2125f * std::max(0.0f, source.color[0] * dlightScale) +
            0.7154f * std::max(0.0f, source.color[1] * dlightScale) +
            0.0721f * std::max(0.0f, source.color[2] * dlightScale);
        cls.power = std::max(0.0f, lum * (kEditorUnitsPerMeter * kEditorUnitsPerMeter));
        outSources.push_back(cls);
    }
}

void UploadSceneLights()
{
    if (sceneLightsUploaded || qrayInstance == QR_NULL_HANDLE) {
        return;
    }
    sceneLightsUploaded = true;

    const double timeSeconds = static_cast<double>(sceneDef.time) * 0.001;
    sceneRuntime.UploadSun(qrayInstance, SceneRuntimeLog);
    sceneRuntime.UploadLights(qrayInstance, timeSeconds, true, SceneRuntimeLog);
    sceneRuntime.UploadEmissiveLights(qrayInstance, static_cast<float>(timeSeconds),
        frameDynamicEmissiveLights.empty() ? nullptr : &frameDynamicEmissiveLights,
        SceneRuntimeLog);
    UploadDynamicLights();

    std::vector<QrClusterLightSource> extraDynamicSources;
    AppendDynamicClusterSources(extraDynamicSources);
    EditorClusterLightUploadExtras extras{};
    if (!extraDynamicSources.empty()) {
        extras.extraQuakeUnitSources = &extraDynamicSources;
    }
    if (!frameDynamicEmissiveLights.empty()) {
        extras.extraDynamicEmissiveLights = &frameDynamicEmissiveLights;
    }
    sceneRuntime.UploadClusterLightSources(qrayInstance, &extras, SceneRuntimeLog);
}

void RotationMatrixQ1(float matrix[16], float angle, float x, float y, float z)
{
    const float c = std::cos(angle);
    const float s = std::sin(angle);

    matrix[0 * 4 + 0] = x * x * (1.0f - c) + c;
    matrix[0 * 4 + 1] = y * x * (1.0f - c) + z * s;
    matrix[0 * 4 + 2] = x * z * (1.0f - c) - y * s;
    matrix[0 * 4 + 3] = 0.0f;

    matrix[1 * 4 + 0] = x * y * (1.0f - c) - z * s;
    matrix[1 * 4 + 1] = y * y * (1.0f - c) + c;
    matrix[1 * 4 + 2] = y * z * (1.0f - c) + x * s;
    matrix[1 * 4 + 3] = 0.0f;

    matrix[2 * 4 + 0] = x * z * (1.0f - c) + y * s;
    matrix[2 * 4 + 1] = y * z * (1.0f - c) - x * s;
    matrix[2 * 4 + 2] = z * z * (1.0f - c) + c;
    matrix[2 * 4 + 3] = 0.0f;

    matrix[3 * 4 + 0] = 0.0f;
    matrix[3 * 4 + 1] = 0.0f;
    matrix[3 * 4 + 2] = 0.0f;
    matrix[3 * 4 + 3] = 1.0f;
}

void TranslationMatrixQ1(float matrix[16], float x, float y, float z)
{
    std::memset(matrix, 0, 16 * sizeof(float));
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[12] = x;
    matrix[13] = y;
    matrix[14] = z;
    matrix[15] = 1.0f;
}

void MultiplyMatrixQ1(float left[16], const float right[16])
{
    float temp[16];
    std::memcpy(temp, left, 16 * sizeof(float));

    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            float value = 0.0f;
            for (int i = 0; i < 4; ++i) {
                value += temp[i * 4 + row] * right[column * 4 + i];
            }
            left[column * 4 + row] = value;
        }
    }
}

void BuildQ1ViewMatrix(const refdef_t &def, float out[16])
{
    const float *forward = def.viewaxis[0];
    float yaw = 0.0f;
    float pitch = 0.0f;

    if (forward[0] == 0.0f && forward[1] == 0.0f) {
        yaw = 0.0f;
        pitch = forward[2] > 0.0f ? 90.0f : 270.0f;
    } else {
        yaw = std::atan2(forward[1], forward[0]) * 180.0f / kPi;
        if (yaw < 0.0f) {
            yaw += 360.0f;
        }
        const float horizontal = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
        pitch = std::atan2(forward[2], horizontal) * 180.0f / kPi;
        if (pitch < 0.0f) {
            pitch += 360.0f;
        }
    }
    pitch = -pitch;

    float rotation[16] = {};
    RotationMatrixQ1(out, -kPi / 2.0f, 1.0f, 0.0f, 0.0f);
    RotationMatrixQ1(rotation, kPi / 2.0f, 0.0f, 0.0f, 1.0f);
    MultiplyMatrixQ1(out, rotation);
    RotationMatrixQ1(rotation, -(0.0f) * (kPi / 180.0f), 1.0f, 0.0f, 0.0f);
    MultiplyMatrixQ1(out, rotation);
    RotationMatrixQ1(rotation, -pitch * (kPi / 180.0f), 0.0f, 1.0f, 0.0f);
    MultiplyMatrixQ1(out, rotation);
    RotationMatrixQ1(rotation, -yaw * (kPi / 180.0f), 0.0f, 0.0f, 1.0f);
    MultiplyMatrixQ1(out, rotation);

    float translation[16] = {};
    TranslationMatrixQ1(translation, -def.vieworg[0], -def.vieworg[1], -def.vieworg[2]);
    MultiplyMatrixQ1(out, translation);
}

void BuildViewMatrix(const refdef_t &def, float out[16])
{
    const float *axis0 = def.viewaxis[0];
    const float *axis1 = def.viewaxis[1];
    const float *axis2 = def.viewaxis[2];
    const float *origin = def.vieworg;
    const int mode = qrayViewMode != nullptr ? qrayViewMode->integer : 7;

    if (mode == 4) {
        BuildQ1ViewMatrix(def, out);
        return;
    }

    if (mode == 7) {
        const float rightVector[3] = {-axis1[0], -axis1[1], -axis1[2]};
        const float upVector[3] = {axis2[0], axis2[1], axis2[2]};
        const float backVector[3] = {-axis0[0], -axis0[1], -axis0[2]};
        out[0] = rightVector[0];
        out[1] = upVector[0];
        out[2] = backVector[0];
        out[3] = 0.0f;
        out[4] = rightVector[1];
        out[5] = upVector[1];
        out[6] = backVector[1];
        out[7] = 0.0f;
        out[8] = rightVector[2];
        out[9] = upVector[2];
        out[10] = backVector[2];
        out[11] = 0.0f;
        out[12] = -(origin[0] * rightVector[0] + origin[1] * rightVector[1] + origin[2] * rightVector[2]);
        out[13] = -(origin[0] * upVector[0] + origin[1] * upVector[1] + origin[2] * upVector[2]);
        out[14] = -(origin[0] * backVector[0] + origin[1] * backVector[1] + origin[2] * backVector[2]);
        out[15] = 1.0f;
        return;
    }

    if (mode == 6) {
        const float rightVector[3] = {-axis1[0], -axis1[1], -axis1[2]};
        const float upVector[3] = {axis2[0], axis2[1], axis2[2]};
        const float backVector[3] = {-axis0[0], -axis0[1], -axis0[2]};
        out[0] = rightVector[0];
        out[1] = rightVector[1];
        out[2] = rightVector[2];
        out[3] = 0.0f;
        out[4] = upVector[0];
        out[5] = upVector[1];
        out[6] = upVector[2];
        out[7] = 0.0f;
        out[8] = backVector[0];
        out[9] = backVector[1];
        out[10] = backVector[2];
        out[11] = 0.0f;
        out[12] = -(origin[0] * rightVector[0] + origin[1] * upVector[0] + origin[2] * backVector[0]);
        out[13] = -(origin[0] * rightVector[1] + origin[1] * upVector[1] + origin[2] * backVector[1]);
        out[14] = -(origin[0] * rightVector[2] + origin[1] * upVector[2] + origin[2] * backVector[2]);
        out[15] = 1.0f;
        return;
    }

    if (mode == 5) {
        float viewer[16] = {
            axis0[0], axis1[0], axis2[0], 0.0f,
            axis0[1], axis1[1], axis2[1], 0.0f,
            axis0[2], axis1[2], axis2[2], 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        const float flip[16] = {
            0.0f, 0.0f, -1.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                out[row * 4 + column] =
                    viewer[row * 4 + 0] * flip[0 * 4 + column] +
                    viewer[row * 4 + 1] * flip[1 * 4 + column] +
                    viewer[row * 4 + 2] * flip[2 * 4 + column] +
                    viewer[row * 4 + 3] * flip[3 * 4 + column];
            }
        }

        for (int column = 0; column < 3; ++column) {
            out[12 + column] = -(
                origin[0] * out[0 + column] +
                origin[1] * out[4 + column] +
                origin[2] * out[8 + column]);
        }
        return;
    }

    if (mode == 0) {
        float viewer[16] = {
            axis0[0], axis1[0], axis2[0], -(origin[0] * axis0[0] + origin[1] * axis0[1] + origin[2] * axis0[2]),
            axis0[1], axis1[1], axis2[1], -(origin[0] * axis1[0] + origin[1] * axis1[1] + origin[2] * axis1[2]),
            axis0[2], axis1[2], axis2[2], -(origin[0] * axis2[0] + origin[1] * axis2[1] + origin[2] * axis2[2]),
            0.0f, 0.0f, 0.0f, 1.0f
        };

        const float flip[16] = {
            0.0f, 0.0f, -1.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                out[row * 4 + column] =
                    viewer[row * 4 + 0] * flip[0 * 4 + column] +
                    viewer[row * 4 + 1] * flip[1 * 4 + column] +
                    viewer[row * 4 + 2] * flip[2 * 4 + column] +
                    viewer[row * 4 + 3] * flip[3 * 4 + column];
            }
        }
        return;
    }

    const float right[3] = {-axis1[0], -axis1[1], -axis1[2]};
    const float up[3] = {axis2[0], axis2[1], axis2[2]};
    const float back[3] = {-axis0[0], -axis0[1], -axis0[2]};

    if (mode == 1) {
        const float row0[3] = {-up[0], -up[1], -up[2]};
        const float row1[3] = {-back[0], -back[1], -back[2]};
        const float row2[3] = {right[0], right[1], right[2]};

        out[0] = row0[0];
        out[1] = row0[1];
        out[2] = row0[2];
        out[3] = 0.0f;
        out[4] = row1[0];
        out[5] = row1[1];
        out[6] = row1[2];
        out[7] = 0.0f;
        out[8] = row2[0];
        out[9] = row2[1];
        out[10] = row2[2];
        out[11] = 0.0f;
        out[12] = -(row0[0] * origin[0] + row0[1] * origin[1] + row0[2] * origin[2]);
        out[13] = -(row1[0] * origin[0] + row1[1] * origin[1] + row1[2] * origin[2]);
        out[14] = -(row2[0] * origin[0] + row2[1] * origin[1] + row2[2] * origin[2]);
        out[15] = 1.0f;
        return;
    }

    if (mode == 2) {
        out[0] = right[0];
        out[1] = right[1];
        out[2] = right[2];
        out[3] = 0.0f;
        out[4] = up[0];
        out[5] = up[1];
        out[6] = up[2];
        out[7] = 0.0f;
        out[8] = back[0];
        out[9] = back[1];
        out[10] = back[2];
        out[11] = 0.0f;
        out[12] = -(right[0] * origin[0] + right[1] * origin[1] + right[2] * origin[2]);
        out[13] = -(up[0] * origin[0] + up[1] * origin[1] + up[2] * origin[2]);
        out[14] = -(back[0] * origin[0] + back[1] * origin[1] + back[2] * origin[2]);
        out[15] = 1.0f;
        return;
    }

    out[0] = right[0];
    out[1] = right[1];
    out[2] = right[2];
    out[3] = -(right[0] * origin[0] + right[1] * origin[1] + right[2] * origin[2]);
    out[4] = up[0];
    out[5] = up[1];
    out[6] = up[2];
    out[7] = -(up[0] * origin[0] + up[1] * origin[1] + up[2] * origin[2]);
    out[8] = back[0];
    out[9] = back[1];
    out[10] = back[2];
    out[11] = -(back[0] * origin[0] + back[1] * origin[1] + back[2] * origin[2]);
    out[12] = 0.0f;
    out[13] = 0.0f;
    out[14] = 0.0f;
    out[15] = 1.0f;
}

void BuildOverlayMatrix(float out[16], QrViewport &viewport)
{
    const float width = static_cast<float>(std::max(1, glConfig.vidWidth));
    const float height = static_cast<float>(std::max(1, glConfig.vidHeight));

    out[0] = 2.0f / width;
    out[1] = 0.0f;
    out[2] = 0.0f;
    out[3] = 0.0f;

    out[4] = 0.0f;
    out[5] = 2.0f / height;
    out[6] = 0.0f;
    out[7] = 0.0f;

    out[8] = 0.0f;
    out[9] = 0.0f;
    out[10] = -1.0f;
    out[11] = 0.0f;

    out[12] = -1.0f;
    out[13] = -1.0f;
    out[14] = 0.0f;
    out[15] = 1.0f;

    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
}

QrTransform IdentityTransform()
{
    QrTransform result{};
    result.matrix[0][0] = 1.0f;
    result.matrix[1][1] = 1.0f;
    result.matrix[2][2] = 1.0f;
    return result;
}

int ParseFloats(const char *text, float *values, int maxValues)
{
    if (text == nullptr || maxValues <= 0) {
        return 0;
    }
    int count = 0;
    const char *cursor = text;
    while (count < maxValues) {
        char *end = nullptr;
        const float value = std::strtof(cursor, &end);
        if (end == cursor) {
            break;
        }
        values[count++] = value;
        cursor = end;
    }
    return count;
}

// A Quake 3 patch is a grid of biquadratic Bezier sub-patches that share their edge
// rows and columns (control points step by two), not one Bezier of degree (W-1, H-1).
// Evaluating it as a single high-degree surface pulls wide patches away from their
// shared control points and opens cracks against the neighbouring brushwork.
void EvaluatePatch(const drawVert_t *control, int width, int height,
    float u, float v, float outPosition[3], float outTexCoord[2], float *outNormal)
{
    outPosition[0] = outPosition[1] = outPosition[2] = 0.0f;
    outTexCoord[0] = outTexCoord[1] = 0.0f;
    if (outNormal != nullptr) {
        outNormal[0] = outNormal[1] = outNormal[2] = 0.0f;
    }

    const int subPatchesU = std::max(1, (width - 1) / 2);
    const int subPatchesV = std::max(1, (height - 1) / 2);
    float fu = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(subPatchesU);
    float fv = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(subPatchesV);
    const int patchU = std::min(static_cast<int>(fu), subPatchesU - 1);
    const int patchV = std::min(static_cast<int>(fv), subPatchesV - 1);
    fu -= static_cast<float>(patchU);
    fv -= static_cast<float>(patchV);

    const float basisU[3] = {(1.0f - fu) * (1.0f - fu), 2.0f * fu * (1.0f - fu), fu * fu};
    const float basisV[3] = {(1.0f - fv) * (1.0f - fv), 2.0f * fv * (1.0f - fv), fv * fv};
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            const float weight = basisU[x] * basisV[y];
            const drawVert_t &point = control[(patchV * 2 + y) * width + (patchU * 2 + x)];
            outPosition[0] += point.xyz[0] * weight;
            outPosition[1] += point.xyz[1] * weight;
            outPosition[2] += point.xyz[2] * weight;
            outTexCoord[0] += point.st[0] * weight;
            outTexCoord[1] += point.st[1] * weight;
            if (outNormal != nullptr) {
                outNormal[0] += point.normal[0] * weight;
                outNormal[1] += point.normal[1] * weight;
                outNormal[2] += point.normal[2] * weight;
            }
        }
    }
}

void OffsetVerticesAlongSmoothNormals(std::vector<QrVertex> &vertices,
    const std::vector<uint32_t> &indices, float offset)
{
    if (offset == 0.0f || vertices.empty() || indices.size() < 3) {
        return;
    }
    std::vector<float> accumNormals(vertices.size() * 3, 0.0f);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i + 0];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
            continue;
        }
        const float *p0 = vertices[i0].position;
        const float *p1 = vertices[i1].position;
        const float *p2 = vertices[i2].position;
        const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
        const float n[3] = {
            e1[1] * e2[2] - e1[2] * e2[1],
            e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0],
        };
        for (uint32_t vi : {i0, i1, i2}) {
            accumNormals[vi * 3 + 0] += n[0];
            accumNormals[vi * 3 + 1] += n[1];
            accumNormals[vi * 3 + 2] += n[2];
        }
    }
    for (size_t vi = 0; vi < vertices.size(); ++vi) {
        float *n = &accumNormals[vi * 3];
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1e-6f) {
            const float scale = offset / len;
            vertices[vi].position[0] += n[0] * scale;
            vertices[vi].position[1] += n[1] * scale;
            vertices[vi].position[2] += n[2] * scale;
        }
    }
}

bool TryBuildMarkTriangle(const EditorSurface &surface, int32_t surfaceIndex, size_t indexOffset,
                          MarkTriangle &out)
{
    out = {};
    out.sourceSurfaceIndex = surfaceIndex;
    out.sourceTriangleIndex = static_cast<uint32_t>(indexOffset / 3);
    out.noMarks = surface.noMarks ||
        (surface.kind == EditorSurfaceKind::Fence && !surface.isCutoutUnderlay);

    for (int corner = 0; corner < 3; ++corner) {
        const uint32_t vertexIndex = surface.indices[indexOffset + static_cast<size_t>(corner)];
        if (vertexIndex >= surface.vertices.size()) {
            return false;
        }
        const QrVertex &vertex = surface.vertices[vertexIndex];
        for (int axis = 0; axis < 3; ++axis) {
            out.points[corner][axis] = vertex.position[axis];
            out.vertexNormals[corner][axis] = vertex.normal[axis];
        }
        out.texCoords[corner][0] = vertex.texCoord[0];
        out.texCoords[corner][1] = vertex.texCoord[1];
    }

    const float ax = out.points[1][0] - out.points[0][0];
    const float ay = out.points[1][1] - out.points[0][1];
    const float az = out.points[1][2] - out.points[0][2];
    const float bx = out.points[2][0] - out.points[0][0];
    const float by = out.points[2][1] - out.points[0][1];
    const float bz = out.points[2][2] - out.points[0][2];
    float normal[3] = {ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx};
    const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    if (length < 1e-6f) {
        return false;
    }
    out.normal[0] = normal[0] / length;
    out.normal[1] = normal[1] / length;
    out.normal[2] = normal[2] / length;

    const float d01 = out.vertexNormals[0][0] * out.vertexNormals[1][0] +
        out.vertexNormals[0][1] * out.vertexNormals[1][1] +
        out.vertexNormals[0][2] * out.vertexNormals[1][2];
    const float d02 = out.vertexNormals[0][0] * out.vertexNormals[2][0] +
        out.vertexNormals[0][1] * out.vertexNormals[2][1] +
        out.vertexNormals[0][2] * out.vertexNormals[2][2];
    out.isCurved = (d01 < 0.999f || d02 < 0.999f);
    return true;
}

void InsertMarkTriangleToGrid(const MarkTriangle &triangle)
{
    constexpr float kMaxWorldCoord = 262144.0f;
    int minCell[3] = {};
    int maxCell[3] = {};
    for (int axis = 0; axis < 3; ++axis) {
        const float low = std::min({triangle.points[0][axis], triangle.points[1][axis],
            triangle.points[2][axis]});
        const float high = std::max({triangle.points[0][axis], triangle.points[1][axis],
            triangle.points[2][axis]});
        if (!std::isfinite(low) || !std::isfinite(high) ||
            low < -kMaxWorldCoord || high > kMaxWorldCoord) {
            return;
        }
        minCell[axis] = static_cast<int>(std::floor((low - 1.0f) / kMarkCellSize));
        maxCell[axis] = static_cast<int>(std::floor((high + 1.0f) / kMarkCellSize));
    }
    const int64_t cellSpan = static_cast<int64_t>(maxCell[0] - minCell[0] + 1) *
        (maxCell[1] - minCell[1] + 1) * (maxCell[2] - minCell[2] + 1);
    if (cellSpan > 65536) {
        return;
    }

    const uint32_t triangleIndex = static_cast<uint32_t>(markTriangles.size());
    markTriangles.push_back(triangle);
    for (int x = minCell[0]; x <= maxCell[0]; ++x) {
        for (int y = minCell[1]; y <= maxCell[1]; ++y) {
            for (int z = minCell[2]; z <= maxCell[2]; ++z) {
                markGrid[MarkCellKey(x, y, z)].push_back(triangleIndex);
            }
        }
    }
}

void BuildMarkAccelerationFromScene(const EditorScene &scene)
{
    markTriangles.clear();
    markGrid.clear();

    size_t triangleCount = 0;
    for (const EditorSurface &surface : scene.surfaces) {
        if (surface.kind != EditorSurfaceKind::Sky && !surface.isOverlayOnly) {
            triangleCount += surface.indices.size() / 3;
        }
    }
    markTriangles.reserve(triangleCount);

    for (size_t s = 0; s < scene.surfaces.size(); ++s) {
        const EditorSurface &surface = scene.surfaces[s];
        if (surface.kind == EditorSurfaceKind::Sky || surface.isOverlayOnly) {
            continue;
        }
        for (size_t idx = 0; idx + 2 < surface.indices.size(); idx += 3) {
            MarkTriangle tri;
            if (TryBuildMarkTriangle(surface, static_cast<int32_t>(s), idx, tri)) {
                InsertMarkTriangleToGrid(tri);
            }
        }
    }

    HostLog("BuildMarkAccelerationFromScene: triangles=%zu cells=%zu",
        markTriangles.size(), markGrid.size());
}

struct BspLumpView
{
    const uint8_t *base = nullptr;
    long fileLength = 0;
    const lump_t *lumps = nullptr;
    const dshader_t *shaders = nullptr;
    int shaderCount = 0;
    const dsurface_t *surfaces = nullptr;
    int surfaceCount = 0;
    const drawVert_t *verts = nullptr;
    int vertexCount = 0;
    const int *indices = nullptr;
    int indexCount = 0;
    const dmodel_t *models = nullptr;
    int modelCount = 0;

    bool LumpValid(int index, size_t elementSize) const
    {
        if (lumps == nullptr || index < 0 || index >= HEADER_LUMPS) {
            return false;
        }
        const lump_t &lump = lumps[index];
        const uint64_t end = static_cast<uint64_t>(lump.fileofs) + lump.filelen;
        return end <= static_cast<uint64_t>(fileLength) &&
               (elementSize == 0 || (lump.filelen % elementSize) == 0);
    }
};

bool ParseBspLumpView(const void *fileData, long fileLength, BspLumpView &out)
{
    out = {};
    if (fileData == nullptr || fileLength < static_cast<long>(sizeof(dheader_t))) {
        return false;
    }
    const auto *base = static_cast<const uint8_t *>(fileData);
    const auto *header = reinterpret_cast<const dheader_t *>(base);
    if (std::memcmp(&header->ident, "IBSP", 4) != 0 || header->version != BSP_VERSION) {
        return false;
    }
    out.base = base;
    out.fileLength = fileLength;
    out.lumps = header->lumps;
    if (!out.LumpValid(LUMP_SHADERS, sizeof(dshader_t)) ||
        !out.LumpValid(LUMP_SURFACES, sizeof(dsurface_t)) ||
        !out.LumpValid(LUMP_DRAWVERTS, sizeof(drawVert_t)) ||
        !out.LumpValid(LUMP_DRAWINDEXES, sizeof(int)) ||
        !out.LumpValid(LUMP_MODELS, sizeof(dmodel_t))) {
        return false;
    }
    out.shaders = reinterpret_cast<const dshader_t *>(base + out.lumps[LUMP_SHADERS].fileofs);
    out.shaderCount = static_cast<int>(out.lumps[LUMP_SHADERS].filelen / sizeof(dshader_t));
    out.surfaces = reinterpret_cast<const dsurface_t *>(base + out.lumps[LUMP_SURFACES].fileofs);
    out.surfaceCount = static_cast<int>(out.lumps[LUMP_SURFACES].filelen / sizeof(dsurface_t));
    out.verts = reinterpret_cast<const drawVert_t *>(base + out.lumps[LUMP_DRAWVERTS].fileofs);
    out.vertexCount = static_cast<int>(out.lumps[LUMP_DRAWVERTS].filelen / sizeof(drawVert_t));
    out.indices = reinterpret_cast<const int *>(base + out.lumps[LUMP_DRAWINDEXES].fileofs);
    out.indexCount = static_cast<int>(out.lumps[LUMP_DRAWINDEXES].filelen / sizeof(int));
    out.models = reinterpret_cast<const dmodel_t *>(base + out.lumps[LUMP_MODELS].fileofs);
    out.modelCount = static_cast<int>(out.lumps[LUMP_MODELS].filelen / sizeof(dmodel_t));
    return true;
}

/// Appends flipped backfaces with negated normals so two-sided shaders trace from both sides.
void AppendTwoSidedTriangles(const std::vector<QrVertex> &srcVerts,
    const std::vector<uint32_t> &srcIndices, std::vector<QrVertex> &outVerts,
    std::vector<uint32_t> &outIndices)
{
    outVerts = srcVerts;
    outIndices = srcIndices;
    if (srcVerts.empty() || srcIndices.size() < 3) {
        return;
    }
    const uint32_t backBase = static_cast<uint32_t>(outVerts.size());
    outVerts.reserve(srcVerts.size() * 2);
    for (const QrVertex &vertex : srcVerts) {
        QrVertex back = vertex;
        back.normal[0] = -back.normal[0];
        back.normal[1] = -back.normal[1];
        back.normal[2] = -back.normal[2];
        outVerts.push_back(back);
    }
    outIndices.reserve(srcIndices.size() * 2);
    for (size_t i = 0; i + 2 < srcIndices.size(); i += 3) {
        outIndices.push_back(backBase + srcIndices[i + 0]);
        outIndices.push_back(backBase + srcIndices[i + 2]);
        outIndices.push_back(backBase + srcIndices[i + 1]);
    }
}

struct BspPolygonSurfaceMeta
{
    float generatedNormal[3] = {0.0f, 0.0f, 1.0f};
    float planeNormal[3] = {0.0f, 0.0f, 1.0f};
    float planeDist = 0.0f;
    float authoredNormal[3] = {0.0f, 0.0f, 1.0f};
    float originalWindingNormal[3] = {0.0f, 0.0f, 1.0f};
    float uploadedWindingNormal[3] = {0.0f, 0.0f, 1.0f};
    bool windingFlipped = false;
    bool planarWindingKept = false;
};

bool BuildBspPatchMesh(const dsurface_t &surface, const drawVert_t *verts, const char *shaderName,
    std::vector<QrVertex> &patchVerts, std::vector<uint32_t> &patchIndices,
    float outAuthoredNormal[3], float outPatchNormal[3])
{
    constexpr int level = 8;
    const int columns = std::max(1, (surface.patchWidth - 1) / 2) * level + 1;
    const int rows = std::max(1, (surface.patchHeight - 1) / 2) * level + 1;
    patchVerts.resize(static_cast<size_t>(columns) * rows);
    std::vector<float> evalNormals(patchVerts.size() * 3, 0.0f);
    for (int y = 0; y < rows; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(rows - 1);
        for (int x = 0; x < columns; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(columns - 1);
            const size_t vertIdx = static_cast<size_t>(y) * columns + x;
            float position[3] = {};
            float texCoord[2] = {};
            EvaluatePatch(verts + surface.firstVert, surface.patchWidth, surface.patchHeight,
                u, v, position, texCoord, &evalNormals[vertIdx * 3]);
            ApplyShaderBaseScale(shaderName, texCoord[0], texCoord[1]);
            QrVertex &dst = patchVerts[vertIdx];
            dst = {};
            dst.position[0] = position[0];
            dst.position[1] = position[1];
            dst.position[2] = position[2];
            dst.texCoord[0] = texCoord[0];
            dst.texCoord[1] = texCoord[1];
            dst.packedColor = 0xFFFFFFFFu;
        }
    }

    patchIndices.clear();
    patchIndices.reserve(static_cast<size_t>((rows - 1) * (columns - 1) * 6));
    for (int y = 0; y < rows - 1; ++y) {
        for (int x = 0; x < columns - 1; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(y * columns + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + static_cast<uint32_t>(columns);
            const uint32_t i3 = i2 + 1;
            // Q3 tessellates grids as (i0,i2,i1)/(i1,i2,i3); the ray tracer needs
            // the same handedness as the swapped planar and triangle soup surfaces.
            patchIndices.push_back(i0);
            patchIndices.push_back(i1);
            patchIndices.push_back(i2);
            patchIndices.push_back(i1);
            patchIndices.push_back(i3);
            patchIndices.push_back(i2);
        }
    }

    float authoredNormal[3] = {0.0f, 0.0f, 0.0f};
    for (int vertexIndex = 0; vertexIndex < surface.numVerts; ++vertexIndex) {
        const drawVert_t &source = verts[surface.firstVert + vertexIndex];
        for (int axis = 0; axis < 3; ++axis) {
            authoredNormal[axis] += source.normal[axis];
        }
    }
    NormalizeVector(authoredNormal);
    std::memcpy(outAuthoredNormal, authoredNormal, sizeof(authoredNormal));

    float sumNormal[3] = {0.0f, 0.0f, 0.0f};
    for (size_t k = 0; k + 2 < patchIndices.size(); k += 3) {
        const uint32_t i0 = patchIndices[k + 0];
        const uint32_t i1 = patchIndices[k + 1];
        const uint32_t i2 = patchIndices[k + 2];
        const QrVertex &p0 = patchVerts[i0];
        const QrVertex &p1 = patchVerts[i1];
        const QrVertex &p2 = patchVerts[i2];
        const float ax = p1.position[0] - p0.position[0];
        const float ay = p1.position[1] - p0.position[1];
        const float az = p1.position[2] - p0.position[2];
        const float bx = p2.position[0] - p0.position[0];
        const float by = p2.position[1] - p0.position[1];
        const float bz = p2.position[2] - p0.position[2];
        const float tn[3] = {
            ay * bz - az * by,
            az * bx - ax * bz,
            ax * by - ay * bx,
        };
        for (uint32_t vi : {i0, i1, i2}) {
            patchVerts[vi].normal[0] += tn[0];
            patchVerts[vi].normal[1] += tn[1];
            patchVerts[vi].normal[2] += tn[2];
        }
        sumNormal[0] += tn[0];
        sumNormal[1] += tn[1];
        sumNormal[2] += tn[2];
    }

    // Weld opposite seam edge normals on closed cylindrical/ring patches so
    // 360-degree towers and domes shade smoothly without a seam crease.
    const auto weldSeamPair = [&](size_t a, size_t b) {
        const float dx = patchVerts[a].position[0] - patchVerts[b].position[0];
        const float dy = patchVerts[a].position[1] - patchVerts[b].position[1];
        const float dz = patchVerts[a].position[2] - patchVerts[b].position[2];
        if (dx * dx + dy * dy + dz * dz < 1.0e-4f) {
            for (int c = 0; c < 3; ++c) {
                const float merged = patchVerts[a].normal[c] + patchVerts[b].normal[c];
                patchVerts[a].normal[c] = merged;
                patchVerts[b].normal[c] = merged;
            }
        }
    };
    for (int y = 0; y < rows; ++y) {
        const size_t rowBase = static_cast<size_t>(y) * columns;
        weldSeamPair(rowBase, rowBase + static_cast<size_t>(columns - 1));
    }
    for (int x = 0; x < columns; ++x) {
        const size_t ux = static_cast<size_t>(x);
        weldSeamPair(ux, static_cast<size_t>(rows - 1) * columns + ux);
    }

    float pwDot = 0.0f;
    for (size_t vi = 0; vi < patchVerts.size(); ++vi) {
        pwDot += patchVerts[vi].normal[0] * evalNormals[vi * 3 + 0] +
            patchVerts[vi].normal[1] * evalNormals[vi * 3 + 1] +
            patchVerts[vi].normal[2] * evalNormals[vi * 3 + 2];
    }
    if (pwDot < 0.0f) {
        for (size_t k = 0; k + 2 < patchIndices.size(); k += 3) {
            std::swap(patchIndices[k + 1], patchIndices[k + 2]);
        }
        for (QrVertex &pv : patchVerts) {
            pv.normal[0] = -pv.normal[0];
            pv.normal[1] = -pv.normal[1];
            pv.normal[2] = -pv.normal[2];
        }
        sumNormal[0] = -sumNormal[0];
        sumNormal[1] = -sumNormal[1];
        sumNormal[2] = -sumNormal[2];
    }
    for (size_t vi = 0; vi < patchVerts.size(); ++vi) {
        QrVertex &pv = patchVerts[vi];
        const float len = std::sqrt(
            pv.normal[0] * pv.normal[0] +
            pv.normal[1] * pv.normal[1] +
            pv.normal[2] * pv.normal[2]);
        if (len > 1.0e-6f) {
            pv.normal[0] /= len;
            pv.normal[1] /= len;
            pv.normal[2] /= len;
        } else {
            const float *en = &evalNormals[vi * 3];
            const float elen = std::sqrt(en[0] * en[0] + en[1] * en[1] + en[2] * en[2]);
            if (elen > 1.0e-6f) {
                pv.normal[0] = en[0] / elen;
                pv.normal[1] = en[1] / elen;
                pv.normal[2] = en[2] / elen;
            } else {
                pv.normal[0] = 0.0f;
                pv.normal[1] = 0.0f;
                pv.normal[2] = 1.0f;
            }
        }
    }
    outPatchNormal[0] = sumNormal[0];
    outPatchNormal[1] = sumNormal[1];
    outPatchNormal[2] = sumNormal[2];
    NormalizeVector(outPatchNormal);
    return !patchVerts.empty() && !patchIndices.empty();
}

bool BuildBspPolygonMesh(const dsurface_t &surface, const drawVert_t *verts, const int *indices,
    const char *shaderName, int normalMode, std::vector<QrVertex> &outVerts,
    std::vector<uint32_t> &outIndices, BspPolygonSurfaceMeta &meta)
{
    meta = {};
    float faceNormal[3] = {0.0f, 0.0f, 1.0f};
    if (surface.surfaceType == MST_PLANAR) {
        faceNormal[0] = surface.lightmapVecs[2][0];
        faceNormal[1] = surface.lightmapVecs[2][1];
        faceNormal[2] = surface.lightmapVecs[2][2];
    } else {
        const int i0 = indices[surface.firstIndex + 0];
        const int i1 = indices[surface.firstIndex + 1];
        const int i2 = indices[surface.firstIndex + 2];
        if (i0 >= 0 && i0 < surface.numVerts && i1 >= 0 && i1 < surface.numVerts &&
            i2 >= 0 && i2 < surface.numVerts) {
            const drawVert_t &v0 = verts[surface.firstVert + i0];
            const drawVert_t &v1 = verts[surface.firstVert + i1];
            const drawVert_t &v2 = verts[surface.firstVert + i2];
            const float ax = v1.xyz[0] - v0.xyz[0];
            const float ay = v1.xyz[1] - v0.xyz[1];
            const float az = v1.xyz[2] - v0.xyz[2];
            const float bx = v2.xyz[0] - v0.xyz[0];
            const float by = v2.xyz[1] - v0.xyz[1];
            const float bz = v2.xyz[2] - v0.xyz[2];
            faceNormal[0] = ay * bz - az * by;
            faceNormal[1] = az * bx - ax * bz;
            faceNormal[2] = ax * by - ay * bx;
        }
    }

    float authoredNormal[3] = {0.0f, 0.0f, 0.0f};
    for (int vertexIndex = 0; vertexIndex < surface.numVerts; ++vertexIndex) {
        const drawVert_t &source = verts[surface.firstVert + vertexIndex];
        for (int axis = 0; axis < 3; ++axis) {
            authoredNormal[axis] += source.normal[axis];
        }
    }
    NormalizeVector(authoredNormal);

    const float normalLength = std::sqrt(faceNormal[0] * faceNormal[0] +
        faceNormal[1] * faceNormal[1] + faceNormal[2] * faceNormal[2]);
    if (normalLength > 1e-6f) {
        faceNormal[0] /= normalLength;
        faceNormal[1] /= normalLength;
        faceNormal[2] /= normalLength;
    } else if (authoredNormal[0] != 0.0f || authoredNormal[1] != 0.0f || authoredNormal[2] != 0.0f) {
        faceNormal[0] = authoredNormal[0];
        faceNormal[1] = authoredNormal[1];
        faceNormal[2] = authoredNormal[2];
    } else {
        faceNormal[0] = 0.0f;
        faceNormal[1] = 0.0f;
        faceNormal[2] = 1.0f;
    }

    std::memcpy(meta.generatedNormal, faceNormal, sizeof(faceNormal));
    if (surface.surfaceType == MST_PLANAR) {
        std::memcpy(meta.planeNormal, faceNormal, sizeof(faceNormal));
        meta.planeDist = faceNormal[0] * verts[surface.firstVert].xyz[0] +
            faceNormal[1] * verts[surface.firstVert].xyz[1] +
            faceNormal[2] * verts[surface.firstVert].xyz[2];
    }
    std::memcpy(meta.authoredNormal, authoredNormal, sizeof(authoredNormal));

    outVerts.resize(static_cast<size_t>(surface.numVerts));
    for (int i = 0; i < surface.numVerts; ++i) {
        const drawVert_t &src = verts[surface.firstVert + i];
        QrVertex &dst = outVerts[static_cast<size_t>(i)];
        dst = {};
        dst.position[0] = src.xyz[0];
        dst.position[1] = src.xyz[1];
        dst.position[2] = src.xyz[2];
        if (surface.surfaceType == MST_TRIANGLE_SOUP) {
            const float srcLen = std::sqrt(
                src.normal[0] * src.normal[0] +
                src.normal[1] * src.normal[1] +
                src.normal[2] * src.normal[2]);
            if (srcLen > 1.0e-6f) {
                dst.normal[0] = src.normal[0] / srcLen;
                dst.normal[1] = src.normal[1] / srcLen;
                dst.normal[2] = src.normal[2] / srcLen;
            } else {
                dst.normal[0] = faceNormal[0];
                dst.normal[1] = faceNormal[1];
                dst.normal[2] = faceNormal[2];
            }
        } else {
            dst.normal[0] = faceNormal[0];
            dst.normal[1] = faceNormal[1];
            dst.normal[2] = faceNormal[2];
        }
        dst.texCoord[0] = src.st[0];
        dst.texCoord[1] = src.st[1];
        ApplyShaderBaseScale(shaderName, dst.texCoord[0], dst.texCoord[1]);
        dst.packedColor = 0xFFFFFFFFu;
    }

    outIndices.resize(static_cast<size_t>(surface.numIndexes));
    for (int i = 0; i < surface.numIndexes; ++i) {
        const int index = indices[surface.firstIndex + i];
        outIndices[static_cast<size_t>(i)] =
            (index >= 0 && index < surface.numVerts) ? static_cast<uint32_t>(index) : 0u;
    }
    if (outIndices.size() >= 3) {
        const drawVert_t &p0 = verts[surface.firstVert + outIndices[0]];
        const drawVert_t &p1 = verts[surface.firstVert + outIndices[1]];
        const drawVert_t &p2 = verts[surface.firstVert + outIndices[2]];
        const float ax = p1.xyz[0] - p0.xyz[0];
        const float ay = p1.xyz[1] - p0.xyz[1];
        const float az = p1.xyz[2] - p0.xyz[2];
        const float bx = p2.xyz[0] - p0.xyz[0];
        const float by = p2.xyz[1] - p0.xyz[1];
        const float bz = p2.xyz[2] - p0.xyz[2];
        float originalWindingNormal[3] = {
            ay * bz - az * by,
            az * bx - ax * bz,
            ax * by - ay * bx,
        };
        NormalizeVector(originalWindingNormal);
        std::memcpy(meta.originalWindingNormal, originalWindingNormal, sizeof(originalWindingNormal));
        std::memcpy(meta.uploadedWindingNormal, originalWindingNormal, sizeof(originalWindingNormal));
    }

    // Q3 treats clockwise as front facing, which is the opposite handedness of
    // QuakeRay's ray traced shadows, so every triangle is flipped once here.
    bool flipWinding = normalMode < 2 || normalMode >= 5;
    bool keptSurfaceWinding = false;
    if (flipWinding && normalMode < 5 && surface.surfaceType == MST_PLANAR &&
        surface.numIndexes >= 3) {
        const int w0 = indices[surface.firstIndex + 0];
        const int w1 = indices[surface.firstIndex + 1];
        const int w2 = indices[surface.firstIndex + 2];
        if (w0 >= 0 && w1 >= 0 && w2 >= 0 && w0 < surface.numVerts &&
            w1 < surface.numVerts && w2 < surface.numVerts) {
            const drawVert_t &p0 = verts[surface.firstVert + w0];
            const drawVert_t &p1 = verts[surface.firstVert + w1];
            const drawVert_t &p2 = verts[surface.firstVert + w2];
            const float e1[3] = {
                p1.xyz[0] - p0.xyz[0], p1.xyz[1] - p0.xyz[1], p1.xyz[2] - p0.xyz[2]};
            const float e2[3] = {
                p2.xyz[0] - p0.xyz[0], p2.xyz[1] - p0.xyz[1], p2.xyz[2] - p0.xyz[2]};
            const float windingNormal[3] = {
                e1[1] * e2[2] - e1[2] * e2[1],
                e1[2] * e2[0] - e1[0] * e2[2],
                e1[0] * e2[1] - e1[1] * e2[0],
            };
            const float alignment = windingNormal[0] * faceNormal[0] +
                windingNormal[1] * faceNormal[1] + windingNormal[2] * faceNormal[2];
            // A few compiled planar surfaces already wind towards their own plane
            // normal; flipping those inverts their lighting, so they are left alone.
            if (alignment > 0.0f) {
                flipWinding = false;
                keptSurfaceWinding = true;
            }
        }
    }
    if (flipWinding) {
        for (size_t i = 0; i + 2 < outIndices.size(); i += 3) {
            std::swap(outIndices[i + 1], outIndices[i + 2]);
        }
        for (int axis = 0; axis < 3; ++axis) {
            meta.uploadedWindingNormal[axis] = -meta.uploadedWindingNormal[axis];
        }
    }
    meta.windingFlipped = flipWinding;
    meta.planarWindingKept = keptSurfaceWinding;
    return !outVerts.empty() && !outIndices.empty();
}

bool ValidateBspDrawableSurface(const dsurface_t &surface, int vertexCount, int indexCount,
    int shaderCount)
{
    if (surface.surfaceType != MST_PLANAR && surface.surfaceType != MST_TRIANGLE_SOUP &&
        surface.surfaceType != MST_PATCH) {
        return false;
    }
    if (surface.surfaceType == MST_PATCH) {
        // Q3 patches have odd dimensions (3, 5, 7, ...) up to MAX_PATCH_SIZE;
        // the 64-bit product cannot wrap for hostile values.
        constexpr int kMaxPatchSize = 65;
        if (surface.patchWidth < 3 || surface.patchHeight < 3 ||
            surface.patchWidth > kMaxPatchSize || surface.patchHeight > kMaxPatchSize ||
            (surface.patchWidth & 1) == 0 || (surface.patchHeight & 1) == 0 ||
            static_cast<int64_t>(surface.patchWidth) * surface.patchHeight > surface.numVerts) {
            return false;
        }
    } else if (surface.numVerts < 3 || surface.numIndexes < 3) {
        return false;
    }
    if (surface.firstVert < 0 || surface.numVerts > vertexCount - surface.firstVert) {
        return false;
    }
    if (surface.surfaceType != MST_PATCH &&
        (surface.firstIndex < 0 || surface.numIndexes > indexCount - surface.firstIndex)) {
        return false;
    }
    return surface.shaderNum >= 0 && surface.shaderNum < shaderCount;
}

/// Loads all inline BSP brush submodels (*1..*N) so dynamic movers enter the scene BLAS.
void LoadInlineBspModels(const char *bspName)
{
    for (auto &entry : modelAssets) {
        if (entry.second.isBspSubmodel) {
            entry.second = {};
            entry.second.isBspSubmodel = true;
        }
    }
    if (bspName == nullptr || bspName[0] == '\0') {
        return;
    }

    LoadShaderScripts();
    void *fileData = nullptr;
    const long fileLength = ri.FS_ReadFile(bspName, &fileData);
    if (fileLength <= 0 || fileData == nullptr) {
        return;
    }
    struct FileGuard {
        void *data;
        ~FileGuard() { if (data != nullptr) { ri.FS_FreeFile(data); } }
    } fileGuard{fileData};

    BspLumpView view;
    if (!ParseBspLumpView(fileData, fileLength, view) || view.modelCount <= 1) {
        HostLog("LoadInlineBspModels: %s submodels=0", bspName);
        return;
    }

    const int normalMode = qrayNormalMode != nullptr ? qrayNormalMode->integer : 0;
    for (int modelIdx = 1; modelIdx < view.modelCount; ++modelIdx) {
        const dmodel_t &bspModel = view.models[modelIdx];
        ModelAsset submodel{};
        submodel.valid = true;
        submodel.isBspSubmodel = true;
        submodel.numFrames = 1;
        char modelKey[32] = {};
        std::snprintf(modelKey, sizeof(modelKey), "*%d", modelIdx);
        submodel.name = modelKey;
        float maxCornerDistSq = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            submodel.bounds[0][axis] = bspModel.mins[axis];
            submodel.bounds[1][axis] = bspModel.maxs[axis];
            const float absCoord = std::max(std::fabs(bspModel.mins[axis]), std::fabs(bspModel.maxs[axis]));
            maxCornerDistSq += absCoord * absCoord;
        }
        submodel.radius = std::sqrt(maxCornerDistSq);

        const int firstSurface = std::max(0, bspModel.firstSurface);
        const int endSurface = std::min(view.surfaceCount,
            firstSurface + std::max(0, bspModel.numSurfaces));
        std::unordered_map<int, size_t> surfaceByShader;
        for (int surfaceIndex = firstSurface; surfaceIndex < endSurface; ++surfaceIndex) {
            const dsurface_t &surface = view.surfaces[surfaceIndex];
            if (!ValidateBspDrawableSurface(surface, view.vertexCount, view.indexCount,
                    view.shaderCount)) {
                continue;
            }
            const dshader_t &shader = view.shaders[surface.shaderNum];
            if ((shader.surfaceFlags & (SURF_SKY | SURF_NODRAW)) != 0) {
                continue;
            }

            std::vector<QrVertex> meshVerts;
            std::vector<uint32_t> meshIndices;
            if (surface.surfaceType == MST_PATCH) {
                float authoredNormal[3] = {};
                float patchNormal[3] = {};
                if (!BuildBspPatchMesh(surface, view.verts, shader.shader,
                        meshVerts, meshIndices, authoredNormal, patchNormal)) {
                    continue;
                }
            } else {
                BspPolygonSurfaceMeta meta;
                if (!BuildBspPolygonMesh(surface, view.verts, view.indices, shader.shader,
                        normalMode, meshVerts, meshIndices, meta)) {
                    continue;
                }
            }

            size_t slotIndex = 0;
            const auto existingSlot = surfaceByShader.find(surface.shaderNum);
            if (existingSlot != surfaceByShader.end()) {
                slotIndex = existingSlot->second;
            } else {
                slotIndex = submodel.surfaces.size();
                surfaceByShader.emplace(surface.shaderNum, slotIndex);
                ModelSurface group;
                group.surfaceName = shader.shader;
                group.shaderName = shader.shader;
                submodel.surfaces.push_back(std::move(group));
            }
            ModelSurface &dstSurface = submodel.surfaces[slotIndex];
            const uint32_t baseVertex = static_cast<uint32_t>(dstSurface.numVerts);
            dstSurface.numVerts += static_cast<int>(meshVerts.size());
            dstSurface.positions.reserve(dstSurface.positions.size() + meshVerts.size() * 3);
            dstSurface.normals.reserve(dstSurface.normals.size() + meshVerts.size() * 3);
            dstSurface.texCoords.reserve(dstSurface.texCoords.size() + meshVerts.size() * 2);
            for (const QrVertex &vertex : meshVerts) {
                dstSurface.positions.push_back(vertex.position[0]);
                dstSurface.positions.push_back(vertex.position[1]);
                dstSurface.positions.push_back(vertex.position[2]);
                dstSurface.normals.push_back(vertex.normal[0]);
                dstSurface.normals.push_back(vertex.normal[1]);
                dstSurface.normals.push_back(vertex.normal[2]);
                dstSurface.texCoords.push_back(vertex.texCoord[0]);
                dstSurface.texCoords.push_back(vertex.texCoord[1]);
            }
            dstSurface.indices.reserve(dstSurface.indices.size() + meshIndices.size());
            for (uint32_t idx : meshIndices) {
                dstSurface.indices.push_back(baseVertex + idx);
            }
        }

        qhandle_t handle = 0;
        const auto existingHandle = modelHandles.find(modelKey);
        if (existingHandle != modelHandles.end()) {
            handle = existingHandle->second;
            modelAssets[handle] = std::move(submodel);
        } else {
            handle = nextModelHandle++;
            modelHandles.emplace(modelKey, handle);
            modelAssets.emplace(handle, std::move(submodel));
        }
    }

    HostLog("LoadInlineBspModels: %s submodels=%d", bspName, view.modelCount - 1);
}

bool ReadBspFileBytes(const char *name, std::vector<uint8_t> &outBytes)
{
    outBytes.clear();
    void *fileData = nullptr;
    const long fileLength = ri.FS_ReadFile(name, &fileData);
    HostLog("LoadBspGeometry: FS_ReadFile len=%ld data=%p", fileLength, fileData);
    if (fileLength <= 0 || fileData == nullptr) {
        if (fileData != nullptr) {
            ri.FS_FreeFile(fileData);
        }
        ri.Printf(PRINT_WARNING, "rendererqray: cannot read %s\n", name);
        return false;
    }

    const auto *base = static_cast<const uint8_t *>(fileData);
    const auto *header = reinterpret_cast<const dheader_t *>(base);
    const bool validBsp = fileLength >= static_cast<long>(sizeof(dheader_t)) &&
        std::memcmp(&header->ident, "IBSP", 4) == 0 && header->version == BSP_VERSION;
    if (validBsp) {
        outBytes.assign(base, base + static_cast<size_t>(fileLength));
    } else {
        HostLog("LoadBspGeometry: %s is not a Quake 3 BSP", name);
        ri.Printf(PRINT_WARNING, "rendererqray: %s is not a Quake 3 BSP\n", name);
    }
    ri.FS_FreeFile(fileData);
    return validBsp;
}

void LogLoadedWorldSummary(const char *name)
{
    const EditorScene &scene = sceneRuntime.scene;
    size_t overlayStages = 0;
    for (const EditorOverlaySurface &overlay : scene.overlaySurfaces) {
        overlayStages += overlay.stages.size();
    }
    HostLog("LoadBspGeometry: %s surfaces=%zu overlayBatches=%zu overlayStages=%zu "
        "clusters=%zu areaLights=%zu customLights=%zu",
        name, scene.surfaces.size(), scene.overlaySurfaces.size(), overlayStages,
        scene.worldClusters.mins.size(), sceneRuntime.emissiveLights.size(),
        sceneRuntime.customLights.size());
    if (scene.hasBounds) {
        HostLog("LoadBspGeometry: bounds min=(%f %f %f) max=(%f %f %f)",
            scene.boundsMin[0], scene.boundsMin[1], scene.boundsMin[2],
            scene.boundsMax[0], scene.boundsMax[1], scene.boundsMax[2]);
    }
    ri.Printf(PRINT_ALL, "rendererqray: uploaded %zu world surfaces from %s\n",
        scene.surfaces.size(), name);
}

bool LoadBspGeometryInner(const char *name)
{
    if (qrayInstance == QR_NULL_HANDLE || name == nullptr || name[0] == '\0') {
        return false;
    }

    HostLog("LoadBspGeometry: %s", name);
    if (!q3AdapterRegistered) {
        RegisterQ3EditorFormatAdapter();
        q3AdapterRegistered = true;
    }
    LoadShaderScripts();
    ConfigureGameAssets();
    sceneRuntime.DestroyGpuResources(qrayInstance);
    markTriangles.clear();
    markGrid.clear();

    std::vector<uint8_t> bspBytes;
    if (!ReadBspFileBytes(name, bspBytes)) {
        return false;
    }

    EditorSceneLoadOptions loadOpts;
    loadOpts.assets = &gameAssets;
    loadOpts.includeInlineSubmodels = false;
    std::string loadError;
    if (!sceneRuntime.LoadBspSceneMemory(bspBytes, name, nullptr, loadOpts, loadError)) {
        HostLog("LoadBspGeometry: LoadBspSceneMemory failed for %s: %s", name, loadError.c_str());
        ri.Printf(PRINT_WARNING, "rendererqray: failed to load %s: %s\n", name, loadError.c_str());
        return false;
    }

    sceneRuntime.materials.Rebuild(sceneRuntime.scene);
    sceneRuntime.settings = EditorRenderSettings{};
    sceneRuntime.sun = EditorSunState{};
    sceneRuntime.ApplySceneRenderDefaults();

    if (AuthoredModeEnabled()) {
        LoadAuthoredSessionFilesForMap(name);
    } else {
        ApplyQrayCvarsToRuntime(false);
    }
    ClearQrayCvarModifiedFlags();

    sceneRuntime.CreateTextureMaterials(qrayInstance, gameAssets, SceneRuntimeLog);
    if (!sceneRuntime.UploadStaticGeometry(qrayInstance, gameAssets, defaultMaterial,
            SceneRuntimeLog)) {
        return false;
    }
    sceneRuntime.UploadWorldClusters(qrayInstance, SceneRuntimeLog);
    BuildMarkAccelerationFromScene(sceneRuntime.scene);
    LogLoadedWorldSummary(name);
    return !sceneRuntime.scene.surfaces.empty();
}

bool LoadBspGeometry(const char *name)
{
    ResetProceduralSmoke();
#ifdef _WIN32
    __try {
        return LoadBspGeometryInner(name);
    } __except (HostExceptionFilter(GetExceptionInformation())) {
        return false;
    }
#else
    try {
        return LoadBspGeometryInner(name);
    } catch (const std::exception &exception) {
        HostLog("LoadBspGeometry: exception: %s", exception.what());
        ri.Printf(PRINT_WARNING, "rendererqray: failed to load %s: %s\n", name, exception.what());
        return false;
    }
#endif
}

void ResetReadback()
{
    readbackBuffer.clear();
    readbackReadyPixels.clear();
    readbackWidth = 0;
    readbackHeight = 0;
    readbackRowPitch = 0;
    readbackBufferSize = 0;
    readbackRequest = 0;
    readbackPending = false;
    readbackValid = false;
}

void UpdateReadback(bool allowRequest)
{
    if (qrayInstance == QR_NULL_HANDLE) {
        return;
    }

    if (readbackWidth == 0) {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t rowPitch = 0;
        uint64_t bufferSize = 0;
        const QrResult result = qrGetFrameReadbackRequirements(
            qrayInstance, static_cast<QrFrameReadbackFormat>(kReadbackFormat), QR_TRUE,
            &width, &height, &rowPitch, &bufferSize);
        if (result == QR_SUCCESS && width > 0 && height > 0 && bufferSize > 0) {
            readbackWidth = width;
            readbackHeight = height;
            readbackRowPitch = rowPitch;
            readbackBufferSize = bufferSize;
            readbackBuffer.assign(static_cast<size_t>(bufferSize), 0);
            ri.Printf(PRINT_ALL, "rendererqray: readback %ux%u pitch %u size %llu\n",
                width, height, rowPitch, static_cast<unsigned long long>(bufferSize));
        }
        return;
    }

    if (allowRequest && !readbackPending && !readbackBuffer.empty()) {
        QrFrameReadbackInfo info{};
        info.width = readbackWidth;
        info.height = readbackHeight;
        info.rowPitch = readbackRowPitch;
        info.bufferSize = readbackBufferSize;
        info.format = static_cast<QrFrameReadbackFormat>(kReadbackFormat);
        info.includeUi = QR_TRUE;
        info.pDst = readbackBuffer.data();
        info.frameId = QR_FRAME_ID_LATEST;

        const QrResult result = qrRequestFrameReadback(qrayInstance, &info, &readbackRequest);
        if (result == QR_SUCCESS) {
            readbackPending = true;
        } else if (result == QR_READBACK_BUSY) {
            readbackPending = true;
        } else if (result == QR_FRAME_NOT_READY) {
            return;
        } else if (result == QR_READBACK_FAILED || result == QR_READBACK_INVALID) {
            ResetReadback();
        }
    }

    if (readbackPending) {
        QrBool32 done = QR_FALSE;
        const QrResult result = qrGetFrameReadbackStatus(qrayInstance, readbackRequest, &done);
        if (result == QR_SUCCESS && done == QR_TRUE) {
            readbackReadyPixels.swap(readbackBuffer);
            readbackBuffer.resize(static_cast<size_t>(readbackBufferSize));
            readbackValid = true;
            readbackPending = false;
        } else if (result == QR_READBACK_FAILED || result == QR_READBACK_INVALID) {
            ResetReadback();
        }
    }
}

void CopyReadbackToCapture()
{
    if (!capturePending) {
        return;
    }
    // TakeVideoFrame hands over client-owned buffers that are only valid for this
    // call sequence (CL_CloseAVI frees them), so a frame is never deferred.
    capturePending = false;
    if (captureBuffer == nullptr || captureWidth <= 0 || captureHeight <= 0) {
        return;
    }

    // Produce exactly what RB_TakeVideoFrameCmd produces: captureWidth x captureHeight,
    // bottom-up RGB rows (glReadPixels order). Pixels the readback does not cover, or the
    // whole frame while no readback is ready yet, are black so the AVI keeps its timing.
    const int width = captureWidth;
    const int height = captureHeight;
    const size_t linePitch = static_cast<size_t>(width) * 3;
    const bool haveReadback = readbackValid && readbackWidth > 0 && readbackHeight > 0 &&
        readbackReadyPixels.size() >= static_cast<size_t>(readbackRowPitch) * readbackHeight;
    const int copyWidth = haveReadback ? std::min<int>(width, static_cast<int>(readbackWidth)) : 0;
    const int copyHeight = haveReadback ? std::min<int>(height, static_cast<int>(readbackHeight)) : 0;

    std::memset(captureBuffer, 0, linePitch * static_cast<size_t>(height));
    for (int y = 0; y < copyHeight; ++y) {
        const uint8_t *srcRow = readbackReadyPixels.data() + static_cast<size_t>(y) * readbackRowPitch;
        byte *dstRow = captureBuffer + static_cast<size_t>(height - 1 - y) * linePitch;
        for (int x = 0; x < copyWidth; ++x) {
            dstRow[x * 3 + 0] = srcRow[x * 4 + 0];
            dstRow[x * 3 + 1] = srcRow[x * 4 + 1];
            dstRow[x * 3 + 2] = srcRow[x * 4 + 2];
        }
    }

    if (captureEncodeBuffer == nullptr) {
        return;
    }
    if (captureMotionJpeg) {
        static bool warnedMotionJpeg = false;
        if (!warnedMotionJpeg) {
            warnedMotionJpeg = true;
            ri.Printf(PRINT_WARNING, "rendererqray: MotionJPEG AVI encoding is not supported; set cl_aviMotionJpeg 0\n");
        }
        return;
    }

    // Uncompressed AVI: BGR with rows padded to AVI_LINE_PADDING (4 bytes).
    const size_t aviPitch = (linePitch + 3u) & ~static_cast<size_t>(3u);
    for (int y = 0; y < height; ++y) {
        const byte *srcRow = captureBuffer + static_cast<size_t>(y) * linePitch;
        byte *dstRow = captureEncodeBuffer + static_cast<size_t>(y) * aviPitch;
        for (int x = 0; x < width; ++x) {
            dstRow[x * 3 + 0] = srcRow[x * 3 + 2];
            dstRow[x * 3 + 1] = srcRow[x * 3 + 1];
            dstRow[x * 3 + 2] = srcRow[x * 3 + 0];
        }
        std::memset(dstRow + linePitch, 0, aviPitch - linePitch);
    }
    ri.CL_WriteAVIVideoFrame(captureEncodeBuffer, static_cast<int>(aviPitch * static_cast<size_t>(height)));
}

void StartFrame()
{
    if (qrayInstance == QR_NULL_HANDLE) {
        return;
    }

    QrStartFrameInfo info{};
    const bool uncapped = qrayUncapped != nullptr && qrayUncapped->integer != 0;
    info.presentMode = uncapped ? QR_PRESENT_MODE_IMMEDIATE : QR_PRESENT_MODE_VSYNC;
    info.maxFrameLatency = uncapped ? 0u : 1u;
    info.requestShaderReload = QR_FALSE;
    info.wantFullscreenExclusive = QR_FALSE;
    info.hasFocus = QR_TRUE;

    QrResult result = qrStartFrame(qrayInstance, &info);
    if (result == QR_FEATURE_UNAVAILABLE && info.presentMode != QR_PRESENT_MODE_VSYNC) {
        // SDK 0.2+ no longer substitutes a present mode the surface lacks (IMMEDIATE is
        // missing on several Linux/X11 drivers); fall back to FIFO instead of a black screen.
        static bool fallbackReported = false;
        if (!fallbackReported) {
            ri.Printf(PRINT_WARNING, "rendererqray: present mode %d unavailable, using vsync\n",
                static_cast<int>(info.presentMode));
            fallbackReported = true;
        }
        info.presentMode = QR_PRESENT_MODE_VSYNC;
        info.maxFrameLatency = 1u;
        result = qrStartFrame(qrayInstance, &info);
    }
    static QrResult lastStartFailure = QR_SUCCESS;
    if (result != QR_SUCCESS) {
        // Minimised or suspended swapchains fail every frame; report each new code once.
        if (result != lastStartFailure) {
            ri.Printf(PRINT_WARNING, "rendererqray: qrStartFrame failed: %d\n", static_cast<int>(result));
            lastStartFailure = result;
        }
        return;
    }
    lastStartFailure = QR_SUCCESS;

    frameStarted = true;
    sceneLightsUploaded = false;
    sceneRendered = false;
    sceneValid = false;
    frameHasWorldScene = false;
    dynamicLights.clear();

    if (!worldLoaded && !pendingWorldName.empty()) {
        LoadBspGeometry(pendingWorldName.c_str());
        worldLoaded = true;
    }

    UpdateReadback(readbackWantedFrames > 0);
    if (readbackWantedFrames > 0) {
        --readbackWantedFrames;
    }
}

uint64_t NextDynamicGeometryId()
{
    return dynamicGeometryId++;
}

struct DynamicMeshUploadDesc
{
    QrMaterial material = QR_NO_MATERIAL;
    QrGeometryPrimaryVisibilityType visibility = QR_GEOMETRY_VISIBILITY_TYPE_WORLD_0;
    QrGeometryPassThroughType passThrough = QR_GEOMETRY_PASS_THROUGH_TYPE_OPAQUE;
    QrGeometryMaterialBlendType blend = QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE;
    float emission = 0.0f;
    QrGeometryUploadFlags extraFlags = 0;
    float layerAlpha = 1.0f;
    bool twoSided = false;
    const EditorDynamicMaterialResult *authoredOverride = nullptr;
};

void ApplyDynamicAuthoredOverride(const EditorDynamicMaterialResult &resolved,
                                  const DynamicMeshUploadDesc &desc,
                                  QrGeometryUploadInfo &upload)
{
    ApplyEditorMaterial(resolved.params, desc.passThrough, upload);
    if (upload.passThroughType == QR_GEOMETRY_PASS_THROUGH_TYPE_GLASS_REFLECT_REFRACT) {
        upload.layerColors[2].data[1] = QuakeUnitsToMeters(upload.layerColors[2].data[1]);
    }
    upload.layerColors[0].data[3] = std::clamp(desc.layerAlpha, 0.0f, 1.0f);
    upload.layerBlendingTypes[0] = desc.blend;
    upload.flags |= desc.extraFlags;
    if (resolved.params.exactNormals) {
        upload.flags &= ~(QR_GEOMETRY_UPLOAD_GENERATE_NORMALS_BIT |
                          QR_GEOMETRY_UPLOAD_GENERATE_INVERTED_NORMALS_BIT);
    }
    if (resolved.emitRayOnly) {
        upload.flags |= QR_GEOMETRY_UPLOAD_EMISSION_RAY_ONLY_BIT;
    }
    if (resolved.surfaceMaterial != QR_NO_MATERIAL) {
        upload.geomMaterial.layerMaterials[0] = resolved.surfaceMaterial;
    }
}

void UploadDynamicGeometry(const std::vector<QrVertex> &vertices,
    const std::vector<uint32_t> &indices, const DynamicMeshUploadDesc &desc)
{
    if (qrayInstance == QR_NULL_HANDLE || vertices.empty() || indices.empty()) {
        return;
    }
    QrMaterial material = desc.material != QR_NO_MATERIAL ? desc.material : defaultMaterial;

    std::vector<QrVertex> twoSidedVertices;
    std::vector<uint32_t> twoSidedIndices;
    const std::vector<QrVertex> *activeVertices = &vertices;
    const std::vector<uint32_t> *activeIndices = &indices;
    if (desc.twoSided) {
        AppendTwoSidedTriangles(vertices, indices, twoSidedVertices, twoSidedIndices);
        activeVertices = &twoSidedVertices;
        activeIndices = &twoSidedIndices;
    }

    const std::vector<QrVertex> meterVertices = QuakeVerticesToMeters(*activeVertices);
    QrGeometryUploadInfo upload{};
    upload.uniqueID = NextDynamicGeometryId();
    upload.flags = desc.extraFlags;
    upload.geomType = QR_GEOMETRY_TYPE_DYNAMIC;
    upload.passThroughType = desc.passThrough;
    upload.visibilityType = desc.visibility;
    upload.vertexCount = static_cast<uint32_t>(meterVertices.size());
    upload.pVertices = meterVertices.data();
    upload.indexCount = static_cast<uint32_t>(activeIndices->size());
    upload.pIndices = activeIndices->data();
    upload.layerColors[0] = {1.0f, 1.0f, 1.0f, std::clamp(desc.layerAlpha, 0.0f, 1.0f)};
    upload.layerBlendingTypes[0] = desc.blend;
    upload.texCoordScale[0] = 1.0f;
    upload.texCoordScale[1] = 1.0f;
    upload.defaultRoughness = 0.8f;
    upload.defaultMetallicity = 0.0f;
    upload.defaultEmission = desc.emission;
    upload.geomMaterial.layerMaterials[0] = material;
    upload.transform = IdentityTransform();

    if (desc.authoredOverride != nullptr) {
        ApplyDynamicAuthoredOverride(*desc.authoredOverride, desc, upload);
    }

    const QrResult result = qrUploadGeometry(qrayInstance, &upload);
    if (result != QR_SUCCESS) {
        HostLog("UploadDynamicGeometry: result=%d verts=%u indices=%u",
            static_cast<int>(result), upload.vertexCount, upload.indexCount);
    }
}

// Semi-transparent effects cannot be composited by the traced pass - QuakeRay's primary path only
// knows alpha cut-out - so sprites, beams and impact marks go through the rasterized overlay. The
// upload passes no view projection, which makes the overlay use the frame's own camera matrices,
// and keeps the GL blend equation of the shader so smoke, additive glows and darkening decals come
// out the way the original renderer draws them.
void ComputeModelEnvironmentTexCoord(const float position[3], const float normal[3], float out[2]);

void UploadRasterizedWorldGeometry(const std::vector<QrVertex> &vertices,
    const std::vector<uint32_t> &indices, QrMaterial material, QrBlendFactor blendSource,
    QrBlendFactor blendDestination, uint32_t pipelineStateOverride = 0xFFFFFFFFu,
    const float texCoordOffset[2] = nullptr, const float color[4] = nullptr,
    const float smokeLook[4] = nullptr)
{
    if (qrayInstance == QR_NULL_HANDLE || vertices.empty() || !frameStarted) {
        return;
    }
    if (material == QR_NO_MATERIAL) {
        material = defaultMaterial;
    }

    const std::vector<QrVertex> meterVertices = QuakeVerticesToMeters(vertices);
    QrRasterizedGeometryUploadInfo info{};
    info.renderType = QR_RASTERIZED_GEOMETRY_RENDER_TYPE_DEFAULT;
    info.vertexCount = static_cast<uint32_t>(meterVertices.size());
    info.pVertices = meterVertices.data();
    info.indexCount = static_cast<uint32_t>(indices.size());
    info.pIndices = indices.empty() ? nullptr : indices.data();
    info.transform = IdentityTransform();
    info.color.data[0] = color != nullptr ? color[0] : 1.0f;
    info.color.data[1] = color != nullptr ? color[1] : 1.0f;
    info.color.data[2] = color != nullptr ? color[2] : 1.0f;
    info.color.data[3] = color != nullptr ? color[3] : 1.0f;
    if (smokeLook != nullptr) {
        info.smokeLook.data[0] = smokeLook[0];
        info.smokeLook.data[1] = smokeLook[1];
        info.smokeLook.data[2] = smokeLook[2];
        info.smokeLook.data[3] = smokeLook[3];
    }
    info.texCoordScale[0] = 1.0f;
    info.texCoordScale[1] = 1.0f;
    info.texCoordOffset[0] = texCoordOffset != nullptr ? texCoordOffset[0] : 0.0f;
    info.texCoordOffset[1] = texCoordOffset != nullptr ? texCoordOffset[1] : 0.0f;
    info.material = material;
    info.pipelineState = pipelineStateOverride != 0xFFFFFFFFu
        ? pipelineStateOverride
        : (QR_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE |
           QR_RASTERIZED_GEOMETRY_STATE_DEPTH_TEST);
    if (info.texCoordOffset[0] != 0.0f || info.texCoordOffset[1] != 0.0f) {
        info.pipelineState |= QR_RASTERIZED_GEOMETRY_STATE_TEXCOORD_TRANSFORM;
    }
    info.blendFuncSrc = blendSource;
    info.blendFuncDst = blendDestination;

    const QrResult result = qrUploadRasterizedGeometry(qrayInstance, &info, nullptr, nullptr);
    if (result != QR_SUCCESS) {
        HostLog("UploadRasterizedWorldGeometry: result=%d verts=%u indices=%u blend=%d/%d",
            static_cast<int>(result), info.vertexCount, info.indexCount,
            static_cast<int>(blendSource), static_cast<int>(blendDestination));
    }
}

std::string ActiveStageTexture(const ShaderStage &stage, float timeSeconds)
{
    if (stage.animationMaps.empty() || stage.animationFps <= 0.0f) {
        return stage.map;
    }
    const size_t frame = static_cast<size_t>(
        std::floor(std::max(timeSeconds, 0.0f) * stage.animationFps)) %
        stage.animationMaps.size();
    return stage.animationMaps[frame];
}

void ApplyStageTcMods(const ShaderStage &stage, float timeSeconds,
    const std::vector<QrVertex> &source, std::vector<QrVertex> &out)
{
    out = source;
    for (const ShaderTcMod &mod : stage.tcMods) {
        switch (mod.type) {
        case TCMOD_SCALE:
            for (QrVertex &v : out) {
                v.texCoord[0] *= mod.params[0];
                v.texCoord[1] *= mod.params[1];
            }
            break;
        case TCMOD_SCROLL: {
            const float du = mod.params[0] * timeSeconds;
            const float dv = mod.params[1] * timeSeconds;
            for (QrVertex &v : out) {
                v.texCoord[0] += du;
                v.texCoord[1] += dv;
            }
            break;
        }
        case TCMOD_ROTATE: {
            const float rad = -mod.params[0] * timeSeconds * (kPi / 180.0f);
            const float c = std::cos(rad);
            const float s = std::sin(rad);
            for (QrVertex &v : out) {
                const float du = v.texCoord[0] - 0.5f;
                const float dv = v.texCoord[1] - 0.5f;
                v.texCoord[0] = du * c - dv * s + 0.5f;
                v.texCoord[1] = du * s + dv * c + 0.5f;
            }
            break;
        }
        case TCMOD_STRETCH: {
            const float w = EvaluateShaderWave(mod.wave, mod.params[0], mod.params[1],
                mod.params[2], mod.params[3], timeSeconds);
            const float inv = 1.0f / std::max(0.001f, w);
            for (QrVertex &v : out) {
                v.texCoord[0] = (v.texCoord[0] - 0.5f) * inv + 0.5f;
                v.texCoord[1] = (v.texCoord[1] - 0.5f) * inv + 0.5f;
            }
            break;
        }
        case TCMOD_TURB: {
            const float amp = mod.params[1];
            const float now = mod.params[2] + timeSeconds * mod.params[3];
            for (QrVertex &v : out) {
                v.texCoord[0] += std::sin(((v.position[0] + v.position[2]) * 0.001f * mod.params[3] + now) * 2.0f * kPi) * amp;
                v.texCoord[1] += std::sin((v.position[1] * 0.001f * mod.params[3] + now) * 2.0f * kPi) * amp;
            }
            break;
        }
        }
    }
}

/// Returns true when an overlay stage duplicates or unlit-overwrites a ray-traced base material.
bool ShouldSkipTracedOverlayStage(const ShaderDefinition &definition, size_t stageIndex,
    const ShaderStage &stage, bool isAdditive)
{
    if (definition.metallicEnvBase) {
        return !isAdditive || stageIndex <= definition.baseStageIndex;
    }
    if (!definition.tracedBase) {
        return false;
    }
    if (stage.blendSrc == QR_BLEND_FACTOR_DST_COLOR &&
        stage.blendDst == QR_BLEND_FACTOR_ZERO) {
        const std::string stageKey = NormalizeShaderName(stage.map);
        if (stageKey == NormalizeShaderName(definition.map) ||
            stageKey == "textures/sfx/shadow") {
            return true;
        }
    }
    if (!stage.animated && !stage.environmentMap &&
        stage.rgbWave == SHADER_WAVE_NONE && stage.alphaWave == SHADER_WAVE_NONE &&
        definition.surfaceLight <= 0.0f &&
        ((stage.blendSrc == QR_BLEND_FACTOR_SRC_ALPHA &&
          stage.blendDst == QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) ||
         (stage.blendSrc == QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA &&
          stage.blendDst == QR_BLEND_FACTOR_SRC_ALPHA))) {
        return true;
    }
    return false;
}

/// Returns the axis-aligned diagonal of the loaded BSP world in Quake units.
float CurrentMapDiagonalQuakeUnits()
{
    const EditorScene &scene = sceneRuntime.scene;
    if (!worldLoaded || !scene.hasBounds || scene.boundsMin[0] > scene.boundsMax[0]) {
        return 0.0f;
    }
    const float dx = scene.boundsMax[0] - scene.boundsMin[0];
    const float dy = scene.boundsMax[1] - scene.boundsMin[1];
    const float dz = scene.boundsMax[2] - scene.boundsMin[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// Returns the active ray-trace cutoff in Quake units, always clearing the current map.
float EffectiveRayLengthQuakeUnits()
{
    const float requested = std::max(CvarFloat(qrayRayLength, 65536.0f), 4096.0f);
    return std::max(requested, CurrentMapDiagonalQuakeUnits() * 2.5f);
}

/// Returns the far clip plane in Quake units so no world or skybox vertex gets clipped.
float EffectiveCameraFarQuakeUnits()
{
    return std::max(EffectiveRayLengthQuakeUnits(), 65536.0f);
}

QrMaterial ResolveModelMaterial(const refEntity_t &entity, const ModelAsset &asset, const ModelSurface &surface)
{
    (void)asset;
    if (entity.customShader != 0) {
        return GetMaterialForHandle(entity.customShader);
    }
    if (entity.customSkin != 0) {
        const auto skinIt = skinAssets.find(entity.customSkin);
        if (skinIt != skinAssets.end()) {
            const auto shaderIt = skinIt->second.shaderBySurface.find(surface.surfaceName);
            if (shaderIt != skinIt->second.shaderBySurface.end()) {
                return GetMaterialForTextureName(shaderIt->second.c_str());
            }
        }
    }
    return GetMaterialForTextureName(surface.shaderName.c_str());
}

const char *ResolveModelShaderName(const refEntity_t &entity, const ModelSurface &surface)
{
    if (entity.customSkin != 0) {
        const auto skinIt = skinAssets.find(entity.customSkin);
        if (skinIt != skinAssets.end()) {
            const auto shaderIt = skinIt->second.shaderBySurface.find(surface.surfaceName);
            if (shaderIt != skinIt->second.shaderBySurface.end()) {
                return shaderIt->second.c_str();
            }
        }
    }
    return surface.shaderName.c_str();
}

// tcGen environment: GL_SPHERE_MAP of the reflection vector, evaluated per vertex the way the
// fixed function pipeline Q3 was authored for did it. Used for the additive glass shells, the
// medkit and armour spheres, which have no usable UVs of their own.
void ComputeModelEnvironmentTexCoord(const float position[3], const float normal[3], float out[2])
{
    float view[3] = {
        position[0] - sceneDef.vieworg[0],
        position[1] - sceneDef.vieworg[1],
        position[2] - sceneDef.vieworg[2]
    };
    const float viewLength = std::sqrt(view[0] * view[0] + view[1] * view[1] + view[2] * view[2]);
    if (viewLength <= 1.0e-4f) {
        out[0] = 0.5f;
        out[1] = 0.5f;
        return;
    }
    view[0] /= viewLength;
    view[1] /= viewLength;
    view[2] /= viewLength;

    const float facing = view[0] * normal[0] + view[1] * normal[1] + view[2] * normal[2];
    const float reflected[3] = {
        view[0] - 2.0f * facing * normal[0],
        view[1] - 2.0f * facing * normal[1],
        view[2] - 2.0f * facing * normal[2]
    };

    // Eye space, +Z pointing back at the viewer.
    const float eyeX = reflected[0] * sceneDef.viewaxis[1][0] + reflected[1] * sceneDef.viewaxis[1][1] +
        reflected[2] * sceneDef.viewaxis[1][2];
    const float eyeY = reflected[0] * sceneDef.viewaxis[2][0] + reflected[1] * sceneDef.viewaxis[2][1] +
        reflected[2] * sceneDef.viewaxis[2][2];
    const float eyeZ = -(reflected[0] * sceneDef.viewaxis[0][0] + reflected[1] * sceneDef.viewaxis[0][1] +
        reflected[2] * sceneDef.viewaxis[0][2]);

    const float denominator = 2.0f * std::sqrt(eyeX * eyeX + eyeY * eyeY + (eyeZ + 1.0f) * (eyeZ + 1.0f));
    if (denominator <= 1.0e-4f) {
        out[0] = 0.5f;
        out[1] = 0.5f;
        return;
    }
    out[0] = 0.5f + eyeX / denominator;
    out[1] = 0.5f - eyeY / denominator;
}

void UploadShaderEffectStages(const ShaderDefinition &definition,
    const std::vector<QrVertex> &baseVertices, const std::vector<uint32_t> &indices,
    const byte entityColor[4], float shaderTimeSeconds, bool allowLitParticle,
    size_t skipStageIndex = static_cast<size_t>(-1), bool cullBackFaces = false,
    size_t stageBegin = 0u, size_t stageEnd = static_cast<size_t>(-1),
    bool cutoutUnderlay = false, bool applyTracedBaseFilter = false,
    uint32_t disabledStagesMask = 0u, bool suppressEnvironmentStages = false)
{
    if (baseVertices.empty() || indices.empty()) {
        return;
    }
    static std::vector<uint32_t> culledIndices;
    const std::vector<uint32_t> *activeIndices = &indices;
    if (cullBackFaces && !definition.twoSided) {
        culledIndices.clear();
        culledIndices.reserve(indices.size());
        for (size_t k = 0; k + 2 < indices.size(); k += 3) {
            const uint32_t i0 = indices[k + 0];
            const uint32_t i1 = indices[k + 1];
            const uint32_t i2 = indices[k + 2];
            if (i0 >= baseVertices.size() || i1 >= baseVertices.size() || i2 >= baseVertices.size()) {
                continue;
            }
            const float *p0 = baseVertices[i0].position;
            const float *p1 = baseVertices[i1].position;
            const float *p2 = baseVertices[i2].position;
            const float ax = p1[0] - p0[0];
            const float ay = p1[1] - p0[1];
            const float az = p1[2] - p0[2];
            const float bx = p2[0] - p0[0];
            const float by = p2[1] - p0[1];
            const float bz = p2[2] - p0[2];
            const float n[3] = {
                ay * bz - az * by,
                az * bx - ax * bz,
                ax * by - ay * bx
            };
            const float toTri[3] = {
                p0[0] - sceneDef.vieworg[0],
                p0[1] - sceneDef.vieworg[1],
                p0[2] - sceneDef.vieworg[2]
            };
            if (n[0] * toTri[0] + n[1] * toTri[1] + n[2] * toTri[2] < 0.0f) {
                culledIndices.push_back(i0);
                culledIndices.push_back(i1);
                culledIndices.push_back(i2);
            }
        }
        if (culledIndices.empty()) {
            return;
        }
        activeIndices = &culledIndices;
    }
    const float sceneTimeSeconds = static_cast<float>(sceneDef.time) * 0.001f;
    const float stageTime = std::max(0.0f, sceneTimeSeconds - shaderTimeSeconds);
    static std::vector<QrVertex> stageVertices;
    static std::vector<QrVertex> modVertices;
    const size_t clampedStageEnd = std::min(stageEnd, definition.stages.size());

    for (size_t stageIndex = stageBegin; stageIndex < clampedStageEnd; ++stageIndex) {
        if (stageIndex == skipStageIndex ||
            (stageIndex < 32u && ((disabledStagesMask >> stageIndex) & 1u) != 0u)) {
            continue;
        }
        const ShaderStage &stage = definition.stages[stageIndex];
        if (stage.lightmap || (suppressEnvironmentStages && stage.environmentMap)) {
            continue;
        }
        const std::string textureName = ActiveStageTexture(stage, stageTime);
        if (textureName.empty() || _stricmp(textureName.c_str(), "$lightmap") == 0) {
            continue;
        }
        QrBlendFactor source = stage.blendSrc;
        QrBlendFactor destination = stage.blendDst;
        uint32_t state = QR_RASTERIZED_GEOMETRY_STATE_DEPTH_TEST;
        if (stage.blended || stage.blend != QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE) {
            state |= QR_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE;
        } else {
            if (!cutoutUnderlay) {
                state |= QR_RASTERIZED_GEOMETRY_STATE_DEPTH_WRITE;
            }
            source = QR_BLEND_FACTOR_ONE;
            destination = QR_BLEND_FACTOR_ZERO;
        }
        if (stage.alphaTested) {
            state |= QR_RASTERIZED_GEOMETRY_STATE_ALPHA_TEST;
        }
        const bool isAdditive = (stage.blend == QR_GEOMETRY_MATERIAL_BLEND_TYPE_ADD) ||
            (source == QR_BLEND_FACTOR_ONE && destination == QR_BLEND_FACTOR_ONE) ||
            (source == QR_BLEND_FACTOR_SRC_ALPHA && destination == QR_BLEND_FACTOR_ONE);
        if (isAdditive && NormalizeShaderName(textureName) == "gfx/colors/black") {
            continue;
        }
        if (applyTracedBaseFilter && !cutoutUnderlay &&
            ShouldSkipTracedOverlayStage(definition, stageIndex, stage, isAdditive)) {
            continue;
        }

        const QrSamplerAddressMode addressMode = stage.clampMap
            ? QR_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            : QR_SAMPLER_ADDRESS_MODE_REPEAT;
        const QrMaterial material = GetDirectImageMaterial(textureName.c_str(), addressMode, false,
            isAdditive && !stage.environmentMap);
        if (material == QR_NO_MATERIAL) {
            continue;
        }

        stageVertices = baseVertices;
        if (stage.environmentMap) {
            for (QrVertex &v : stageVertices) {
                ComputeModelEnvironmentTexCoord(v.position, v.normal, v.texCoord);
            }
        }
        if (!stage.tcMods.empty()) {
            ApplyStageTcMods(stage, stageTime, stageVertices, modVertices);
            stageVertices.swap(modVertices);
        }

        const bool useEntityRgb = stage.rgbGen == COLORGEN_ENTITY || stage.rgbGen == COLORGEN_VERTEX;
        const bool useEntityAlpha = stage.alphaGen == ALPHAGEN_ENTITY || stage.alphaGen == ALPHAGEN_VERTEX;
        const byte stageColor[4] = {
            useEntityRgb ? entityColor[0] : static_cast<byte>(255),
            useEntityRgb ? entityColor[1] : static_cast<byte>(255),
            useEntityRgb ? entityColor[2] : static_cast<byte>(255),
            useEntityAlpha ? entityColor[3] : static_cast<byte>(255)
        };
        const uint32_t packedColor = PackColor(stageColor);
        for (QrVertex &v : stageVertices) {
            v.packedColor = packedColor;
        }

        const float rgbWave = EvaluateShaderWave(stage.rgbWave, stage.rgbWaveBase,
            stage.rgbWaveAmplitude, stage.rgbWavePhase, stage.rgbWaveFrequency, stageTime);
        const float alphaWave = EvaluateShaderWave(stage.alphaWave, stage.alphaWaveBase,
            stage.alphaWaveAmplitude, stage.alphaWavePhase, stage.alphaWaveFrequency, stageTime);
        const float gain = isAdditive ? std::max(0.0f, CvarFloat(qrayOverlayAddScale, 1.5f)) : 1.0f;
        const float color[4] = {rgbWave * gain, rgbWave * gain, rgbWave * gain, alphaWave};

        const bool useLitParticle = allowLitParticle && !isAdditive && !stage.alphaTested &&
            source == QR_BLEND_FACTOR_SRC_ALPHA &&
            destination == QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        const float smokeLook[4] = {0.0f, 0.35f, 1.0f, 0.015f};
        if (useLitParticle) {
            state |= QR_RASTERIZED_GEOMETRY_STATE_PARTICLE;
        }
        UploadRasterizedWorldGeometry(stageVertices, *activeIndices, material, source, destination,
            state, nullptr, color, useLitParticle ? smokeLook : nullptr);
    }
}

void CollectDynamicModelDtal(const EditorDynamicMaterialResult *authored, QrMaterial lightMat,
    const std::vector<QrVertex> &vertices, const std::vector<uint32_t> &indices,
    uint32_t entityCluster)
{
    if (authored == nullptr || !authored->params.luma || authored->params.lightBrightness <= 0.0f ||
        sceneRuntime.settings.truelight <= 0.0f || !sceneRuntime.settings.modelLights) {
        return;
    }
    const QrMaterial effectiveLightMat =
        authored->lightMaterial != QR_NO_MATERIAL ? authored->lightMaterial : lightMat;
    EditorMaterialEmission emission = authored->emission;
    if (!emission.valid) {
        emission = sceneRuntime.MaterialEmissionOf(effectiveLightMat);
    }

    EditorDynamicMeshDtalRequest req{};
    req.settings = &sceneRuntime.settings;
    req.params = &authored->params;
    req.lightMaterial = effectiveLightMat;
    req.emission = emission;
    req.vertices = &vertices;
    req.indices = &indices;
    req.cluster = entityCluster;
    req.budgetRemaining = &frameDynamicDtalBudget;
    req.nextUniqueId = &frameDynamicDtalNextId;
    BuildEditorDynamicMeshAreaLights(req, frameDynamicEmissiveLights);
}

uint32_t AssignModelVertexCluster(const ModelAsset &asset, const QrVertex &dst,
    uint32_t entityCluster, bool &hasMissingSubmodelCluster)
{
    if (!asset.isBspSubmodel) {
        return entityCluster;
    }
    const float nudgePos[3] = {
        dst.position[0] + dst.normal[0] * 2.0f,
        dst.position[1] + dst.normal[1] * 2.0f,
        dst.position[2] + dst.normal[2] * 2.0f,
    };
    uint32_t c = ClusterIndexForPoint(nudgePos);
    if (c == 0u) {
        c = ClusterIndexForPoint(dst.position);
    }
    if (c == 0u) {
        hasMissingSubmodelCluster = true;
    }
    return c;
}

void ResolveSubmodelClusterFallback(const ModelAsset &asset, uint32_t entityCluster,
    const float centroidSum[3], std::vector<QrVertex> &outVertices)
{
    if (!asset.isBspSubmodel || outVertices.empty()) {
        return;
    }
    const float inv = 1.0f / static_cast<float>(outVertices.size());
    const float centroid[3] = {
        centroidSum[0] * inv, centroidSum[1] * inv, centroidSum[2] * inv,
    };
    uint32_t fallbackCluster = ClusterIndexForPoint(centroid);
    if (fallbackCluster == 0u) {
        fallbackCluster = entityCluster;
    }
    if (fallbackCluster == 0u) {
        return;
    }
    for (QrVertex &v : outVertices) {
        if (v.cluster == 0u) {
            v.cluster = fallbackCluster;
        }
    }
}

struct EntityMeshBuildParams
{
    const refEntity_t *entity = nullptr;
    const ModelAsset *asset = nullptr;
    const ModelSurface *surface = nullptr;
    const ShaderDefinition *definition = nullptr;
    float shaderTimeSeconds = 0.0f;
    uint32_t entityCluster = 0u;
};

void BuildInterpolatedSurfaceVertex(const EntityMeshBuildParams &p, int vi, int frame,
    int oldFrame, float backLerp, float deformTime, QrVertex &dst)
{
    const size_t n = static_cast<size_t>(frame) * p.surface->numVerts * 3 + static_cast<size_t>(vi) * 3;
    const size_t o = static_cast<size_t>(oldFrame) * p.surface->numVerts * 3 + static_cast<size_t>(vi) * 3;
    const float frontLerp = 1.0f - backLerp;
    float lx = p.surface->positions[o + 0] * backLerp + p.surface->positions[n + 0] * frontLerp;
    float ly = p.surface->positions[o + 1] * backLerp + p.surface->positions[n + 1] * frontLerp;
    float lz = p.surface->positions[o + 2] * backLerp + p.surface->positions[n + 2] * frontLerp;
    float ln[3] = {
        p.surface->normals[o + 0] * backLerp + p.surface->normals[n + 0] * frontLerp,
        p.surface->normals[o + 1] * backLerp + p.surface->normals[n + 1] * frontLerp,
        p.surface->normals[o + 2] * backLerp + p.surface->normals[n + 2] * frontLerp,
    };
    NormalizeVector(ln);
    if (p.definition != nullptr && p.definition->deformWave != SHADER_WAVE_NONE) {
        const float phaseOffset = p.definition->deformFrequency != 0.0f
            ? (lx + ly + lz) * p.definition->deformSpread
            : 0.0f;
        const float disp = EvaluateShaderWaveSigned(p.definition->deformWave,
            p.definition->deformBase, p.definition->deformAmplitude,
            p.definition->deformPhase + phaseOffset, p.definition->deformFrequency, deformTime);
        lx += ln[0] * disp;
        ly += ln[1] * disp;
        lz += ln[2] * disp;
    }

    dst = {};
    for (int a = 0; a < 3; ++a) {
        dst.position[a] = p.entity->origin[a] + p.entity->axis[0][a] * lx +
            p.entity->axis[1][a] * ly + p.entity->axis[2][a] * lz;
        dst.normal[a] = p.entity->axis[0][a] * ln[0] +
            p.entity->axis[1][a] * ln[1] + p.entity->axis[2][a] * ln[2];
    }
    NormalizeVector(dst.normal);
    if (p.definition != nullptr && p.definition->environmentMap) {
        ComputeModelEnvironmentTexCoord(dst.position, dst.normal, dst.texCoord);
    } else {
        dst.texCoord[0] = p.surface->texCoords[static_cast<size_t>(vi) * 2 + 0];
        dst.texCoord[1] = p.surface->texCoords[static_cast<size_t>(vi) * 2 + 1];
    }
    dst.packedColor = 0xFFFFFFFFu;
}

bool BuildEntitySurfaceMesh(const EntityMeshBuildParams &p,
    std::vector<QrVertex> &outVertices, std::vector<uint32_t> &outIndices)
{
    const int frameCount = static_cast<int>(p.surface->positions.size() /
        (static_cast<size_t>(p.surface->numVerts) * 3));
    if (frameCount <= 0) {
        return false;
    }
    const int frame = std::clamp(p.entity->frame, 0, frameCount - 1);
    const int oldFrame = std::clamp(p.entity->oldframe, 0, frameCount - 1);
    const float backLerp = frame == oldFrame ? 0.0f : std::clamp(p.entity->backlerp, 0.0f, 1.0f);
    const float deformTime = std::max(0.0f,
        static_cast<float>(sceneDef.time) * 0.001f - p.shaderTimeSeconds);

    outVertices.resize(static_cast<size_t>(p.surface->numVerts));
    float centroidSum[3] = {0.0f, 0.0f, 0.0f};
    bool hasMissingSubmodelCluster = false;
    for (int vi = 0; vi < p.surface->numVerts; ++vi) {
        QrVertex &dst = outVertices[static_cast<size_t>(vi)];
        BuildInterpolatedSurfaceVertex(p, vi, frame, oldFrame, backLerp, deformTime, dst);
        centroidSum[0] += dst.position[0];
        centroidSum[1] += dst.position[1];
        centroidSum[2] += dst.position[2];
        dst.cluster = AssignModelVertexCluster(*p.asset, dst, p.entityCluster,
            hasMissingSubmodelCluster);
    }
    if (hasMissingSubmodelCluster) {
        ResolveSubmodelClusterFallback(*p.asset, p.entityCluster, centroidSum, outVertices);
    }

    outIndices = p.surface->indices;
    if (!p.asset->isBspSubmodel && qrayModelWinding != nullptr && qrayModelWinding->integer != 0) {
        for (size_t i = 0; i + 2 < outIndices.size(); i += 3) {
            std::swap(outIndices[i + 1], outIndices[i + 2]);
        }
    }
    return true;
}

struct ModelSurfaceUploadContext
{
    const refEntity_t *entity = nullptr;
    const ModelAsset *asset = nullptr;
    const ShaderDefinition *definition = nullptr;
    const EditorDynamicMaterialResult *authored = nullptr;
    const std::vector<QrVertex> *vertices = nullptr;
    const std::vector<uint32_t> *indices = nullptr;
    QrMaterial effectiveMat = QR_NO_MATERIAL;
    QrGeometryPrimaryVisibilityType visibility = QR_GEOMETRY_VISIBILITY_TYPE_WORLD_0;
    QrGeometryPassThroughType passThrough = QR_GEOMETRY_PASS_THROUGH_TYPE_OPAQUE;
    float emission = 0.0f;
    bool twoSided = false;
    float shaderTimeSeconds = 0.0f;
    uint32_t disabledStagesMask = 0u;
    uint32_t entityCluster = 0u;
};

void UploadAdditiveEnvironmentSurface(const ModelSurfaceUploadContext &ctx)
{
    if (qrayEnvShellTrace != nullptr && qrayEnvShellTrace->integer == 2) {
        return;
    }
    if (qrayEnvShellTrace != nullptr && qrayEnvShellTrace->integer != 0) {
        DynamicMeshUploadDesc desc{ctx.effectiveMat, ctx.visibility,
            QR_GEOMETRY_PASS_THROUGH_TYPE_GLASS_REFLECT_REFRACT,
            QR_GEOMETRY_MATERIAL_BLEND_TYPE_ADD, 0.0f, 0, 1.0f, ctx.twoSided, ctx.authored};
        UploadDynamicGeometry(*ctx.vertices, *ctx.indices, desc);
        return;
    }
    const float shellScale = std::clamp(CvarFloat(qrayEnvShellScale, 1.0f), 0.0f, 1.0f);
    const QrMaterial shellMat = GetScaledEnvMaterialForTextureName(
        ctx.definition->map.empty() ? nullptr : ctx.definition->map.c_str(), shellScale);
    UploadRasterizedWorldGeometry(*ctx.vertices, *ctx.indices,
        shellMat == QR_NO_MATERIAL ? ctx.effectiveMat : shellMat,
        ctx.definition->blendSrc, ctx.definition->blendDst);
}

void UploadEnvironmentModelSurface(const ModelSurfaceUploadContext &ctx)
{
    const QrBlendFactor src = ctx.definition->blendSrc;
    const QrBlendFactor dst = ctx.definition->blendDst;
    if (src != QR_BLEND_FACTOR_ONE || dst != QR_BLEND_FACTOR_ZERO) {
        UploadAdditiveEnvironmentSurface(ctx);
        return;
    }

    const bool useRasterBody = !ctx.asset->isBspSubmodel && qrayEnvMapRaster != nullptr &&
        qrayEnvMapRaster->integer != 0 && (ctx.authored == nullptr || !ctx.authored->hasAuthoredOverride);
    if (useRasterBody) {
        const float bodyScale = std::clamp(CvarFloat(qrayEnvMapScale, 1.15f), 0.0f, 8.0f);
        const float bodySat = std::clamp(CvarFloat(qrayEnvMapSaturation, 1.6f), 0.0f, 4.0f);
        const QrMaterial bodyMat = ctx.definition->map.empty()
            ? ctx.effectiveMat
            : GetScaledEnvMaterialForTextureName(ctx.definition->map.c_str(), bodyScale, bodySat);
        UploadRasterizedWorldGeometry(*ctx.vertices, *ctx.indices,
            bodyMat == QR_NO_MATERIAL ? ctx.effectiveMat : bodyMat, src, dst,
            QR_RASTERIZED_GEOMETRY_STATE_DEPTH_TEST | QR_RASTERIZED_GEOMETRY_STATE_DEPTH_WRITE);
        if (ctx.definition->usesOverlay && ctx.definition->tracedBase) {
            UploadShaderEffectStages(*ctx.definition, *ctx.vertices, *ctx.indices,
                ctx.entity->shaderRGBA, ctx.shaderTimeSeconds, false,
                ctx.definition->baseStageIndex, true, 0u, static_cast<size_t>(-1),
                false, true, ctx.disabledStagesMask);
        }
        return;
    }

    DynamicMeshUploadDesc desc{ctx.effectiveMat, ctx.visibility, ctx.passThrough,
        QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE, ctx.emission, 0, 1.0f, ctx.twoSided, ctx.authored};
    UploadDynamicGeometry(*ctx.vertices, *ctx.indices, desc);
}

bool HasEnabledModelUnderlayStage(const ShaderDefinition &definition, uint32_t disabledStagesMask)
{
    if (!definition.cutoutFrameUnderlay || definition.baseStageIndex == 0u ||
        definition.baseStageIndex > definition.stages.size()) {
        return false;
    }
    for (size_t i = 0u; i < definition.baseStageIndex; ++i) {
        if (!definition.stages[i].lightmap &&
            (i >= 32u || ((disabledStagesMask >> i) & 1u) == 0u)) {
            return true;
        }
    }
    return false;
}

void UploadModelEntitySurfaceOverlays(const ModelSurfaceUploadContext &ctx,
                                      bool baseStageDisabled, bool mirrorActive,
                                      bool tracedMaterialOverride)
{
    if (ctx.definition == nullptr) {
        return;
    }
    if (ctx.definition->cutoutFrameUnderlay) {
        if (!baseStageDisabled &&
            HasEnabledModelUnderlayStage(*ctx.definition, ctx.disabledStagesMask)) {
            std::vector<QrVertex> backingVertices = *ctx.vertices;
            OffsetVerticesAlongSmoothNormals(backingVertices, *ctx.indices, -0.8f);
            DynamicMeshUploadDesc backingDesc{
                sceneRuntime.overlayCache.GetCutoutBackingMaterial(qrayInstance), ctx.visibility,
                QR_GEOMETRY_PASS_THROUGH_TYPE_OPAQUE, QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE,
                0.0f, 0, 1.0f, ctx.twoSided, nullptr};
            UploadDynamicGeometry(backingVertices, *ctx.indices, backingDesc);
        }
        if (ctx.definition->baseStageIndex > 0 &&
            ctx.definition->baseStageIndex <= ctx.definition->stages.size()) {
            std::vector<QrVertex> underlayVertices = *ctx.vertices;
            if (!baseStageDisabled) {
                OffsetVerticesAlongSmoothNormals(underlayVertices, *ctx.indices, -0.4f);
            }
            UploadShaderEffectStages(*ctx.definition, underlayVertices, *ctx.indices,
                ctx.entity->shaderRGBA, ctx.shaderTimeSeconds, false,
                ctx.definition->baseStageIndex, true, 0u, ctx.definition->baseStageIndex,
                !baseStageDisabled, false, ctx.disabledStagesMask, mirrorActive);
        }
        if (ctx.definition->baseStageIndex + 1u < ctx.definition->stages.size()) {
            std::vector<QrVertex> overlayVertices = *ctx.vertices;
            if (!baseStageDisabled) {
                OffsetVerticesAlongSmoothNormals(overlayVertices, *ctx.indices, 0.08f);
            }
            UploadShaderEffectStages(*ctx.definition, overlayVertices, *ctx.indices,
                ctx.entity->shaderRGBA, ctx.shaderTimeSeconds, false,
                ctx.definition->baseStageIndex, true, ctx.definition->baseStageIndex + 1u,
                ctx.definition->stages.size(), false, ctx.definition->tracedBase,
                ctx.disabledStagesMask, mirrorActive);
        }
        return;
    }
    if (ctx.definition->usesOverlay) {
        std::vector<QrVertex> overlayVertices = *ctx.vertices;
        if (!baseStageDisabled) {
            for (QrVertex &v : overlayVertices) {
                v.position[0] += v.normal[0] * 0.08f;
                v.position[1] += v.normal[1] * 0.08f;
                v.position[2] += v.normal[2] * 0.08f;
            }
        }
        const size_t skipStage = (ctx.definition->tracedBase || tracedMaterialOverride)
            ? ctx.definition->baseStageIndex
            : static_cast<size_t>(-1);
        UploadShaderEffectStages(*ctx.definition, overlayVertices, *ctx.indices,
            ctx.entity->shaderRGBA, ctx.shaderTimeSeconds, false, skipStage, true,
            0u, static_cast<size_t>(-1), false, ctx.definition->tracedBase,
            ctx.disabledStagesMask, mirrorActive);
    }
}

void UploadPreparedModelSurface(const ModelSurfaceUploadContext &ctx, const char *activeShaderName)
{
    const bool baseStageDisabled = ctx.definition != nullptr &&
        ctx.definition->baseStageIndex < ctx.definition->stages.size() &&
        ctx.definition->baseStageIndex < 32u &&
        ((ctx.disabledStagesMask >> ctx.definition->baseStageIndex) & 1u) != 0u;
    const bool mirrorActive = ctx.authored != nullptr && ctx.authored->params.mirror &&
        ctx.authored->params.type == EditorMaterialType::None;
    const bool tracedMaterialOverride = ctx.authored != nullptr &&
        EditorMaterialPromotesOverlayToTraced(ctx.authored->params);

    if (!tracedMaterialOverride && ctx.definition != nullptr &&
        (ctx.definition->hasOnlyBlendedStages ||
         (ctx.definition->transparentSurface && !ctx.definition->tracedBase))) {
        UploadShaderEffectStages(*ctx.definition, *ctx.vertices, *ctx.indices,
            ctx.entity->shaderRGBA, ctx.shaderTimeSeconds, false, static_cast<size_t>(-1),
            true, 0u, static_cast<size_t>(-1), false, false, ctx.disabledStagesMask,
            mirrorActive);
        CollectDynamicModelDtal(ctx.authored, ctx.effectiveMat, *ctx.vertices, *ctx.indices,
            ctx.entityCluster);
        return;
    }

    if (!tracedMaterialOverride && ctx.definition != nullptr && ctx.definition->environmentMap &&
        (ctx.authored == nullptr || !ctx.authored->params.forceRasterize)) {
        if (!baseStageDisabled) {
            UploadEnvironmentModelSurface(ctx);
            CollectDynamicModelDtal(ctx.authored, ctx.effectiveMat, *ctx.vertices, *ctx.indices,
                ctx.entityCluster);
        } else if (ctx.definition->usesOverlay && ctx.definition->tracedBase) {
            UploadShaderEffectStages(*ctx.definition, *ctx.vertices, *ctx.indices,
                ctx.entity->shaderRGBA, ctx.shaderTimeSeconds, false,
                ctx.definition->baseStageIndex, true, 0u, static_cast<size_t>(-1),
                false, true, ctx.disabledStagesMask, mirrorActive);
        }
        return;
    }

    if (!baseStageDisabled) {
        if (ctx.authored != nullptr && ctx.authored->params.forceRasterize) {
            UploadRasterizedWorldGeometry(*ctx.vertices, *ctx.indices, ctx.effectiveMat,
                QR_BLEND_FACTOR_SRC_ALPHA, QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                QR_RASTERIZED_GEOMETRY_STATE_DEPTH_TEST |
                QR_RASTERIZED_GEOMETRY_STATE_DEPTH_WRITE |
                QR_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE);
        } else {
            DynamicMeshUploadDesc desc{ctx.effectiveMat, ctx.visibility, ctx.passThrough,
                QR_GEOMETRY_MATERIAL_BLEND_TYPE_OPAQUE, ctx.emission, 0, 1.0f, ctx.twoSided,
                ctx.authored};
            UploadDynamicGeometry(*ctx.vertices, *ctx.indices, desc);
        }
        CollectDynamicModelDtal(ctx.authored, ctx.effectiveMat, *ctx.vertices, *ctx.indices,
            ctx.entityCluster);
    }
    (void)activeShaderName;
    UploadModelEntitySurfaceOverlays(ctx, baseStageDisabled, mirrorActive, tracedMaterialOverride);
}

const EditorDynamicMaterialResult *ResolveModelSurfaceAuthored(
    const char *activeShaderName, EditorDynamicMaterialResult &outDynMat,
    uint32_t &outDisabledMask, bool &outDisabled, bool forceEnvBase)
{
    outDisabledMask = 0u;
    outDisabled = false;
    if (!AuthoredModeEnabled() || activeShaderName == nullptr || activeShaderName[0] == '\0') {
        return nullptr;
    }
    outDynMat = sceneRuntime.ResolveDynamicMaterial(qrayInstance, gameAssets, activeShaderName,
        &activeMaterialsDoc, hasBaseMaterialsDoc ? &baseMaterialsDoc : nullptr, forceEnvBase);
    if (!outDynMat.params.enabled) {
        outDisabled = true;
        return nullptr;
    }
    outDisabledMask = outDynMat.params.disabledStagesMask;
    return &outDynMat;
}

QrMaterial ResolveModelSurfaceEffectiveMaterial(
    const refEntity_t &entity, const ModelAsset &asset, const ModelSurface &surface,
    const ShaderDefinition *definition, const EditorDynamicMaterialResult *authored)
{
    if (authored != nullptr && authored->surfaceMaterial != QR_NO_MATERIAL) {
        return authored->surfaceMaterial;
    }
    if (definition != nullptr && definition->environmentMap) {
        const QrMaterial envMat = GetMaterialForTextureName(definition->map.c_str());
        if (envMat != QR_NO_MATERIAL) {
            return envMat;
        }
    }
    return ResolveModelMaterial(entity, asset, surface);
}

void PopulateModelSurfaceUploadFlags(ModelSurfaceUploadContext &ctx, const char *activeShaderName)
{
    const bool alphaTested = qrayAlphaTest != nullptr && qrayAlphaTest->integer != 0 &&
        ShaderUsesAlphaTest(activeShaderName);
    ctx.passThrough = alphaTested
        ? QR_GEOMETRY_PASS_THROUGH_TYPE_ALPHA_TESTED
        : QR_GEOMETRY_PASS_THROUGH_TYPE_OPAQUE;
    const bool hasSurfaceGlow = ctx.definition != nullptr && ctx.definition->surfaceLight > 0.0f &&
        qraySurfaceLightGlow != nullptr && qraySurfaceLightGlow->integer != 0;
    ctx.emission = hasSurfaceGlow ? 1.0f : 0.0f;
    ctx.twoSided = ctx.definition != nullptr && ctx.definition->twoSided;
}

void ProcessSingleModelSurface(const refEntity_t &entity, const ModelAsset &asset,
    const ModelSurface &surface, const char *customShaderName, float shaderTimeSeconds,
    QrGeometryPrimaryVisibilityType visibility, uint32_t entityCluster)
{
    if (surface.numVerts <= 0 || surface.indices.empty()) {
        return;
    }
    const char *activeShaderName = (customShaderName != nullptr && customShaderName[0] != '\0')
        ? customShaderName
        : ResolveModelShaderName(entity, surface);

    const auto definitionIt = shaderDefinitions.find(NormalizeShaderName(activeShaderName));
    const ShaderDefinition *definition =
        definitionIt != shaderDefinitions.end() ? &definitionIt->second : nullptr;
    const bool forceEnvBase = definition != nullptr && definition->environmentMap;

    EditorDynamicMaterialResult dynMat;
    uint32_t disabledStagesMask = 0u;
    bool surfaceDisabled = false;
    const EditorDynamicMaterialResult *authored = ResolveModelSurfaceAuthored(
        activeShaderName, dynMat, disabledStagesMask, surfaceDisabled, forceEnvBase);
    if (surfaceDisabled) {
        return;
    }

    std::vector<QrVertex> vertices;
    std::vector<uint32_t> indices;
    const EntityMeshBuildParams buildParams{
        &entity, &asset, &surface, definition, shaderTimeSeconds, entityCluster};
    if (!BuildEntitySurfaceMesh(buildParams, vertices, indices)) {
        return;
    }

    ModelSurfaceUploadContext ctx{};
    ctx.entity = &entity;
    ctx.asset = &asset;
    ctx.definition = definition;
    ctx.authored = authored;
    ctx.vertices = &vertices;
    ctx.indices = &indices;
    ctx.effectiveMat = ResolveModelSurfaceEffectiveMaterial(
        entity, asset, surface, definition, authored);
    ctx.visibility = visibility;
    ctx.shaderTimeSeconds = shaderTimeSeconds;
    ctx.disabledStagesMask = disabledStagesMask;
    ctx.entityCluster = entityCluster;
    PopulateModelSurfaceUploadFlags(ctx, activeShaderName);
    UploadPreparedModelSurface(ctx, activeShaderName);
}

void UploadModelEntity(const refEntity_t &entity, float shaderTimeSeconds)
{
    const auto modelIt = modelAssets.find(entity.hModel);
    if (modelIt == modelAssets.end() || !modelIt->second.valid) {
        return;
    }
    const ModelAsset &asset = modelIt->second;
    const uint32_t entityCluster = ClusterIndexForPoint(entity.origin);
    const QrGeometryPrimaryVisibilityType visibility =
        (entity.renderfx & (RF_FIRST_PERSON | RF_DEPTHHACK)) != 0
        ? QR_GEOMETRY_VISIBILITY_TYPE_FIRST_PERSON
        : QR_GEOMETRY_VISIBILITY_TYPE_WORLD_0;

    LoadShaderScripts();
    const char *customShaderName = entity.customShader != 0
        ? ShaderNameForHandle(entity.customShader)
        : nullptr;

    for (const ModelSurface &surface : asset.surfaces) {
        ProcessSingleModelSurface(entity, asset, surface, customShaderName,
            shaderTimeSeconds, visibility, entityCluster);
    }
}

void UploadSpriteEntity(const refEntity_t &entity, float shaderTimeSeconds)
{
    const float radius = entity.radius;
    if (radius <= 0.0f) {
        return;
    }

    if (qraySmoke == nullptr || qraySmoke->integer != 0) {
        const std::string shaderKey = NormalizeShaderName(ShaderNameForHandle(entity.customShader));
        if (shaderKey == "smokepuff" || shaderKey == "smokepuffragepro") {
            // Keep jump-pad constant-radius puffs (radius 32, alpha ~84), suppress rocket/grenade trails.
            const bool isJumpPadPuff =
                std::abs(radius - 32.0f) < 1e-3f && entity.shaderRGBA[3] > 16;
            if (!isJumpPadPuff) {
                return;
            }
        }
    }

    const float left[3] = {sceneDef.viewaxis[1][0], sceneDef.viewaxis[1][1], sceneDef.viewaxis[1][2]};
    const float up[3] = {sceneDef.viewaxis[2][0], sceneDef.viewaxis[2][1], sceneDef.viewaxis[2][2]};
    float rotatedLeft[3] = {left[0], left[1], left[2]};
    float rotatedUp[3] = {up[0], up[1], up[2]};
    if (entity.rotation != 0.0f) {
        const float angle = kPi * entity.rotation / 180.0f;
        const float sine = std::sin(angle);
        const float cosine = std::cos(angle);
        for (int axis = 0; axis < 3; ++axis) {
            rotatedLeft[axis] = (left[axis] * cosine - up[axis] * sine) * radius;
            rotatedUp[axis] = (up[axis] * cosine + left[axis] * sine) * radius;
        }
    } else {
        for (int axis = 0; axis < 3; ++axis) {
            rotatedLeft[axis] *= radius;
            rotatedUp[axis] *= radius;
        }
    }

    byte fallbackColor[4] = {
        entity.shaderRGBA[0], entity.shaderRGBA[1], entity.shaderRGBA[2], entity.shaderRGBA[3]
    };
    if (fallbackColor[0] == 0 && fallbackColor[1] == 0 && fallbackColor[2] == 0) {
        fallbackColor[0] = 255;
        fallbackColor[1] = 255;
        fallbackColor[2] = 255;
    }
    if (fallbackColor[3] == 0) {
        fallbackColor[3] = 255;
    }

    const float cornerOffsets[4][2] = {{1.0f, 1.0f}, {1.0f, -1.0f}, {-1.0f, -1.0f}, {-1.0f, 1.0f}};
    const float textureCoords[4][2] = {{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
    const uint32_t spriteCluster = ClusterIndexForPoint(entity.origin);
    std::vector<QrVertex> vertices(4);
    for (int corner = 0; corner < 4; ++corner) {
        QrVertex &vertex = vertices[static_cast<size_t>(corner)];
        vertex = {};
        for (int axis = 0; axis < 3; ++axis) {
            vertex.position[axis] = entity.origin[axis] +
                rotatedLeft[axis] * cornerOffsets[corner][0] + rotatedUp[axis] * cornerOffsets[corner][1];
            vertex.normal[axis] = -sceneDef.viewaxis[0][axis];
        }
        vertex.texCoord[0] = textureCoords[corner][0];
        vertex.texCoord[1] = textureCoords[corner][1];
        vertex.packedColor = PackColor(fallbackColor);
        vertex.cluster = spriteCluster;
    }

    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    LoadShaderScripts();
    const char *shaderName = ShaderNameForHandle(entity.customShader);
    const auto definitionIt = shaderDefinitions.find(NormalizeShaderName(shaderName));
    if (definitionIt != shaderDefinitions.end() && !definitionIt->second.stages.empty()) {
        UploadShaderEffectStages(definitionIt->second, vertices, indices, entity.shaderRGBA,
            shaderTimeSeconds, true);
        return;
    }

    const QrGeometryPrimaryVisibilityType visibility =
        (entity.renderfx & (RF_FIRST_PERSON | RF_DEPTHHACK)) != 0
        ? QR_GEOMETRY_VISIBILITY_TYPE_FIRST_PERSON
        : QR_GEOMETRY_VISIBILITY_TYPE_WORLD_0;
    const float layerAlpha = static_cast<float>(fallbackColor[3]) / 255.0f;
    const QrGeometryMaterialBlendType blend = EffectBlendForHandle(entity.customShader);
    const QrMaterial material = GetMaterialForHandle(entity.customShader);
    QrBlendFactor blendSource = QR_BLEND_FACTOR_ONE;
    QrBlendFactor blendDestination = QR_BLEND_FACTOR_ONE;
    const bool blended = EffectBlendFactorsForHandle(entity.customShader, blendSource, blendDestination);
    if (qrayDebugEntities != nullptr && qrayDebugEntities->integer != 0) {
        HostLog("UploadSprite: shader=%d name='%s' radius=%f alpha=%f blend=%d material=%d default=%d "
            "src=%d dst=%d",
            static_cast<int>(entity.customShader), ShaderNameForHandle(entity.customShader),
            radius, layerAlpha, static_cast<int>(blend), static_cast<int>(material),
            material == defaultMaterial ? 1 : 0, static_cast<int>(blendSource),
            static_cast<int>(blendDestination));
    }
    if (blended) {
        UploadRasterizedWorldGeometry(vertices, indices, material, blendSource, blendDestination);
        return;
    }
    DynamicMeshUploadDesc desc{};
    desc.material = material;
    desc.visibility = visibility;
    desc.passThrough = QR_GEOMETRY_PASS_THROUGH_TYPE_ALPHA_TESTED;
    desc.blend = blend;
    desc.emission = 1.0f;
    desc.extraFlags = QR_GEOMETRY_UPLOAD_ALPHA_TRANSMISSION_BIT;
    desc.layerAlpha = layerAlpha;
    UploadDynamicGeometry(vertices, indices, desc);
}

void UploadBeamEntity(const refEntity_t &entity)
{
    const float direction[3] = {
        entity.oldorigin[0] - entity.origin[0],
        entity.oldorigin[1] - entity.origin[1],
        entity.oldorigin[2] - entity.origin[2]
    };
    const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (length < 1e-4f) {
        return;
    }

    const float normalizedDirection[3] = {direction[0] / length, direction[1] / length, direction[2] / length};
    const float viewDirection[3] = {
        sceneDef.vieworg[0] - entity.origin[0],
        sceneDef.vieworg[1] - entity.origin[1],
        sceneDef.vieworg[2] - entity.origin[2]
    };
    float side[3] = {
        normalizedDirection[1] * viewDirection[2] - normalizedDirection[2] * viewDirection[1],
        normalizedDirection[2] * viewDirection[0] - normalizedDirection[0] * viewDirection[2],
        normalizedDirection[0] * viewDirection[1] - normalizedDirection[1] * viewDirection[0]
    };
    NormalizeVector(side);
    if (side[0] == 0.0f && side[1] == 0.0f && side[2] == 0.0f) {
        side[0] = sceneDef.viewaxis[1][0];
        side[1] = sceneDef.viewaxis[1][1];
        side[2] = sceneDef.viewaxis[1][2];
    }
    const float width = entity.reType == RT_RAIL_CORE ? 6.0f : 4.0f;
    for (int axis = 0; axis < 3; ++axis) {
        side[axis] *= width;
    }

    byte fallbackColor[4] = {
        entity.shaderRGBA[0], entity.shaderRGBA[1], entity.shaderRGBA[2], entity.shaderRGBA[3]
    };
    if (fallbackColor[0] == 0 && fallbackColor[1] == 0 && fallbackColor[2] == 0) {
        fallbackColor[0] = 255;
        fallbackColor[1] = 255;
        fallbackColor[2] = 255;
    }
    if (fallbackColor[3] == 0) {
        fallbackColor[3] = 255;
    }

    std::vector<QrVertex> vertices(4);
    const uint32_t beamCluster = ClusterIndexForPoint(entity.origin);
    const float *positions[4] = {
        entity.origin, entity.oldorigin, entity.oldorigin, entity.origin
    };
    const float signs[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    const float textureU[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float textureV[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    for (int corner = 0; corner < 4; ++corner) {
        QrVertex &vertex = vertices[static_cast<size_t>(corner)];
        vertex = {};
        for (int axis = 0; axis < 3; ++axis) {
            vertex.position[axis] = positions[corner][axis] + side[axis] * signs[corner];
            vertex.normal[axis] = -sceneDef.viewaxis[0][axis];
        }
        vertex.texCoord[0] = textureU[corner];
        vertex.texCoord[1] = textureV[corner];
        vertex.packedColor = PackColor(fallbackColor);
        vertex.cluster = beamCluster;
    }

    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    const float layerAlpha = entity.shaderRGBA[3] != 0
        ? static_cast<float>(entity.shaderRGBA[3]) / 255.0f
        : 1.0f;
    const QrMaterial material = GetMaterialForHandle(entity.customShader);
    QrBlendFactor blendSource = QR_BLEND_FACTOR_ONE;
    QrBlendFactor blendDestination = QR_BLEND_FACTOR_ONE;
    if (EffectBlendFactorsForHandle(entity.customShader, blendSource, blendDestination)) {
        UploadRasterizedWorldGeometry(vertices, indices, material, blendSource, blendDestination);
        return;
    }
    DynamicMeshUploadDesc desc{};
    desc.material = material;
    desc.visibility = QR_GEOMETRY_VISIBILITY_TYPE_WORLD_0;
    desc.passThrough = QR_GEOMETRY_PASS_THROUGH_TYPE_ALPHA_TESTED;
    desc.blend = EffectBlendForHandle(entity.customShader);
    desc.emission = 1.0f;
    desc.extraFlags = QR_GEOMETRY_UPLOAD_ALPHA_TRANSMISSION_BIT;
    desc.layerAlpha = layerAlpha;
    UploadDynamicGeometry(vertices, indices, desc);
}

void UploadPolyGeometry(const PendingPoly &poly)
{
    if (poly.verts.size() < 3) {
        return;
    }

    // Bullet marks, scorches and plasma decals are generated coplanar with the surface they
    // belong to, which makes every ray hit them twice. Push them out along the polygon's
    // average normal toward the viewer so perspective near FOV edges never flips the offset.
    float faceNormal[3] = {0.0f, 0.0f, 0.0f};
    {
        float centroid[3] = {0.0f, 0.0f, 0.0f};
        for (const polyVert_t &v : poly.verts) {
            centroid[0] += v.xyz[0];
            centroid[1] += v.xyz[1];
            centroid[2] += v.xyz[2];
        }
        const float invVerts = 1.0f / static_cast<float>(poly.verts.size());
        centroid[0] *= invVerts;
        centroid[1] *= invVerts;
        centroid[2] *= invVerts;

        const polyVert_t &v0 = poly.verts[0];
        for (size_t i = 1; i + 1 < poly.verts.size(); ++i) {
            const polyVert_t &v1 = poly.verts[i];
            const polyVert_t &v2 = poly.verts[i + 1];
            const float ax = v1.xyz[0] - v0.xyz[0];
            const float ay = v1.xyz[1] - v0.xyz[1];
            const float az = v1.xyz[2] - v0.xyz[2];
            const float bx = v2.xyz[0] - v0.xyz[0];
            const float by = v2.xyz[1] - v0.xyz[1];
            const float bz = v2.xyz[2] - v0.xyz[2];
            faceNormal[0] += ay * bz - az * by;
            faceNormal[1] += az * bx - ax * bz;
            faceNormal[2] += ax * by - ay * bx;
        }
        NormalizeVector(faceNormal);
        const float toPoly[3] = {
            centroid[0] - sceneDef.vieworg[0],
            centroid[1] - sceneDef.vieworg[1],
            centroid[2] - sceneDef.vieworg[2]
        };
        const float facing = faceNormal[0] * toPoly[0] +
            faceNormal[1] * toPoly[1] + faceNormal[2] * toPoly[2];
        if (facing > 0.0f) {
            faceNormal[0] = -faceNormal[0];
            faceNormal[1] = -faceNormal[1];
            faceNormal[2] = -faceNormal[2];
        }
        const float offset = qrayDecalOffset != nullptr ? qrayDecalOffset->value : 0.25f;
        for (int axis = 0; axis < 3; ++axis) {
            faceNormal[axis] *= offset;
        }
    }

    std::vector<QrVertex> vertices(poly.verts.size());
    const uint32_t polyCluster = ClusterIndexForPoint(poly.verts[0].xyz);
    for (size_t vertexIndex = 0; vertexIndex < poly.verts.size(); ++vertexIndex) {
        const polyVert_t &source = poly.verts[vertexIndex];
        QrVertex &destination = vertices[vertexIndex];
        destination = {};
        destination.position[0] = source.xyz[0] + faceNormal[0];
        destination.position[1] = source.xyz[1] + faceNormal[1];
        destination.position[2] = source.xyz[2] + faceNormal[2];
        destination.normal[0] = -sceneDef.viewaxis[0][0];
        destination.normal[1] = -sceneDef.viewaxis[0][1];
        destination.normal[2] = -sceneDef.viewaxis[0][2];
        destination.texCoord[0] = source.st[0];
        destination.texCoord[1] = source.st[1];
        destination.packedColor = PackColor(source.modulate);
        destination.cluster = polyCluster;
    }

    std::vector<uint32_t> indices;
    indices.reserve((poly.verts.size() - 2) * 3);
    for (size_t vertexIndex = 0; vertexIndex + 2 < poly.verts.size(); ++vertexIndex) {
        indices.push_back(0);
        indices.push_back(static_cast<uint32_t>(vertexIndex + 1));
        indices.push_back(static_cast<uint32_t>(vertexIndex + 2));
    }
    const float layerAlpha = poly.verts[0].modulate[3] != 0
        ? static_cast<float>(poly.verts[0].modulate[3]) / 255.0f
        : 1.0f;
    const QrGeometryMaterialBlendType blend = EffectBlendForHandle(poly.shader);
    QrBlendFactor blendSource = QR_BLEND_FACTOR_ONE;
    QrBlendFactor blendDestination = QR_BLEND_FACTOR_ONE;
    const bool blended = EffectBlendFactorsForHandle(poly.shader, blendSource, blendDestination);
    const bool isEnergyMark =
        NormalizeShaderName(ShaderNameForHandle(poly.shader)) == "gfx/damage/plasma_mrk";
    if (isEnergyMark) {
        blendSource = QR_BLEND_FACTOR_SRC_ALPHA;
        blendDestination = QR_BLEND_FACTOR_ONE;
    }
    // Darken decals with rgbGen exactVertex should preserve their vertex color fade; only replace
    // pure-black RGB with white when alpha > 0 (legacy shadow masks) or when rgbGen is identity.
    if (blend == QR_GEOMETRY_MATERIAL_BLEND_TYPE_SHADE) {
        LoadShaderScripts();
        const auto polyDefIt = shaderDefinitions.find(
            NormalizeShaderName(ShaderNameForHandle(poly.shader)));
        const bool stageIdentity = polyDefIt != shaderDefinitions.end() &&
            !polyDefIt->second.stages.empty() &&
            polyDefIt->second.stages.front().rgbGen == COLORGEN_IDENTITY;
        for (size_t vi = 0; vi < vertices.size(); ++vi) {
            const byte *c = poly.verts[vi].modulate;
            if (stageIdentity || (c[0] == 0 && c[1] == 0 && c[2] == 0 && c[3] > 0)) {
                vertices[vi].packedColor = 0xFFFFFFFFu;
            }
        }
    }
    // A darken decal is only dark once its real blend equation runs, so the blend-equation path
    // takes the untouched image instead of the luminance-to-alpha conversion the traced path needs.
    const QrMaterial material = blended ? GetRawMaterialForHandle(poly.shader)
                                        : GetMaterialForHandle(poly.shader);
    if (qrayDebugEntities != nullptr && qrayDebugEntities->integer != 0) {
        float minX = poly.verts[0].xyz[0], maxX = minX;
        float minY = poly.verts[0].xyz[1], maxY = minY;
        float minZ = poly.verts[0].xyz[2], maxZ = minZ;
        for (const polyVert_t &vertex : poly.verts) {
            minX = std::min(minX, vertex.xyz[0]);
            maxX = std::max(maxX, vertex.xyz[0]);
            minY = std::min(minY, vertex.xyz[1]);
            maxY = std::max(maxY, vertex.xyz[1]);
            minZ = std::min(minZ, vertex.xyz[2]);
            maxZ = std::max(maxZ, vertex.xyz[2]);
        }
        HostLog("UploadPoly: shader=%d name='%s' verts=%u alpha=%f blend=%d src=%d dst=%d "
            "material=%d default=%d size=(%.2f %.2f %.2f) modulate=(%u %u %u %u)",
            static_cast<int>(poly.shader), ShaderNameForHandle(poly.shader),
            static_cast<uint32_t>(poly.verts.size()), layerAlpha, static_cast<int>(blend),
            static_cast<int>(blendSource), static_cast<int>(blendDestination),
            static_cast<int>(material), material == defaultMaterial ? 1 : 0,
            maxX - minX, maxY - minY, maxZ - minZ,
            poly.verts[0].modulate[0], poly.verts[0].modulate[1],
            poly.verts[0].modulate[2], poly.verts[0].modulate[3]);
        static int polyLogBudget = 0;
        const bool impactMark = std::strstr(ShaderNameForHandle(poly.shader), "mrk") != nullptr;
        if (polyLogBudget < 12 && impactMark) {
            ++polyLogBudget;
            float centroid[3] = {0.0f, 0.0f, 0.0f};
            for (const QrVertex &vertex : vertices) {
                for (int axis = 0; axis < 3; ++axis) {
                    centroid[axis] += vertex.position[axis] / static_cast<float>(vertices.size());
                }
            }
            const float dx = centroid[0] - sceneDef.vieworg[0];
            const float dy = centroid[1] - sceneDef.vieworg[1];
            const float dz = centroid[2] - sceneDef.vieworg[2];
            const float forward = dx * sceneDef.viewaxis[0][0] + dy * sceneDef.viewaxis[0][1] +
                dz * sceneDef.viewaxis[0][2];
            HostLog("PolyPlacement: dist=%.1f forward=%.1f vieworg=(%.1f %.1f %.1f) "
                "centroid=(%.1f %.1f %.1f) fwdAxis=(%.2f %.2f %.2f)", std::sqrt(dx * dx + dy * dy + dz * dz), forward,
                sceneDef.vieworg[0], sceneDef.vieworg[1], sceneDef.vieworg[2],
                centroid[0], centroid[1], centroid[2],
                sceneDef.viewaxis[0][0], sceneDef.viewaxis[0][1], sceneDef.viewaxis[0][2]);
        }
    }
    if (qrayPolyDebug != nullptr && qrayPolyDebug->integer == 7) {
        const float scale = 8.0f;
        float centroid[3] = {0.0f, 0.0f, 0.0f};
        for (const QrVertex &vertex : vertices) {
            for (int axis = 0; axis < 3; ++axis) {
                centroid[axis] += vertex.position[axis] / static_cast<float>(vertices.size());
            }
        }
        std::vector<QrVertex> magnified = vertices;
        for (QrVertex &vertex : magnified) {
            for (int axis = 0; axis < 3; ++axis) {
                vertex.position[axis] = centroid[axis] + (vertex.position[axis] - centroid[axis]) * scale;
            }
        }
        UploadRasterizedWorldGeometry(magnified, indices, material, blendSource, blendDestination);
        return;
    }
    if (qrayPolyDebug != nullptr && qrayPolyDebug->integer == 8) {
        const float scale = 8.0f;
        float centroid[3] = {0.0f, 0.0f, 0.0f};
        for (const QrVertex &vertex : vertices) {
            for (int axis = 0; axis < 3; ++axis) {
                centroid[axis] += vertex.position[axis] / static_cast<float>(vertices.size());
            }
        }
        std::vector<QrVertex> magnified = vertices;
        for (QrVertex &vertex : magnified) {
            for (int axis = 0; axis < 3; ++axis) {
                vertex.position[axis] = centroid[axis] + (vertex.position[axis] - centroid[axis]) * scale;
            }
        }
        UploadRasterizedWorldGeometry(magnified, indices, material, blendSource, blendDestination,
            static_cast<uint32_t>(QR_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE));
        return;
    }
    if (qrayPolyDebug != nullptr && qrayPolyDebug->integer >= 4) {
        const float forwardDistance = 96.0f;
        const float scale = 3.0f;
        float centroid[3] = {0.0f, 0.0f, 0.0f};
        for (const QrVertex &vertex : vertices) {
            for (int axis = 0; axis < 3; ++axis) {
                centroid[axis] += vertex.position[axis] / static_cast<float>(vertices.size());
            }
        }
        std::vector<QrVertex> transplanted = vertices;
        for (QrVertex &vertex : transplanted) {
            const float local[3] = {
                (vertex.position[0] - centroid[0]) * scale,
                (vertex.position[1] - centroid[1]) * scale,
                (vertex.position[2] - centroid[2]) * scale};
            for (int axis = 0; axis < 3; ++axis) {
                vertex.position[axis] =
                    sceneDef.vieworg[axis] + sceneDef.viewaxis[0][axis] * forwardDistance +
                    sceneDef.viewaxis[1][axis] * local[0] +
                    sceneDef.viewaxis[2][axis] * local[1] +
                    sceneDef.viewaxis[0][axis] * local[2];
            }
        }
        UploadRasterizedWorldGeometry(transplanted, indices, material,
            blendSource, blendDestination);
        return;
    }
    if (qrayPolyDebug != nullptr && qrayPolyDebug->integer > 0) {
        const uint32_t debugState = qrayPolyDebug->integer == 1
            ? 0u
            : static_cast<uint32_t>(QR_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE);
        const QrMaterial debugMaterial = qrayPolyDebug->integer == 1 ? defaultMaterial : material;
        const QrBlendFactor debugSource = qrayPolyDebug->integer == 1
            ? QR_BLEND_FACTOR_ONE : blendSource;
        const QrBlendFactor debugDestination = qrayPolyDebug->integer == 1
            ? QR_BLEND_FACTOR_ONE : blendDestination;
        UploadRasterizedWorldGeometry(vertices, indices, debugMaterial,
            debugSource, debugDestination, debugState);
        return;
    }
    if (blended) {
        if (isEnergyMark) {
            const float energyMarkColor[4] = {0.85f, 0.85f, 0.85f, 1.0f};
            UploadRasterizedWorldGeometry(vertices, indices, material, blendSource, blendDestination,
                0xFFFFFFFFu, nullptr, energyMarkColor);
        } else {
            UploadRasterizedWorldGeometry(vertices, indices, material, blendSource, blendDestination);
        }
        return;
    }
    DynamicMeshUploadDesc desc{};
    desc.material = material;
    desc.visibility = QR_GEOMETRY_VISIBILITY_TYPE_WORLD_0;
    desc.passThrough = QR_GEOMETRY_PASS_THROUGH_TYPE_ALPHA_TESTED;
    desc.blend = blend;
    desc.emission = 1.0f;
    desc.layerAlpha = layerAlpha;
    UploadDynamicGeometry(vertices, indices, desc);
}

void UploadTestQuad();

void UploadTestQuad()
{
    if (qrayInstance == QR_NULL_HANDLE || !frameStarted) {
        return;
    }

    const int variant = qrayTestQuad != nullptr ? qrayTestQuad->integer : 0;
    const float forwardDistance = 96.0f;
    const float halfSize = 48.0f;
    float center[3];
    for (int axis = 0; axis < 3; ++axis) {
        center[axis] = sceneDef.vieworg[axis] + sceneDef.viewaxis[0][axis] * forwardDistance;
    }

    const float corners[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    std::vector<QrVertex> vertices(4);
    for (int i = 0; i < 4; ++i) {
        QrVertex &vertex = vertices[static_cast<size_t>(i)];
        vertex = {};
        for (int axis = 0; axis < 3; ++axis) {
            vertex.position[axis] = center[axis] +
                sceneDef.viewaxis[1][axis] * (corners[i][0] * halfSize) +
                sceneDef.viewaxis[2][axis] * (corners[i][1] * halfSize);
            vertex.normal[axis] = -sceneDef.viewaxis[0][axis];
        }
        vertex.texCoord[0] = (corners[i][0] + 1.0f) * 0.5f;
        vertex.texCoord[1] = (corners[i][1] + 1.0f) * 0.5f;
        vertex.packedColor = 0xFFFFFFFFu;
    }
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

    QrMaterial material = defaultMaterial;
    uint32_t pipelineState = 0;
    QrBlendFactor blendSource = QR_BLEND_FACTOR_ONE;
    QrBlendFactor blendDestination = QR_BLEND_FACTOR_ONE;
    float drawColorAlpha = 1.0f;

    switch (variant) {
    case 2:
        pipelineState = QR_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE |
            QR_RASTERIZED_GEOMETRY_STATE_DEPTH_TEST;
        blendSource = QR_BLEND_FACTOR_SRC_ALPHA;
        blendDestination = QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case 3:
    case 4: {
        pipelineState = QR_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE |
            QR_RASTERIZED_GEOMETRY_STATE_DEPTH_TEST;
        blendSource = QR_BLEND_FACTOR_ZERO;
        blendDestination = QR_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        if (variant == 3) {
            const QrMaterial markMaterial = GetRawMaterialForHandle(50097);
            if (markMaterial != QR_NO_MATERIAL) {
                material = markMaterial;
            }
        }
        break;
    }
    case 5: {
        pipelineState = QR_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE;
        blendSource = QR_BLEND_FACTOR_SRC_ALPHA;
        blendDestination = QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        const QrMaterial puffMaterial = GetMaterialForTextureName("gfx/misc/smokepuff3.tga");
        if (puffMaterial != QR_NO_MATERIAL) {
            material = puffMaterial;
        }
        for (QrVertex &vertex : vertices) {
            vertex.packedColor = 0x52FFFFFFu;
        }
        break;
    }
    case 6: {
        pipelineState = QR_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE;
        blendSource = QR_BLEND_FACTOR_SRC_ALPHA;
        blendDestination = QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        const QrMaterial puffMaterial = GetMaterialForTextureName("gfx/misc/smokepuff3.tga");
        if (puffMaterial != QR_NO_MATERIAL) {
            material = puffMaterial;
        }
        break;
    }
    case 9: {
        pipelineState = QR_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE;
        blendSource = QR_BLEND_FACTOR_SRC_ALPHA;
        blendDestination = QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        const QrMaterial puffMaterial = GetMaterialForTextureName("gfx/misc/smokepuff3.tga");
        if (puffMaterial != QR_NO_MATERIAL) {
            material = puffMaterial;
        }
        for (QrVertex &vertex : vertices) {
            vertex.packedColor = 0x00FFFFFFu;
        }
        break;
    }
    case 10: {
        pipelineState = QR_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE;
        blendSource = QR_BLEND_FACTOR_SRC_ALPHA;
        blendDestination = QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        const QrMaterial puffMaterial = GetMaterialForTextureName("gfx/misc/smokepuff3.tga");
        if (puffMaterial != QR_NO_MATERIAL) {
            material = puffMaterial;
        }
        drawColorAlpha = 0.25f;
        break;
    }
    default:
        break;
    }

    const std::vector<QrVertex> meterVertices = QuakeVerticesToMeters(vertices);
    QrRasterizedGeometryUploadInfo info{};
    info.renderType = QR_RASTERIZED_GEOMETRY_RENDER_TYPE_DEFAULT;
    info.vertexCount = static_cast<uint32_t>(meterVertices.size());
    info.pVertices = meterVertices.data();
    info.indexCount = static_cast<uint32_t>(indices.size());
    info.pIndices = indices.data();
    info.transform = IdentityTransform();
    info.color.data[0] = 1.0f;
    info.color.data[1] = 1.0f;
    info.color.data[2] = 1.0f;
    info.color.data[3] = drawColorAlpha;
    info.texCoordScale[0] = 1.0f;
    info.texCoordScale[1] = 1.0f;
    info.material = material;
    info.pipelineState = pipelineState;
    info.blendFuncSrc = blendSource;
    info.blendFuncDst = blendDestination;

    const QrResult result = qrUploadRasterizedGeometry(qrayInstance, &info, nullptr, nullptr);
    static int loggedVariant = -1;
    if (loggedVariant != variant) {
        loggedVariant = variant;
        HostLog("UploadTestQuad: variant=%d result=%d material=%d pipelineState=%u blend=%d/%d",
            variant, static_cast<int>(result), static_cast<int>(material),
            pipelineState, static_cast<int>(blendSource), static_cast<int>(blendDestination));
    }
}

constexpr int kSmokeMaxPuffs = 2048;
constexpr float kSmokeDrift = 128.0f;
constexpr float kSmokeRise = 64.0f;
constexpr float kSmokeDamp = 5.0f;
constexpr float kSmokeSpacing = 4.0f;
constexpr float kSmokeShape = 0.0f;
constexpr float kSmokeMedium = 0.1f;
constexpr float kSmokeDetail = 0.1f;
constexpr float kSmokeWind = 8.0f;
constexpr float kSmokeDisplace = 1.0f;
constexpr float kSmokeBreakup = 0.1f;
constexpr float kSmokeEdgePower = 2.0f;
constexpr float kSmokeEdgeGain = 1.0f;
constexpr float kSmokeTrailScaleRocket = 1.0f / 6.0f;
constexpr float kSmokeTrailScaleGrenade = 0.7f / 8.0f;
constexpr float kSmokeTrailIntervalSeconds = 1.0f / 72.0f;

struct SmokePuff
{
    float org[3] = {0.0f, 0.0f, 0.0f};
    float vel[3] = {0.0f, 0.0f, 0.0f};
    uint32_t cluster = 0u;
    float birth = 0.0f;
    float lifetime = 1.0f;
    float size = 1.0f;
    float growth = 0.0f;
    float seed = 0.0f;
    float alpha = 1.0f;
    float density = 2.0f;
    byte color[3] = {102, 102, 102};
};

struct TrackedSmokeMissile
{
    int kind = 0;
    float trailOrigin[3] = {0.0f, 0.0f, 0.0f};
    float lastOrigin[3] = {0.0f, 0.0f, 0.0f};
    float forward[3] = {1.0f, 0.0f, 0.0f};
    float scale = kSmokeTrailScaleRocket;
    float trailDelay = kSmokeTrailIntervalSeconds;
    bool matchedThisFrame = false;
};

struct SmokePoint3
{
    float xyz[3];
};

std::vector<SmokePuff> smokePuffs;
std::vector<TrackedSmokeMissile> trackedSmokeMissiles;
bool smokeTimeValid = false;
float lastSmokeUpdateSeconds = 0.0f;

float SmokeRandom01()
{
    return static_cast<float>(std::rand() & 0x7FFF) / 32767.0f;
}

float SmokeRandomRange(float lo, float hi)
{
    return lo + (hi - lo) * SmokeRandom01();
}

float PointDistanceSquared(const float a[3], const float b[3])
{
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

void CopyVec3(const float source[3], float destination[3])
{
    destination[0] = source[0];
    destination[1] = source[1];
    destination[2] = source[2];
}

bool AnyPointWithinDistance(const std::vector<SmokePoint3> &points, const float origin[3],
    float maxDistance, float *outNearest = nullptr)
{
    const float maxDistSq = maxDistance * maxDistance;
    float bestDistSq = maxDistSq;
    bool found = false;
    for (const SmokePoint3 &candidate : points) {
        const float distSq = PointDistanceSquared(candidate.xyz, origin);
        if (distSq <= bestDistSq) {
            bestDistSq = distSq;
            found = true;
            if (outNearest != nullptr) {
                CopyVec3(candidate.xyz, outNearest);
            }
        }
    }
    return found;
}

void ResolveSmokeColor(byte outColor[3])
{
    float rgb[3] = {0.45f, 0.45f, 0.45f};
    const char *text = qraySmokeColor != nullptr ? qraySmokeColor->string : "0.4 0.4 0.4";
    float parsed[3] = {};
    if (text != nullptr && ParseFloats(text, parsed, 3) == 3) {
        rgb[0] = parsed[0];
        rgb[1] = parsed[1];
        rgb[2] = parsed[2];
    }
    for (int i = 0; i < 3; ++i) {
        const float clamped = std::clamp(rgb[i], 0.0f, 1.0f);
        outColor[i] = static_cast<byte>(std::clamp(clamped * 255.0f, 0.0f, 255.0f));
    }
}

void SpawnSmokePuff(const float org[3], const float vel[3], float sizeScale, float alpha,
    float nowSeconds)
{
    const int maxPuffs = std::clamp(
        qraySmokeMax != nullptr ? qraySmokeMax->integer : 1024, 0, kSmokeMaxPuffs);
    if (maxPuffs <= 0) {
        return;
    }

    size_t slot = 0;
    if (smokePuffs.size() < static_cast<size_t>(maxPuffs)) {
        slot = smokePuffs.size();
        smokePuffs.emplace_back();
    } else {
        for (size_t i = 1; i < smokePuffs.size(); ++i) {
            if (smokePuffs[i].birth < smokePuffs[slot].birth) {
                slot = i;
            }
        }
    }

    SmokePuff &puff = smokePuffs[slot];
    CopyVec3(org, puff.org);
    CopyVec3(vel, puff.vel);
    puff.cluster = ClusterIndexForPoint(org);
    puff.birth = nowSeconds;
    puff.lifetime = std::max(0.05f, CvarFloat(qraySmokeLife, 1.5f) * SmokeRandomRange(0.75f, 1.25f));
    puff.size = std::max(0.5f, CvarFloat(qraySmokeSize, 16.0f) * sizeScale *
        SmokeRandomRange(0.7f, 1.3f));
    puff.growth = std::max(0.0f, CvarFloat(qraySmokeGrowth, 96.0f) * sizeScale);
    puff.seed = SmokeRandom01();
    puff.alpha = std::clamp(alpha, 0.0f, 1.0f);
    puff.density = std::max(0.0f, CvarFloat(qraySmokeDensity, 2.0f));
    ResolveSmokeColor(puff.color);
}

void EmitSmokeTrail(const float start[3], const float end[3], float sizeScale, float nowSeconds)
{
    if (sizeScale <= 0.0f) {
        return;
    }

    const float delta[3] = {end[0] - start[0], end[1] - start[1], end[2] - start[2]};
    const float len = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
    if (len < 0.25f) {
        return;
    }

    const float dir[3] = {delta[0] / len, delta[1] / len, delta[2] / len};
    const float baseSize = std::max(0.5f, CvarFloat(qraySmokeSize, 16.0f));
    const float spacing = std::max(0.25f,
        std::max(baseSize * sizeScale * 0.5f, kSmokeSpacing * sizeScale));
    const int count = std::clamp(static_cast<int>(len / spacing), 1, 64);
    const float jitter = std::max(1.0f, kSmokeDrift) * 0.35f;
    const float baseAlpha = std::max(0.0f, CvarFloat(qraySmokeAlpha, 1.0f));

    for (int i = 0; i < count; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
        const float org[3] = {
            start[0] + delta[0] * t + SmokeRandomRange(-2.0f, 2.0f),
            start[1] + delta[1] * t + SmokeRandomRange(-2.0f, 2.0f),
            start[2] + delta[2] * t + SmokeRandomRange(-2.0f, 2.0f)
        };
        const float vel[3] = {
            -dir[0] * kSmokeDrift + SmokeRandomRange(-jitter, jitter),
            -dir[1] * kSmokeDrift + SmokeRandomRange(-jitter, jitter),
            -dir[2] * kSmokeDrift + kSmokeRise + SmokeRandomRange(-jitter, jitter)
        };
        const float alpha = baseAlpha * SmokeRandomRange(0.8f, 1.15f);
        SpawnSmokePuff(org, vel, sizeScale, alpha, nowSeconds);
    }
}

void AdvanceSmokePuffs(float nowSeconds, float dt)
{
    const float damp = std::max(0.0f, 1.0f - kSmokeDamp * dt);
    for (size_t i = 0; i < smokePuffs.size();) {
        SmokePuff &puff = smokePuffs[i];
        if ((nowSeconds - puff.birth) >= puff.lifetime) {
            smokePuffs[i] = smokePuffs.back();
            smokePuffs.pop_back();
            continue;
        }
        puff.size = std::max(0.5f, puff.size + puff.growth * dt);
        for (int axis = 0; axis < 3; ++axis) {
            puff.vel[axis] *= damp;
            puff.org[axis] += puff.vel[axis] * dt;
        }
        const uint32_t updatedCluster = ClusterIndexForPoint(puff.org);
        if (updatedCluster != 0u) {
            puff.cluster = updatedCluster;
        }
        ++i;
    }
}

bool UpdateAndEmitProceduralSmoke(float &outNowSeconds)
{
    const float nowSeconds = static_cast<float>(sceneDef.time) * 0.001f;
    outNowSeconds = nowSeconds;
    if (qraySmoke != nullptr && qraySmoke->integer == 0) {
        ResetProceduralSmoke();
        return false;
    }
    if (!smokeTimeValid || nowSeconds < lastSmokeUpdateSeconds ||
        (nowSeconds - lastSmokeUpdateSeconds) > 1.0f) {
        ResetProceduralSmoke();
        lastSmokeUpdateSeconds = nowSeconds;
        smokeTimeValid = true;
    }

    const float dt = nowSeconds - lastSmokeUpdateSeconds;
    lastSmokeUpdateSeconds = nowSeconds;
    if (dt <= 0.0f) {
        return !smokePuffs.empty();
    }

    AdvanceSmokePuffs(nowSeconds, dt);

    std::vector<SmokePoint3> lightningEnds;
    std::vector<SmokePoint3> bubblePoints;
    std::vector<SmokePoint3> explosionPoints;
    for (const PendingEntity &pending : pendingEntities) {
        const refEntity_t &entity = pending.entity;
        if (entity.reType == RT_LIGHTNING) {
            lightningEnds.push_back({{entity.oldorigin[0], entity.oldorigin[1], entity.oldorigin[2]}});
        } else if (entity.reType == RT_SPRITE) {
            const std::string shaderKey = NormalizeShaderName(ShaderNameForHandle(entity.customShader));
            if (shaderKey == "waterbubble") {
                bubblePoints.push_back({{entity.origin[0], entity.origin[1], entity.origin[2]}});
            } else if (shaderKey == "rocketexplosion" || shaderKey == "grenadeexplosion") {
                explosionPoints.push_back({{entity.origin[0], entity.origin[1], entity.origin[2]}});
            }
        }
    }

    for (TrackedSmokeMissile &tracked : trackedSmokeMissiles) {
        tracked.matchedThisFrame = false;
    }

    const float maxMatchDist = std::max(96.0f, 1600.0f * dt);
    const float maxMatchDistSq = maxMatchDist * maxMatchDist;
    for (const PendingEntity &pending : pendingEntities) {
        const refEntity_t &entity = pending.entity;
        if (entity.reType != RT_MODEL ||
            (entity.renderfx & (RF_THIRD_PERSON | RF_FIRST_PERSON)) != 0) {
            continue;
        }
        const auto modelIt = modelAssets.find(entity.hModel);
        if (modelIt == modelAssets.end() || !modelIt->second.valid) {
            continue;
        }
        const std::string &modelName = modelIt->second.name;
        int kind = -1;
        float scale = 0.0f;
        if (modelName == "models/ammo/rocket/rocket" ||
            modelName == "models/ammo/rocket/rocket.md3") {
            if (AnyPointWithinDistance(lightningEnds, entity.origin, 8.0f)) {
                continue;
            }
            kind = 0;
            scale = kSmokeTrailScaleRocket;
        } else if (modelName == "models/ammo/grenade1" ||
            modelName == "models/ammo/grenade1.md3") {
            kind = 1;
            scale = kSmokeTrailScaleGrenade;
        } else {
            continue;
        }

        if (AnyPointWithinDistance(bubblePoints, entity.origin, 48.0f)) {
            continue;
        }

        size_t bestIdx = trackedSmokeMissiles.size();
        float bestDistSq = maxMatchDistSq;
        for (size_t i = 0; i < trackedSmokeMissiles.size(); ++i) {
            const TrackedSmokeMissile &candidate = trackedSmokeMissiles[i];
            if (candidate.matchedThisFrame || candidate.kind != kind) {
                continue;
            }
            const float distSq = PointDistanceSquared(candidate.lastOrigin, entity.origin);
            if (distSq <= bestDistSq) {
                bestDistSq = distSq;
                bestIdx = i;
            }
        }

        if (bestIdx < trackedSmokeMissiles.size()) {
            TrackedSmokeMissile &missile = trackedSmokeMissiles[bestIdx];
            const float stepDistSq = PointDistanceSquared(missile.lastOrigin, entity.origin);
            if (stepDistSq > (0.05f * 0.05f)) {
                missile.trailDelay -= dt;
                if (missile.trailDelay <= 0.0f) {
                    EmitSmokeTrail(missile.trailOrigin, entity.origin, scale, nowSeconds);
                    missile.trailDelay = std::max(0.0f, missile.trailDelay + kSmokeTrailIntervalSeconds);
                    CopyVec3(entity.origin, missile.trailOrigin);
                }
            }
            CopyVec3(entity.origin, missile.lastOrigin);
            CopyVec3(entity.axis[0], missile.forward);
            missile.matchedThisFrame = true;
        } else {
            const float tailStart[3] = {
                entity.origin[0] - entity.axis[0][0] * 10.0f,
                entity.origin[1] - entity.axis[0][1] * 10.0f,
                entity.origin[2] - entity.axis[0][2] * 10.0f
            };
            if (ClusterIndexForPoint(tailStart) != 0u) {
                EmitSmokeTrail(tailStart, entity.origin, scale, nowSeconds);
            }
            TrackedSmokeMissile added{};
            added.kind = kind;
            CopyVec3(entity.origin, added.trailOrigin);
            CopyVec3(entity.origin, added.lastOrigin);
            CopyVec3(entity.axis[0], added.forward);
            added.scale = scale;
            added.trailDelay = kSmokeTrailIntervalSeconds;
            added.matchedThisFrame = true;
            trackedSmokeMissiles.push_back(added);
        }
    }

    for (size_t i = 0; i < trackedSmokeMissiles.size();) {
        const TrackedSmokeMissile &missile = trackedSmokeMissiles[i];
        if (!missile.matchedThisFrame) {
            float nearestExplosion[3] = {};
            if (AnyPointWithinDistance(explosionPoints, missile.lastOrigin, maxMatchDist,
                    nearestExplosion)) {
                EmitSmokeTrail(missile.trailOrigin, nearestExplosion, missile.scale, nowSeconds);
            }
            trackedSmokeMissiles[i] = trackedSmokeMissiles.back();
            trackedSmokeMissiles.pop_back();
            continue;
        }
        ++i;
    }

    return !smokePuffs.empty();
}

void UploadProceduralSmoke(float nowSeconds)
{
    if (smokePuffs.empty() || qrayInstance == QR_NULL_HANDLE) {
        return;
    }

    std::vector<size_t> sortedIndices(smokePuffs.size());
    for (size_t i = 0; i < smokePuffs.size(); ++i) {
        sortedIndices[i] = i;
    }
    std::sort(sortedIndices.begin(), sortedIndices.end(), [](size_t a, size_t b) {
        const float distA = PointDistanceSquared(smokePuffs[a].org, sceneDef.vieworg);
        const float distB = PointDistanceSquared(smokePuffs[b].org, sceneDef.vieworg);
        return distA > distB;
    });

    static const float smokeCorners[6][2] = {
        {-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f},
        {-1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}
    };

    std::vector<QrVertex> vertices(smokePuffs.size() * 6u);
    size_t vertexOffset = 0u;
    for (size_t index : sortedIndices) {
        const SmokePuff &puff = smokePuffs[index];
        const float age01 = std::clamp((nowSeconds - puff.birth) / puff.lifetime, 0.0f, 1.0f);
        const byte rgba[4] = {puff.color[0], puff.color[1], puff.color[2], 255};
        const uint32_t packedColor = PackColor(rgba);
        const float sizeMeters = QuakeUnitsToMeters(puff.size);

        for (int corner = 0; corner < 6; ++corner, ++vertexOffset) {
            QrVertex &vert = vertices[vertexOffset];
            vert = {};
            QuakePositionToMeters(puff.org, vert.position);
            vert.packedColor = packedColor;
            vert.texCoord[0] = smokeCorners[corner][0];
            vert.texCoord[1] = smokeCorners[corner][1];
            vert.normal[0] = age01;
            vert.normal[1] = sizeMeters;
            vert.normal[2] = puff.seed;
            vert.texCoordLayer1[0] = puff.alpha;
            vert.texCoordLayer1[1] = puff.density;
            vert.cluster = puff.cluster;
        }
    }

    QrRasterizedGeometryUploadInfo info{};
    info.renderType = QR_RASTERIZED_GEOMETRY_RENDER_TYPE_DEFAULT;
    info.vertexCount = static_cast<uint32_t>(vertices.size());
    info.pVertices = vertices.data();
    info.indexCount = 0u;
    info.pIndices = nullptr;
    info.transform = IdentityTransform();
    info.color.data[0] = 1.0f;
    info.color.data[1] = 1.0f;
    info.color.data[2] = 1.0f;
    info.color.data[3] = 1.0f;
    info.texCoordScale[0] = 1.0f;
    info.texCoordScale[1] = 1.0f;
    info.material = QR_NO_MATERIAL;
    info.pipelineState = QR_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE |
        QR_RASTERIZED_GEOMETRY_STATE_DEPTH_TEST |
        QR_RASTERIZED_GEOMETRY_STATE_SMOKE;
    info.blendFuncSrc = QR_BLEND_FACTOR_ONE;
    info.blendFuncDst = QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    info.smokeNoise.data[0] = kSmokeShape / kQuakeUnitToMeter;
    info.smokeNoise.data[1] = kSmokeMedium / kQuakeUnitToMeter;
    info.smokeNoise.data[2] = kSmokeDetail / kQuakeUnitToMeter;
    info.smokeNoise.data[3] = kSmokeWind * kQuakeUnitToMeter;
    info.smokeLook.data[0] = kSmokeDisplace;
    info.smokeLook.data[1] = kSmokeBreakup;
    info.smokeLook.data[2] = kSmokeEdgePower;
    info.smokeLook.data[3] = kSmokeEdgeGain;

    const QrResult result = qrUploadRasterizedGeometry(qrayInstance, &info, nullptr, nullptr);
    if (result != QR_SUCCESS) {
        HostLog("UploadProceduralSmoke: failed result=%d puffs=%zu",
            static_cast<int>(result), smokePuffs.size());
    }
}

void ResetProceduralSmoke()
{
    smokePuffs.clear();
    trackedSmokeMissiles.clear();
    smokeTimeValid = false;
    lastSmokeUpdateSeconds = 0.0f;
}

EditorCameraBasis BuildGameCameraBasis()
{
    EditorCameraBasis cam;
    std::copy_n(sceneDef.vieworg, 3, cam.origin);
    for (int axis = 0; axis < 3; ++axis) {
        cam.forward[axis] = sceneDef.viewaxis[0][axis];
        cam.right[axis] = -sceneDef.viewaxis[1][axis];
        cam.up[axis] = sceneDef.viewaxis[2][axis];
    }
    cam.viewportWidth = static_cast<float>(std::max(1, glConfig.vidWidth));
    cam.viewportHeight = static_cast<float>(std::max(1, glConfig.vidHeight));
    const float aspect = cam.viewportWidth / cam.viewportHeight;
    float fovYRadians = sceneDef.fov_y * (kPi / 180.0f);
    if (sceneDef.fov_x > 0.0f && aspect > 0.0f) {
        fovYRadians = 2.0f * std::atan(std::tan(sceneDef.fov_x * (kPi / 360.0f)) / aspect);
    }
    cam.tanHalfFovY = std::tan(fovYRadians * 0.5f);
    cam.tanHalfFovX = cam.tanHalfFovY * aspect;
    return cam;
}

void UploadPendingEntities(int skipMask)
{
    for (const PendingEntity &pending : pendingEntities) {
        const refEntity_t &entity = pending.entity;
        if ((entity.renderfx & RF_THIRD_PERSON) != 0) {
            continue;
        }
        if (entity.reType == RT_MODEL && (skipMask & 1) == 0) {
            UploadModelEntity(entity, pending.shaderTimeSeconds);
        } else if (entity.reType == RT_SPRITE && (skipMask & 2) == 0) {
            UploadSpriteEntity(entity, pending.shaderTimeSeconds);
        } else if ((entity.reType == RT_BEAM || entity.reType == RT_RAIL_CORE ||
             entity.reType == RT_LIGHTNING) && (skipMask & 4) == 0) {
            UploadBeamEntity(entity);
        }
    }
}

void UploadPendingSceneGeometry()
{
    if (qrayInstance == QR_NULL_HANDLE || !frameStarted) {
        pendingEntities.clear();
        pendingPolys.clear();
        return;
    }

    frameDynamicEmissiveLights.clear();
    frameDynamicDtalBudget = 256;
    frameDynamicDtalNextId = 0x5000000000000000ull;

    const double timeSeconds = static_cast<double>(sceneDef.time) * 0.001;
    float timeSecondsF = static_cast<float>(timeSeconds);
    const bool hasProceduralSmoke = UpdateAndEmitProceduralSmoke(timeSecondsF);

    sceneRuntime.UploadWorldOverlays(qrayInstance, gameAssets, BuildGameCameraBasis(), timeSecondsF);

    const int skipMask = qraySkipEntities != nullptr ? qraySkipEntities->integer : 0;
    if ((skipMask & 8) == 0) {
        for (const PendingPoly &poly : pendingPolys) {
            UploadPolyGeometry(poly);
        }
    }
    UploadPendingEntities(skipMask);
    if (qrayTestQuad != nullptr && qrayTestQuad->integer != 0) {
        UploadTestQuad();
    }
    if (hasProceduralSmoke) {
        UploadProceduralSmoke(timeSecondsF);
    }
    sceneRuntime.UploadSky(qrayInstance, sceneDef.vieworg, timeSeconds,
        EffectiveCameraFarQuakeUnits());

    pendingEntities.clear();
    pendingPolys.clear();
}

void BuildGameDrawFrameBase(bool renderUiOnly, QrDrawFrameInfo &info)
{
    BuildViewMatrix(sceneDef, info.view);
    info.worldUpVector = {0.0f, 0.0f, 1.0f};
    const float aspect = glConfig.vidHeight > 0
        ? static_cast<float>(glConfig.vidWidth) / static_cast<float>(glConfig.vidHeight)
        : 1.0f;
    float fovYRadians = sceneDef.fov_y * (kPi / 180.0f);
    if (sceneDef.fov_x > 0.0f && aspect > 0.0f) {
        fovYRadians = 2.0f * std::atan(std::tan(sceneDef.fov_x * (kPi / 360.0f)) / aspect);
    }
    info.fovYRadians = fovYRadians;
    const float tanHalfFovY = std::max(std::tan(fovYRadians * 0.5f), 1.0e-3f);
    const float tanHalfFovX = std::max(tanHalfFovY * std::max(aspect, 1.0e-3f), 1.0e-3f);
    const float minCotHalfFov = std::min(1.0f / tanHalfFovX, 1.0f / tanHalfFovY);
    info.cameraNear = std::clamp(12.0f * minCotHalfFov, 1.0f, 3.0f);
    info.cameraFar = EffectiveCameraFarQuakeUnits();
    info.rayLength = EffectiveRayLengthQuakeUnits();
    info.rayCullMaskWorld = QR_DRAW_FRAME_RAY_CULL_WORLD_0_BIT |
        QR_DRAW_FRAME_RAY_CULL_WORLD_1_BIT | QR_DRAW_FRAME_RAY_CULL_SKY_BIT;
    info.disableRayTracedGeometry = QR_FALSE;
    info.disableRasterization = QR_FALSE;
    info.currentTime = static_cast<double>(sceneDef.time) * 0.001;
    info.disableEyeAdaptation = (!AuthoredModeEnabled() && qrayEyeAdaptation != nullptr &&
        qrayEyeAdaptation->integer == 0) ? QR_TRUE : QR_FALSE;
    info.renderUiOnly = renderUiOnly ? QR_TRUE : QR_FALSE;
    info.enableCpuProfiling = QR_FALSE;
}

void DrawFrame()
{
    if (qrayInstance == QR_NULL_HANDLE || !frameStarted) {
        return;
    }

    const bool renderUiOnly = !frameHasWorldScene;
    if (!sceneRendered) {
        std::memset(&sceneDef, 0, sizeof(sceneDef));
        sceneDef.viewaxis[0][0] = 1.0f;
        sceneDef.viewaxis[1][1] = 1.0f;
        sceneDef.viewaxis[2][2] = 1.0f;
        sceneDef.fov_y = 90.0f;
        sceneDef.time = ri.Milliseconds();
        sceneDef.width = glConfig.vidWidth;
        sceneDef.height = glConfig.vidHeight;
    }

    ApplyQrayCvarsToRuntime(true);

    QrDrawFrameInfo info{};
    BuildGameDrawFrameBase(renderUiOnly, info);

    EditorFrameParams frameParams{};
    sceneRuntime.PopulateDrawFrame(qrayInstance, sceneDef.vieworg,
        static_cast<uint32_t>(std::max(1, glConfig.vidWidth)),
        static_cast<uint32_t>(std::max(1, glConfig.vidHeight)),
        frameParams, info);

    for (uint32_t style = 1; style < QR_LIGHT_STYLE_COUNT; ++style) {
        frameParams.textures.lightStyleScales[style] =
            EvaluateEditorLightStyle(static_cast<int32_t>(style), info.currentTime);
    }
    if (gHasTonemapPowerOverride || !AuthoredModeEnabled()) {
        frameParams.tonemap.tonemapPower = gHasTonemapPowerOverride
            ? gTonemapPowerOverride
            : std::clamp(CvarFloat(qrayTonemapPower, 0.9f), 0.0f, 1.0f);
    }
    if (gHasExposureBiasOverride || !AuthoredModeEnabled()) {
        frameParams.tonemap.exposureBias = gHasExposureBiasOverride
            ? gExposureBiasOverride
            : CvarFloat(qrayExposureBias, 0.0f);
    }
    if (gHasEyeAdaptationOverride) {
        info.disableEyeAdaptation = gEyeAdaptationOverride ? QR_FALSE : QR_TRUE;
    }

    UploadSceneLights();

    const QrResult result = EditorDrawFrame(qrayInstance, &info);
    if (result != QR_SUCCESS) {
        ri.Printf(PRINT_WARNING, "rendererqray: EditorDrawFrame failed: %d\n",
            static_cast<int>(result));
    }

    UpdateReadback(false);
    CopyReadbackToCapture();

    frameStarted = false;
    sceneValid = false;
    sceneRendered = false;
    frameHasWorldScene = false;
}

void ShutdownRenderer(qboolean destroyWindow)
{
    if (destroyWindow == qfalse) {
        if (frameStarted && qrayInstance != QR_NULL_HANDLE) {
            DrawFrame();
        }
        frameStarted = false;
        sceneValid = false;
        return;
    }

    if (frameStarted && qrayInstance != QR_NULL_HANDLE) {
        DrawFrame();
    }

    // The SDK may still write into readbackBuffer until the instance is gone, so the
    // host buffers are released only after qrDestroyInstance.
    if (qrayInstance != QR_NULL_HANDLE) {
        sceneRuntime.DestroyGpuResources(qrayInstance);
        qrDestroyInstance(qrayInstance);
        qrayInstance = QR_NULL_HANDLE;
    }
    ResetReadback();
    capturePending = false;
    captureBuffer = nullptr;

    if (commandsRegistered) {
        ri.Cmd_RemoveCommand("screenshot");
        ri.Cmd_RemoveCommand("screenshotJPEG");
        ri.Cmd_RemoveCommand("qray_readback");
        ri.Cmd_RemoveCommand("qray_probe");
        commandsRegistered = false;
    }

    ResetMaterialState();
    frameStarted = false;
    sceneLightsUploaded = false;
    sceneValid = false;
    probeSceneValid = false;

    QrayWindow_Shutdown(ri);
}

void BeginRegistration(glconfig_t *config)
{
    HostLog("BeginRegistration: entry config=%p", config);
    RegisterCvars();

    if (config != nullptr) {
        if (!QrayWindow_IsOpen()) {
            HostLog("BeginRegistration: QrayWindow_Init");
            if (!QrayWindow_Init(ri, &glConfig)) {
                ri.Error(ERR_FATAL, "rendererqray: failed to create SDL Vulkan window");
                return;
            }
            HostLog("BeginRegistration: QrayWindow_Init done %dx%d", glConfig.vidWidth, glConfig.vidHeight);
        } else {
            glConfig = *config;
        }
    }

    if (qrayInstance == QR_NULL_HANDLE) {
        if (!CreateQrayInstance()) {
            HostLog("BeginRegistration: CreateQrayInstance failed");
            ri.Printf(PRINT_WARNING, "rendererqray: QuakeRay initialization failed; check qray_shaderDir and qray_blueNoise, or use cl_renderer opengl2\n");
            return;
        }
        if (!ConfigureSpatialConvention()) {
            qrDestroyInstance(qrayInstance);
            qrayInstance = QR_NULL_HANDLE;
            HostLog("BeginRegistration: ConfigureSpatialConvention failed");
            return;
        }
        HostLog("BeginRegistration: CreateDefaultMaterial");
        CreateDefaultMaterial();
        RebuildUiMaterials();
    }

    if (config != nullptr) {
        QrAdapterInfo adapter{};
        if (qrayInstance != QR_NULL_HANDLE && qrGetAdapterInfo(qrayInstance, &adapter) == QR_SUCCESS) {
            CopyString(glConfig.renderer_string, "QuakeRay", sizeof(glConfig.renderer_string));
            CopyString(glConfig.vendor_string, adapter.name, sizeof(glConfig.vendor_string));
            CopyString(glConfig.version_string, QR_API_VERSION, sizeof(glConfig.version_string));
        }
        glConfig.colorBits = 32;
        glConfig.depthBits = 24;
        glConfig.stencilBits = 8;
        glConfig.maxTextureSize = 16384;
        glConfig.numTextureUnits = 8;
        glConfig.deviceSupportsGamma = qfalse;
        glConfig.driverType = GLDRV_ICD;
        glConfig.hardwareType = GLHW_GENERIC;
        glConfig.windowAspect = glConfig.vidHeight > 0
            ? static_cast<float>(glConfig.vidWidth) / static_cast<float>(glConfig.vidHeight)
            : 1.0f;
        *config = glConfig;
    }

    if (!commandsRegistered) {
        ri.Cmd_AddCommand("screenshot", QrayScreenshot_f);
        ri.Cmd_AddCommand("screenshotJPEG", QrayScreenshot_f);
        ri.Cmd_AddCommand("qray_readback", QrayReadbackDump_f);
        ri.Cmd_AddCommand("qray_probe", QrayProbe_f);
        commandsRegistered = true;
    }
}

void EndRegistration()
{
    HostLog("EndRegistration");
}

// Console commands can be issued by game VMs (trap_SendConsoleCommand), so file names
// for the screenshot/readback commands must stay relative and inside the game root.
bool IsSafeOutputFileName(const char *name)
{
    if (name == nullptr || name[0] == '\0' || name[0] == '/' || name[0] == '\\') {
        return false;
    }
    if (std::strstr(name, "..") != nullptr || std::strchr(name, ':') != nullptr) {
        return false;
    }
    for (const char *c = name; *c != '\0'; ++c) {
        if (static_cast<unsigned char>(*c) < 0x20) {
            return false;
        }
    }
    return true;
}

void QrayScreenshot_f()
{
    if (qrayInstance == QR_NULL_HANDLE) {
        return;
    }

    const char *name = ri.Cmd_Argc() > 1 ? ri.Cmd_Argv(1) : "";
    if (name[0] == '\0' || _stricmp(name, "silent") == 0 || _stricmp(name, "levelshot") == 0) {
        name = "qray-screenshot.png";
    }
    if (!IsSafeOutputFileName(name)) {
        ri.Printf(PRINT_WARNING, "rendererqray: refusing unsafe screenshot name '%s'\n", name);
        return;
    }

    const QrResult result = qrRequestScreenshot(qrayInstance, name);
    if (result == QR_SUCCESS) {
        ri.Printf(PRINT_ALL, "rendererqray: requested screenshot %s\n", name);
    } else {
        ri.Printf(PRINT_WARNING, "rendererqray: screenshot request failed: %d\n", static_cast<int>(result));
    }
}

void QrayReadbackDump_f()
{
    readbackWantedFrames = kReadbackKeepAliveFrames;
    if (!readbackValid || readbackWidth == 0 || readbackHeight == 0 ||
        readbackReadyPixels.size() < static_cast<size_t>(readbackWidth) * readbackHeight * 4) {
        ri.Printf(PRINT_WARNING, "rendererqray: no completed readback frame yet; readback started, run qray_readback again\n");
        return;
    }

    const uint32_t width = readbackWidth;
    const uint32_t height = readbackHeight;
    const uint32_t imageSize = width * height * 4;

    // BITMAPFILEHEADER (14 bytes) + BITMAPINFOHEADER (40 bytes), little endian, written
    // by hand so the dump does not depend on <windows.h> or struct packing.
    uint8_t header[54] = {};
    const auto put16 = [&header](size_t offset, uint32_t value) {
        header[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        header[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    };
    const auto put32 = [&header](size_t offset, uint32_t value) {
        for (size_t i = 0; i < 4; ++i) {
            header[offset + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xffu);
        }
    };
    put16(0, 0x4D42u);                                     // bfType 'BM'
    put32(2, static_cast<uint32_t>(sizeof(header)) + imageSize); // bfSize
    put32(10, static_cast<uint32_t>(sizeof(header)));      // bfOffBits
    put32(14, 40u);                                         // biSize
    put32(18, width);                                       // biWidth
    put32(22, static_cast<uint32_t>(-static_cast<int32_t>(height))); // top-down
    put16(26, 1u);                                          // biPlanes
    put16(28, 32u);                                         // biBitCount
    put32(30, 0u);                                          // BI_RGB
    put32(34, imageSize);                                   // biSizeImage

    const char *name = ri.Cmd_Argc() > 1 ? ri.Cmd_Argv(1) : "qray-readback.bmp";
    if (!IsSafeOutputFileName(name)) {
        ri.Printf(PRINT_WARNING, "rendererqray: refusing unsafe readback name '%s'\n", name);
        return;
    }
    std::ofstream stream(name, std::ios::binary);
    if (!stream.is_open()) {
        ri.Printf(PRINT_WARNING, "rendererqray: cannot write %s\n", name);
        return;
    }

    stream.write(reinterpret_cast<const char *>(header), sizeof(header));
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *row = readbackReadyPixels.data() + static_cast<size_t>(y) * readbackRowPitch;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t bgra[4] = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0], row[x * 4 + 3]};
            stream.write(reinterpret_cast<const char *>(bgra), 4);
        }
    }

    ri.Printf(PRINT_ALL, "rendererqray: wrote %s (%ux%u)\n", name, width, height);
}

struct ProbeHit
{
    const MarkTriangle *triangle = nullptr;
    float distance = FLT_MAX;
    float u = 0.0f;
    float v = 0.0f;
    float facing = 0.0f;
    bool frontFacing = false;
};

void FindNearestProbeHit(const float origin[3], const float direction[3],
                         ProbeHit &nearestAny, ProbeHit &nearestFront)
{
    constexpr float kProbeDistance = 32768.0f;
    float hitDistance = 0.0f;
    float hitU = 0.0f;
    float hitV = 0.0f;
    for (const MarkTriangle &triangle : markTriangles) {
        if (!RayIntersectsTriangle(triangle, origin, direction, kProbeDistance,
                hitDistance, &hitU, &hitV)) {
            continue;
        }
        const float facing = triangle.normal[0] * direction[0] +
            triangle.normal[1] * direction[1] + triangle.normal[2] * direction[2];
        if (hitDistance < nearestAny.distance) {
            nearestAny = ProbeHit{&triangle, hitDistance, hitU, hitV, facing, facing < -0.001f};
        }
        if (facing < -0.001f && hitDistance < nearestFront.distance) {
            nearestFront = ProbeHit{&triangle, hitDistance, hitU, hitV, facing, true};
        }
    }
}

void InterpolateProbeHitAttrs(const MarkTriangle &triangle, const ProbeHit &primary,
    float hitUv[2], float interpolatedNormal[3])
{
    const float barycentric[3] = {1.0f - primary.u - primary.v, primary.u, primary.v};
    for (int axis = 0; axis < 3; ++axis) {
        interpolatedNormal[axis] = 0.0f;
        for (int corner = 0; corner < 3; ++corner) {
            interpolatedNormal[axis] += triangle.vertexNormals[corner][axis] * barycentric[corner];
        }
    }
    NormalizeVector(interpolatedNormal);
    for (int axis = 0; axis < 2; ++axis) {
        hitUv[axis] = 0.0f;
        for (int corner = 0; corner < 3; ++corner) {
            hitUv[axis] += triangle.texCoords[corner][axis] * barycentric[corner];
        }
    }
}

void LogProbeHitDetails(int sequence, const float origin[3], const float direction[3],
    const ProbeHit &primary)
{
    const MarkTriangle &triangle = *primary.triangle;
    float hitPoint[3] = {};
    for (int axis = 0; axis < 3; ++axis) {
        hitPoint[axis] = origin[axis] + direction[axis] * primary.distance;
    }
    float hitUv[2] = {};
    float interpolatedNormal[3] = {};
    InterpolateProbeHitAttrs(triangle, primary, hitUv, interpolatedNormal);

    const EditorScene &scene = sceneRuntime.scene;
    const EditorSurface *surface = (triangle.sourceSurfaceIndex >= 0 &&
        static_cast<size_t>(triangle.sourceSurfaceIndex) < scene.surfaces.size())
        ? &scene.surfaces[static_cast<size_t>(triangle.sourceSurfaceIndex)]
        : nullptr;
    const char *shaderName = surface != nullptr ? surface->textureName.c_str() : "_unknown";
    const QrMaterial material = surface != nullptr
        ? sceneRuntime.SurfaceMaterial(*surface, defaultMaterial)
        : QR_NO_MATERIAL;
    const uint32_t cluster = ClusterIndexForPoint(hitPoint);
    const char *kindName = surface != nullptr ? SurfaceKindName(surface->kind) : "UNKNOWN";
    const char *tag = primary.frontFacing ? "QRAY_PROBE" : "QRAY_PROBE_BACKFACE";

    ProbeLog("%s seq=%d status=HIT map=%s origin=(%.3f %.3f %.3f) dir=(%.6f %.6f %.6f) "
        "surface=%d tri=%u shader=%s material=%d kind=%s dist=%.3f point=(%.3f %.3f %.3f) "
        "uv=(%.6f %.6f) cluster=%u front=%d rayDotNormal=%.6f triNormal=(%.6f %.6f %.6f) "
        "vertexNormal=(%.6f %.6f %.6f)",
        tag, sequence, pendingWorldName.c_str(), origin[0], origin[1], origin[2],
        direction[0], direction[1], direction[2], triangle.sourceSurfaceIndex,
        triangle.sourceTriangleIndex, shaderName, static_cast<int>(material), kindName,
        primary.distance, hitPoint[0], hitPoint[1], hitPoint[2], hitUv[0], hitUv[1], cluster,
        primary.frontFacing ? 1 : 0, primary.facing,
        triangle.normal[0], triangle.normal[1], triangle.normal[2],
        interpolatedNormal[0], interpolatedNormal[1], interpolatedNormal[2]);

    ri.Printf(PRINT_ALL,
        "rendererqray: qray_probe #%d: %s surface=%d dist=%.1f front=%d texture=%s\n",
        sequence, kindName, triangle.sourceSurfaceIndex, primary.distance,
        primary.frontFacing ? 1 : 0, shaderName);
}

void QrayProbe_f()
{
    static int probeSequence = 0;
    const int sequence = ++probeSequence;
    if (!probeSceneValid || markTriangles.empty()) {
        ProbeLog("QRAY_PROBE seq=%d status=NO_SCENE map=%s triangles=%zu",
            sequence, pendingWorldName.c_str(), markTriangles.size());
        ri.Printf(PRINT_WARNING, "rendererqray: qray_probe has no loaded world scene\n");
        return;
    }

    const float *origin = probeSceneDef.vieworg;
    float direction[3] = {
        probeSceneDef.viewaxis[0][0],
        probeSceneDef.viewaxis[0][1],
        probeSceneDef.viewaxis[0][2],
    };
    NormalizeVector(direction);
    if (direction[0] == 0.0f && direction[1] == 0.0f && direction[2] == 0.0f) {
        ProbeLog("QRAY_PROBE seq=%d status=INVALID_VIEW map=%s", sequence, pendingWorldName.c_str());
        ri.Printf(PRINT_WARNING, "rendererqray: qray_probe has an invalid view direction\n");
        return;
    }

    ProbeHit nearestAny;
    ProbeHit nearestFront;
    FindNearestProbeHit(origin, direction, nearestAny, nearestFront);
    if (nearestAny.triangle == nullptr) {
        ProbeLog("QRAY_PROBE seq=%d status=NO_HIT map=%s origin=(%.3f %.3f %.3f) "
            "dir=(%.6f %.6f %.6f) tested=%zu",
            sequence, pendingWorldName.c_str(), origin[0], origin[1], origin[2],
            direction[0], direction[1], direction[2], markTriangles.size());
        ri.Printf(PRINT_ALL, "rendererqray: qray_probe #%d: no world triangle hit\n", sequence);
        return;
    }

    const ProbeHit &primary = nearestFront.triangle != nullptr ? nearestFront : nearestAny;
    LogProbeHitDetails(sequence, origin, direction, primary);
}

void LoadWorld(const char *name)
{
    HostLog("LoadWorld: name=%s instance=%p", name != nullptr ? name : "(null)", qrayInstance);
    if (name == nullptr || name[0] == '\0' || qrayInstance == QR_NULL_HANDLE) {
        return;
    }
    pendingWorldName = name;
    worldLoaded = false;
    probeSceneValid = false;
    ResetProceduralSmoke();
    LoadInlineBspModels(name);
}

void ClearScene()
{
    sceneValid = false;
    pendingEntities.clear();
    pendingPolys.clear();
}

void AddRefEntityToScene(const refEntity_t *entity)
{
    if (entity == nullptr) {
        return;
    }
    if (pendingEntities.size() >= 16384) {
        return;
    }
    PendingEntity pending{};
    pending.entity = *entity;
    pending.shaderTimeSeconds = entity->shaderTime;
    pendingEntities.push_back(pending);
}

void AddPolyToScene(qhandle_t shader, int numVerts, const polyVert_t *verts, int num)
{
    if (numVerts < 3 || verts == nullptr || num <= 0) {
        return;
    }
    const std::string shaderKey = NormalizeShaderName(ShaderNameForHandle(shader));
    if (shaderKey == "markshadow" || shaderKey == "projectionshadow") {
        return;
    }
    for (int polyIndex = 0; polyIndex < num && pendingPolys.size() < 16384; ++polyIndex) {
        PendingPoly poly;
        poly.shader = shader;
        const polyVert_t *source = verts + static_cast<size_t>(polyIndex) * numVerts;
        poly.verts.assign(source, source + numVerts);
        pendingPolys.push_back(std::move(poly));
    }
}

int LightForPoint(vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir)
{
    (void)point;
    ambientLight[0] = ambientLight[1] = ambientLight[2] = 0.0f;
    directedLight[0] = directedLight[1] = directedLight[2] = 0.0f;
    lightDir[0] = 0.0f;
    lightDir[1] = 0.0f;
    lightDir[2] = 1.0f;
    return 0;
}

void AddDynamicLight(const float origin[3], float radius, float r, float g, float b,
    bool additive, bool linear)
{
    if (origin == nullptr || radius <= 0.0f || dynamicLights.size() >= MAX_DLIGHTS) {
        return;
    }

    DynamicLight light{};
    light.origin[0] = origin[0];
    light.origin[1] = origin[1];
    light.origin[2] = origin[2];
    light.color[0] = std::max(r, 0.0f);
    light.color[1] = std::max(g, 0.0f);
    light.color[2] = std::max(b, 0.0f);
    light.radius = radius;
    light.additive = additive;
    light.linear = linear;
    dynamicLights.push_back(light);
}

void AddLightToScene(const vec3_t org, float intensity, float r, float g, float b)
{
    AddDynamicLight(org, intensity, r, g, b, false, false);
}

void AddAdditiveLightToScene(const vec3_t org, float intensity, float r, float g, float b)
{
    AddDynamicLight(org, intensity, r, g, b, true, false);
}

void RenderScene(const refdef_t *fd)
{
    if (fd == nullptr) {
        return;
    }
    const bool worldScene = (fd->rdflags & RDF_NOWORLDMODEL) == 0;
    if (worldScene || !frameHasWorldScene) {
        const bool firstWorldScene = worldScene && !frameHasWorldScene;
        sceneDef = *fd;
        sceneValid = true;
        sceneRendered = true;
        if (worldScene) {
            frameHasWorldScene = true;
            probeSceneDef = *fd;
            probeSceneValid = true;
        }
        if (firstWorldScene) {
            UploadPendingSceneGeometry();
        }
    }
}

void SetColor(const float *rgba)
{
    if (rgba != nullptr) {
        currentColor[0] = rgba[0];
        currentColor[1] = rgba[1];
        currentColor[2] = rgba[2];
        currentColor[3] = rgba[3];
    } else {
        currentColor[0] = 1.0f;
        currentColor[1] = 1.0f;
        currentColor[2] = 1.0f;
        currentColor[3] = 1.0f;
    }
}

void DrawStretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t shader)
{
    if (qrayInstance == QR_NULL_HANDLE || !frameStarted || w <= 0.0f || h <= 0.0f) {
        return;
    }

    const float x0 = x;
    const float y0 = y;
    const float x1 = x + w;
    const float y1 = y + h;

    QrVertex vertices[6] = {};
    const float positions[4][2] = {
        {x0, y0},
        {x1, y0},
        {x1, y1},
        {x0, y1}
    };
    const float texCoords[4][2] = {
        {s1, t1},
        {s2, t1},
        {s2, t2},
        {s1, t2}
    };
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) {
        const int corner = order[i];
        vertices[i].position[0] = positions[corner][0];
        vertices[i].position[1] = positions[corner][1];
        vertices[i].position[2] = 0.0f;
        vertices[i].texCoord[0] = texCoords[corner][0];
        vertices[i].texCoord[1] = texCoords[corner][1];
        vertices[i].packedColor = 0xFFFFFFFFu;
    }

    QrMaterial material = defaultMaterial;
    const auto materialIt = uiMaterials.find(shader);
    if (materialIt != uiMaterials.end()) {
        material = materialIt->second;
    }

    float overlayMatrix[16] = {};
    QrViewport viewport{};
    BuildOverlayMatrix(overlayMatrix, viewport);

    QrRasterizedGeometryUploadInfo info{};
    info.renderType = QR_RASTERIZED_GEOMETRY_RENDER_TYPE_SWAPCHAIN;
    info.vertexCount = 6;
    info.pVertices = vertices;
    info.indexCount = 0;
    info.pIndices = nullptr;
    info.transform = IdentityTransform();
    info.color.data[0] = currentColor[0];
    info.color.data[1] = currentColor[1];
    info.color.data[2] = currentColor[2];
    info.color.data[3] = currentColor[3];
    info.texCoordScale[0] = 1.0f;
    info.texCoordScale[1] = 1.0f;
    info.material = material;
    info.pipelineState = QR_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE;
    info.blendFuncSrc = QR_BLEND_FACTOR_SRC_ALPHA;
    info.blendFuncDst = QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    const QrResult result = qrUploadRasterizedGeometry(qrayInstance, &info, overlayMatrix, &viewport);
    if (result != QR_SUCCESS) {
        HostLog("DrawStretchPic: upload failed result=%d", static_cast<int>(result));
    }
}

void DrawStretchRaw(int x, int y, int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)cols;
    (void)rows;
    (void)data;
    (void)client;
    (void)dirty;
}

void UploadCinematic(int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty)
{
    (void)w;
    (void)h;
    (void)cols;
    (void)rows;
    (void)data;
    (void)client;
    (void)dirty;
}

void BeginFrame(stereoFrame_t stereoFrame)
{
    (void)stereoFrame;
    StartFrame();
}

void EndFrame(int *frontEndMsec, int *backEndMsec)
{
    DrawFrame();
    QrayWindow_CheckFullscreenToggle(ri);
    if (frontEndMsec != nullptr) {
        *frontEndMsec = 0;
    }
    if (backEndMsec != nullptr) {
        *backEndMsec = 0;
    }
}

bool RayIntersectsTriangle(const MarkTriangle &triangle, const float origin[3], const float direction[3],
    float maxDistance, float &hitDistance, float *outU = nullptr, float *outV = nullptr)
{
    const float *v0 = triangle.points[0];
    const float *v1 = triangle.points[1];
    const float *v2 = triangle.points[2];
    const float edge1[3] = {v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]};
    const float edge2[3] = {v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]};
    const float pvec[3] = {
       direction[1] * edge2[2] - direction[2] * edge2[1],
       direction[2] * edge2[0] - direction[0] * edge2[2],
       direction[0] * edge2[1] - direction[1] * edge2[0],
    };
    const float determinant = edge1[0] * pvec[0] + edge1[1] * pvec[1] + edge1[2] * pvec[2];
    if (std::fabs(determinant) < 1e-7f) {
       return false;
    }

    const float inverse = 1.0f / determinant;
    const float tvec[3] = {origin[0] - v0[0], origin[1] - v0[1], origin[2] - v0[2]};
    const float u = (tvec[0] * pvec[0] + tvec[1] * pvec[1] + tvec[2] * pvec[2]) * inverse;
    if (u < 0.0f || u > 1.0f) {
       return false;
    }

    const float qvec[3] = {
       tvec[1] * edge1[2] - tvec[2] * edge1[1],
       tvec[2] * edge1[0] - tvec[0] * edge1[2],
       tvec[0] * edge1[1] - tvec[1] * edge1[0],
    };
    const float v = (direction[0] * qvec[0] + direction[1] * qvec[1] + direction[2] * qvec[2]) * inverse;
    if (v < 0.0f || u + v > 1.0f) {
       return false;
    }

    const float distance = (edge2[0] * qvec[0] + edge2[1] * qvec[1] + edge2[2] * qvec[2]) * inverse;
    if (distance <= 0.0f || distance > maxDistance) {
       return false;
    }
    hitDistance = distance;
    if (outU != nullptr) {
        *outU = u;
    }
    if (outV != nullptr) {
        *outV = v;
    }
    return true;
}

constexpr int kMaxMarkClipVerts = 64;

void ChopPolyBehindPlane(int numInPoints, const float inPoints[kMaxMarkClipVerts][3],
    int &numOutPoints, float outPoints[kMaxMarkClipVerts][3], const float normal[3], float dist,
    float epsilon)
{
    if (numInPoints < 3 || numInPoints >= kMaxMarkClipVerts - 2) {
        numOutPoints = 0;
        return;
    }

    enum ClipSide
    {
        SIDE_FRONT = 0,
        SIDE_BACK = 1,
        SIDE_ON = 2
    };

    float dists[kMaxMarkClipVerts + 1] = {};
    ClipSide sides[kMaxMarkClipVerts + 1] = {};
    int counts[3] = {0, 0, 0};

    for (int i = 0; i < numInPoints; ++i) {
        const float d = inPoints[i][0] * normal[0] + inPoints[i][1] * normal[1] +
            inPoints[i][2] * normal[2] - dist;
        dists[i] = d;
        if (d > epsilon) {
            sides[i] = SIDE_FRONT;
        } else if (d < -epsilon) {
            sides[i] = SIDE_BACK;
        } else {
            sides[i] = SIDE_ON;
        }
        ++counts[sides[i]];
    }
    sides[numInPoints] = sides[0];
    dists[numInPoints] = dists[0];

    numOutPoints = 0;
    if (counts[SIDE_FRONT] == 0) {
        return;
    }
    if (counts[SIDE_BACK] == 0) {
        numOutPoints = numInPoints;
        std::memcpy(outPoints, inPoints, static_cast<size_t>(numInPoints) * sizeof(float) * 3);
        return;
    }

    for (int i = 0; i < numInPoints; ++i) {
        const float *p1 = inPoints[i];
        if (sides[i] == SIDE_ON) {
            if (numOutPoints >= kMaxMarkClipVerts) {
                break;
            }
            outPoints[numOutPoints][0] = p1[0];
            outPoints[numOutPoints][1] = p1[1];
            outPoints[numOutPoints][2] = p1[2];
            ++numOutPoints;
            continue;
        }
        if (sides[i] == SIDE_FRONT) {
            if (numOutPoints >= kMaxMarkClipVerts) {
                break;
            }
            outPoints[numOutPoints][0] = p1[0];
            outPoints[numOutPoints][1] = p1[1];
            outPoints[numOutPoints][2] = p1[2];
            ++numOutPoints;
        }
        if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i]) {
            continue;
        }
        if (numOutPoints >= kMaxMarkClipVerts) {
            break;
        }
        const float *p2 = inPoints[(i + 1) % numInPoints];
        const float denom = dists[i] - dists[i + 1];
        const float frac = denom != 0.0f ? (dists[i] / denom) : 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            outPoints[numOutPoints][axis] = p1[axis] + frac * (p2[axis] - p1[axis]);
        }
        ++numOutPoints;
    }
}

int BuildMarkProjectionPlanes(int numPoints, const vec3_t *points, const vec3_t projection,
    const float direction[3], float projectionLength,
    float planeNormals[kMaxMarkClipVerts + 2][3], float planeDists[kMaxMarkClipVerts + 2])
{
    float center[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < numPoints; ++i) {
        center[0] += points[i][0];
        center[1] += points[i][1];
        center[2] += points[i][2];
    }
    const float invPoints = 1.0f / static_cast<float>(numPoints);
    center[0] *= invPoints;
    center[1] *= invPoints;
    center[2] *= invPoints;

    int numPlanes = 0;
    for (int i = 0; i < numPoints; ++i) {
        const float *p0 = points[i];
        const float *p1 = points[(i + 1) % numPoints];
        const float v1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        const float v2[3] = {-projection[0], -projection[1], -projection[2]};
        float n[3] = {
            v1[1] * v2[2] - v1[2] * v2[1],
            v1[2] * v2[0] - v1[0] * v2[2],
            v1[0] * v2[1] - v1[1] * v2[0],
        };
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len < 1e-6f) {
            continue;
        }
        n[0] /= len;
        n[1] /= len;
        n[2] /= len;
        float d = n[0] * p0[0] + n[1] * p0[1] + n[2] * p0[2];
        if (n[0] * center[0] + n[1] * center[1] + n[2] * center[2] - d < 0.0f) {
            n[0] = -n[0];
            n[1] = -n[1];
            n[2] = -n[2];
            d = -d;
        }
        std::memcpy(planeNormals[numPlanes], n, sizeof(n));
        planeDists[numPlanes] = d;
        ++numPlanes;
    }

    std::memcpy(planeNormals[numPlanes], direction, sizeof(float) * 3);
    planeDists[numPlanes] = direction[0] * points[0][0] + direction[1] * points[0][1] +
        direction[2] * points[0][2] - 32.0f;
    ++numPlanes;

    const float farDist = std::max(20.0f, projectionLength);
    planeNormals[numPlanes][0] = -direction[0];
    planeNormals[numPlanes][1] = -direction[1];
    planeNormals[numPlanes][2] = -direction[2];
    planeDists[numPlanes] = -(direction[0] * points[0][0] + direction[1] * points[0][1] +
        direction[2] * points[0][2]) - farDist;
    return numPlanes + 1;
}

void CollectMarkCandidates(const float mins[3], const float maxs[3], std::vector<uint32_t> &candidates)
{
    candidates.clear();
    const int minCell[3] = {
        static_cast<int>(std::floor(mins[0] / kMarkCellSize)),
        static_cast<int>(std::floor(mins[1] / kMarkCellSize)),
        static_cast<int>(std::floor(mins[2] / kMarkCellSize)),
    };
    const int maxCell[3] = {
        static_cast<int>(std::floor(maxs[0] / kMarkCellSize)),
        static_cast<int>(std::floor(maxs[1] / kMarkCellSize)),
        static_cast<int>(std::floor(maxs[2] / kMarkCellSize)),
    };
    for (int x = minCell[0]; x <= maxCell[0]; ++x) {
        for (int y = minCell[1]; y <= maxCell[1]; ++y) {
            for (int z = minCell[2]; z <= maxCell[2]; ++z) {
                const auto cell = markGrid.find(MarkCellKey(x, y, z));
                if (cell != markGrid.end()) {
                    candidates.insert(candidates.end(), cell->second.begin(), cell->second.end());
                }
            }
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
}

bool TriangleOverlapsBounds(const MarkTriangle &triangle, const float mins[3], const float maxs[3])
{
    for (int axis = 0; axis < 3; ++axis) {
        const float lo = std::min({triangle.points[0][axis], triangle.points[1][axis],
            triangle.points[2][axis]});
        const float hi = std::max({triangle.points[0][axis], triangle.points[1][axis],
            triangle.points[2][axis]});
        if (hi < mins[axis] - 0.5f || lo > maxs[axis] + 0.5f) {
            return false;
        }
    }
    return true;
}

int ClipMarkTriangle(const MarkTriangle &triangle, const float direction[3], int numPlanes,
    const float planeNormals[kMaxMarkClipVerts + 2][3], const float planeDists[kMaxMarkClipVerts + 2],
    float outPoly[kMaxMarkClipVerts][3])
{
    const float facingThreshold = triangle.isCurved ? -0.1f : -0.5f;
    const float facing = triangle.normal[0] * direction[0] +
        triangle.normal[1] * direction[1] + triangle.normal[2] * direction[2];
    if (facing > facingThreshold) {
        return 0;
    }

    float clipPoints[2][kMaxMarkClipVerts][3];
    int numClipPoints = 3;
    int pingPong = 0;
    std::memcpy(clipPoints[0], triangle.points, sizeof(float) * 9);
    for (int planeIdx = 0; planeIdx < numPlanes; ++planeIdx) {
        int nextNumPoints = 0;
        ChopPolyBehindPlane(numClipPoints, clipPoints[pingPong], nextNumPoints,
            clipPoints[pingPong ^ 1], planeNormals[planeIdx], planeDists[planeIdx], 0.1f);
        numClipPoints = nextNumPoints;
        pingPong ^= 1;
        if (numClipPoints < 3) {
            return 0;
        }
    }
    std::memcpy(outPoly, clipPoints[pingPong], static_cast<size_t>(numClipPoints) * sizeof(float) * 3);
    return numClipPoints;
}

struct MarkClipRequest
{
    const float *direction = nullptr;
    const float *mins = nullptr;
    const float *maxs = nullptr;
    int numPlanes = 0;
    const float (*planeNormals)[3] = nullptr;
    const float *planeDists = nullptr;
    int maxPoints = 0;
    vec3_t *pointBuffer = nullptr;
    int maxFragments = 0;
    markFragment_t *fragmentBuffer = nullptr;
};

int EmitClippedMarkFragments(const MarkClipRequest &req, const std::vector<uint32_t> &candidates)
{
    int returnedPoints = 0;
    int returnedFragments = 0;
    float clipped[kMaxMarkClipVerts][3];
    for (uint32_t triangleIndex : candidates) {
        const MarkTriangle &triangle = markTriangles[triangleIndex];
        if (triangle.noMarks || !TriangleOverlapsBounds(triangle, req.mins, req.maxs)) {
            continue;
        }
        const int numClipPoints = ClipMarkTriangle(triangle, req.direction, req.numPlanes,
            req.planeNormals, req.planeDists, clipped);
        if (numClipPoints < 3) {
            continue;
        }
        if (returnedPoints + numClipPoints > req.maxPoints) {
            break;
        }
        req.fragmentBuffer[returnedFragments] = {returnedPoints, numClipPoints};
        for (int i = 0; i < numClipPoints; ++i) {
            for (int axis = 0; axis < 3; ++axis) {
                req.pointBuffer[returnedPoints + i][axis] = clipped[i][axis];
            }
        }
        returnedPoints += numClipPoints;
        if (++returnedFragments >= req.maxFragments) {
            break;
        }
    }
    return returnedFragments;
}

// Clips front-facing world triangles against the projected prism (matching R_MarkFragments in
// tr_marks.c) so impact marks wrap cleanly across wall seams and steps without corner dropouts.
int MarkFragments(int numPoints, const vec3_t *points, const vec3_t projection, int maxPoints,
    vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer)
{
    if (numPoints < 3 || points == nullptr || maxPoints < 3 || maxFragments <= 0 ||
        pointBuffer == nullptr || fragmentBuffer == nullptr || markTriangles.empty()) {
        return 0;
    }
    numPoints = std::min(numPoints, kMaxMarkClipVerts);

    float direction[3] = {projection[0], projection[1], projection[2]};
    const float projectionLength = std::sqrt(direction[0] * direction[0] +
        direction[1] * direction[1] + direction[2] * direction[2]);
    if (projectionLength < 1e-5f) {
        return 0;
    }
    for (float &c : direction) {
        c /= projectionLength;
    }

    float mins[3] = {points[0][0], points[0][1], points[0][2]};
    float maxs[3] = {points[0][0], points[0][1], points[0][2]};
    for (int i = 0; i < numPoints; ++i) {
        for (int axis = 0; axis < 3; ++axis) {
            const float p = points[i][axis];
            mins[axis] = std::min({mins[axis], p, p + projection[axis], p - direction[axis] * 32.0f});
            maxs[axis] = std::max({maxs[axis], p, p + projection[axis], p - direction[axis] * 32.0f});
        }
    }

    float planeNormals[kMaxMarkClipVerts + 2][3] = {};
    float planeDists[kMaxMarkClipVerts + 2] = {};
    const int numPlanes = BuildMarkProjectionPlanes(numPoints, points, projection, direction,
        projectionLength, planeNormals, planeDists);

    std::vector<uint32_t> candidates;
    CollectMarkCandidates(mins, maxs, candidates);
    if (candidates.empty()) {
        return 0;
    }

    const MarkClipRequest req{direction, mins, maxs, numPlanes, planeNormals, planeDists,
        maxPoints, reinterpret_cast<vec3_t *>(pointBuffer), maxFragments, fragmentBuffer};
    return EmitClippedMarkFragments(req, candidates);
}

const md3Tag_t *FindModelTag(const ModelAsset &asset, int frame, const char *tagName)
{
    if (!asset.valid || asset.numTags <= 0 || tagName == nullptr) {
        return nullptr;
    }
    frame = std::clamp(frame, 0, asset.numFrames - 1);
    const md3Tag_t *tags = asset.tags.data() + static_cast<size_t>(frame) * static_cast<size_t>(asset.numTags);
    for (int tagIndex = 0; tagIndex < asset.numTags; ++tagIndex) {
        if (_stricmp(tags[tagIndex].name, tagName) == 0) {
            return &tags[tagIndex];
        }
    }
    return nullptr;
}

void NormalizeVector(float vector[3])
{
    const float length = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
    if (length > 1e-6f) {
        vector[0] /= length;
        vector[1] /= length;
        vector[2] /= length;
    }
}

int LerpTag(orientation_t *tag, qhandle_t model, int startFrame, int endFrame, float frac, const char *tagName)
{
    if (tag == nullptr) {
        return 0;
    }

    const auto modelIt = modelAssets.find(model);
    if (modelIt == modelAssets.end()) {
        std::memset(tag, 0, sizeof(*tag));
        return 0;
    }

    const ModelAsset &asset = modelIt->second;
    const md3Tag_t *start = FindModelTag(asset, startFrame, tagName);
    const md3Tag_t *end = FindModelTag(asset, endFrame, tagName);
    if (start == nullptr || end == nullptr) {
        std::memset(tag, 0, sizeof(*tag));
        return 0;
    }

    const float frontLerp = std::clamp(frac, 0.0f, 1.0f);
    const float backLerp = 1.0f - frontLerp;
    for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
        tag->origin[axisIndex] = start->origin[axisIndex] * backLerp + end->origin[axisIndex] * frontLerp;
        tag->axis[0][axisIndex] = start->axis[0][axisIndex] * backLerp + end->axis[0][axisIndex] * frontLerp;
        tag->axis[1][axisIndex] = start->axis[1][axisIndex] * backLerp + end->axis[1][axisIndex] * frontLerp;
        tag->axis[2][axisIndex] = start->axis[2][axisIndex] * backLerp + end->axis[2][axisIndex] * frontLerp;
    }
    NormalizeVector(tag->axis[0]);
    NormalizeVector(tag->axis[1]);
    NormalizeVector(tag->axis[2]);
    return 1;
}

void ModelBounds(qhandle_t model, vec3_t mins, vec3_t maxs)
{
    if (mins == nullptr || maxs == nullptr) {
        return;
    }
    const auto modelIt = modelAssets.find(model);
    if (modelIt == modelAssets.end() || !modelIt->second.valid) {
        mins[0] = mins[1] = mins[2] = 0.0f;
        maxs[0] = maxs[1] = maxs[2] = 0.0f;
        return;
    }
    for (int axis = 0; axis < 3; ++axis) {
        mins[axis] = modelIt->second.bounds[0][axis];
        maxs[axis] = modelIt->second.bounds[1][axis];
    }
}

void RegisterFont(const char *fontName, int pointSize, fontInfo_t *font)
{
    (void)pointSize;
    if (font == nullptr) {
        return;
    }

    const qhandle_t glyphShader = RegisterUiShader("gfx/2d/bigchars");
    std::memset(font, 0, sizeof(*font));
    font->glyphScale = 1.0f;
    CopyString(font->name, fontName != nullptr ? fontName : "bigchars", sizeof(font->name));

    for (int glyphIndex = 0; glyphIndex < GLYPHS_PER_FONT; ++glyphIndex) {
        glyphInfo_t &glyph = font->glyphs[glyphIndex];
        const int column = glyphIndex % 16;
        const int row = glyphIndex / 16;
        glyph.height = 32.0f;
        glyph.top = 0.0f;
        glyph.bottom = 32.0f;
        glyph.pitch = 32.0f;
        glyph.imageWidth = 512;
        glyph.imageHeight = 512;
        glyph.s = static_cast<float>(column) / 16.0f;
        glyph.t = static_cast<float>(row) / 16.0f;
        glyph.s2 = static_cast<float>(column + 1) / 16.0f;
        glyph.t2 = static_cast<float>(row + 1) / 16.0f;
        glyph.glyph = glyphShader;
        CopyString(glyph.shaderName, "gfx/2d/bigchars", sizeof(glyph.shaderName));
    }
}

void RemapShader(const char *oldShader, const char *newShader, const char *offsetTime)
{
    (void)oldShader;
    (void)newShader;
    (void)offsetTime;
}

qboolean GetEntityToken(char *buffer, int size)
{
    (void)buffer;
    (void)size;
    return qfalse;
}

qboolean InPVS(const vec3_t p1, const vec3_t p2)
{
    (void)p1;
    (void)p2;
    return qfalse;
}

void TakeVideoFrame(int h, int w, byte *capture, byte *encode, qboolean motionJpeg)
{
    captureWidth = w;
    captureHeight = h;
    captureBuffer = capture;
    captureEncodeBuffer = encode;
    captureMotionJpeg = motionJpeg != qfalse;
    capturePending = true;
    readbackWantedFrames = kReadbackKeepAliveFrames;
    CopyReadbackToCapture();
    captureBuffer = nullptr;
    captureEncodeBuffer = nullptr;
}

void SetWorldVisData(const byte *vis)
{
    (void)vis;
}

bool LoadMd3Asset(const char *name, ModelAsset &asset)
{
    if (name == nullptr || name[0] == '\0') {
        return false;
    }

    void *fileData = nullptr;
    long fileLength = ri.FS_ReadFile(name, &fileData);
    if ((fileLength <= 0 || fileData == nullptr) && std::strstr(name, ".md3") == nullptr) {
        char withExtension[MAX_QPATH] = {};
        std::snprintf(withExtension, sizeof(withExtension), "%s.md3", name);
        fileLength = ri.FS_ReadFile(withExtension, &fileData);
    }
    if (fileLength <= 0 || fileData == nullptr) {
        return false;
    }

    bool loaded = false;
    const auto *base = static_cast<const uint8_t *>(fileData);
    if (fileLength >= static_cast<long>(sizeof(md3Header_t))) {
        const auto *header = reinterpret_cast<const md3Header_t *>(base);
        if (header->ident == MD3_IDENT && header->version == MD3_VERSION &&
            header->numFrames > 0 && header->numFrames <= MD3_MAX_FRAMES &&
            header->numSurfaces >= 0 && header->numSurfaces <= MD3_MAX_SURFACES &&
            header->ofsFrames >= 0 && static_cast<uint64_t>(header->ofsFrames) <= static_cast<uint64_t>(fileLength) &&
            header->ofsTags >= 0 && static_cast<uint64_t>(header->ofsTags) <= static_cast<uint64_t>(fileLength) &&
            header->ofsSurfaces >= 0 && static_cast<uint64_t>(header->ofsSurfaces) <= static_cast<uint64_t>(fileLength) &&
            header->numTags >= 0 && header->numTags <= MD3_MAX_TAGS) {
            const uint64_t framesEnd = static_cast<uint64_t>(header->ofsFrames) +
                static_cast<uint64_t>(header->numFrames) * sizeof(md3Frame_t);
            const uint64_t tagsEnd = static_cast<uint64_t>(header->ofsTags) +
                static_cast<uint64_t>(header->numFrames) * static_cast<uint64_t>(header->numTags) * sizeof(md3Tag_t);
            if (framesEnd <= static_cast<uint64_t>(fileLength) && tagsEnd <= static_cast<uint64_t>(fileLength)) {
                asset = {};
                asset.valid = true;
                asset.numFrames = header->numFrames;
                asset.numTags = header->numTags;
                const auto *frames = reinterpret_cast<const md3Frame_t *>(base + header->ofsFrames);
                std::memcpy(asset.bounds, frames[0].bounds, sizeof(asset.bounds));
                asset.radius = frames[0].radius;
                asset.tags.resize(static_cast<size_t>(asset.numFrames) * static_cast<size_t>(asset.numTags));
                if (!asset.tags.empty()) {
                    std::memcpy(asset.tags.data(), base + header->ofsTags,
                        asset.tags.size() * sizeof(md3Tag_t));
                }

                const uint8_t *surfaceData = base + header->ofsSurfaces;
                const uint8_t *fileEnd = base + fileLength;
                for (int surfaceIndex = 0; surfaceIndex < header->numSurfaces; ++surfaceIndex) {
                    if (surfaceData + sizeof(md3Surface_t) > fileEnd) {
                        asset.valid = false;
                        break;
                    }
                    const auto *surface = reinterpret_cast<const md3Surface_t *>(surfaceData);
                    if (surface->ident != MD3_IDENT || surface->numVerts <= 0 ||
                        surface->numVerts > MD3_MAX_VERTS || surface->numTriangles < 0 ||
                        surface->numTriangles > MD3_MAX_TRIANGLES ||
                        surface->numFrames <= 0 || surface->numFrames > MD3_MAX_FRAMES) {
                        asset.valid = false;
                        break;
                    }
                    if (surface->ofsEnd < 0 ||
                        surface->ofsShaders < 0 || surface->ofsTriangles < 0 ||
                        surface->ofsSt < 0 || surface->ofsXyzNormals < 0) {
                        asset.valid = false;
                        break;
                    }
                    const uint64_t surfaceEnd = static_cast<uint64_t>(surface->ofsEnd);
                    if (surfaceEnd > static_cast<uint64_t>(fileEnd - surfaceData) ||
                        surface->ofsTriangles > surface->ofsEnd ||
                        surface->ofsShaders > surface->ofsEnd ||
                        surface->ofsSt > surface->ofsEnd ||
                        surface->ofsXyzNormals > surface->ofsEnd ||
                        surface->numShaders < 0 || surface->numShaders > MD3_MAX_SHADERS) {
                        asset.valid = false;
                        break;
                    }

                    ModelSurface modelSurface;
                    modelSurface.numVerts = surface->numVerts;
                    modelSurface.surfaceName.assign(surface->name, strnlen(surface->name, MAX_QPATH));
                    std::transform(modelSurface.surfaceName.begin(), modelSurface.surfaceName.end(),
                        modelSurface.surfaceName.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    modelSurface.shaderName.assign(surface->name, strnlen(surface->name, MAX_QPATH));
                    const uint64_t shadersBytes = static_cast<uint64_t>(surface->numShaders) * sizeof(md3Shader_t);
                    if (surface->numShaders > 0 &&
                        static_cast<uint64_t>(surface->ofsShaders) + shadersBytes <= surfaceEnd) {
                        const auto *shader = reinterpret_cast<const md3Shader_t *>(surfaceData + surface->ofsShaders);
                        if (shader->name[0] != '\0') {
                            modelSurface.shaderName.assign(shader->name, strnlen(shader->name, MAX_QPATH));
                        }
                    }

                    const uint64_t triangleCount = static_cast<uint64_t>(surface->numTriangles);
                    const uint64_t triangleBytes = triangleCount * sizeof(md3Triangle_t);
                    if (static_cast<uint64_t>(surface->ofsTriangles) + triangleBytes > surfaceEnd ||
                        static_cast<uint64_t>(surface->ofsSt) + static_cast<uint64_t>(surface->numVerts) * sizeof(md3St_t) > surfaceEnd) {
                        asset.valid = false;
                        break;
                    }

                    const auto *triangles = reinterpret_cast<const md3Triangle_t *>(surfaceData + surface->ofsTriangles);
                    modelSurface.indices.reserve(static_cast<size_t>(triangleCount) * 3);
                    bool indicesValid = true;
                    for (uint64_t triangleIndex = 0; triangleIndex < triangleCount && indicesValid; ++triangleIndex) {
                        const md3Triangle_t &triangle = triangles[triangleIndex];
                        for (int corner = 0; corner < 3; ++corner) {
                            // The indexes go straight to qrUploadGeometry; an out-of-range
                            // value would make the SDK read past the vertex array.
                            const int vertex = triangle.indexes[corner];
                            if (vertex < 0 || vertex >= surface->numVerts) {
                                indicesValid = false;
                                break;
                            }
                            modelSurface.indices.push_back(static_cast<uint32_t>(vertex));
                        }
                    }
                    if (!indicesValid) {
                        HostLog("LoadMd3Asset: %s has an out-of-range triangle index", name);
                        asset.valid = false;
                        break;
                    }

                    const auto *texCoords = reinterpret_cast<const md3St_t *>(surfaceData + surface->ofsSt);
                    modelSurface.texCoords.resize(static_cast<size_t>(surface->numVerts) * 2);
                    for (int vertexIndex = 0; vertexIndex < surface->numVerts; ++vertexIndex) {
                        modelSurface.texCoords[static_cast<size_t>(vertexIndex) * 2 + 0] = texCoords[vertexIndex].st[0];
                        modelSurface.texCoords[static_cast<size_t>(vertexIndex) * 2 + 1] = texCoords[vertexIndex].st[1];
                    }

                    const uint64_t xyzBytes = static_cast<uint64_t>(surface->numVerts) *
                        static_cast<uint64_t>(surface->numFrames) * sizeof(short) * 4;
                    if (static_cast<uint64_t>(surface->ofsXyzNormals) + xyzBytes > surfaceEnd) {
                        asset.valid = false;
                        break;
                    }

                    const auto *xyzNormals = reinterpret_cast<const short *>(surfaceData + surface->ofsXyzNormals);
                    modelSurface.positions.resize(static_cast<size_t>(surface->numFrames) *
                        static_cast<size_t>(surface->numVerts) * 3);
                    modelSurface.normals.resize(modelSurface.positions.size());
                    for (int frameIndex = 0; frameIndex < surface->numFrames; ++frameIndex) {
                        for (int vertexIndex = 0; vertexIndex < surface->numVerts; ++vertexIndex) {
                            const short *source = xyzNormals +
                                (static_cast<size_t>(frameIndex) * surface->numVerts + vertexIndex) * 4;
                            const size_t destination = (static_cast<size_t>(frameIndex) * surface->numVerts + vertexIndex) * 3;
                            modelSurface.positions[destination + 0] = static_cast<float>(source[0]) * MD3_XYZ_SCALE;
                            modelSurface.positions[destination + 1] = static_cast<float>(source[1]) * MD3_XYZ_SCALE;
                            modelSurface.positions[destination + 2] = static_cast<float>(source[2]) * MD3_XYZ_SCALE;

                            const uint32_t lat = (static_cast<uint32_t>(source[3]) >> 8) & 0xffu;
                            const uint32_t lng = static_cast<uint32_t>(source[3]) & 0xffu;
                            const float latRadians = static_cast<float>(lat) * (2.0f * kPi / 256.0f);
                            const float lngRadians = static_cast<float>(lng) * (2.0f * kPi / 256.0f);
                            const float sinLat = std::sin(latRadians);
                            const float cosLat = std::cos(latRadians);
                            const float sinLng = std::sin(lngRadians);
                            modelSurface.normals[destination + 0] = cosLat * sinLng;
                            modelSurface.normals[destination + 1] = sinLat * sinLng;
                            modelSurface.normals[destination + 2] = std::cos(lngRadians);
                        }
                    }

                    asset.surfaces.push_back(std::move(modelSurface));
                    surfaceData += surface->ofsEnd;
                }

                loaded = asset.valid;
            }
        }
    }

    ri.FS_FreeFile(fileData);
    if (!loaded) {
        asset = {};
    }
    return loaded;
}

SkinAsset LoadSkinAsset(const char *name)
{
    SkinAsset skin;
    if (name == nullptr || name[0] == '\0') {
        return skin;
    }

    void *fileData = nullptr;
    const long fileLength = ri.FS_ReadFile(name, &fileData);
    if (fileLength <= 0 || fileData == nullptr) {
        return skin;
    }

    const char *cursor = static_cast<const char *>(fileData);
    const char *end = cursor + fileLength;
    while (cursor < end) {
        const char *lineEnd = cursor;
        while (lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r') {
            ++lineEnd;
        }
        std::string line(cursor, lineEnd - cursor);
        cursor = lineEnd;
        while (cursor < end && (*cursor == '\n' || *cursor == '\r')) {
            ++cursor;
        }

        const size_t comment = line.find("//");
        if (comment != std::string::npos) {
            line.resize(comment);
        }
        const size_t comma = line.find(',');
        if (comma == std::string::npos) {
            continue;
        }
        size_t firstStart = 0;
        size_t firstEnd = comma;
        while (firstStart < firstEnd && std::isspace(static_cast<unsigned char>(line[firstStart]))) {
            ++firstStart;
        }
        while (firstEnd > firstStart && std::isspace(static_cast<unsigned char>(line[firstEnd - 1]))) {
            --firstEnd;
        }
        size_t secondStart = comma + 1;
        size_t secondEnd = line.size();
        while (secondStart < secondEnd && std::isspace(static_cast<unsigned char>(line[secondStart]))) {
            ++secondStart;
        }
        while (secondEnd > secondStart && std::isspace(static_cast<unsigned char>(line[secondEnd - 1]))) {
            --secondEnd;
        }
        if (firstStart < firstEnd && secondStart < secondEnd) {
            std::string surfaceKey = line.substr(firstStart, firstEnd - firstStart);
            std::transform(surfaceKey.begin(), surfaceKey.end(), surfaceKey.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            skin.shaderBySurface.emplace(std::move(surfaceKey),
                line.substr(secondStart, secondEnd - secondStart));
        }
    }

    ri.FS_FreeFile(fileData);
    return skin;
}

qhandle_t RegisterModel(const char *name)
{
    if (name == nullptr || name[0] == '\0') {
        return 0;
    }

    const std::string key(name);
    const auto existing = modelHandles.find(key);
    if (existing != modelHandles.end()) {
        return existing->second;
    }

    const qhandle_t handle = nextModelHandle++;
    ModelAsset asset;
    if (name[0] == '*') {
        asset.isBspSubmodel = true;
    } else if (!LoadMd3Asset(name, asset)) {
        HostLog("RegisterModel: failed to load %s", name);
        ri.Printf(PRINT_DEVELOPER, "rendererqray: failed to load model %s\n", name);
    }
    asset.name = NormalizeShaderName(name);
    modelAssets.emplace(handle, std::move(asset));
    modelHandles.emplace(key, handle);
    return handle;
}

qhandle_t RegisterSkin(const char *name)
{
    if (name == nullptr || name[0] == '\0') {
        return 0;
    }

    const std::string key(name);
    const auto existing = skinHandles.find(key);
    if (existing != skinHandles.end()) {
        return existing->second;
    }

    const qhandle_t handle = nextSkinHandle++;
    skinAssets.emplace(handle, LoadSkinAsset(name));
    skinHandles.emplace(key, handle);
    return handle;
}

qhandle_t RegisterShader(const char *name)
{
    return RegisterUiShader(name);
}

qhandle_t RegisterShaderNoMip(const char *name)
{
    return RegisterUiShader(name);
}

}

extern "C" Q_EXPORT refexport_t *QDECL GetRefAPI(int apiVersion, refimport_t *rimp)
{
    ri = *rimp;
    HostLog("GetRefAPI: apiVersion=%d expected=%d rimp=%p", apiVersion, REF_API_VERSION, rimp);

    std::memset(&re, 0, sizeof(re));

    if (apiVersion != REF_API_VERSION) {
        ri.Printf(PRINT_ALL, "Mismatched REF_API_VERSION: expected %i, got %i\n", REF_API_VERSION, apiVersion);
        return nullptr;
    }

    std::memset(&glConfig, 0, sizeof(glConfig));

    re.Shutdown = ShutdownRenderer;
    re.BeginRegistration = BeginRegistration;
    re.RegisterModel = RegisterModel;
    re.RegisterSkin = RegisterSkin;
    re.RegisterShader = RegisterShader;
    re.RegisterShaderNoMip = RegisterShaderNoMip;
    re.LoadWorld = LoadWorld;
    re.SetWorldVisData = SetWorldVisData;
    re.EndRegistration = EndRegistration;
    re.ClearScene = ClearScene;
    re.AddRefEntityToScene = AddRefEntityToScene;
    re.AddPolyToScene = AddPolyToScene;
    re.LightForPoint = LightForPoint;
    re.AddLightToScene = AddLightToScene;
    re.AddAdditiveLightToScene = AddAdditiveLightToScene;
    re.RenderScene = RenderScene;
    re.SetColor = SetColor;
    re.DrawStretchPic = DrawStretchPic;
    re.DrawStretchRaw = DrawStretchRaw;
    re.UploadCinematic = UploadCinematic;
    re.BeginFrame = BeginFrame;
    re.EndFrame = EndFrame;
    re.MarkFragments = MarkFragments;
    re.LerpTag = LerpTag;
    re.ModelBounds = ModelBounds;
    re.RegisterFont = RegisterFont;
    re.RemapShader = RemapShader;
    re.GetEntityToken = GetEntityToken;
    re.inPVS = InPVS;
    re.TakeVideoFrame = TakeVideoFrame;

    return &re;
}
