// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include "capturesessionfixture_p.h"
#include <QtTest/qtest.h>

QT_BEGIN_NAMESPACE

using namespace std::chrono;

CaptureSessionFixture::CaptureSessionFixture(StreamType streamType) : m_streamType{ streamType } { }

CaptureSessionFixture::~CaptureSessionFixture()
{
    if (!m_recorder.actualLocation().isEmpty())
        QFile::remove(m_recorder.actualLocation().toLocalFile());
}

void CaptureSessionFixture::setVideoSink(QVideoSink *videoSink)
{
    m_videoSink = videoSink;
}

void CaptureSessionFixture::start(RunMode mode, AutoStop autoStop)
{
    if (hasVideo()) {
        m_session.setVideoFrameInput(&m_videoInput);

        QObject::connect(&m_videoGenerator, &VideoGenerator::frameCreated, //
                         &m_videoInput, &QVideoFrameInput::sendVideoFrame);

        if (autoStop == AutoStop::EmitEmpty) {
            m_recorder.setAutoStop(true);
            m_videoGenerator.emitEmptyFrameOnStop();
        }
    }

    if (hasAudio()) {
        m_session.setAudioBufferInput(&m_audioInput);

        QObject::connect(&m_audioGenerator, &AudioGenerator::audioBufferCreated, //
                         &m_audioInput, &QAudioBufferInput::sendAudioBuffer);

        if (autoStop == AutoStop::EmitEmpty) {
            m_recorder.setAutoStop(true);
            m_audioGenerator.emitEmptyBufferOnStop();
        }
    }

    if (mode == RunMode::Pull) {
        if (hasVideo())
            QObject::connect(&m_videoInput, &QVideoFrameInput::readyToSendVideoFrame, //
                             &m_videoGenerator, &VideoGenerator::nextFrame);

        if (hasAudio())
            QObject::connect(&m_audioInput, &QAudioBufferInput::readyToSendAudioBuffer, //
                             &m_audioGenerator, &AudioGenerator::nextBuffer);
    }

    m_session.setRecorder(&m_recorder);
    m_recorder.setQuality(QMediaRecorder::VeryHighQuality);

    if (m_recorder.outputLocation().isEmpty()) {
        // Create a temporary file.
        // The file name is not available without opening.
        m_tempFile.open();
        m_tempFile.close();

        m_recorder.setOutputLocation(m_tempFile.fileName());
    }
    // else: outputLocation has been already set

    m_recorder.record();

    // HACK: Add sink after starting recording because setting the video sink will
    // emit ready signals and start the processing chain. TODO: Fix this
    if (m_videoSink)
        m_session.setVideoSink(m_videoSink);
}

bool CaptureSessionFixture::waitForRecorderStopped(std::chrono::milliseconds duration)
{
    // StoppedState is emitted when media is finalized.
    const bool stopped = QTest::qWaitFor(
            [&] { //
                return recorderStateChanged.contains(
                        QList<QVariant>{ QMediaRecorder::RecorderState::StoppedState });
            },
            duration);

    if (!stopped)
        return false;

    return m_recorder.recorderState() == QMediaRecorder::StoppedState;
}

bool CaptureSessionFixture::hasAudio() const
{
    return m_streamType == StreamType::Audio || m_streamType == StreamType::AudioAndVideo;
}

bool CaptureSessionFixture::hasVideo() const
{
    return m_streamType == StreamType::Video || m_streamType == StreamType::AudioAndVideo;
}

QT_END_NAMESPACE
