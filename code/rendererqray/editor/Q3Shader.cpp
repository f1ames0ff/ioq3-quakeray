#include "Q3Shader.h"

#include "Assets.h"
#include "Scene.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace
{

bool EqualsAsciiIgnoreCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
    {
        return false;
    }

    for (size_t i = 0; i < a.size(); i++)
    {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
        {
            return false;
        }
    }

    return true;
}

bool StartsWithAsciiIgnoreCase(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() &&
           EqualsAsciiIgnoreCase(text.substr(0, prefix.size()), prefix);
}

void SkipShaderComment(const char *data, size_t size, size_t &i)
{
    if (i + 1 < size && data[i] == '/' && data[i + 1] == '/')
    {
        i += 2;
        while (i < size && data[i] != '\n')
        {
            i++;
        }
        return;
    }

    if (i + 1 < size && data[i] == '/' && data[i + 1] == '*')
    {
        i += 2;
        while (i + 1 < size && !(data[i] == '*' && data[i + 1] == '/'))
        {
            i++;
        }
        i = std::min(size, i + 2);
    }
}

bool IsCommentStart(const char *data, size_t size, size_t i)
{
    return i + 1 < size && data[i] == '/' && (data[i + 1] == '/' || data[i + 1] == '*');
}

std::string ReadQuotedToken(const char *data, size_t size, size_t &i)
{
    i++;
    const size_t start = i;
    while (i < size && data[i] != '"')
    {
        i++;
    }
    std::string tok(data + start, i - start);
    if (i < size)
    {
        i++;
    }
    return tok;
}

std::string ReadWordToken(const char *data, size_t size, size_t &i)
{
    const size_t start = i;
    while (i < size)
    {
        const auto ch = static_cast<unsigned char>(data[i]);
        if (std::isspace(ch) || data[i] == '{' || data[i] == '}' || IsCommentStart(data, size, i))
        {
            break;
        }
        i++;
    }
    return std::string(data + start, i - start);
}

std::vector<std::string> TokenizeShaderScript(const char *data, size_t size)
{
    std::vector<std::string> tokens;
    size_t i = 0;

    while (i < size)
    {
        if (std::isspace(static_cast<unsigned char>(data[i])))
        {
            i++;
        }
        else if (IsCommentStart(data, size, i))
        {
            SkipShaderComment(data, size, i);
        }
        else if (data[i] == '{' || data[i] == '}')
        {
            tokens.emplace_back(1, data[i]);
            i++;
        }
        else if (data[i] == '"')
        {
            tokens.push_back(ReadQuotedToken(data, size, i));
        }
        else
        {
            tokens.push_back(ReadWordToken(data, size, i));
        }
    }

    return tokens;
}

bool HasBraceBefore(const std::vector<std::string> &tokens, size_t index, size_t count)
{
    if (index + count >= tokens.size())
    {
        return false;
    }

    for (size_t offset = 1; offset <= count; offset++)
    {
        const std::string &t = tokens[index + offset];
        if (t == "{" || t == "}")
        {
            return false;
        }
    }

    return true;
}

float ParseFloatToken(const std::string &token, float fallback = 0.0f)
{
    char *end = nullptr;
    const float value = std::strtof(token.c_str(), &end);
    return (end != nullptr && end != token.c_str()) ? value : fallback;
}

void ParseHeaderSun(const std::vector<std::string> &tokens, size_t &index, Q3ShaderDef &def)
{
    if (!HasBraceBefore(tokens, index, 6))
    {
        return;
    }

    def.hasSun = true;
    def.sunColor[0] = std::max(0.0f, ParseFloatToken(tokens[index + 1], 1.0f));
    def.sunColor[1] = std::max(0.0f, ParseFloatToken(tokens[index + 2], 1.0f));
    def.sunColor[2] = std::max(0.0f, ParseFloatToken(tokens[index + 3], 1.0f));
    def.sunIntensity = std::max(0.0f, ParseFloatToken(tokens[index + 4], 0.0f));
    def.sunDegrees = ParseFloatToken(tokens[index + 5], 0.0f);
    def.sunElevation = ParseFloatToken(tokens[index + 6], 45.0f);
    index += 6;
}

void ParseHeaderSkyParms(const std::vector<std::string> &tokens, size_t &index, Q3ShaderDef &def)
{
    if (!HasBraceBefore(tokens, index, 2))
    {
        return;
    }

    def.isSky = true;
    const std::string &boxTok = tokens[index + 1];
    const bool isNumeric =
        !boxTok.empty() &&
        std::all_of(boxTok.begin(), boxTok.end(), [](unsigned char c) { return std::isdigit(c); });
    if (boxTok != "-" && !EqualsAsciiIgnoreCase(boxTok, "full") && !isNumeric)
    {
        def.outerBox = NormalizeQ3AssetPath(boxTok);
    }
    if (tokens[index + 2] != "-" && !EqualsAsciiIgnoreCase(tokens[index + 2], "full"))
    {
        def.cloudHeight = std::max(1.0f, ParseFloatToken(tokens[index + 2], 128.0f));
    }
    index += HasBraceBefore(tokens, index, 3) ? 3u : 2u;
}

void ParseHeaderSurfaceParm(std::string_view parm, Q3ShaderDef &def)
{
    if (EqualsAsciiIgnoreCase(parm, "sky"))
    {
        def.isSky = true;
    }
    else if (EqualsAsciiIgnoreCase(parm, "nodraw"))
    {
        def.isNoDraw = true;
    }
    else if (EqualsAsciiIgnoreCase(parm, "fog"))
    {
        def.isFog = true;
    }
    else if (EqualsAsciiIgnoreCase(parm, "trans"))
    {
        def.isTrans = true;
    }
    else if (EqualsAsciiIgnoreCase(parm, "water"))
    {
        def.isWater = true;
    }
    else if (EqualsAsciiIgnoreCase(parm, "slime"))
    {
        def.isSlime = true;
    }
    else if (EqualsAsciiIgnoreCase(parm, "lava"))
    {
        def.isLava = true;
    }
}

bool ParseHeaderLightToken(const std::vector<std::string> &tokens, size_t &index, Q3ShaderDef &def)
{
    const std::string &tok = tokens[index];
    if (EqualsAsciiIgnoreCase(tok, "q3map_surfacelight") && HasBraceBefore(tokens, index, 1))
    {
        def.surfaceLight = std::max(0.0f, ParseFloatToken(tokens[index + 1], 0.0f));
        index += 1;
        return true;
    }
    if (EqualsAsciiIgnoreCase(tok, "q3map_lightrgb") && HasBraceBefore(tokens, index, 3))
    {
        def.hasLightRGB = true;
        def.lightRGB[0] = std::max(0.0f, ParseFloatToken(tokens[index + 1], 1.0f));
        def.lightRGB[1] = std::max(0.0f, ParseFloatToken(tokens[index + 2], 1.0f));
        def.lightRGB[2] = std::max(0.0f, ParseFloatToken(tokens[index + 3], 1.0f));
        index += 3;
        return true;
    }
    if (EqualsAsciiIgnoreCase(tok, "qer_editorimage") && HasBraceBefore(tokens, index, 1))
    {
        def.editorImage = NormalizeQ3AssetPath(tokens[index + 1]);
        index += 1;
        return true;
    }
    if (EqualsAsciiIgnoreCase(tok, "q3map_lightimage") && HasBraceBefore(tokens, index, 1))
    {
        def.lightImage = NormalizeQ3AssetPath(tokens[index + 1]);
        index += 1;
        return true;
    }
    return false;
}

Q3ShaderWaveType ParseShaderWave(std::string_view token)
{
    if (EqualsAsciiIgnoreCase(token, "sin"))
    {
        return Q3ShaderWaveType::Sin;
    }
    if (EqualsAsciiIgnoreCase(token, "square"))
    {
        return Q3ShaderWaveType::Square;
    }
    if (EqualsAsciiIgnoreCase(token, "triangle"))
    {
        return Q3ShaderWaveType::Triangle;
    }
    if (EqualsAsciiIgnoreCase(token, "sawtooth"))
    {
        return Q3ShaderWaveType::Sawtooth;
    }
    if (EqualsAsciiIgnoreCase(token, "inversesawtooth"))
    {
        return Q3ShaderWaveType::InverseSawtooth;
    }
    if (EqualsAsciiIgnoreCase(token, "noise"))
    {
        return Q3ShaderWaveType::Noise;
    }
    return Q3ShaderWaveType::None;
}

void ParseHeaderDeform(const std::vector<std::string> &tokens, size_t &index, Q3ShaderDef &def)
{
    if (!HasBraceBefore(tokens, index, 1))
    {
        return;
    }
    const std::string &mode = tokens[index + 1];
    if (EqualsAsciiIgnoreCase(mode, "wave") && HasBraceBefore(tokens, index, 7))
    {
        const float div = ParseFloatToken(tokens[index + 2], 0.0f);
        const Q3ShaderWaveType wave = ParseShaderWave(tokens[index + 3]);
        if (wave != Q3ShaderWaveType::None)
        {
            def.deformWave = wave;
            def.deformSpread = div != 0.0f ? (1.0f / div) : 100.0f;
            def.deformBase = ParseFloatToken(tokens[index + 4], 0.0f);
            def.deformAmplitude = ParseFloatToken(tokens[index + 5], 0.0f);
            def.deformPhase = ParseFloatToken(tokens[index + 6], 0.0f);
            def.deformFrequency = ParseFloatToken(tokens[index + 7], 0.0f);
        }
        index += 7;
    }
    else if (EqualsAsciiIgnoreCase(mode, "autosprite"))
    {
        def.autoSprite = true;
        index += 1;
    }
    else if (EqualsAsciiIgnoreCase(mode, "autosprite2"))
    {
        def.autoSprite2 = true;
        index += 1;
    }
    else if (EqualsAsciiIgnoreCase(mode, "move") && HasBraceBefore(tokens, index, 9))
    {
        index += 9;
    }
    else if (EqualsAsciiIgnoreCase(mode, "bulge") && HasBraceBefore(tokens, index, 4))
    {
        index += 4;
    }
    else if (EqualsAsciiIgnoreCase(mode, "normal") && HasBraceBefore(tokens, index, 3))
    {
        index += 3;
    }
    else
    {
        index += 1;
    }
}

void ParseHeaderToken(const std::vector<std::string> &tokens, size_t &index, Q3ShaderDef &def)
{
    if (ParseHeaderLightToken(tokens, index, def))
    {
        return;
    }

    const std::string &tok = tokens[index];
    if (StartsWithAsciiIgnoreCase(tok, "q3map_sun"))
    {
        ParseHeaderSun(tokens, index, def);
    }
    else if (EqualsAsciiIgnoreCase(tok, "skyparms"))
    {
        ParseHeaderSkyParms(tokens, index, def);
    }
    else if (EqualsAsciiIgnoreCase(tok, "deformvertexes"))
    {
        ParseHeaderDeform(tokens, index, def);
    }
    else if (EqualsAsciiIgnoreCase(tok, "cull") && HasBraceBefore(tokens, index, 1))
    {
        const std::string &mode = tokens[index + 1];
        def.twoSided = EqualsAsciiIgnoreCase(mode, "none") ||
                       EqualsAsciiIgnoreCase(mode, "disable") ||
                       EqualsAsciiIgnoreCase(mode, "twosided");
        index += 1;
    }
    else if (EqualsAsciiIgnoreCase(tok, "surfaceparm") && HasBraceBefore(tokens, index, 1))
    {
        ParseHeaderSurfaceParm(tokens[index + 1], def);
        index += 1;
    }
    else if (EqualsAsciiIgnoreCase(tok, "portal"))
    {
        def.isPortal = true;
    }
}

bool IsStageKeyword(std::string_view tok)
{
    return EqualsAsciiIgnoreCase(tok, "map") || EqualsAsciiIgnoreCase(tok, "clampmap") ||
           EqualsAsciiIgnoreCase(tok, "animmap") || EqualsAsciiIgnoreCase(tok, "blendfunc") ||
           EqualsAsciiIgnoreCase(tok, "alphafunc") || EqualsAsciiIgnoreCase(tok, "rgbgen") ||
           EqualsAsciiIgnoreCase(tok, "alphagen") || EqualsAsciiIgnoreCase(tok, "tcgen") ||
           EqualsAsciiIgnoreCase(tok, "tcmod") || EqualsAsciiIgnoreCase(tok, "depthfunc") ||
           EqualsAsciiIgnoreCase(tok, "depthwrite") || EqualsAsciiIgnoreCase(tok, "detail") ||
           EqualsAsciiIgnoreCase(tok, "nomipmaps") || EqualsAsciiIgnoreCase(tok, "nopicmip");
}

bool IsWhiteMapName(std::string_view mapName)
{
    return EqualsAsciiIgnoreCase(mapName, "*white") ||
           EqualsAsciiIgnoreCase(mapName, "$whiteimage");
}

QrBlendFactor ParseGlBlendFactor(std::string_view token)
{
    if (EqualsAsciiIgnoreCase(token, "GL_ONE"))
    {
        return QR_BLEND_FACTOR_ONE;
    }
    if (EqualsAsciiIgnoreCase(token, "GL_ZERO"))
    {
        return QR_BLEND_FACTOR_ZERO;
    }
    if (EqualsAsciiIgnoreCase(token, "GL_SRC_COLOR"))
    {
        return QR_BLEND_FACTOR_SRC_COLOR;
    }
    if (EqualsAsciiIgnoreCase(token, "GL_ONE_MINUS_SRC_COLOR"))
    {
        return QR_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    }
    if (EqualsAsciiIgnoreCase(token, "GL_DST_COLOR"))
    {
        return QR_BLEND_FACTOR_DST_COLOR;
    }
    if (EqualsAsciiIgnoreCase(token, "GL_ONE_MINUS_DST_COLOR"))
    {
        return QR_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    }
    if (EqualsAsciiIgnoreCase(token, "GL_SRC_ALPHA"))
    {
        return QR_BLEND_FACTOR_SRC_ALPHA;
    }
    if (EqualsAsciiIgnoreCase(token, "GL_ONE_MINUS_SRC_ALPHA"))
    {
        return QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    if (EqualsAsciiIgnoreCase(token, "GL_DST_ALPHA"))
    {
        return QR_BLEND_FACTOR_ONE;
    }
    if (EqualsAsciiIgnoreCase(token, "GL_ONE_MINUS_DST_ALPHA"))
    {
        return QR_BLEND_FACTOR_ZERO;
    }
    return QR_BLEND_FACTOR_ONE;
}

void ApplyGlBlendFuncPair(const std::string &first, const std::string &second, Q3ShaderStage &stage)
{
    stage.blendSrc = ParseGlBlendFactor(first);
    stage.blendDst = ParseGlBlendFactor(second);

    const bool isOpaque =
        EqualsAsciiIgnoreCase(first, "GL_ONE") && EqualsAsciiIgnoreCase(second, "GL_ZERO");
    stage.blended = !isOpaque;

    const bool srcOneOrAlpha =
        EqualsAsciiIgnoreCase(first, "GL_ONE") || EqualsAsciiIgnoreCase(first, "GL_SRC_ALPHA");
    stage.additive = srcOneOrAlpha && EqualsAsciiIgnoreCase(second, "GL_ONE");

    const bool dstZeroOrAlphaOrColor = EqualsAsciiIgnoreCase(second, "GL_ZERO") ||
                                       EqualsAsciiIgnoreCase(second, "GL_SRC_ALPHA") ||
                                       EqualsAsciiIgnoreCase(second, "GL_SRC_COLOR");
    stage.filter = EqualsAsciiIgnoreCase(first, "GL_DST_COLOR") && dstZeroOrAlphaOrColor;

    const bool stdAlpha = EqualsAsciiIgnoreCase(first, "GL_SRC_ALPHA") &&
                          EqualsAsciiIgnoreCase(second, "GL_ONE_MINUS_SRC_ALPHA");
    const bool invAlpha = EqualsAsciiIgnoreCase(first, "GL_ONE_MINUS_SRC_ALPHA") &&
                          EqualsAsciiIgnoreCase(second, "GL_SRC_ALPHA");
    stage.alphaBlend = stdAlpha || invAlpha;
    stage.invertedAlphaBlend = invAlpha;
    stage.darkenDecal = EqualsAsciiIgnoreCase(first, "GL_ZERO") &&
                        EqualsAsciiIgnoreCase(second, "GL_ONE_MINUS_SRC_COLOR");
}

void ParseStageBlendFunc(const std::vector<std::string> &tokens, size_t &index,
                         Q3ShaderStage &stage)
{
    if (!HasBraceBefore(tokens, index, 1))
    {
        return;
    }

    const std::string &first = tokens[index + 1];
    if (StartsWithAsciiIgnoreCase(first, "GL_") && HasBraceBefore(tokens, index, 2))
    {
        ApplyGlBlendFuncPair(first, tokens[index + 2], stage);
        index += 2;
        return;
    }

    if (EqualsAsciiIgnoreCase(first, "add"))
    {
        stage.blended = true;
        stage.additive = true;
        stage.blendSrc = QR_BLEND_FACTOR_ONE;
        stage.blendDst = QR_BLEND_FACTOR_ONE;
    }
    else if (EqualsAsciiIgnoreCase(first, "filter"))
    {
        stage.blended = true;
        stage.filter = true;
        stage.blendSrc = QR_BLEND_FACTOR_DST_COLOR;
        stage.blendDst = QR_BLEND_FACTOR_ZERO;
    }
    else if (EqualsAsciiIgnoreCase(first, "blend"))
    {
        stage.blended = true;
        stage.alphaBlend = true;
        stage.blendSrc = QR_BLEND_FACTOR_SRC_ALPHA;
        stage.blendDst = QR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    index += 1;
}

bool ParseStageMapToken(const std::vector<std::string> &tokens, size_t &index,
                        Q3ShaderStage &stage)
{
    const std::string &tok = tokens[index];
    const bool isMap = EqualsAsciiIgnoreCase(tok, "map");
    const bool isClamp = EqualsAsciiIgnoreCase(tok, "clampmap");
    if ((isMap || isClamp) && HasBraceBefore(tokens, index, 1))
    {
        stage.map = NormalizeQ3AssetPath(tokens[index + 1]);
        stage.clampMap = isClamp;
        stage.isLightmap = EqualsAsciiIgnoreCase(stage.map, "$lightmap");
        stage.isWhite = IsWhiteMapName(stage.map);
        stage.animationMaps.clear();
        stage.animationFps = 0.0f;
        index += 1;
        return true;
    }

    if (EqualsAsciiIgnoreCase(tok, "animmap") && HasBraceBefore(tokens, index, 2))
    {
        stage.animationFps = std::max(0.0f, ParseFloatToken(tokens[index + 1], 0.0f));
        stage.animationMaps.clear();
        size_t endIdx = index + 2;
        while (endIdx < tokens.size() && tokens[endIdx] != "}" && tokens[endIdx] != "{" &&
               !IsStageKeyword(tokens[endIdx]))
        {
            const std::string frame = NormalizeQ3AssetPath(tokens[endIdx]);
            if (!frame.empty())
            {
                stage.animationMaps.push_back(frame);
            }
            endIdx++;
        }
        if (!stage.animationMaps.empty())
        {
            stage.map = stage.animationMaps.front();
        }
        stage.clampMap = false;
        stage.isLightmap = EqualsAsciiIgnoreCase(stage.map, "$lightmap");
        stage.isWhite = IsWhiteMapName(stage.map);
        if (stage.animationMaps.size() > 1u)
        {
            stage.animated = true;
            stage.dynamic = true;
        }
        index = endIdx - 1u;
        return true;
    }

    return false;
}

bool ParseStageWaveTcMod(const std::vector<std::string> &tokens, size_t &index,
                         const std::string &sub, Q3ShaderStage &stage)
{
    if (EqualsAsciiIgnoreCase(sub, "stretch") && HasBraceBefore(tokens, index, 6))
    {
        const Q3ShaderWaveType wave = ParseShaderWave(tokens[index + 2]);
        if (wave != Q3ShaderWaveType::None)
        {
            Q3TcMod mod{};
            mod.type = Q3TcModType::Stretch;
            mod.wave = wave;
            mod.params[0] = ParseFloatToken(tokens[index + 3], 1.0f);
            mod.params[1] = ParseFloatToken(tokens[index + 4], 0.0f);
            mod.params[2] = ParseFloatToken(tokens[index + 5], 0.0f);
            mod.params[3] = ParseFloatToken(tokens[index + 6], 0.0f);
            stage.tcMods.push_back(mod);
            stage.animated = true;
            stage.dynamic = true;
        }
        index += 6;
        return true;
    }
    if (EqualsAsciiIgnoreCase(sub, "turb") && HasBraceBefore(tokens, index, 5))
    {
        Q3TcMod mod{};
        mod.type = Q3TcModType::Turb;
        mod.params[0] = ParseFloatToken(tokens[index + 2], 0.0f);
        mod.params[1] = ParseFloatToken(tokens[index + 3], 0.0f);
        mod.params[2] = ParseFloatToken(tokens[index + 4], 0.0f);
        mod.params[3] = ParseFloatToken(tokens[index + 5], 0.0f);
        stage.tcMods.push_back(mod);
        stage.animated = true;
        stage.dynamic = true;
        index += 5;
        return true;
    }
    return false;
}

void ParseStageTcMod(const std::vector<std::string> &tokens, size_t &index, Q3ShaderStage &stage)
{
    if (!HasBraceBefore(tokens, index, 1))
    {
        return;
    }
    const std::string &sub = tokens[index + 1];
    if (ParseStageWaveTcMod(tokens, index, sub, stage))
    {
        return;
    }
    if (EqualsAsciiIgnoreCase(sub, "scale") && HasBraceBefore(tokens, index, 3))
    {
        const float sx = ParseFloatToken(tokens[index + 2], 1.0f);
        const float sy = ParseFloatToken(tokens[index + 3], 1.0f);
        stage.hasScale = true;
        stage.scale[0] *= sx;
        stage.scale[1] *= sy;
        Q3TcMod mod{};
        mod.type = Q3TcModType::Scale;
        mod.params[0] = sx;
        mod.params[1] = sy;
        stage.tcMods.push_back(mod);
        index += 3;
    }
    else if (EqualsAsciiIgnoreCase(sub, "scroll") && HasBraceBefore(tokens, index, 3))
    {
        Q3TcMod mod{};
        mod.type = Q3TcModType::Scroll;
        mod.params[0] = ParseFloatToken(tokens[index + 2], 0.0f);
        mod.params[1] = ParseFloatToken(tokens[index + 3], 0.0f);
        stage.tcMods.push_back(mod);
        stage.animated = true;
        stage.dynamic = true;
        index += 3;
    }
    else if (EqualsAsciiIgnoreCase(sub, "rotate") && HasBraceBefore(tokens, index, 2))
    {
        Q3TcMod mod{};
        mod.type = Q3TcModType::Rotate;
        mod.params[0] = ParseFloatToken(tokens[index + 2], 0.0f);
        stage.tcMods.push_back(mod);
        stage.animated = true;
        stage.dynamic = true;
        index += 2;
    }
    else if (EqualsAsciiIgnoreCase(sub, "entitytranslate"))
    {
        stage.dynamic = true;
        index += 1;
    }
    else if (EqualsAsciiIgnoreCase(sub, "transform") && HasBraceBefore(tokens, index, 7))
    {
        index += 7;
    }
    else
    {
        index += 1;
    }
}

void ApplyStageWaveGen(const std::vector<std::string> &tokens, size_t &index,
                       Q3ShaderStage &stage, bool isAlpha)
{
    const Q3ShaderWaveType wave = ParseShaderWave(tokens[index + 2]);
    if (wave != Q3ShaderWaveType::None)
    {
        const float base = ParseFloatToken(tokens[index + 3], 1.0f);
        const float amp = ParseFloatToken(tokens[index + 4], 0.0f);
        const float phase = ParseFloatToken(tokens[index + 5], 0.0f);
        const float freq = ParseFloatToken(tokens[index + 6], 0.0f);
        if (isAlpha)
        {
            stage.alphaGen = Q3AlphaGen::Wave;
            stage.alphaWave = wave;
            stage.alphaWaveBase = base;
            stage.alphaWaveAmplitude = amp;
            stage.alphaWavePhase = phase;
            stage.alphaWaveFrequency = freq;
        }
        else
        {
            stage.rgbGen = Q3ColorGen::Wave;
            stage.rgbWave = wave;
            stage.rgbWaveBase = base;
            stage.rgbWaveAmplitude = amp;
            stage.rgbWavePhase = phase;
            stage.rgbWaveFrequency = freq;
        }
        stage.animated = true;
        stage.dynamic = true;
    }
    index += 6;
}

bool ApplyRgbGenKeyword(const std::string &mode, Q3ShaderStage &stage)
{
    if (EqualsAsciiIgnoreCase(mode, "vertex") || EqualsAsciiIgnoreCase(mode, "exactvertex"))
    {
        stage.rgbGen = Q3ColorGen::Vertex;
        return true;
    }
    if (EqualsAsciiIgnoreCase(mode, "entity"))
    {
        stage.rgbGen = Q3ColorGen::Entity;
        return true;
    }
    if (EqualsAsciiIgnoreCase(mode, "identity") ||
        EqualsAsciiIgnoreCase(mode, "identitylighting") ||
        EqualsAsciiIgnoreCase(mode, "lightingdiffuse"))
    {
        stage.rgbGen = Q3ColorGen::Identity;
        return true;
    }
    return false;
}

bool ApplyAlphaGenKeyword(const std::string &mode, Q3ShaderStage &stage)
{
    if (EqualsAsciiIgnoreCase(mode, "vertex"))
    {
        stage.alphaGen = Q3AlphaGen::Vertex;
        return true;
    }
    if (EqualsAsciiIgnoreCase(mode, "entity"))
    {
        stage.alphaGen = Q3AlphaGen::Entity;
        return true;
    }
    if (EqualsAsciiIgnoreCase(mode, "identity"))
    {
        stage.alphaGen = Q3AlphaGen::Identity;
        return true;
    }
    if (EqualsAsciiIgnoreCase(mode, "lightingspecular"))
    {
        stage.alphaGen = Q3AlphaGen::LightingSpecular;
        return true;
    }
    return false;
}

void SkipConstGenTokens(const std::vector<std::string> &tokens, size_t &index)
{
    size_t skip = index + 2;
    while (skip < tokens.size() && tokens[skip] != "}" && tokens[skip] != "{" &&
           !IsStageKeyword(tokens[skip]))
    {
        if (tokens[skip].find(')') != std::string::npos)
        {
            skip++;
            break;
        }
        skip++;
    }
    index = skip - 1u;
}

void ParseStageColorOrAlphaGen(const std::vector<std::string> &tokens, size_t &index,
                               Q3ShaderStage &stage, bool isAlpha)
{
    if (!HasBraceBefore(tokens, index, 1))
    {
        return;
    }

    const std::string &mode = tokens[index + 1];
    if (EqualsAsciiIgnoreCase(mode, "wave") && HasBraceBefore(tokens, index, 6))
    {
        ApplyStageWaveGen(tokens, index, stage, isAlpha);
        return;
    }
    if ((!isAlpha && ApplyRgbGenKeyword(mode, stage)) ||
        (isAlpha && ApplyAlphaGenKeyword(mode, stage)))
    {
        index += 1;
        return;
    }
    if (EqualsAsciiIgnoreCase(mode, "const"))
    {
        SkipConstGenTokens(tokens, index);
    }
    else
    {
        index += 1;
    }
}

void ParseStageToken(const std::vector<std::string> &tokens, size_t &index, Q3ShaderStage &stage)
{
    if (ParseStageMapToken(tokens, index, stage))
    {
        return;
    }

    const std::string &tok = tokens[index];
    if (EqualsAsciiIgnoreCase(tok, "alphafunc") && HasBraceBefore(tokens, index, 1))
    {
        stage.alphaTested = true;
        index += 1;
    }
    else if (EqualsAsciiIgnoreCase(tok, "tcgen") && HasBraceBefore(tokens, index, 1))
    {
        stage.isEnvironment =
            stage.isEnvironment || EqualsAsciiIgnoreCase(tokens[index + 1], "environment");
        index += 1;
    }
    else if (EqualsAsciiIgnoreCase(tok, "tcmod"))
    {
        ParseStageTcMod(tokens, index, stage);
    }
    else if (EqualsAsciiIgnoreCase(tok, "rgbgen"))
    {
        ParseStageColorOrAlphaGen(tokens, index, stage, false);
    }
    else if (EqualsAsciiIgnoreCase(tok, "alphagen"))
    {
        ParseStageColorOrAlphaGen(tokens, index, stage, true);
    }
    else if (EqualsAsciiIgnoreCase(tok, "blendfunc"))
    {
        ParseStageBlendFunc(tokens, index, stage);
    }
}

bool IsNonRenderMapStage(const Q3ShaderStage &stage)
{
    return stage.isLightmap || stage.isWhite || stage.map.empty();
}

bool IsDirectRenderStage(const Q3ShaderStage &s)
{
    return !s.isLightmap && !s.animated && !s.isEnvironment && !s.isWhite && !s.map.empty();
}

size_t FindFilterMultiplyStage(const Q3ShaderDef &def, size_t invalidIdx)
{
    if (def.stages.size() < 3u || def.stages[1].blendSrc != QR_BLEND_FACTOR_DST_COLOR ||
        def.stages[1].blendDst != QR_BLEND_FACTOR_SRC_COLOR)
    {
        return invalidIdx;
    }
    for (size_t si = 2u; si < def.stages.size(); si++)
    {
        const Q3ShaderStage &s = def.stages[si];
        if (IsDirectRenderStage(s) && s.blendSrc == QR_BLEND_FACTOR_DST_COLOR &&
            s.blendDst == QR_BLEND_FACTOR_ZERO)
        {
            return si;
        }
    }
    return invalidIdx;
}

size_t FindLightmapAlphaCompositeStage(const Q3ShaderDef &def, size_t invalidIdx)
{
    if (!def.stages[0].isLightmap)
    {
        return invalidIdx;
    }
    for (size_t si = 1u; si < def.stages.size(); si++)
    {
        const Q3ShaderStage &s = def.stages[si];
        if (IsDirectRenderStage(s) && s.alphaBlend && !s.invertedAlphaBlend)
        {
            return si;
        }
    }
    return invalidIdx;
}

size_t FindCompositePrimaryStage(const Q3ShaderDef &def, size_t invalidIdx)
{
    if (def.stages.size() < 2u)
    {
        return invalidIdx;
    }
    const Q3ShaderStage &s1 = def.stages[1];
    if (IsDirectRenderStage(s1) && s1.blendSrc == QR_BLEND_FACTOR_DST_COLOR &&
        (s1.blendDst == QR_BLEND_FACTOR_ZERO || s1.blendDst == QR_BLEND_FACTOR_SRC_ALPHA))
    {
        return 1u;
    }
    const size_t filterIdx = FindFilterMultiplyStage(def, invalidIdx);
    if (filterIdx != invalidIdx)
    {
        return filterIdx;
    }
    return FindLightmapAlphaCompositeStage(def, invalidIdx);
}

size_t ScanFallbackPrimaryStage(const Q3ShaderDef &def, bool stage0WhiteOrLm, size_t invalidIdx)
{
    size_t filterOrBlendFallback = invalidIdx;
    size_t anyFallback = invalidIdx;
    size_t envFallback = invalidIdx;
    for (size_t i = 0; i < def.stages.size(); i++)
    {
        const Q3ShaderStage &stage = def.stages[i];
        if (IsNonRenderMapStage(stage))
        {
            continue;
        }
        if (stage.isEnvironment)
        {
            envFallback = envFallback == invalidIdx ? i : envFallback;
            continue;
        }
        if (!stage.blended)
        {
            return i;
        }
        const bool preferredBlend =
            stage0WhiteOrLm ? (stage.filter || stage.alphaBlend) : stage.alphaBlend;
        if (filterOrBlendFallback == invalidIdx && preferredBlend)
        {
            filterOrBlendFallback = i;
        }
        anyFallback = anyFallback == invalidIdx ? i : anyFallback;
    }

    if (filterOrBlendFallback != invalidIdx)
    {
        return filterOrBlendFallback;
    }
    return anyFallback != invalidIdx ? anyFallback : envFallback;
}

size_t SelectPrimaryStageIndex(const Q3ShaderDef &def, size_t invalidIdx)
{
    const bool stage0WhiteOrLm =
        !def.stages.empty() && (def.stages[0].isLightmap || def.stages[0].isWhite) &&
        !def.stages[0].filter && !def.stages[0].isEnvironment;
    if (!def.isTrans && stage0WhiteOrLm)
    {
        const size_t compositeIdx = FindCompositePrimaryStage(def, invalidIdx);
        if (compositeIdx != invalidIdx)
        {
            return compositeIdx;
        }
    }
    return ScanFallbackPrimaryStage(def, stage0WhiteOrLm, invalidIdx);
}

bool IsOpaqueDirectStage(const Q3ShaderStage &stage)
{
    return !IsNonRenderMapStage(stage) && !stage.isEnvironment && !stage.blended;
}

bool HasCompositeOverlayStage(const Q3ShaderDef &def, bool baseWhiteOrLm, bool baseEnv)
{
    for (size_t i = 1; i < def.stages.size(); i++)
    {
        const Q3ShaderStage &s = def.stages[i];
        if (IsNonRenderMapStage(s) || s.isEnvironment)
        {
            continue;
        }
        const bool whiteComposite = baseWhiteOrLm && (s.filter || s.alphaBlend);
        const bool envComposite = baseEnv && s.alphaBlend;
        if (whiteComposite || envComposite)
        {
            return true;
        }
    }
    return false;
}

bool HasOpaqueShaderComposite(const Q3ShaderDef &def)
{
    for (const Q3ShaderStage &stage : def.stages)
    {
        if (IsOpaqueDirectStage(stage))
        {
            return true;
        }
    }
    if (def.stages.size() < 2u)
    {
        return false;
    }

    const Q3ShaderStage &s0 = def.stages[0];
    const bool baseWhiteOrLm =
        (s0.isLightmap || s0.isWhite) && !s0.filter && !s0.isEnvironment && !def.isTrans;
    const bool baseEnv = s0.isEnvironment && !s0.blended;
    return HasCompositeOverlayStage(def, baseWhiteOrLm, baseEnv);
}

bool IsEmissiveUnderlayStage(const Q3ShaderStage &stage, float surfaceLight)
{
    if (surfaceLight > 0.0f || stage.animationMaps.size() > 1u || stage.animated)
    {
        return true;
    }
    const std::string key = NormalizeQ3AssetPath(stage.map);
    static constexpr const char *kEmissiveTokens[] = {
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
        "teleport",
    };
    for (const char *token : kEmissiveTokens)
    {
        if (key.find(token) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

bool TextureHasBinaryCutoutHoles(const std::string &mapName, const EditorAssetSource *assets,
                                 std::unordered_map<std::string, bool> *cache)
{
    if (assets == nullptr)
    {
        return false;
    }
    const std::string key = NormalizeQ3AssetPath(mapName);
    if (key.empty())
    {
        return false;
    }
    if (cache != nullptr)
    {
        const auto existing = cache->find(key);
        if (existing != cache->end())
        {
            return existing->second;
        }
    }

    std::vector<uint8_t> pixels;
    uint32_t w = 0;
    uint32_t h = 0;
    std::string resolved;
    bool hasHoles = false;
    if (LoadQ3ImageRgba(assets, mapName, pixels, w, h, resolved) && w > 0 && h > 0)
    {
        const size_t total = size_t(w) * size_t(h);
        size_t lowAlphaCount = 0;
        size_t opaqueAlphaCount = 0;
        for (size_t i = 0; i < total && (i * 4u + 3u) < pixels.size(); i++)
        {
            if (pixels[i * 4u + 3u] < 128u)
            {
                lowAlphaCount++;
            }
            else
            {
                opaqueAlphaCount++;
            }
        }
        hasHoles = (lowAlphaCount * 200u >= total) && (opaqueAlphaCount * 10u >= total);
    }
    if (cache != nullptr)
    {
        cache->emplace(key, hasHoles);
    }
    return hasHoles;
}

std::string StagePrimaryTextureMap(const Q3ShaderStage &stage)
{
    if (!stage.map.empty())
    {
        return stage.map;
    }
    if (!stage.animationMaps.empty())
    {
        return stage.animationMaps.front();
    }
    return {};
}

std::string ResolveEmissionMaskMap(const Q3ShaderDef &def)
{
    if (def.surfaceLight <= 0.0f && !def.tracedBase && !def.isNonTracedOverlay)
    {
        return {};
    }

    for (const Q3ShaderStage &stage : def.stages)
    {
        if (stage.additive && !stage.isLightmap && !stage.isWhite && !stage.isEnvironment)
        {
            const std::string stageMap = StagePrimaryTextureMap(stage);
            if (!stageMap.empty() &&
                (def.isNonTracedOverlay || !EqualsAsciiIgnoreCase(stageMap, def.map)))
            {
                return stageMap;
            }
        }
    }
    if (def.cutoutFrameUnderlay && def.baseStageIndex > 0u)
    {
        for (size_t i = 0; i < def.baseStageIndex && i < def.stages.size(); ++i)
        {
            const Q3ShaderStage &stage = def.stages[i];
            if (!stage.isLightmap && !stage.isWhite && !stage.isEnvironment)
            {
                const std::string stageMap = StagePrimaryTextureMap(stage);
                if (!stageMap.empty())
                {
                    return stageMap;
                }
            }
        }
    }
    if (def.compositeUnderlayBase && def.underlayEmissive && !def.underlayMap.empty())
    {
        return def.underlayMap;
    }
    if (def.surfaceLight > 0.0f && !def.lightImage.empty() &&
        !EqualsAsciiIgnoreCase(def.lightImage, def.map))
    {
        return def.lightImage;
    }
    return {};
}

void ScanNonLightmapBlendFlags(const Q3ShaderDef &def, bool &anyNonLm, bool &hasAdditive,
                               bool &hasAlphaBlend)
{
    for (const Q3ShaderStage &stage : def.stages)
    {
        if (stage.isLightmap)
        {
            continue;
        }
        anyNonLm = true;
        hasAdditive = hasAdditive || stage.additive;
        hasAlphaBlend = hasAlphaBlend || stage.alphaBlend;
    }
}

bool IsSpecialNoOverlayShader(const Q3ShaderDef &def)
{
    return def.isSky || def.isNoDraw || def.isWater || def.isSlime || def.isLava || def.isPortal;
}

void ApplyAutoSpriteOverlayFlags(Q3ShaderDef &def)
{
    def.cutoutFrameUnderlay = false;
    def.compositeUnderlayBase = false;
    def.metallicEnvBase = false;
    def.wetGlossAlphaMask = false;
    def.tracedBase = false;
    def.isNonTracedOverlay = true;
    def.usesOverlay = true;
}

bool HasPreclassifiedSurfaceRole(const Q3ShaderDef &def)
{
    return def.alphaTested || def.cutoutFrameUnderlay || def.compositeUnderlayBase ||
           def.metallicEnvBase || def.wetGlossAlphaMask ||
           (def.tracedBase && HasOpaqueShaderComposite(def));
}

void ClassifyNonTracedAndCutout(Q3ShaderDef &def)
{
    if (def.isFog)
    {
        def.isNonTracedOverlay = true;
        def.tracedBase = false;
        def.usesOverlay = true;
        return;
    }
    const bool skipSpecial = IsSpecialNoOverlayShader(def);
    if (!skipSpecial && (def.autoSprite || def.autoSprite2))
    {
        ApplyAutoSpriteOverlayFlags(def);
        return;
    }
    if (skipSpecial || HasPreclassifiedSurfaceRole(def))
    {
        return;
    }

    bool anyNonLm = false;
    bool hasAdditive = false;
    bool hasAlphaBlend = false;
    ScanNonLightmapBlendFlags(def, anyNonLm, hasAdditive, hasAlphaBlend);
    if (!anyNonLm)
    {
        return;
    }
    const bool animatedNonTraced = !def.tracedBase && def.baseStageIndex < def.stages.size() &&
                                   def.stages[def.baseStageIndex].animated;
    if (hasAdditive || def.isTrans || animatedNonTraced)
    {
        def.isNonTracedOverlay = true;
        def.tracedBase = false;
        def.usesOverlay = true;
    }
    else if (hasAlphaBlend)
    {
        def.alphaTested = true;
        def.tracedBase = true;
    }
}

bool IsStaticAlphaBlendStage(const Q3ShaderStage &stage)
{
    return !stage.isLightmap && !stage.animated && !stage.isEnvironment && stage.alphaBlend &&
           !stage.invertedAlphaBlend;
}

bool IsOpaqueNonEnvStage(const Q3ShaderStage &stage)
{
    return !stage.blended && !stage.isEnvironment && !stage.isLightmap;
}

void MarkPostOverlayAfterFrame(Q3ShaderDef &def, size_t frameIdx)
{
    def.usesOverlay = false;
    for (size_t si = frameIdx + 1u; si < def.stages.size(); si++)
    {
        const Q3ShaderStage &post = def.stages[si];
        if (!post.isLightmap && (post.blended || post.dynamic))
        {
            def.usesOverlay = true;
            return;
        }
    }
}

bool TryClassifyAnimatedUnderlayWithCutout(Q3ShaderDef &def, bool hasLightmapStage,
                                           const EditorAssetSource *assets,
                                           std::unordered_map<std::string, bool> *cutoutCache)
{
    constexpr size_t kInvalidIdx = (std::numeric_limits<size_t>::max)();
    const bool isModelObject = StartsWithAsciiIgnoreCase(def.name, "models/mapobjects/");
    if (def.isTrans || (!hasLightmapStage && !isModelObject) || def.stages.size() < 2u ||
        !IsOpaqueNonEnvStage(def.stages[0]) || !def.stages[0].animated)
    {
        return false;
    }

    size_t frameIdx = kInvalidIdx;
    for (size_t si = 1u; si < def.stages.size(); si++)
    {
        if (IsStaticAlphaBlendStage(def.stages[si]))
        {
            frameIdx = si;
        }
    }
    if (frameIdx == kInvalidIdx)
    {
        return false;
    }

    if (IsEmissiveUnderlayStage(def.stages[0], def.surfaceLight) &&
        TextureHasBinaryCutoutHoles(def.stages[frameIdx].map, assets, cutoutCache))
    {
        def.cutoutFrameUnderlay = true;
        def.baseStageIndex = frameIdx;
        def.map = def.stages[frameIdx].map;
        def.alphaTested = true;
        def.tracedBase = true;
        def.usesOverlay = true;
        return true;
    }

    def.compositeUnderlayBase = true;
    def.underlayMap = def.stages[0].map;
    def.underlayScale[0] = def.stages[0].scale[0];
    def.underlayScale[1] = def.stages[0].scale[1];
    def.underlayEmissive = false;
    def.baseStageIndex = frameIdx;
    def.map = def.stages[frameIdx].map;
    def.alphaTested = false;
    def.tracedBase = true;
    MarkPostOverlayAfterFrame(def, frameIdx);
    return true;
}

bool TryClassifyAdditiveAndAlphaMask(Q3ShaderDef &def, bool hasLightmapStage,
                                     const EditorAssetSource *assets,
                                     std::unordered_map<std::string, bool> *cutoutCache)
{
    constexpr size_t kInvalidIdx = (std::numeric_limits<size_t>::max)();
    const bool isModelObject = StartsWithAsciiIgnoreCase(def.name, "models/mapobjects/");
    if (def.isTrans || (!hasLightmapStage && !isModelObject) || def.stages.size() < 3u ||
        !IsOpaqueNonEnvStage(def.stages[0]) || def.stages[0].animated)
    {
        return false;
    }

    const std::string baseKey = NormalizeQ3AssetPath(def.stages[0].map);
    size_t maskIdx = kInvalidIdx;
    bool allIntermediateAdditive = true;
    for (size_t si = 1u; si < def.stages.size(); si++)
    {
        const Q3ShaderStage &stage = def.stages[si];
        if (IsStaticAlphaBlendStage(stage) && NormalizeQ3AssetPath(stage.map) == baseKey)
        {
            maskIdx = si;
            break;
        }
        if (!stage.additive)
        {
            allIntermediateAdditive = false;
            break;
        }
    }
    if (maskIdx == kInvalidIdx || maskIdx < 2u || !allIntermediateAdditive)
    {
        return false;
    }

    if (TextureHasBinaryCutoutHoles(def.stages[maskIdx].map, assets, cutoutCache))
    {
        def.cutoutFrameUnderlay = true;
        def.baseStageIndex = maskIdx;
        def.map = def.stages[maskIdx].map;
        def.alphaTested = true;
        def.tracedBase = true;
        def.usesOverlay = true;
    }
    else
    {
        def.wetGlossAlphaMask = true;
        def.baseStageIndex = 0u;
        def.map = def.stages[0].map;
        def.alphaTested = false;
        def.tracedBase = true;
        def.usesOverlay = false;
    }
    return true;
}

bool TryClassifyPreAdditiveAlphaTested(Q3ShaderDef &def)
{
    constexpr size_t kInvalidIdx = (std::numeric_limits<size_t>::max)();
    if (def.isTrans || def.baseStageIndex == kInvalidIdx || def.baseStageIndex == 0u ||
        def.baseStageIndex >= def.stages.size() || !def.stages[def.baseStageIndex].alphaTested)
    {
        return false;
    }
    for (size_t si = 0; si < def.baseStageIndex; si++)
    {
        if (!def.stages[si].isLightmap && (def.stages[si].additive || def.stages[si].animated))
        {
            def.cutoutFrameUnderlay = true;
            def.tracedBase = true;
            def.usesOverlay = true;
            return true;
        }
    }
    return false;
}

void CollectMetallicEnvOverlayStages(Q3ShaderDef &def, size_t diffuseIdx)
{
    def.usesOverlay = false;
    bool seenLightmap = false;
    for (size_t si = diffuseIdx + 1u; si < def.stages.size(); si++)
    {
        const Q3ShaderStage &stage = def.stages[si];
        if (stage.isLightmap)
        {
            seenLightmap = true;
            continue;
        }
        if (!seenLightmap && !stage.isEnvironment && !stage.animated && stage.alphaBlend)
        {
            def.decalOverlayMaps.push_back(stage.map);
            def.decalOverlayInvertedAlpha.push_back(stage.invertedAlphaBlend);
        }
        else if (stage.additive)
        {
            def.usesOverlay = true;
        }
    }
}

bool TryClassifyMetallicEnvShader(Q3ShaderDef &def)
{
    constexpr size_t kInvalidIdx = (std::numeric_limits<size_t>::max)();
    if (def.stages.size() < 2u || !def.stages[0].isEnvironment || def.stages[0].blended)
    {
        return false;
    }

    size_t diffuseIdx = kInvalidIdx;
    bool diffuseInverted = false;
    for (size_t si = 1u; si < def.stages.size(); si++)
    {
        const Q3ShaderStage &stage = def.stages[si];
        if (!stage.isLightmap && !stage.isEnvironment && !stage.animated && stage.alphaBlend)
        {
            diffuseIdx = si;
            diffuseInverted = stage.invertedAlphaBlend;
            break;
        }
    }
    if (diffuseIdx == kInvalidIdx)
    {
        return false;
    }

    def.metallicEnvBase = true;
    def.metallicEnvInvertedAlpha = diffuseInverted;
    def.baseStageIndex = diffuseIdx;
    def.map = def.stages[diffuseIdx].map;
    def.tracedBase = !def.isTrans;
    CollectMetallicEnvOverlayStages(def, diffuseIdx);
    return true;
}

void TryClassifyUnlitUnderlayComposite(Q3ShaderDef &def, bool hasLightmapStage,
                                       const EditorAssetSource *assets,
                                       std::unordered_map<std::string, bool> *cutoutCache)
{
    constexpr size_t kInvalidIdx = (std::numeric_limits<size_t>::max)();
    if (hasLightmapStage || def.stages.size() < 2u || def.stages[0].blended ||
        def.stages[0].isLightmap || def.stages[0].isWhite)
    {
        return;
    }

    size_t diffuseIdx = kInvalidIdx;
    for (size_t si = 1u; si < def.stages.size(); si++)
    {
        if (IsStaticAlphaBlendStage(def.stages[si]))
        {
            diffuseIdx = si;
        }
    }
    if (diffuseIdx == kInvalidIdx)
    {
        return;
    }

    if (!def.isTrans && IsEmissiveUnderlayStage(def.stages[0], def.surfaceLight) &&
        TextureHasBinaryCutoutHoles(def.stages[diffuseIdx].map, assets, cutoutCache))
    {
        def.cutoutFrameUnderlay = true;
        def.baseStageIndex = diffuseIdx;
        def.map = def.stages[diffuseIdx].map;
        def.alphaTested = true;
        def.tracedBase = true;
        def.usesOverlay = true;
        return;
    }

    def.compositeUnderlayBase = true;
    def.underlayMap = def.stages[0].map;
    def.underlayScale[0] = def.stages[0].scale[0];
    def.underlayScale[1] = def.stages[0].scale[1];
    def.underlayEmissive = def.stages[0].animated && !def.stages[0].isEnvironment;
    def.baseStageIndex = diffuseIdx;
    def.map = def.stages[diffuseIdx].map;
    def.tracedBase = !def.isTrans;
    def.usesOverlay = false;
}

void ClassifyMultiStagePatterns(Q3ShaderDef &def, bool hasLightmapStage,
                                const EditorAssetSource *assets,
                                std::unordered_map<std::string, bool> *cutoutCache)
{
    if (TryClassifyAnimatedUnderlayWithCutout(def, hasLightmapStage, assets, cutoutCache) ||
        TryClassifyAdditiveAndAlphaMask(def, hasLightmapStage, assets, cutoutCache) ||
        TryClassifyPreAdditiveAlphaTested(def) ||
        TryClassifyMetallicEnvShader(def))
    {
        return;
    }
    TryClassifyUnlitUnderlayComposite(def, hasLightmapStage, assets, cutoutCache);
}

void AssignFallbackShaderMap(Q3ShaderDef &def)
{
    if (!def.editorImage.empty())
    {
        def.map = def.editorImage;
    }
    else if (!def.lightImage.empty())
    {
        def.map = def.lightImage;
    }
    else
    {
        def.map = def.name;
    }
}

bool IsLightmapCompositeStage(const Q3ShaderDef &def, const Q3ShaderStage &chosen)
{
    const bool stage0WhiteOrLm =
        !def.stages.empty() && (def.stages[0].isLightmap || def.stages[0].isWhite) &&
        !def.stages[0].filter && !def.stages[0].isEnvironment;
    return stage0WhiteOrLm && (chosen.filter || (chosen.alphaBlend && !chosen.invertedAlphaBlend));
}

void AssignBaseStageMapAndTracedFlag(Q3ShaderDef &def, size_t chosenIdx, size_t invalidIdx)
{
    if (chosenIdx == invalidIdx)
    {
        AssignFallbackShaderMap(def);
        return;
    }
    const Q3ShaderStage &chosen = def.stages[chosenIdx];
    def.map = chosen.map;
    def.darkenDecal = chosen.darkenDecal;
    const bool lmComposite = IsLightmapCompositeStage(def, chosen);
    const bool opaqueCutout = chosen.alphaTested && !chosen.blended;
    const bool opaqueOrLit = !def.isTrans && (!chosen.blended || chosen.alphaTested || lmComposite);
    def.tracedBase = !chosen.animated && !chosen.isEnvironment && (opaqueCutout || opaqueOrLit);
}

void DetectImplicitLiquidLava(Q3ShaderDef &def)
{
    if (IsSpecialNoOverlayShader(def) || def.isFog || def.isTrans || def.stages.size() != 1u ||
        !IsOpaqueNonEnvStage(def.stages[0]) ||
        !StartsWithAsciiIgnoreCase(def.name, "textures/liquids/"))
    {
        return;
    }
    const std::string lowerName = NormalizeQ3AssetPath(def.name);
    const std::string lowerMap = NormalizeQ3AssetPath(def.stages[0].map);
    if (lowerName.find("lava") != std::string::npos || lowerMap.find("lava") != std::string::npos)
    {
        def.isLava = true;
    }
}

bool ShouldSuppressPostOverlay(const Q3ShaderDef &def)
{
    const bool suppressSpecial = def.isSky || def.isNoDraw || def.isWater || def.isSlime ||
                                 def.isPortal || def.isFog ||
                                 (def.isLava && def.deformWave == Q3ShaderWaveType::None);
    return suppressSpecial || def.metallicEnvBase || def.compositeUnderlayBase ||
           def.wetGlossAlphaMask;
}

void MarkPostStageOverlayUsage(Q3ShaderDef &def)
{
    if (ShouldSuppressPostOverlay(def))
    {
        return;
    }
    for (size_t stageIndex = 0; stageIndex < def.stages.size(); stageIndex++)
    {
        const Q3ShaderStage &stage = def.stages[stageIndex];
        const bool isBase = def.tracedBase && stageIndex == def.baseStageIndex;
        if (!isBase && !stage.isLightmap && (stage.blended || stage.dynamic))
        {
            def.usesOverlay = true;
        }
    }
    if (!def.tracedBase)
    {
        def.usesOverlay = !def.stages.empty();
    }
}

void FinalizeShaderDefinition(Q3ShaderDef &def, const EditorAssetSource *assets,
                              std::unordered_map<std::string, bool> *cutoutCache)
{
    constexpr size_t kInvalidIdx = (std::numeric_limits<size_t>::max)();
    bool hasLightmapStage = false;
    for (const Q3ShaderStage &stage : def.stages)
    {
        def.alphaTested = def.alphaTested || stage.alphaTested;
        def.hasEnvironment = def.hasEnvironment || stage.isEnvironment;
        hasLightmapStage = hasLightmapStage || stage.isLightmap;
    }

    const size_t chosenIdx = SelectPrimaryStageIndex(def, kInvalidIdx);
    def.baseStageIndex = chosenIdx;
    AssignBaseStageMapAndTracedFlag(def, chosenIdx, kInvalidIdx);
    DetectImplicitLiquidLava(def);

    if (!IsSpecialNoOverlayShader(def) && !def.isFog)
    {
        ClassifyMultiStagePatterns(def, hasLightmapStage, assets, cutoutCache);
    }

    MarkPostStageOverlayUsage(def);
    ClassifyNonTracedAndCutout(def);

    if (def.tracedBase && !def.compositeUnderlayBase && def.baseStageIndex < def.stages.size() &&
        def.stages[def.baseStageIndex].hasScale)
    {
        def.baseScale[0] = def.stages[def.baseStageIndex].scale[0];
        def.baseScale[1] = def.stages[def.baseStageIndex].scale[1];
    }

    def.emissionMaskMap = ResolveEmissionMaskMap(def);
}

bool DecodeFromCandidates(const EditorAssetSource &assets,
                          const std::vector<std::string> &candidates,
                          std::vector<uint8_t> &outRgba, uint32_t &outWidth, uint32_t &outHeight,
                          std::string &outResolvedPath)
{
    std::vector<uint8_t> encoded;
    for (const std::string &candidate : candidates)
    {
        if (candidate.empty() || !assets.ReadFile(candidate, encoded))
        {
            continue;
        }
        if (DecodeEditorImageRgba(encoded, outWidth, outHeight, outRgba))
        {
            outResolvedPath = candidate;
            return true;
        }
    }

    return false;
}

struct ShaderParseState
{
    int depth = 0;
    size_t parsedCount = 0;
    bool hasDef = false;
    Q3ShaderDef currentDef;
    Q3ShaderStage currentStage;
    const EditorAssetSource *assets = nullptr;
    std::unordered_map<std::string, bool> *cutoutCache = nullptr;
};

void HandleClosingBrace(bool overwrite, ShaderParseState &state,
                        std::unordered_map<std::string, Q3ShaderDef> &defs)
{
    if (state.depth == 2 && state.hasDef)
    {
        state.currentDef.stages.push_back(std::move(state.currentStage));
        state.currentStage = Q3ShaderStage{};
    }
    else if (state.depth == 1 && state.hasDef)
    {
        FinalizeShaderDefinition(state.currentDef, state.assets, state.cutoutCache);
        if (overwrite || defs.find(state.currentDef.name) == defs.end())
        {
            defs[state.currentDef.name] = std::move(state.currentDef);
        }
        state.currentDef = Q3ShaderDef{};
        state.hasDef = false;
        state.parsedCount++;
    }
    state.depth = std::max(0, state.depth - 1);
}

void ProcessShaderToken(const std::vector<std::string> &tokens, size_t &i, bool overwrite,
                        ShaderParseState &state,
                        std::unordered_map<std::string, Q3ShaderDef> &defs)
{
    const std::string &tok = tokens[i];
    if (tok == "{")
    {
        state.depth++;
        if (state.depth == 2 && state.hasDef)
        {
            state.currentStage = Q3ShaderStage{};
        }
        return;
    }

    if (tok == "}")
    {
        HandleClosingBrace(overwrite, state, defs);
        return;
    }

    if (state.depth == 0)
    {
        state.currentDef = Q3ShaderDef{};
        state.currentDef.name = NormalizeQ3AssetPath(tok);
        state.hasDef = !state.currentDef.name.empty();
    }
    else if (state.depth == 1 && state.hasDef)
    {
        ParseHeaderToken(tokens, i, state.currentDef);
    }
    else if (state.depth == 2 && state.hasDef)
    {
        ParseStageToken(tokens, i, state.currentStage);
    }
}

} // namespace

void Q3ShaderDatabase::Clear()
{
    m_defs.clear();
    m_binaryCutoutCache.clear();
}

size_t Q3ShaderDatabase::ParseScript(const char *data, size_t size, bool overwrite,
                                     const EditorAssetSource *assets)
{
    if (data == nullptr || size == 0)
    {
        return 0;
    }

    const std::vector<std::string> tokens = TokenizeShaderScript(data, size);
    ShaderParseState state;
    state.assets = assets;
    state.cutoutCache = &m_binaryCutoutCache;

    for (size_t i = 0; i < tokens.size(); i++)
    {
        ProcessShaderToken(tokens, i, overwrite, state, m_defs);
    }

    return state.parsedCount;
}

void Q3ShaderDatabase::LoadFromAssets(const EditorAssetSource &assets)
{
    m_defs.clear();
    m_binaryCutoutCache.clear();
    std::vector<EditorAssetCatalogEntry> shaderEntries;
    assets.ListFilesWithPrefixAndExtension("scripts/", ".shader", shaderEntries);

    std::vector<uint8_t> bytes;
    for (const EditorAssetCatalogEntry &entry : shaderEntries)
    {
        if (!assets.ReadFile(entry.path, bytes) || bytes.empty())
        {
            continue;
        }
        ParseScript(reinterpret_cast<const char *>(bytes.data()), bytes.size(), false, &assets);
    }
}

void ComputeMeanUnderlayRgb(const std::vector<uint8_t> &underlayPixels, uint32_t uw, uint32_t uh,
                            float outMean[3])
{
    const size_t uCount = size_t(uw) * size_t(uh);
    outMean[0] = 0.0f;
    outMean[1] = 0.0f;
    outMean[2] = 0.0f;
    for (size_t ui = 0; ui < uCount && (ui * 4u + 2u) < underlayPixels.size(); ui++)
    {
        outMean[0] += float(underlayPixels[ui * 4u + 0u]);
        outMean[1] += float(underlayPixels[ui * 4u + 1u]);
        outMean[2] += float(underlayPixels[ui * 4u + 2u]);
    }
    if (uCount > 0)
    {
        const float inv = 1.0f / float(uCount);
        outMean[0] *= inv;
        outMean[1] *= inv;
        outMean[2] *= inv;
    }
}

void CompositeUnderlayOntoTexture(const Q3ShaderDef &def, const EditorAssetSource *assets,
                                  EditorTexture &tex)
{
    if (!def.compositeUnderlayBase || def.underlayMap.empty() ||
        tex.rgba.size() < size_t(tex.width) * tex.height * 4u)
    {
        return;
    }
    std::vector<uint8_t> underlayPixels;
    uint32_t uw = 0;
    uint32_t uh = 0;
    std::string resolved;
    if (!LoadQ3ImageRgba(assets, def.underlayMap, underlayPixels, uw, uh, resolved) || uw == 0 ||
        uh == 0)
    {
        return;
    }

    float meanUnderlay[3] = {};
    ComputeMeanUnderlayRgb(underlayPixels, uw, uh, meanUnderlay);
    const float sx = def.underlayScale[0] != 0.0f ? def.underlayScale[0] : 1.0f;
    const float sy = def.underlayScale[1] != 0.0f ? def.underlayScale[1] : 1.0f;

    for (uint32_t y = 0; y < tex.height; y++)
    {
        const float vf = (float(y) + 0.5f) / float(tex.height) * sy;
        const uint32_t uy = std::min(uh - 1u, uint32_t(std::max(0.0f, (vf - std::floor(vf)) * float(uh))));
        for (uint32_t x = 0; x < tex.width; x++)
        {
            const float uf = (float(x) + 0.5f) / float(tex.width) * sx;
            const uint32_t ux =
                std::min(uw - 1u, uint32_t(std::max(0.0f, (uf - std::floor(uf)) * float(uw))));
            const size_t idx = size_t(y) * size_t(tex.width) + size_t(x);
            const size_t uidx = size_t(uy) * size_t(uw) + size_t(ux);
            const float a = float(tex.rgba[idx * 4u + 3u]) / 255.0f;
            float bg[3] = {
                float(underlayPixels[uidx * 4u + 0u]),
                float(underlayPixels[uidx * 4u + 1u]),
                float(underlayPixels[uidx * 4u + 2u]),
            };
            if (def.underlayEmissive && a < 0.99f)
            {
                for (int c = 0; c < 3; c++)
                {
                    bg[c] = std::max(bg[c], std::min(255.0f, meanUnderlay[c] * 1.6f));
                }
            }
            for (int c = 0; c < 3; c++)
            {
                const float fg = float(tex.rgba[idx * 4u + size_t(c)]);
                tex.rgba[idx * 4u + size_t(c)] =
                    uint8_t(std::clamp(std::round(a * fg + (1.0f - a) * bg[c]), 0.0f, 255.0f));
            }
            tex.rgba[idx * 4u + 3u] = 255u;
        }
    }
}

void ApplyDecalOverlaysToTexture(const Q3ShaderDef &def, const EditorAssetSource *assets,
                                 EditorTexture &tex)
{
    if (def.decalOverlayMaps.empty() || tex.rgba.size() < size_t(tex.width) * tex.height * 4u)
    {
        return;
    }
    for (size_t di = 0; di < def.decalOverlayMaps.size(); di++)
    {
        std::vector<uint8_t> decalPixels;
        uint32_t dw = 0;
        uint32_t dh = 0;
        std::string resolved;
        if (!LoadQ3ImageRgba(assets, def.decalOverlayMaps[di], decalPixels, dw, dh, resolved) ||
            dw == 0 || dh == 0)
        {
            continue;
        }
        const bool invAlpha =
            di < def.decalOverlayInvertedAlpha.size() && def.decalOverlayInvertedAlpha[di];
        for (uint32_t y = 0; y < tex.height; y++)
        {
            const uint32_t dy = std::min(dh - 1u, y * dh / tex.height);
            for (uint32_t x = 0; x < tex.width; x++)
            {
                const uint32_t dx = std::min(dw - 1u, x * dw / tex.width);
                const size_t idx = size_t(y) * size_t(tex.width) + size_t(x);
                const size_t didx = size_t(dy) * size_t(dw) + size_t(dx);
                const float da = float(decalPixels[didx * 4u + 3u]) / 255.0f;
                const float coverage = std::clamp(invAlpha ? (1.0f - da) : da, 0.0f, 1.0f);
                for (int c = 0; c < 3; c++)
                {
                    const float fg = float(decalPixels[didx * 4u + size_t(c)]);
                    const float bg = float(tex.rgba[idx * 4u + size_t(c)]);
                    tex.rgba[idx * 4u + size_t(c)] = uint8_t(std::clamp(
                        std::round(coverage * fg + (1.0f - coverage) * bg), 0.0f, 255.0f));
                }
                float dstAlpha = float(tex.rgba[idx * 4u + 3u]) / 255.0f;
                dstAlpha = def.metallicEnvInvertedAlpha ? dstAlpha * (1.0f - coverage)
                                                        : std::max(dstAlpha, coverage);
                tex.rgba[idx * 4u + 3u] =
                    uint8_t(std::clamp(std::round(dstAlpha * 255.0f), 0.0f, 255.0f));
            }
        }
    }
}

void ApplySurfaceMaterialMasks(const Q3ShaderDef &def, EditorTexture &tex)
{
    const size_t pixels = size_t(tex.width) * size_t(tex.height);
    if (pixels == 0 || tex.rgba.size() < pixels * 4u)
    {
        return;
    }
    if (def.metallicEnvBase)
    {
        tex.isMetallicEnv = true;
        tex.roughnessMask.resize(pixels);
        tex.metallicMask.resize(pixels);
        for (size_t i = 0; i < pixels; i++)
        {
            const float rawA = float(tex.rgba[i * 4u + 3u]) / 255.0f;
            const float envMask = def.metallicEnvInvertedAlpha ? rawA : (1.0f - rawA);
            tex.roughnessMask[i] =
                uint8_t(std::clamp(0.32f - 0.24f * envMask, 0.08f, 0.32f) * 255.0f);
            tex.metallicMask[i] =
                uint8_t(std::clamp(0.25f + 0.70f * envMask, 0.0f, 1.0f) * 255.0f);
            const float chromeLift = envMask * 0.5f;
            for (int c = 0; c < 3; c++)
            {
                const float v = float(tex.rgba[i * 4u + size_t(c)]);
                tex.rgba[i * 4u + size_t(c)] =
                    uint8_t(std::clamp(v + (215.0f - v) * chromeLift, 0.0f, 255.0f));
            }
            tex.rgba[i * 4u + 3u] = 255u;
        }
    }
    else if (def.wetGlossAlphaMask)
    {
        tex.isWetGloss = true;
        tex.roughnessMask.resize(pixels);
        for (size_t i = 0; i < pixels; i++)
        {
            const float wetMask = 1.0f - (float(tex.rgba[i * 4u + 3u]) / 255.0f);
            tex.roughnessMask[i] =
                uint8_t(std::clamp(0.78f - 1.20f * wetMask, 0.14f, 0.78f) * 255.0f);
            tex.rgba[i * 4u + 3u] = 255u;
        }
    }
    else if (def.cutoutFrameUnderlay)
    {
        if (def.baseStageIndex < def.stages.size() && def.stages[def.baseStageIndex].alphaBlend)
        {
            for (size_t i = 0; i < pixels; i++)
            {
                const uint8_t a = tex.rgba[i * 4u + 3u];
                tex.rgba[i * 4u + 3u] =
                    (a >= 245u) ? 255u : uint8_t(float(a) * (110.0f / 245.0f));
            }
        }
    }
    else if (def.tracedBase && !def.alphaTested && !def.isTrans && !def.isNonTracedOverlay)
    {
        for (size_t i = 0; i < pixels; i++)
        {
            tex.rgba[i * 4u + 3u] = 255u;
        }
    }
}

void AssignNormalizedSurfaceLightColor(const double sum[3], EditorTexture &tex)
{
    if (tex.hasSurfaceLightColor)
    {
        return;
    }
    const double peak = std::max({ sum[0], sum[1], sum[2] });
    if (peak <= 1.0e-4)
    {
        return;
    }
    tex.hasSurfaceLightColor = true;
    for (int c = 0; c < 3; c++)
    {
        tex.surfaceLightColor[c] = float(sum[c] / peak);
    }
}

bool HasAnimatedEmissiveOverlayStage(const Q3ShaderDef &def)
{
    for (size_t i = 0; i < def.stages.size(); ++i)
    {
        if (def.tracedBase && i == def.baseStageIndex)
        {
            continue;
        }
        const Q3ShaderStage &stage = def.stages[i];
        if (stage.isLightmap || stage.isWhite || stage.isEnvironment)
        {
            continue;
        }
        const bool isUnderlay = def.cutoutFrameUnderlay && i < def.baseStageIndex;
        if (!stage.additive && !isUnderlay)
        {
            continue;
        }
        if (stage.animated || !stage.animationMaps.empty() ||
            stage.rgbWave != Q3ShaderWaveType::None ||
            stage.alphaWave != Q3ShaderWaveType::None || !stage.tcMods.empty())
        {
            return true;
        }
    }
    return false;
}

bool TryBakeExplicitEmissionMask(const Q3ShaderDef &def, const EditorAssetSource *assets,
                                 EditorTexture &tex)
{
    std::vector<uint8_t> maskRgba;
    uint32_t mw = 0;
    uint32_t mh = 0;
    std::string resolvedMask;
    if (def.emissionMaskMap.empty() ||
        !LoadQ3ImageRgba(assets, def.emissionMaskMap, maskRgba, mw, mh, resolvedMask) ||
        mw == 0 || mh == 0)
    {
        return false;
    }

    const bool bakeIntoBaseAlbedo =
        def.tracedBase && !def.cutoutFrameUnderlay && !HasAnimatedEmissiveOverlayStage(def);
    double sum[3] = { 0.0, 0.0, 0.0 };
    for (uint32_t y = 0; y < tex.height; y++)
    {
        const uint32_t my = std::min(mh - 1u, y * mh / tex.height);
        for (uint32_t x = 0; x < tex.width; x++)
        {
            const uint32_t mx = std::min(mw - 1u, x * mw / tex.width);
            const size_t srcPx = (size_t(my) * mw + mx) * 4u;
            const uint8_t m =
                std::max({ maskRgba[srcPx + 0], maskRgba[srcPx + 1], maskRgba[srcPx + 2] });
            const size_t dstIdx = size_t(y) * tex.width + x;
            tex.fullbright[dstIdx] = m;
            if (bakeIntoBaseAlbedo && m > 0u)
            {
                for (int c = 0; c < 3; c++)
                {
                    const int combined =
                        int(tex.rgba[dstIdx * 4u + size_t(c)]) + int(maskRgba[srcPx + size_t(c)]);
                    tex.rgba[dstIdx * 4u + size_t(c)] = uint8_t(std::min(255, combined));
                }
            }
            const double w = double(m) / 255.0;
            sum[0] += double(maskRgba[srcPx + 0]) * w;
            sum[1] += double(maskRgba[srcPx + 1]) * w;
            sum[2] += double(maskRgba[srcPx + 2]) * w;
        }
    }
    AssignNormalizedSurfaceLightColor(sum, tex);
    return true;
}

void CompositeStageBufferPixel(const Q3ShaderStage &stage, const uint8_t *srcPx, float waveScale,
                                bool hasContributed, std::vector<float> &accumRgba, size_t dstOff)
{
    const float sr = float(srcPx[0]) * waveScale;
    const float sg = float(srcPx[1]) * waveScale;
    const float sb = float(srcPx[2]) * waveScale;
    const float sa = float(srcPx[3]) / 255.0f;

    if (stage.additive)
    {
        accumRgba[dstOff + 0u] = std::min(255.0f, accumRgba[dstOff + 0u] + sr);
        accumRgba[dstOff + 1u] = std::min(255.0f, accumRgba[dstOff + 1u] + sg);
        accumRgba[dstOff + 2u] = std::min(255.0f, accumRgba[dstOff + 2u] + sb);
        const float peakAlpha = std::max({ sr, sg, sb });
        accumRgba[dstOff + 3u] = std::min(255.0f, std::max(accumRgba[dstOff + 3u], peakAlpha));
    }
    else if (stage.alphaBlend)
    {
        const float cov = std::clamp(stage.invertedAlphaBlend ? (1.0f - sa) : sa, 0.0f, 1.0f);
        accumRgba[dstOff + 0u] = cov * sr + (1.0f - cov) * accumRgba[dstOff + 0u];
        accumRgba[dstOff + 1u] = cov * sg + (1.0f - cov) * accumRgba[dstOff + 1u];
        accumRgba[dstOff + 2u] = cov * sb + (1.0f - cov) * accumRgba[dstOff + 2u];
        accumRgba[dstOff + 3u] = std::min(255.0f, std::max(accumRgba[dstOff + 3u], float(srcPx[3])));
    }
    else if (stage.filter && hasContributed)
    {
        accumRgba[dstOff + 0u] = (accumRgba[dstOff + 0u] * float(srcPx[0])) / 255.0f;
        accumRgba[dstOff + 1u] = (accumRgba[dstOff + 1u] * float(srcPx[1])) / 255.0f;
        accumRgba[dstOff + 2u] = (accumRgba[dstOff + 2u] * float(srcPx[2])) / 255.0f;
    }
    else if (!stage.blended || stage.filter)
    {
        accumRgba[dstOff + 0u] = sr;
        accumRgba[dstOff + 1u] = sg;
        accumRgba[dstOff + 2u] = sb;
        accumRgba[dstOff + 3u] = 255.0f;
    }
}

void CompositeSingleOverlayStage(const Q3ShaderStage &stage, const EditorAssetSource *assets,
                                 uint32_t dstW, uint32_t dstH, std::vector<float> &accumRgba,
                                 bool &hasContributed)
{
    const std::string stageMap =
        !stage.map.empty() ? stage.map
                           : (!stage.animationMaps.empty() ? stage.animationMaps[0] : std::string{});
    if (stage.isLightmap || stage.isWhite || stage.isEnvironment || stageMap.empty())
    {
        return;
    }

    std::vector<uint8_t> stageRgba;
    uint32_t sw = 0;
    uint32_t sh = 0;
    std::string resolved;
    if (!LoadQ3ImageRgba(assets, stageMap, stageRgba, sw, sh, resolved) || sw == 0 || sh == 0)
    {
        return;
    }

    const float waveScale =
        stage.rgbWave == Q3ShaderWaveType::None
            ? 1.0f
            : std::clamp(stage.rgbWaveBase + 0.5f * std::fabs(stage.rgbWaveAmplitude), 0.35f, 1.0f);
    const float sx = stage.scale[0] != 0.0f ? stage.scale[0] : 1.0f;
    const float sy = stage.scale[1] != 0.0f ? stage.scale[1] : 1.0f;

    for (uint32_t y = 0; y < dstH; y++)
    {
        const float vf = (float(y) + 0.5f) / float(dstH) * sy;
        const uint32_t py = std::min(sh - 1u, uint32_t(std::max(0.0f, (vf - std::floor(vf)) * float(sh))));
        for (uint32_t x = 0; x < dstW; x++)
        {
            const float uf = (float(x) + 0.5f) / float(dstW) * sx;
            const uint32_t px =
                std::min(sw - 1u, uint32_t(std::max(0.0f, (uf - std::floor(uf)) * float(sw))));
            const size_t srcOff = (size_t(py) * size_t(sw) + size_t(px)) * 4u;
            const size_t dstOff = (size_t(y) * size_t(dstW) + size_t(x)) * 4u;
            CompositeStageBufferPixel(stage, stageRgba.data() + srcOff, waveScale, hasContributed,
                                      accumRgba, dstOff);
        }
    }
    hasContributed = true;
}

void CompositeNonTracedOverlayStagesToTexture(const Q3ShaderDef &def,
                                              const EditorAssetSource *assets, EditorTexture &tex)
{
    if (!def.isNonTracedOverlay || def.stages.size() <= 1u || tex.width == 0 || tex.height == 0)
    {
        return;
    }

    const size_t pixels = size_t(tex.width) * size_t(tex.height);
    std::vector<float> accumRgba(pixels * 4u, 0.0f);
    bool hasContributed = false;

    for (const Q3ShaderStage &stage : def.stages)
    {
        CompositeSingleOverlayStage(stage, assets, tex.width, tex.height, accumRgba, hasContributed);
    }

    if (!hasContributed)
    {
        return;
    }

    tex.rgba.resize(pixels * 4u);
    for (size_t i = 0; i < pixels; i++)
    {
        tex.rgba[i * 4u + 0u] = uint8_t(std::clamp(std::round(accumRgba[i * 4u + 0u]), 0.0f, 255.0f));
        tex.rgba[i * 4u + 1u] = uint8_t(std::clamp(std::round(accumRgba[i * 4u + 1u]), 0.0f, 255.0f));
        tex.rgba[i * 4u + 2u] = uint8_t(std::clamp(std::round(accumRgba[i * 4u + 2u]), 0.0f, 255.0f));
        tex.rgba[i * 4u + 3u] = 255u;
    }
}

void ApplyLightImageColorFallback(const Q3ShaderDef &def, const EditorAssetSource *assets,
                                  EditorTexture &tex)
{
    if (tex.hasSurfaceLightColor || def.lightImage.empty())
    {
        return;
    }

    std::vector<uint8_t> lightRgba;
    uint32_t lw = 0;
    uint32_t lh = 0;
    std::string resolvedLight;
    if (!LoadQ3ImageRgba(assets, def.lightImage, lightRgba, lw, lh, resolvedLight) ||
        lw == 0 || lh == 0)
    {
        return;
    }

    double sum[3] = { 0.0, 0.0, 0.0 };
    for (size_t i = 0; i + 3u < lightRgba.size(); i += 4u)
    {
        sum[0] += lightRgba[i + 0u];
        sum[1] += lightRgba[i + 1u];
        sum[2] += lightRgba[i + 2u];
    }
    AssignNormalizedSurfaceLightColor(sum, tex);
}

void ApplySurfaceEmissionAndMask(const Q3ShaderDef &def, const EditorAssetSource *assets,
                                 EditorTexture &tex)
{
    if (!def.emissionMaskMap.empty())
    {
        tex.defaultEmissiveTexture = StripQ3ImageExtension(NormalizeQ3AssetPath(def.emissionMaskMap));
    }
    if (def.surfaceLight <= 0.0f)
    {
        if (!def.emissionMaskMap.empty() && def.tracedBase)
        {
            tex.fullbright.assign(size_t(tex.width) * size_t(tex.height), 0u);
            TryBakeExplicitEmissionMask(def, assets, tex);
        }
        return;
    }

    const size_t pixels = size_t(tex.width) * size_t(tex.height);
    tex.isEmissiveShader = true;
    tex.surfaceLightValue = def.surfaceLight;
    if (def.hasLightRGB)
    {
        tex.hasSurfaceLightColor = true;
        const float peak = std::max({ def.lightRGB[0], def.lightRGB[1], def.lightRGB[2] });
        const float invPeak = peak > 1.0e-4f ? (1.0f / peak) : 1.0f;
        for (int c = 0; c < 3; c++)
        {
            tex.surfaceLightColor[c] = def.lightRGB[c] * invPeak;
        }
    }

    tex.fullbright.assign(pixels, 0u);
    if (TryBakeExplicitEmissionMask(def, assets, tex))
    {
        return;
    }

    ApplyLightImageColorFallback(def, assets, tex);

    for (size_t i = 0; i < pixels && i * 4u + 2u < tex.rgba.size(); i++)
    {
        const float peak =
            std::max({ tex.rgba[i * 4u + 0], tex.rgba[i * 4u + 1], tex.rgba[i * 4u + 2] }) /
            255.0f;
        tex.fullbright[i] = uint8_t(std::clamp(peak * peak * 255.0f, 0.0f, 255.0f));
    }

    if (tex.defaultEmissiveTexture.empty())
    {
        const bool hasUsableDefMap =
            !def.map.empty() && def.map[0] != '$' && !IsWhiteMapName(def.map);
        const std::string fallbackMap = hasUsableDefMap ? def.map : tex.name;
        if (!fallbackMap.empty())
        {
            tex.defaultEmissiveTexture = StripQ3ImageExtension(NormalizeQ3AssetPath(fallbackMap));
        }
    }
}

void PopulateEditorTextureShaderWires(const Q3ShaderDef &def, EditorTexture &tex)
{
    tex.shaderStages = def.stages;
    tex.baseStageIndex = def.baseStageIndex;
    tex.isNonTracedOverlay = def.isNonTracedOverlay;
    tex.twoSided = def.twoSided;
    tex.autoSprite = def.autoSprite;
    tex.autoSprite2 = def.autoSprite2;
    tex.deformWave = def.deformWave;
    tex.deformFrequency = def.deformFrequency;
    tex.deformAmplitude = def.deformAmplitude;
    if (!def.map.empty())
    {
        tex.defaultBaseTexture = StripQ3ImageExtension(NormalizeQ3AssetPath(def.map));
    }
    if (!def.decalOverlayMaps.empty())
    {
        tex.defaultOverlayTexture =
            StripQ3ImageExtension(NormalizeQ3AssetPath(def.decalOverlayMaps[0]));
    }
    else if (!def.underlayMap.empty())
    {
        tex.defaultOverlayTexture = StripQ3ImageExtension(NormalizeQ3AssetPath(def.underlayMap));
    }
}

void ApplyQ3ShaderToEditorTexture(const Q3ShaderDef &def, const EditorAssetSource *assets,
                                  bool isSky, EditorTexture &tex)
{
    if (isSky || def.isSky || tex.width == 0 || tex.height == 0)
    {
        return;
    }

    tex.isCutoutUnderlay = def.cutoutFrameUnderlay;
    PopulateEditorTextureShaderWires(def, tex);
    CompositeUnderlayOntoTexture(def, assets, tex);
    CompositeNonTracedOverlayStagesToTexture(def, assets, tex);
    ApplyDecalOverlaysToTexture(def, assets, tex);
    ApplySurfaceMaterialMasks(def, tex);
    ApplySurfaceEmissionAndMask(def, assets, tex);
}

const Q3ShaderDef *Q3ShaderDatabase::Find(std::string_view name) const
{
    const std::string normalized = NormalizeQ3AssetPath(name);
    if (normalized.empty())
    {
        return nullptr;
    }

    const auto direct = m_defs.find(normalized);
    if (direct != m_defs.end())
    {
        return &direct->second;
    }

    const std::string stripped = StripQ3ImageExtension(normalized);
    if (stripped != normalized)
    {
        const auto noExt = m_defs.find(stripped);
        if (noExt != m_defs.end())
        {
            return &noExt->second;
        }
    }

    return nullptr;
}

std::unordered_map<std::string, std::string> ParseQ3SkinBytes(const std::vector<uint8_t> &bytes)
{
    std::unordered_map<std::string, std::string> result;
    const std::string text(bytes.begin(), bytes.end());
    size_t pos = 0;

    while (pos < text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;

        const size_t comment = line.find("//");
        if (comment != std::string::npos)
        {
            line.resize(comment);
        }

        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        {
            line.pop_back();
        }

        const size_t comma = line.find(',');
        if (comma == std::string::npos)
        {
            continue;
        }

        const std::string surface = NormalizeQ3AssetPath(line.substr(0, comma));
        std::string shader = line.substr(comma + 1);
        while (!shader.empty() && (shader.front() == ' ' || shader.front() == '\t'))
        {
            shader.erase(shader.begin());
        }
        shader = NormalizeQ3AssetPath(shader);

        if (surface.empty() || StartsWithAsciiIgnoreCase(surface, "tag_") || shader.empty())
        {
            continue;
        }

        result[surface] = shader;
    }

    return result;
}

bool LoadQ3ImageRgba(const EditorAssetSource *assets, const std::string &requestedPath,
                     std::vector<uint8_t> &outRgba, uint32_t &outWidth, uint32_t &outHeight,
                     std::string &outResolvedPath)
{
    outRgba.clear();
    outWidth = 0;
    outHeight = 0;
    outResolvedPath.clear();

    const std::string normalized = NormalizeQ3AssetPath(requestedPath);
    if (normalized.empty() || normalized == "*white" || normalized == "$whiteimage")
    {
        if (normalized == "*white" || normalized == "$whiteimage")
        {
            outWidth = 1;
            outHeight = 1;
            outRgba = { 255, 255, 255, 255 };
            outResolvedPath = normalized;
            return true;
        }
        return false;
    }

    if (assets == nullptr)
    {
        return false;
    }

    const std::string stem = StripQ3ImageExtension(normalized);
    std::vector<std::string> candidates = {
        normalized,
        stem + ".tga",
        stem + ".jpg",
        stem + ".jpeg",
        stem + ".png",
        stem + ".pcx",
        stem + ".bmp",
    };

    return DecodeFromCandidates(*assets, candidates, outRgba, outWidth, outHeight, outResolvedPath);
}
