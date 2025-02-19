// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

import QtQuick
import QtQuick3D
import QtQuick3D.Xr

XrController {
    handInput.poseSpace: XrHandInput.AimPose
    controller: XrController.ControllerLeft
    ActionMapper {}
    Lazer {
        enableBeam: true
    }
}
