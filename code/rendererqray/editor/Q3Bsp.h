#pragma once

#include "Scene.h"

bool IsQuake3BspMemory(const std::vector<uint8_t> &bytes);

bool LoadQ3BspSceneMemory(const std::vector<uint8_t> &bytes, const std::string &sourceName,
                          EditorScene &scene, std::string &error,
                          const EditorSceneLoadOptions &options = {});
