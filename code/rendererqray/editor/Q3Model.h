#pragma once

#include "Scene.h"

bool LoadQ3PreviewModel(const std::string &modelPath, const EditorAssetSource &assets,
                        EditorPreviewModel &outModel, std::string &error);
