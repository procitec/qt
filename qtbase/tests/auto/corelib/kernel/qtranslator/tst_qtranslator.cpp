// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QTest>
#include <QWaitCondition>
#include <QMutex>
#include <QStandardPaths>
#include <qtranslator.h>
#include <qfile.h>
#include <qtemporarydir.h>

#include <QtCore/private/qlocale_p.h>

#ifdef Q_OS_ANDROID
#include <QDirIterator>
#endif

#if !defined(QT_NO_SYSTEMLOCALE) && defined(QT_BUILD_INTERNAL)
// from tst_qlocale.cpp; override the system locale with one that supports multiple
// languages
class MySystemLocale : public QSystemLocale
{
    Q_DISABLE_COPY_MOVE(MySystemLocale)
public:
    MySystemLocale(const QStringList &languages)
        : m_languages(languages), m_locale(languages.first())
        , m_id(QLocaleId::fromName(languages.first()))
    {
    }

    QVariant query(QueryType type, QVariant &&/*in*/) const override
    {
        switch (type) {
        case UILanguages:
            return QVariant(m_languages);
        case LanguageId:
            return m_id.language_id;
        case TerritoryId:
            return m_id.territory_id;
        case ScriptId:
            return m_id.script_id;

        default:
            break;
        }
        return QVariant();
    }

    QLocale fallbackLocale() const override
    {
        return m_locale;
    }

private:
    QStringList m_languages;
    const QLocale m_locale;
    const QLocaleId m_id;
};
#endif // !defined(QT_NO_SYSTEMLOCALE) && defined(QT_BUILD_INTERNAL)

class tst_QTranslator : public QObject
{
    Q_OBJECT

public:
    tst_QTranslator();
protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
private slots:
    void initTestCase();
    void init();

    void load_data();
    void load();
    void loadLocale_data();
    void loadLocale();
    void threadLoad();
    void testLanguageChange();
    void plural();
    void translate_qm_file_generated_with_msgfmt();
    void loadDirectory();
    void dependencies();
    void translationInThreadWhileInstallingTranslator();

private:
    int languageChangeEventCounter;
    QSharedPointer<QTemporaryDir> dataDir;
};

tst_QTranslator::tst_QTranslator()
    : languageChangeEventCounter(0)
{
    qApp->installEventFilter(this);
}

void tst_QTranslator::initTestCase()
{
    dataDir = QEXTRACTTESTDATA(QStringLiteral("/tst_qtranslator"));
    QVERIFY2(!dataDir.isNull(), qPrintable("Could not extract test data"));
}

void tst_QTranslator::init()
{
    QVERIFY2(QDir::setCurrent(dataDir->path()),
             qPrintable("Could not chdir to " + dataDir->path()));
}

bool tst_QTranslator::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        ++languageChangeEventCounter;
    return false;
}

void tst_QTranslator::load_data()
{
    QTest::addColumn<QString>("filepath");
    QTest::addColumn<bool>("isEmpty");
    QTest::addColumn<QString>("translation");
    QTest::addColumn<QString>("language");

    QTest::newRow("hellotr_la") << "hellotr_la.qm" << false << "Hallo Welt!" << "de";
    QTest::newRow("hellotr_empty") << "hellotr_empty.qm" << true << "" << "";
}

void tst_QTranslator::load()
{
    QFETCH(QString, filepath);
    QFETCH(bool, isEmpty);
    QFETCH(QString, translation);
    QFETCH(QString, language);

    {
        QTranslator tor;
        QVERIFY(tor.load(QFileInfo(filepath).baseName()));
        QCOMPARE(tor.isEmpty(), isEmpty);
        QCOMPARE(tor.translate("QPushButton", "Hello world!"), translation);
        QCOMPARE(tor.filePath(), filepath);
        QCOMPARE(tor.language(), language);
    }

    {
        QFile file(filepath);
        QVERIFY(file.open(QFile::ReadOnly));
        QByteArray data = file.readAll();
        QTranslator tor;
        QVERIFY(tor.load((const uchar *)data.constData(), data.length()));
        QCOMPARE(tor.isEmpty(), isEmpty);
        QCOMPARE(tor.translate("QPushButton", "Hello world!"), translation);
        QCOMPARE(tor.filePath(), "");
        QCOMPARE(tor.language(), language);
    }

    {
        QTranslator tor;
        QString path = QString(":/tst_qtranslator/%1").arg(filepath);
        QVERIFY(tor.load(path));
        QCOMPARE(tor.isEmpty(), isEmpty);
        QCOMPARE(tor.translate("QPushButton", "Hello world!"), translation);
        QCOMPARE(tor.filePath(), path);
        QCOMPARE(tor.language(), language);
    }
}

void tst_QTranslator::loadLocale_data()
{
    QTest::addColumn<QLocale>("wantedLocale");
    QTest::addColumn<QStringList>("languages");

    // variation of translation files for the same language
    QTest::addRow("US English")
                            << QLocale("en-US")
                            << QStringList{"en-US", "en"};
    QTest::addRow("Australia")
                            << QLocale("en-AU")
                            << QStringList{"en-Latn-AU", "en-AU", "en"};

    // This produces a QLocale::uiLanguages list of
    // {"en-NO", "en-Latn-NO", "nb-NO", "nb-Latn-NO", "nb",
    //  "de-DE", "de-Latn-DE", "de", "zh-Hant-NO"}
    QTest::addRow("System, mixed languages")
                            << QLocale::system()
                            << QStringList{"en-NO", "nb-NO", "de-DE", "zh-Hant-NO"};
}

void tst_QTranslator::loadLocale()
{
    QFETCH(const QLocale, wantedLocale);
    QFETCH(const QStringList, languages);

#if !defined(QT_NO_SYSTEMLOCALE) && defined(QT_BUILD_INTERNAL)
    std::unique_ptr<MySystemLocale> systemLocaleOverride;
    if (wantedLocale == QLocale::system())
        systemLocaleOverride.reset(new MySystemLocale(languages));
#else
    if (wantedLocale == QLocale::system())
        QSKIP("Test only applicable in developer builds with system locale");
#endif

    QByteArray ba;
    {
        QFile file(":/tst_qtranslator/hellotr_la.qm");
        QVERIFY2(file.open(QFile::ReadOnly), qPrintable(file.errorString()));
        ba = file.readAll();
        QVERIFY(!ba.isEmpty());
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const auto path = dir.path();
    QFile file(path + "/dummy");
    QVERIFY2(file.open(QFile::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(ba), ba.size());
    file.close();

    QStringList files;
    for (auto language : languages) {
        language.replace('-', '_');
        const QString filename = path + "/foo-" + language;
        files.append(filename + ".qm");
        QVERIFY2(file.copy(files.last()), qPrintable(file.errorString()));
        files.append(filename);
        QVERIFY2(file.copy(files.last()), qPrintable(file.errorString()));
    }

    files.append(path + "/foo.qm");
    QVERIFY2(file.copy(files.last()), qPrintable(file.errorString()));

    files.append(path + "/foo-");
    QVERIFY2(file.copy(files.last()), qPrintable(file.errorString()));

    files.append(path + "/foo");
    QVERIFY2(file.rename(files.last()), qPrintable(file.errorString()));

    // Verify that all files exist. They are removed at the latest when
    // the temporary directory is destroyed.
    for (const auto &filePath : files)
        QVERIFY(QFile::exists(filePath));

    const QRegularExpression localeExpr("foo-(.*)(\\.qm|$)");
    QTranslator tor;
    // Load the translation for the wanted locale
    QVERIFY(tor.load(wantedLocale, "foo", "-", path, ".qm"));
    // The loaded translation file should be for the preferred language.
    const QFileInfo fileInfo(tor.filePath());
    const auto matches = localeExpr.match(fileInfo.fileName());
    QVERIFY(matches.hasMatch());
    QVERIFY(matches.hasCaptured(1));
    const QLocale matchedLocale(matches.captured(1));
    QCOMPARE(matchedLocale.language(), wantedLocale.language());

    // Remove one file at a time, and verify that QTranslator falls back to the
    // more general alternatives, or to languages with lower priority.
    for (const auto &filePath : files) {
        QVERIFY(tor.load(wantedLocale, "foo", "-", path, ".qm"));
        QCOMPARE(tor.filePath(), filePath);
        QVERIFY2(file.remove(filePath), qPrintable(file.errorString()));
    }
}

class TranslatorThread : public QThread
{
    void run() override {
        QTranslator tor( 0 );
        (void)tor.load("hellotr_la");

        if (tor.isEmpty())
            qFatal("Could not load translation");
        if (tor.translate("QPushButton", "Hello world!") !=  QLatin1String("Hallo Welt!"))
            qFatal("Test string was not translated correctlys");
    }
};


void tst_QTranslator::threadLoad()
{
    TranslatorThread thread;
    thread.start();
    QVERIFY(thread.wait(10 * 1000));
}

void tst_QTranslator::testLanguageChange()
{
    languageChangeEventCounter = 0;

    QTranslator *tor = new QTranslator;
    QVERIFY(tor->load("hellotr_la.qm"));
    qApp->sendPostedEvents();
    qApp->sendPostedEvents();
    QCOMPARE(languageChangeEventCounter, 0);

    QVERIFY(!tor->load("doesn't exist, same as clearing"));
    qApp->sendPostedEvents();
    qApp->sendPostedEvents();
    QCOMPARE(languageChangeEventCounter, 0);

    QVERIFY(tor->load("hellotr_la.qm"));
    qApp->sendPostedEvents();
    qApp->sendPostedEvents();
    QCOMPARE(languageChangeEventCounter, 0);

    qApp->installTranslator(tor);
    qApp->sendPostedEvents();
    qApp->sendPostedEvents();
    QCOMPARE(languageChangeEventCounter, 1);

    QVERIFY(!tor->load("doesn't exist, same as clearing"));
    qApp->sendPostedEvents();
    qApp->sendPostedEvents();
    QCOMPARE(languageChangeEventCounter, 2);

    QVERIFY(tor->load("hellotr_la.qm"));
    qApp->sendPostedEvents();
    qApp->sendPostedEvents();
    QCOMPARE(languageChangeEventCounter, 3);

    qApp->removeTranslator(tor);
    qApp->sendPostedEvents();
    qApp->sendPostedEvents();
    QCOMPARE(languageChangeEventCounter, 4);

    QVERIFY(!tor->load("doesn't exist, same as clearing"));
    qApp->sendPostedEvents();
    qApp->sendPostedEvents();
    QCOMPARE(languageChangeEventCounter, 4);

    qApp->installTranslator(tor);
    qApp->sendPostedEvents();
    qApp->sendPostedEvents();
    QCOMPARE(languageChangeEventCounter, 4);

    QVERIFY(tor->load("hellotr_la.qm"));
    qApp->sendPostedEvents();
    qApp->sendPostedEvents();
    QCOMPARE(languageChangeEventCounter, 5);

    delete tor;
    tor = 0;
    qApp->sendPostedEvents();
    qApp->sendPostedEvents();
    QCOMPARE(languageChangeEventCounter, 6);
}


void tst_QTranslator::plural()
{

    QTranslator tor( 0 );
    QVERIFY(tor.load("hellotr_la"));
    QVERIFY(!tor.isEmpty());
    QCoreApplication::installTranslator(&tor);
    QCOMPARE(QCoreApplication::translate("QPushButton", "Hello %n world(s)!", 0, 0), QLatin1String("Hallo 0 Welten!"));
    QCOMPARE(QCoreApplication::translate("QPushButton", "Hello %n world(s)!", 0, 1), QLatin1String("Hallo 1 Welt!"));
    QCOMPARE(QCoreApplication::translate("QPushButton", "Hello %n world(s)!", 0, 2), QLatin1String("Hallo 2 Welten!"));
}

void tst_QTranslator::translate_qm_file_generated_with_msgfmt()
{
    QTranslator translator;
    QVERIFY(translator.load("msgfmt_from_po"));
    qApp->installTranslator(&translator);

    QCOMPARE(QCoreApplication::translate("", "Intro"), QLatin1String("Einleitung"));
    // The file is converted from a po file, thus it does not have any context info.
    // The following should then not be translated
    QCOMPARE(QCoreApplication::translate("contekst", "Intro"), QLatin1String("Intro"));
    QCOMPARE(QCoreApplication::translate("contekst", "Intro\0\0"), QLatin1String("Intro"));
    QCOMPARE(QCoreApplication::translate("contekst", "Intro\0x"), QLatin1String("Intro"));
    QCOMPARE(QCoreApplication::translate("", "Intro\0\0"), QLatin1String("Einleitung"));
    QCOMPARE(QCoreApplication::translate("", "Intro\0x"), QLatin1String("Einleitung"));

    qApp->removeTranslator(&translator);
}

void tst_QTranslator::loadDirectory()
{
    QString current_base = QDir::current().dirName();
    QVERIFY(QFileInfo("../" + current_base).isDir());

    QTranslator tor;
    QVERIFY(!tor.load(current_base, ".."));
    QVERIFY(tor.isEmpty());
}

void tst_QTranslator::dependencies()
{
    {
        // load
        QTranslator tor;
        QVERIFY(tor.load("dependencies_la"));
        QVERIFY(!tor.isEmpty());
        QCOMPARE(tor.translate("QPushButton", "Hello world!"), QLatin1String("Hallo Welt!"));

        // plural
        QCoreApplication::installTranslator(&tor);
        QCOMPARE(QCoreApplication::translate("QPushButton", "Hello %n world(s)!", 0, 0), QLatin1String("Hallo 0 Welten!"));
        QCOMPARE(QCoreApplication::translate("QPushButton", "Hello %n world(s)!", 0, 1), QLatin1String("Hallo 1 Welt!"));
        QCOMPARE(QCoreApplication::translate("QPushButton", "Hello %n world(s)!", 0, 2), QLatin1String("Hallo 2 Welten!"));

        // pick up translation from the file with dependencies
        QCOMPARE(tor.translate("QPushButton", "It's a small world"), QLatin1String("Es ist eine kleine Welt"));
    }

    {
        QTranslator tor( 0 );
        QFile file("dependencies_la.qm");
        QVERIFY(file.open(QFile::ReadOnly));
        QByteArray data = file.readAll();
        QVERIFY(tor.load((const uchar *)data.constData(), data.length()));
        QVERIFY(!tor.isEmpty());
        QCOMPARE(tor.translate("QPushButton", "Hello world!"), QLatin1String("Hallo Welt!"));
    }

    {
        // Test resolution of paths relative to main file
        const QString absoluteFile = QFileInfo("dependencies_la").absoluteFilePath();
        QDir::setCurrent(QDir::tempPath());
        QTranslator tor;
        QVERIFY(tor.load(absoluteFile));
        QVERIFY(!tor.isEmpty());
    }
}

struct TranslateThread : public QThread
{
    bool ok = false;
    QAtomicInt terminate;
    QMutex startupLock;
    QWaitCondition runningCondition;

    void run() override {
        bool startSignalled = false;

        while (terminate.loadRelaxed() == 0) {
            const QString result =  QCoreApplication::translate("QPushButton", "Hello %n world(s)!", 0, 0);

            if (!startSignalled) {
                QMutexLocker startupLocker(&startupLock);
                runningCondition.wakeAll();
                startSignalled = true;
            }

            ok = (result == QLatin1String("Hallo 0 Welten!"))
                  || (result == QLatin1String("Hello 0 world(s)!"));
            if (!ok)
                break;
        }
    }
};

void tst_QTranslator::translationInThreadWhileInstallingTranslator()
{
    TranslateThread thread;

    QMutexLocker startupLocker(&thread.startupLock);

    thread.start();

    thread.runningCondition.wait(&thread.startupLock);

    QTranslator tor;
    QVERIFY(tor.load("hellotr_la"));
    QVERIFY(QCoreApplication::installTranslator(&tor));

    ++thread.terminate;

    QVERIFY(thread.wait());
    QVERIFY(thread.ok);
}

QTEST_MAIN(tst_QTranslator)
#include "tst_qtranslator.moc"
