// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include "protocplugintestcommon.h"

#include <QtTest/qtest.h>

#include <QtCore/qcryptographichash.h>
#include <QtCore/qdiriterator.h>
#if QT_CONFIG(process)
#  include <QtCore/qprocess.h>
#endif

namespace {

QByteArray hash(const QByteArray &fileData)
{
    return QCryptographicHash::hash(fileData, QCryptographicHash::Sha1);
}

// Return size diff and first NOT equal line;
QByteArray doCompare(const QByteArrayList &actual, const QByteArrayList &expected)
{
    QByteArray ba;

    if (actual.size() != expected.size()) {
        ba.append(QString("Length count different: actual: %1, expected: %2")
                      .arg(actual.size())
                      .arg(expected.size())
                      .toUtf8());
    }

    for (int i = 0, n = expected.size(); i != n; ++i) {
        QString expectedLine = expected.at(i);
        if (expectedLine != actual.at(i)) {
            ba.append("\n<<<<<< ACTUAL\n" + actual.at(i) + "\n======\n" + expectedLine.toUtf8()
                      + "\n>>>>>> EXPECTED\n");
            break;
        }
    }
    return ba;
}

QByteArrayList splitToLines(const QByteArray &data)
{
    return data.split('\n');
}
} // namespace

QByteArray ProtocPluginTest::msgCannotReadFile(const QFile &file)
{
    const QString result = QLatin1StringView("Could not read file: ")
        + QDir::toNativeSeparators(file.fileName()) + QLatin1StringView(": ") + file.errorString();
    return result.toLocal8Bit();
}

#if QT_CONFIG(process)
QByteArray ProtocPluginTest::msgProcessStartFailed(const QProcess &p)
{
    const QString result = QLatin1StringView("Could not start \"")
        + QDir::toNativeSeparators(p.program()) + QLatin1StringView("\": ") + p.errorString();
    return result.toLocal8Bit();
}

QByteArray ProtocPluginTest::msgProcessTimeout(const QProcess &p)
{
    return '"' + QDir::toNativeSeparators(p.program()).toLocal8Bit() + "\" timed out.";
}

QByteArray ProtocPluginTest::msgProcessCrashed(QProcess &p)
{
    return '"' + QDir::toNativeSeparators(p.program()).toLocal8Bit() + "\" crashed.\n"
        + p.readAll();
}

QByteArray ProtocPluginTest::msgProcessFailed(QProcess &p)
{
    return '"' + QDir::toNativeSeparators(p.program()).toLocal8Bit() + "\" returned "
        + QByteArray::number(p.exitCode()) + ":\n" + p.readAll();
}

bool ProtocPluginTest::protocolCompilerAvailableToRun(const QString &protocPath)
{
    QProcess protoc;
    protoc.startCommand(protocPath + " --version");

    if (!protoc.waitForStarted())
        return false;

    if (!protoc.waitForFinished()) {
        protoc.kill();
        return false;
    }
    return protoc.exitStatus() == QProcess::NormalExit;
}
#endif // QT_CONFIG(process)

void ProtocPluginTest::compareTwoFiles(const QString &expectedFileName,
                                       const QString &actualFileName)
{
    QFile expectedResultFile(expectedFileName);
    QFile generatedFile(actualFileName);

    QVERIFY2(expectedResultFile.exists(), qPrintable(expectedResultFile.fileName()));
    QVERIFY2(generatedFile.exists(), qPrintable(expectedResultFile.fileName()));

    QVERIFY2(expectedResultFile.open(QIODevice::ReadOnly | QIODevice::Text),
             msgCannotReadFile(expectedResultFile).constData());
    QVERIFY2(generatedFile.open(QIODevice::ReadOnly | QIODevice::Text),
             msgCannotReadFile(generatedFile).constData());

    QByteArray expectedData = expectedResultFile.readAll();
    QByteArray generatedData = generatedFile.readAll();

    expectedResultFile.close();
    generatedFile.close();

    if (hash(expectedData).toHex() != hash(generatedData).toHex()) {
        const QString diff = doCompare(splitToLines(generatedData), splitToLines(expectedData));
        QCOMPARE_GT(diff.size(), 0); // Hashes can only differ if content does.
        QFAIL(qPrintable(diff));
    }
    // Ensure we do see a failure, even in the unlikely case of a hash collision:
    QVERIFY(generatedData == expectedData);
}

void ProtocPluginTest::cleanFolder(const QString &folderName)
{
    QDir dir(folderName);
    dir.removeRecursively();
}

QFileInfoList ProtocPluginTest::scanDirectoryRecursively(const QDir &dir)
{
    QFileInfoList result;
    QDirIterator it(dir.path(), QStringList() << "*.cpp" << "*.h", QDir::Files | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        result.append(it.fileInfo());
    }
    std::sort(result.begin(), result.end(), [&dir](const QFileInfo &lhs, const QFileInfo &rhs) {
        return dir.relativeFilePath(lhs.absoluteFilePath())
        < dir.relativeFilePath(rhs.absoluteFilePath());
    });
    return result;
}

QStringList ProtocPluginTest::relativePaths(const QDir &dir, const QFileInfoList &files)
{
    QStringList result;
    for (const auto &file : files) {
        result.append(dir.relativeFilePath(file.absoluteFilePath()));
    }
    return result;
}

bool ProtocPluginTest::copyDirectoryRecursively(const QDir &from, QDir to)
{
    if (!from.exists()) {
        qDebug() << "Unable to copy directory" << from << ". Directory doesn't exists.";
        return false;
    }
    for (const auto &item :
         from.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        const auto fileName = item.fileName();
        const auto absolutePath = item.absoluteFilePath();
        if (fileName.isEmpty()) {
            qDebug() << "fileName is empty";
            return false;
        }
        if (item.isFile()) {
            if (!QFile::copy(item.absoluteFilePath(), to.absolutePath() + u'/' + fileName)) {
                qDebug() << "Unable to copy " << item.absoluteFilePath() << "to"
                         << to.absolutePath() + u'/' + fileName;
                return false;
            }
        } else {
            if (!to.mkdir(fileName)) {
                qDebug() << "Unable to create " << to.absolutePath() + u'/' + fileName;
                return false;
            }
            if (!to.cd(fileName)) {
                qDebug() << "Unable to enter " << to.absolutePath() + u'/' + fileName;
                return false;
            }
            if (!copyDirectoryRecursively(QDir(absolutePath), to))
                return false;
            if (!to.cdUp())
                return false;
        }
    }
    return true;
}
