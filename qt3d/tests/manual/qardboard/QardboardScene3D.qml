// Copyright (C) 2017 Klaralvdalens Datakonsult AB (KDAB).
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

import QtQuick 2.0
import QtQuick.Scene3D 2.0

import Qt3D.Core 2.0
import Qt3D.Render 2.0
import Qt3D.Input 2.0
import Qt3D.Extras 2.0

Scene3D {
    id: scene3d
    focus: true
    aspects: ["input", "logic"]
    cameraAspectRatioMode: Scene3D.UserAspectRatio
    multisample: true

    QardboardRootEntity {
    }
}
