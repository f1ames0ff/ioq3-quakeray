#include "Q3EditorAdapter.h"

#include "Assets.h"
#include "Q3Bsp.h"
#include "Q3Model.h"
#include "Q3Shader.h"
#include "Scene.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
    RegisterEditorFormatAdapter(adapter);
}
