# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

# Generated from qblendedclipanimator.pro.

#####################################################################
## tst_qblendedclipanimator Test:
#####################################################################

if(NOT QT_BUILD_STANDALONE_TESTS AND NOT QT_BUILDING_QT)
    cmake_minimum_required(VERSION 3.16)
    project(tst_qblendedclipanimator LANGUAGES CXX)
    find_package(Qt6BuildInternals REQUIRED COMPONENTS STANDALONE_TEST)
endif()

qt_internal_add_test(tst_qblendedclipanimator
    SOURCES
        tst_qblendedclipanimator.cpp
    INCLUDE_DIRECTORIES
        ../../core/common
    LIBRARIES
        Qt::3DAnimation
        Qt::3DAnimationPrivate
        Qt::3DCore
        Qt::3DCorePrivate
        Qt::CorePrivate
        Qt::Gui
)

#### Keys ignored in scope 1:.:.:qblendedclipanimator.pro:<TRUE>:
# TEMPLATE = "app"

## Scopes:
#####################################################################

qt_internal_extend_target(tst_qblendedclipanimator CONDITION QT_FEATURE_private_tests
    SOURCES
        ../../core/common/qbackendnodetester.cpp ../../core/common/qbackendnodetester.h
        ../../core/common/testarbiter.h
)
