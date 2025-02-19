# Copyright (C) 2024 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.16)
project(sidepanel LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)

if(NOT DEFINED INSTALL_EXAMPLESDIR)
    set(INSTALL_EXAMPLESDIR "examples")
endif()

set(INSTALL_EXAMPLEDIR "${INSTALL_EXAMPLESDIR}/multimedia/qml-gstreamer-rtp")

if(NOT QT_BUILD_STANDALONE_TESTS AND NOT QT_BUILDING_QT)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Multimedia Qml Quick)
endif()

qt_add_executable( qml-gstreamer-rtp WIN32 MACOSX_BUNDLE
    qml-gstreamer-rtp.cpp
)

qt_add_qml_module( qml-gstreamer-rtp
    URI QmlMinimalplayer
    NO_RESOURCE_TARGET_PATH
    QML_FILES
        "qml-gstreamer-rtp.qml"
)

target_link_libraries( qml-gstreamer-rtp PUBLIC
    Qt::Core
    Qt::Gui
    Qt::Quick
)

install(TARGETS  qml-gstreamer-rtp
    RUNTIME DESTINATION "${INSTALL_EXAMPLEDIR}"
    BUNDLE DESTINATION "${INSTALL_EXAMPLEDIR}"
    LIBRARY DESTINATION "${INSTALL_EXAMPLEDIR}"
)

set_target_properties( qml-gstreamer-rtp PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
)
