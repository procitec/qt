// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include <QtGui/qguiapplication.h>
#include <QtQml/qqmlengine.h>
#include <QtQuick/qquickview.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQuickView viewer;

// The following are needed to make examples run without having to install the
// module in desktop environments.
#ifdef Q_OS_WIN
    QString extraImportPath(QStringLiteral("%1/../../../../%2"));
#else
    QString extraImportPath(QStringLiteral("%1/../../../%2"));
#endif
    viewer.engine()->addImportPath(
        extraImportPath.arg(QGuiApplication::applicationDirPath(), QString::fromLatin1("qml")));
    viewer.setTitle(QStringLiteral("Cockpit"));

    viewer.setMaximumSize({1280, 720});
    viewer.setMinimumSize({1280, 720});
    viewer.setSource(QUrl("qrc:/qml/cockpit/main.qml"));
    viewer.setResizeMode(QQuickView::SizeRootObjectToView);
    viewer.setColor("black");
    viewer.show();

    return app.exec();
}
