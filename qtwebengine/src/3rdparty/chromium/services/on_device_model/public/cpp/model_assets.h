// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_ON_DEVICE_MODEL_PUBLIC_CPP_MODEL_ASSETS_H_
#define SERVICES_ON_DEVICE_MODEL_PUBLIC_CPP_MODEL_ASSETS_H_

#include "base/component_export.h"
#include "base/files/file.h"
#include "base/files/file_path.h"

namespace on_device_model {

// A bundle of file paths to use for execution.
struct COMPONENT_EXPORT(ON_DEVICE_MODEL_ASSETS_CPP) ModelAssetPaths {
  ModelAssetPaths();
  ModelAssetPaths(const ModelAssetPaths&);
  ~ModelAssetPaths();

  // Returns whether the required models to determine text safety are set.
  bool HasSafetyFiles() const {
    return !ts_data.empty() && !ts_sp_model.empty();
  }

  base::FilePath sp_model;
  base::FilePath model;
  base::FilePath weights;
  base::FilePath ts_data;
  base::FilePath ts_sp_model;
  base::FilePath language_detection_model;
};

// A bundle of opened file assets comprising model description to use for
// execution.
struct COMPONENT_EXPORT(ON_DEVICE_MODEL_ASSETS_CPP) ModelAssets {
  ModelAssets();
  ModelAssets(ModelAssets&&);
  ModelAssets& operator=(ModelAssets&&);
  ~ModelAssets();

  base::File sp_model;
  base::File model;
  base::File weights;
  base::File ts_data;
  base::File ts_sp_model;
  base::File language_detection_model;
};

// Helper to open files for ModelAssets given their containing paths.
COMPONENT_EXPORT(ON_DEVICE_MODEL_ASSETS_CPP)
ModelAssets LoadModelAssets(const ModelAssetPaths& paths);

}  // namespace on_device_model

#endif  //  SERVICES_ON_DEVICE_MODEL_PUBLIC_CPP_MODEL_ASSETS_H_
