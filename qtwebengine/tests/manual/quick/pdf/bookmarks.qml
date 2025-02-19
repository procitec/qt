// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Pdf
import QtQuick.Shapes

ApplicationWindow {
    id: root
    width: 800
    height: 940
    color: "darkgrey"
    title: doc.source
    visible: true

    property PdfDocument doc: PdfDocument { source: "test.pdf" }

    Component.onCompleted: {
        if (Application.arguments.length > 2)
            doc.source = Application.arguments[Application.arguments.length - 1]
    }
    FileDialog {
        id: fileDialog
        title: "Open a PDF file"
        nameFilters: [ "PDF files (*.pdf)" ]
        onAccepted: doc.source = selectedFile
    }

    SplitView {
        anchors.fill: parent

        Pane {
            SplitView.minimumWidth: 6
            SplitView.preferredWidth: 200
            TreeView {
                id: bookmarksTree
                anchors.fill: parent
                columnWidthProvider: function() { return width }
                onWidthChanged: forceLayout() // workaround to avoid column width getting stuck
                clip: true
                delegate: TreeViewDelegate {
                    width: parent.width
                    onClicked: image.currentFrame = page
                }
                model: PdfBookmarkModel {
                    document: root.doc
                }
                ScrollIndicator.vertical: ScrollIndicator {
                    // get the ScrollIndicator out into the margin area of the Pane...
                    // no need to overlap the tree when so much space is wasted anyway
                    parent: bookmarksTree.parent
                    anchors {
                        top: bookmarksTree.top
                        left: bookmarksTree.right
                        bottom: bookmarksTree.bottom
                    }
                }
            }
        }

        ScrollView {
            contentWidth: paper.width
            contentHeight: paper.height

            Rectangle {
                id: paper
                width: image.width
                height: image.height
                PdfPageImage {
                    id: image
                    document: doc

                    property real zoomFactor: Math.sqrt(2)

                    Shortcut {
                        sequence: StandardKey.MoveToNextPage
                        enabled: image.currentFrame < image.frameCount - 1
                        onActivated: image.currentFrame++
                    }
                    Shortcut {
                        sequence: StandardKey.MoveToPreviousPage
                        enabled: image.currentFrame > 0
                        onActivated: image.currentFrame--
                    }
                    Shortcut {
                        sequence: StandardKey.ZoomIn
                        enabled: image.sourceSize.width < 5000
                        onActivated: {
                            image.sourceSize.width = image.implicitWidth * image.zoomFactor
                            image.sourceSize.height = image.implicitHeight * image.zoomFactor
                        }
                    }
                    Shortcut {
                        sequence: StandardKey.ZoomOut
                        enabled: image.width > 50
                        onActivated: {
                            image.sourceSize.width = image.implicitWidth / image.zoomFactor
                            image.sourceSize.height = image.implicitHeight / image.zoomFactor
                        }
                    }
                    Shortcut {
                        sequence: "Ctrl+0"
                        onActivated: image.sourceSize = undefined
                    }
                    Shortcut {
                        sequence: StandardKey.Open
                        onActivated: fileDialog.open()
                    }
                    Shortcut {
                        sequence: StandardKey.Quit
                        onActivated: Qt.quit()
                    }
                }

                Repeater {
                    model: PdfLinkModel {
                        id: linkModel
                        document: doc
                        page: image.currentFrame
                    }
                    delegate: Rectangle {
                        color: "transparent"
                        border.color: "lightgrey"
                        x: rect.x
                        y: rect.y
                        width: rect.width
                        height: rect.height
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                if (page >= 0)
                                    image.currentFrame = page
                                else
                                    Qt.openUrlExternally(url)
                            }
                        }
                    }
                }
            }
        }
    }
    Label {
        anchors { bottom: parent.bottom; right: parent.right; margins: 6 }
        text: "page " + doc.pageLabel(image.currentFrame) + " of " + doc.pageCount
    }
}
