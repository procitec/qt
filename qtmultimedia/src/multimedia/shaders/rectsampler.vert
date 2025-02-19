// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#version 440
#extension GL_GOOGLE_include_directive : enable

#include "uniformbuffer.glsl"

layout(location = 0) in vec4 vertexPosition;
layout(location = 1) in vec2 vertexTexCoord;

layout(location = 0) out vec2 texCoord;

out gl_PerVertex { vec4 gl_Position; };

void main() {
    texCoord = (ubuf.colorMatrix * vec4(vertexTexCoord, 0.0, 1.0)).xy;
    gl_Position = ubuf.matrix * vertexPosition;
}
