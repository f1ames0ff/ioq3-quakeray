#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <qray/qray.h>

#include "Scene.h"

class EditorAssetSource;
struct EditorTexture;

using Q3ShaderWaveType = EditorWaveType;
using Q3TcModType = EditorTcModType;
using Q3TcMod = EditorTcMod;
using Q3ColorGen = EditorColorGen;
using Q3AlphaGen = EditorAlphaGen;
using Q3ShaderStage = EditorOverlayStage;

struct Q3ShaderDef
{
    std::string name;
    std::string map;
    std::string editorImage;
    std::string lightImage;
    std::string emissionMaskMap;
    size_t baseStageIndex = (std::numeric_limits<size_t>::max)();
    bool tracedBase = false;
    bool usesOverlay = false;
    bool cutoutFrameUnderlay = false;
    bool compositeUnderlayBase = false;
    bool metallicEnvBase = false;
    bool metallicEnvInvertedAlpha = false;
    bool wetGlossAlphaMask = false;
    std::string underlayMap;
    float underlayScale[2] = { 1.0f, 1.0f };
    bool underlayEmissive = false;
    std::vector<std::string> decalOverlayMaps;
    std::vector<bool> decalOverlayInvertedAlpha;
    bool alphaTested = false;
    bool twoSided = false;
    bool autoSprite = false;
    bool autoSprite2 = false;
    Q3ShaderWaveType deformWave = Q3ShaderWaveType::None;
    float deformSpread = 0.0f;
    float deformBase = 0.0f;
    float deformAmplitude = 0.0f;
    float deformPhase = 0.0f;
    float deformFrequency = 0.0f;
    bool isSky = false;
    bool isNoDraw = false;
    bool isFog = false;
    bool isPortal = false;
    bool isTrans = false;
    bool isWater = false;
    bool isSlime = false;
    bool isLava = false;
    bool isNonTracedOverlay = false;
    bool hasEnvironment = false;
    bool darkenDecal = false;
    float baseScale[2] = { 1.0f, 1.0f };
    float surfaceLight = 0.0f;
    bool hasLightRGB = false;
    float lightRGB[3] = { 1.0f, 1.0f, 1.0f };
    std::string outerBox;
    float cloudHeight = 128.0f;
    bool hasSun = false;
    float sunColor[3] = { 1.0f, 0.95f, 0.85f };
    float sunIntensity = 0.0f;
    float sunDegrees = 0.0f;
    float sunElevation = 45.0f;
    std::vector<Q3ShaderStage> stages;
};

class Q3ShaderDatabase
{
public:
    void Clear();
    size_t ParseScript(const char *data, size_t size, bool overwrite = false,
                       const EditorAssetSource *assets = nullptr);
    void LoadFromAssets(const EditorAssetSource &assets);
    const Q3ShaderDef *Find(std::string_view name) const;
    size_t Count() const { return m_defs.size(); }

private:
    std::unordered_map<std::string, Q3ShaderDef> m_defs;
    std::unordered_map<std::string, bool> m_binaryCutoutCache;
};

inline float EvaluateQ3ShaderWaveSigned(Q3ShaderWaveType type, float base, float amplitude,
                                        float phase, float frequency, float timeSeconds)
{
    return EvaluateEditorWaveSigned(type, base, amplitude, phase, frequency, timeSeconds);
}

inline float EvaluateQ3ShaderWave(Q3ShaderWaveType type, float base, float amplitude, float phase,
                                  float frequency, float timeSeconds)
{
    return EvaluateEditorWave(type, base, amplitude, phase, frequency, timeSeconds);
}

inline std::string NormalizeQ3AssetPath(std::string_view raw)
{
    return NormalizeEditorAssetPath(raw);
}

inline std::string StripQ3ImageExtension(std::string_view path)
{
    return StripEditorImageExtension(path);
}

std::unordered_map<std::string, std::string> ParseQ3SkinBytes(const std::vector<uint8_t> &bytes);
bool LoadQ3ImageRgba(const EditorAssetSource *assets, const std::string &requestedPath,
                     std::vector<uint8_t> &outRgba, uint32_t &outWidth, uint32_t &outHeight,
                     std::string &outResolvedPath);
void ApplyQ3ShaderToEditorTexture(const Q3ShaderDef &def, const EditorAssetSource *assets,
                                  bool isSky, EditorTexture &tex);

inline void OffsetQ3VerticesAlongSmoothNormals(std::vector<QrVertex> &vertices,
                                               const std::vector<uint32_t> &indices, float distance)
{
    OffsetEditorVerticesAlongSmoothNormals(vertices, indices, distance);
}
