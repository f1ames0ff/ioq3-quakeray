#include "Q3EditorAdapter.h"

#include "Assets.h"
#include "Q3Bsp.h"
#include "Q3Model.h"
#include "Q3Shader.h"
#include "Scene.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace
{

bool EndsWith(const std::string &value, const char *suffix, size_t suffixLen)
{
    return value.size() >= suffixLen &&
           value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
}

std::string TreeDirOf(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string CleanRelativeAssetPath(const std::string &raw)
{
    std::string out = raw;
    for (char &ch : out)
    {
        if (ch == '\\')
        {
            ch = '/';
        }
    }
    while (!out.empty() && out.front() == '/')
    {
        out.erase(out.begin());
    }
    while (out.size() >= 2 && out[0] == '.' && out[1] == '/')
    {
        out.erase(0, 2);
    }
    return out;
}

bool CanLoadQ3PreviewModel(const std::string &modelPath, const EditorAssetSource &assets)
{
    const std::string lowered = NormalizeEditorAssetPath(modelPath);
    if (EndsWith(lowered, ".md3", 4) || EndsWith(lowered, ".character", 10))
    {
        return true;
    }
    return assets.Exists(modelPath + "/lower.md3") || assets.Exists(modelPath + "/upper.md3") ||
           assets.Exists(modelPath + "/head.md3");
}

bool ShouldHideQ3CatalogModelEntry(const std::string &lowerPath)
{
    return EndsWith(lowerPath, "_hand.md3", 9) || EndsWith(lowerPath, "_1.md3", 6) ||
           EndsWith(lowerPath, "_2.md3", 6);
}

void PostProcessQ3ModelCatalog(std::vector<EditorAssetCatalogEntry> &entries)
{
    struct CharParts
    {
        bool hasLower = false;
        bool hasUpper = false;
        bool hasHead = false;
        std::string rawDir;
        std::string source;
    };

    std::unordered_set<std::string> seen;
    seen.reserve(entries.size());
    std::unordered_map<std::string, CharParts> dirs;

    for (const EditorAssetCatalogEntry &entry : entries)
    {
        const std::string cleanRaw = CleanRelativeAssetPath(entry.path);
        const std::string lowerPath = NormalizeEditorAssetPath(cleanRaw);
        seen.insert(lowerPath);
        const size_t slash = lowerPath.rfind('/');
        if (slash == std::string::npos || slash == 0)
        {
            continue;
        }
        const std::string leaf = lowerPath.substr(slash + 1);
        const std::string dirLower = lowerPath.substr(0, slash);
        CharParts &parts = dirs[dirLower];
        if (parts.rawDir.empty())
        {
            parts.rawDir = cleanRaw.substr(0, slash);
            parts.source = entry.source;
        }
        if (leaf == "lower.md3")
        {
            parts.hasLower = true;
        }
        else if (leaf == "upper.md3")
        {
            parts.hasUpper = true;
        }
        else if (leaf == "head.md3")
        {
            parts.hasHead = true;
        }
    }

    for (const auto &[dirLower, parts] : dirs)
    {
        (void)dirLower;
        if ((int(parts.hasLower) + int(parts.hasUpper) + int(parts.hasHead)) < 2)
        {
            continue;
        }
        const std::string virtualPath = parts.rawDir + ".character";
        const std::string key = NormalizeEditorAssetPath(virtualPath);
        if (seen.insert(key).second)
        {
            entries.push_back(
                EditorAssetCatalogEntry{ virtualPath, parts.source, TreeDirOf(virtualPath) });
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const EditorAssetCatalogEntry &a, const EditorAssetCatalogEntry &b) {
                  const std::string aLower = NormalizeEditorAssetPath(a.path);
                  const std::string bLower = NormalizeEditorAssetPath(b.path);
                  const bool aChar = EndsWith(aLower, ".character", 10);
                  const bool bChar = EndsWith(bLower, ".character", 10);
                  if (aChar != bChar)
                  {
                      return aChar;
                  }
                  return aLower < bLower;
              });
}

bool ResolveQ3ModelMaterialPrefix(const std::string &lowerModelPath, std::string &outMaterialPrefix)
{
    if (EndsWith(lowerModelPath, ".character", 10))
    {
        outMaterialPrefix = lowerModelPath.substr(0, lowerModelPath.size() - 10) + "/";
        return true;
    }
    if (EndsWith(lowerModelPath, ".md3", 4))
    {
        outMaterialPrefix = lowerModelPath.substr(0, lowerModelPath.size() - 4);
        return true;
    }
    return false;
}

int32_t FindDefaultQ3PreviewModelIndex(const std::vector<EditorAssetCatalogEntry> &catalog)
{
    int32_t firstCharacter = -1;
    for (size_t i = 0; i < catalog.size(); i++)
    {
        const std::string lower = NormalizeEditorAssetPath(catalog[i].path);
        if (lower == "models/players/sarge.character")
        {
            return int32_t(i);
        }
        if (firstCharacter < 0 && EndsWith(lower, ".character", 10))
        {
            firstCharacter = int32_t(i);
        }
    }
    return firstCharacter;
}

bool EqualsIgnoreCaseAscii(std::string_view a, std::string_view b)
{
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char ca, char cb) {
               return std::tolower(static_cast<unsigned char>(ca)) ==
                      std::tolower(static_cast<unsigned char>(cb));
           });
}

std::string NormalizeAbsoluteQ3Path(std::filesystem::path path)
{
    if (path.empty())
    {
        return {};
    }
    std::error_code ec;
    if (path.is_relative())
    {
        path = std::filesystem::absolute(path, ec);
    }
    path = path.lexically_normal();
    std::string out = path.string();
    while (out.size() > 1u && (out.back() == '/' || out.back() == '\\'))
    {
        if (out.size() == 3u && out[1] == ':')
        {
            break;
        }
        out.pop_back();
    }
    return out;
}

void ResolveQ3BaseAndMod(const EditorPlayLevelRequest &request, std::string &outBasePath,
                         std::string &outModName)
{
    outBasePath.clear();
    outModName.clear();
    std::error_code ec;

    std::filesystem::path gameDir(NormalizeAbsoluteQ3Path(request.gameDir));
    if (gameDir.empty() && !request.scenePath.empty())
    {
        const std::filesystem::path sceneParent =
            std::filesystem::path(NormalizeAbsoluteQ3Path(request.scenePath)).parent_path();
        if (EqualsIgnoreCaseAscii(sceneParent.filename().string(), "maps"))
        {
            gameDir = sceneParent.parent_path();
        }
    }
    if (gameDir.empty())
    {
        return;
    }

    if (std::filesystem::is_directory(gameDir / "baseq3", ec))
    {
        outBasePath = gameDir.string();
        outModName = "baseq3";
        return;
    }

    const std::filesystem::path parent = gameDir.parent_path();
    if (!parent.empty())
    {
        outBasePath = parent.string();
        outModName = gameDir.filename().string();
    }
}

bool IsQ3ExecutableFile(const std::filesystem::path &path)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
    {
        return false;
    }
#if !defined(_WIN32)
    return access(path.c_str(), X_OK) == 0;
#else
    return true;
#endif
}

std::string FindQ3ExecutableInDir(const std::filesystem::path &dir, bool requireRenderer)
{
    if (dir.empty())
    {
        return {};
    }
#if defined(_WIN32)
    static constexpr const char *kRendererName = "renderer_qray.dll";
    static constexpr const char *kExeNames[] = { "ioquake3.exe", "ioquake3.x86_64.exe" };
#else
    static constexpr const char *kRendererName = "renderer_qray.so";
    static constexpr const char *kExeNames[] = { "ioquake3", "ioquake3.x86_64" };
#endif
    std::error_code ec;
    if (requireRenderer && !std::filesystem::exists(dir / kRendererName, ec))
    {
        return {};
    }
    for (const char *name : kExeNames)
    {
        const std::filesystem::path cand = dir / name;
        if (IsQ3ExecutableFile(cand))
        {
            return NormalizeAbsoluteQ3Path(cand);
        }
    }
    return {};
}

std::string FindQ3GameExecutable(const EditorPlayLevelRequest &request, const std::string &basePath)
{
    if (!request.gameExecutable.empty())
    {
        return IsQ3ExecutableFile(request.gameExecutable)
                   ? NormalizeAbsoluteQ3Path(request.gameExecutable)
                   : std::string();
    }
    if (const char *envExe = std::getenv("QRAY_GAME_EXE");
        envExe != nullptr && envExe[0] != '\0' && IsQ3ExecutableFile(envExe))
    {
        return NormalizeAbsoluteQ3Path(envExe);
    }

    std::vector<std::filesystem::path> dirs;
    if (!request.executableDir.empty())
    {
        const std::filesystem::path exeDir(request.executableDir);
        const std::filesystem::path p1 = exeDir.parent_path();
        const std::filesystem::path p2 = p1.parent_path();
        dirs.push_back(exeDir);
        for (const char *cfg : { "Release", "RelWithDebInfo", "Debug" })
        {
            dirs.push_back(p2 / cfg);
            dirs.push_back(p1 / cfg);
        }
        dirs.push_back(p2);
        dirs.push_back(p1);
    }
    if (!basePath.empty())
    {
        dirs.emplace_back(basePath);
    }

    for (const bool requireRenderer : { true, false })
    {
        for (const std::filesystem::path &dir : dirs)
        {
            const std::string found = FindQ3ExecutableInDir(dir, requireRenderer);
            if (!found.empty())
            {
                return found;
            }
        }
    }
    return {};
}

void AppendQ3SetArg(std::vector<std::string> &args, const char *name, const std::string &value)
{
    args.emplace_back("+set");
    args.emplace_back(name);
    args.push_back(value);
}

void AppendQ3RuntimeAssetArgs(const EditorPlayLevelRequest &request, std::vector<std::string> &args)
{
    std::error_code ec;
    if (!request.shaderFolder.empty() && std::filesystem::exists(request.shaderFolder, ec))
    {
        std::string shaderDir = NormalizeAbsoluteQ3Path(request.shaderFolder);
        if (!shaderDir.empty() && shaderDir.back() != '/' && shaderDir.back() != '\\')
        {
            shaderDir.push_back('/');
        }
        AppendQ3SetArg(args, "qray_shaderDir", shaderDir);
    }
    if (!request.blueNoisePath.empty() && std::filesystem::exists(request.blueNoisePath, ec))
    {
        AppendQ3SetArg(args, "qray_blueNoise", NormalizeAbsoluteQ3Path(request.blueNoisePath));
    }
    if (!request.waterNormalPath.empty() && std::filesystem::exists(request.waterNormalPath, ec))
    {
        AppendQ3SetArg(args, "qray_waterNormal", NormalizeAbsoluteQ3Path(request.waterNormalPath));
    }
}

void AppendExistingQ3FileCvar(const std::filesystem::path &candidate, const char *cvarName,
                              std::vector<std::string> &args)
{
    std::error_code ec;
    if (!candidate.empty() && std::filesystem::is_regular_file(candidate, ec))
    {
        AppendQ3SetArg(args, cvarName, NormalizeAbsoluteQ3Path(candidate));
    }
}

void AppendQ3AuthoredArgs(const EditorPlayLevelRequest &request, std::vector<std::string> &args)
{
    AppendQ3SetArg(args, "qray_authored", "1");

    const std::filesystem::path gameDir(request.gameDir);
    std::filesystem::path materialsPath(request.materialsPath);
    if (materialsPath.empty() && !gameDir.empty())
    {
        materialsPath = gameDir / "qray.materials.yaml";
    }
    std::filesystem::path lightsPath(request.lightsPath);
    if (lightsPath.empty() && !gameDir.empty())
    {
        lightsPath = gameDir / "qray.lights.yaml";
    }
    std::filesystem::path configPath(request.configPath);
    if (configPath.empty())
    {
        const std::filesystem::path matParent = materialsPath.parent_path();
        if (!matParent.empty())
        {
            configPath = matParent / "config.cfg";
        }
        else if (!gameDir.empty())
        {
            configPath = gameDir / "config.cfg";
        }
    }

    AppendExistingQ3FileCvar(materialsPath, "qray_materialsPath", args);
    AppendExistingQ3FileCvar(lightsPath, "qray_lightsPath", args);
    AppendExistingQ3FileCvar(configPath, "qray_editorConfigPath", args);
}

bool BuildQ3PlayLevelCommand(const EditorPlayLevelRequest &request, EditorLaunchCommand &outCommand,
                             std::string &outError)
{
    outCommand = EditorLaunchCommand{};
    if (request.levelName.empty())
    {
        outError = "no level loaded to launch";
        return false;
    }

    std::string basePath;
    std::string modName;
    ResolveQ3BaseAndMod(request, basePath, modName);

    const std::string exe = FindQ3GameExecutable(request, basePath);
    if (exe.empty())
    {
        outError = "ioquake3 executable not found (pass --game-exe or set QRAY_GAME_EXE)";
        return false;
    }

    outCommand.executable = exe;
    outCommand.workingDirectory = std::filesystem::path(exe).parent_path().string();

    AppendQ3SetArg(outCommand.arguments, "cl_renderer", "qray");
    AppendQ3SetArg(outCommand.arguments, "sv_pure", "0");
    if (!basePath.empty())
    {
        AppendQ3SetArg(outCommand.arguments, "fs_basepath", basePath);
    }
    if (!modName.empty() && !EqualsIgnoreCaseAscii(modName, "baseq3"))
    {
        AppendQ3SetArg(outCommand.arguments, "fs_game", modName);
    }
    AppendQ3RuntimeAssetArgs(request, outCommand.arguments);
    AppendQ3AuthoredArgs(request, outCommand.arguments);
    outCommand.arguments.emplace_back("+devmap");
    outCommand.arguments.push_back(request.levelName);

#if !defined(_WIN32)
    const char *wayland = std::getenv("WAYLAND_DISPLAY");
    const char *sdlDriver = std::getenv("SDL_VIDEODRIVER");
    if (wayland != nullptr && wayland[0] != '\0' && (sdlDriver == nullptr || sdlDriver[0] == '\0'))
    {
        outCommand.envOverrides.emplace_back("SDL_VIDEODRIVER", "x11");
    }
#endif
    return true;
}

struct Q3EditorAdapterAutoRegistrar
{
    Q3EditorAdapterAutoRegistrar() { RegisterQ3EditorFormatAdapter(); }
};

[[maybe_unused]] const Q3EditorAdapterAutoRegistrar g_q3EditorAdapterAutoRegistrar;

} // namespace

void RegisterQ3EditorFormatAdapter()
{
    EditorFormatAdapter adapter;
    adapter.extraArchiveExtensions = { ".pk3" };
    adapter.extraModelExtensions = { ".md3" };
    adapter.extraBaseGameDirs = { "baseq3" };
    adapter.extraModConfigFiles = { "q3config.cfg" };
    adapter.extraModSubdirs = { "scripts", "models" };
    adapter.modDirectoryDescription =
        "Mount a Quake / Quake 3 game or mod folder (id1, baseq3, custom mods).";
    adapter.canLoadBspMemory = &IsQuake3BspMemory;
    adapter.loadBspSceneMemory = &LoadQ3BspSceneMemory;
    adapter.canLoadPreviewModel = &CanLoadQ3PreviewModel;
    adapter.loadPreviewModel = &LoadQ3PreviewModel;
    adapter.shouldHideModelCatalogEntry = &ShouldHideQ3CatalogModelEntry;
    adapter.postProcessModelCatalog = &PostProcessQ3ModelCatalog;
    adapter.resolveModelMaterialPrefix = &ResolveQ3ModelMaterialPrefix;
    adapter.findDefaultPreviewModelIndex = &FindDefaultQ3PreviewModelIndex;
    adapter.loadExternalImageRgba = &LoadQ3ImageRgba;
    adapter.buildPlayLevelCommand = &BuildQ3PlayLevelCommand;
    RegisterEditorFormatAdapter(adapter);
}
