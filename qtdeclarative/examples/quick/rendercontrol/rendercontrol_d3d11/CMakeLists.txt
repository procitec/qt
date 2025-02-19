# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

cmake_minimum_required(VERSION 3.16)
project(rendercontrol_d3d11 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(rendercontrol_d3d11example WIN32 MACOSX_BUNDLE
    engine.cpp engine.h
    main.cpp
    window.cpp window.h
)

target_link_libraries(rendercontrol_d3d11example PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    d3d11
    dxgi
    dxguid
    uuid
)

# Resources:
qt_add_qml_module(rendercontrol_d3d11example
    URI rendercontrol
    QML_FILES
        "demo.qml"
)

install(TARGETS rendercontrol_d3d11example
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_qml_app_script(
    TARGET rendercontrol_d3d11example
    OUTPUT_SCRIPT deploy_script
    MACOS_BUNDLE_POST_BUILD
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
)
install(SCRIPT ${deploy_script})
