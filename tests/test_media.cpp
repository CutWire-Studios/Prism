#include <QtTest>
#include <QTemporaryDir>

#include "core/media/MediaFormats.h"
#include "core/media/MediaProbe.h"
#include "core/media/StillImage.h"
#include "core/media/VideoDecoder.h"
#include "core/media/GpuVideoUploader.h"
#include "core/sources/VideoFileSource.h"
#include "ui/recording/ProgramRecorder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

namespace {

// Writes a small MPEG-4 Part 2 clip (always built into libavcodec) with the given frame times in
// milliseconds. Only the first frame is a keyframe, so any seek has to decode forward to land.
// Frame i has luma 16 + 20*i so tests can tell frames apart. rotation is the display rotation a
// player should apply, clockwise.
bool writeClip(const QString &path, const QList<int> &ptsMs, int rotation = 0,
               int width = 64, int height = 32)
{
    const QByteArray utf8 = path.toUtf8();
    AVFormatContext *fmt = nullptr;
    if (avformat_alloc_output_context2(&fmt, nullptr, nullptr, utf8.constData()) < 0)
        return false;
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    AVStream *stream = avformat_new_stream(fmt, nullptr);
    AVCodecContext *enc = avcodec_alloc_context3(codec);
    enc->width = width;
    enc->height = height;
    enc->pix_fmt = AV_PIX_FMT_YUV420P;
    enc->time_base = {1, 1000};
    enc->framerate = {25, 1};
    enc->gop_size = 1000;
    enc->max_b_frames = 0;
    if (fmt->oformat->flags & AVFMT_GLOBALHEADER)
        enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    // Every frame differs completely, which scene-change detection would turn into keyframes.
    av_opt_set_int(enc, "sc_threshold", 1000000000, AV_OPT_SEARCH_CHILDREN);
    if (avcodec_open2(enc, codec, nullptr) < 0)
        return false;
    avcodec_parameters_from_context(stream->codecpar, enc);
    stream->time_base = enc->time_base;
    if (rotation != 0) {
        AVPacketSideData *sd = av_packet_side_data_new(&stream->codecpar->coded_side_data,
                                                        &stream->codecpar->nb_coded_side_data,
                                                        AV_PKT_DATA_DISPLAYMATRIX,
                                                        sizeof(int32_t) * 9, 0);
        // _set takes a clockwise angle while _get reports counter-clockwise, so this reads back
        // as -rotation — what a phone writes for a portrait clip.
        av_display_rotation_set(reinterpret_cast<int32_t *>(sd->data), rotation);
    }
    if (avio_open(&fmt->pb, utf8.constData(), AVIO_FLAG_WRITE) < 0
        || avformat_write_header(fmt, nullptr) < 0)
        return false;

    AVFrame *frame = av_frame_alloc();
    frame->format = enc->pix_fmt;
    frame->width = width;
    frame->height = height;
    av_frame_get_buffer(frame, 0);
    AVPacket *pkt = av_packet_alloc();

    auto drain = [&] {
        while (avcodec_receive_packet(enc, pkt) == 0) {
            av_packet_rescale_ts(pkt, enc->time_base, stream->time_base);
            pkt->stream_index = stream->index;
            av_interleaved_write_frame(fmt, pkt);
        }
    };
    for (int i = 0; i < ptsMs.size(); ++i) {
        av_frame_make_writable(frame);
        for (int y = 0; y < height; ++y)
            memset(frame->data[0] + y * frame->linesize[0], 16 + 20 * i, width);
        for (int y = 0; y < height / 2; ++y) {
            memset(frame->data[1] + y * frame->linesize[1], 128, width / 2);
            memset(frame->data[2] + y * frame->linesize[2], 128, width / 2);
        }
        frame->pts = ptsMs[i];
        // Without a duration the muxer writes the last sample as zero-length and it is lost.
        frame->duration = i + 1 < ptsMs.size() ? ptsMs[i + 1] - ptsMs[i] : 40;
        avcodec_send_frame(enc, frame);
        drain();
    }
    avcodec_send_frame(enc, nullptr);
    drain();
    av_write_trailer(fmt);

    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&enc);
    avio_closep(&fmt->pb);
    avformat_free_context(fmt);
    return true;
}

int lumaOf(const DecodedVideoFrame &f)
{
    return f.frame ? f.frame->data[0][f.frame->linesize[0] * 8 + 8] : -1;
}

} // namespace

class TestMedia : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    const QList<int> m_vfrTimes = {0, 40, 80, 200, 240, 500, 540, 580};

    QString clip(const QString &name) const { return m_dir.filePath(name); }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        m_dir.setAutoRemove(qEnvironmentVariableIsEmpty("PRISM_KEEP_TEST_MEDIA"));
        qInfo() << "fixtures in" << m_dir.path();
        // One frame past the checked ones: the mp4 edit list trims the final sample, as
        // ffprobe also shows, and that is the muxer's business rather than the decoder's.
        QVERIFY(writeClip(clip("vfr.mp4"), QList<int>(m_vfrTimes) << 620));
        QVERIFY(writeClip(clip("portrait.mp4"), {0, 40, 80}, 90));
        // Nothing here needs a GPU; keep every run on the software path.
        VideoDecoder::setMode(VideoDecoder::Mode::Software, prism::hwaccel::Backend::None);
    }

    void formats_classifySuffixes() {
        QVERIFY(MediaFormats::isVideoPath("/a/b/clip.MTS"));
        QVERIFY(MediaFormats::isVideoPath("clip.flv"));
        QVERIFY(MediaFormats::isVideoPath("clip.mxf"));
        QVERIFY(MediaFormats::isImagePath("photo.HEIC"));
        QVERIFY(MediaFormats::isImagePath("photo.avif"));
        QVERIFY(MediaFormats::isAudioPath("song.opus"));
        QVERIFY(!MediaFormats::isMediaPath("notes.txt"));
        QVERIFY(MediaFormats::nameFilters(true, false, false).contains("*.ts"));
        QVERIFY(!MediaFormats::nameFilters(true, false, false).contains("*.png"));
        QVERIFY(MediaFormats::globPattern(true, true, true).contains("*.m4a"));
    }

    void probe_readsRotation() {
        const MediaInfo info = MediaProbe::probe(clip("portrait.mp4"));
        QVERIFY(info.ok);
        QVERIFY(hasStream(info, StreamInfo::Type::Video));
        QVERIFY(!hasStream(info, StreamInfo::Type::Audio));
        QCOMPARE(info.streams.first().rotationDegrees, 90);
    }

    void stillImage_appliesRotation() {
        const QImage img = StillImage::decodeVideoFrame(clip("portrait.mp4"), 0.0);
        QVERIFY(!img.isNull());
        QCOMPARE(img.size(), QSize(32, 64));
    }

    void decoder_deliversPtsInOrder() {
        VideoDecoder dec;
        QVERIFY(dec.open(clip("vfr.mp4")));
        QCOMPARE(dec.codedSize(), QSize(64, 32));
        QCOMPARE(dec.rotation(), 0);
        for (int i = 0; i < m_vfrTimes.size(); ++i) {
            DecodedVideoFrame f;
            QVERIFY2(dec.popNext(f, 2000), qPrintable(QString("frame %1").arg(i)));
            QCOMPARE(f.ptsUs, int64_t(m_vfrTimes[i]) * 1000);
            QCOMPARE(f.frame->format, int(AV_PIX_FMT_NV12));
            QVERIFY(qAbs(lumaOf(f) - (16 + 20 * i)) <= 3);
        }
        DecodedVideoFrame rest;
        while (dec.popNext(rest, 500)) {}
        QVERIFY(dec.drained());
        QCOMPARE(dec.activeBackendName(), QStringLiteral("Software"));
    }

    void decoder_seekLandsOnCoveringFrame() {
        VideoDecoder dec;
        QVERIFY(dec.open(clip("vfr.mp4")));
        // 0.21 s falls inside the frame shown from 200 ms to 240 ms; the only keyframe is at 0.
        dec.seek(0.21);
        DecodedVideoFrame f;
        QVERIFY(dec.popNext(f, 2000));
        QCOMPARE(f.ptsUs, int64_t(200000));
        QVERIFY(qAbs(lumaOf(f) - (16 + 20 * 3)) <= 3);

        // A long gap: 0.45 s is still covered by the 240 ms frame only if frames were sized by
        // their real duration; with the nominal 25 fps duration it is dropped and 500 ms follows.
        dec.seek(0.45);
        QVERIFY(dec.popNext(f, 2000));
        QVERIFY(f.ptsUs == 240000 || f.ptsUs == 500000);
    }

    void decoder_popUpToSkipsToDueFrame() {
        VideoDecoder dec;
        QVERIFY(dec.open(clip("vfr.mp4")));
        DecodedVideoFrame f;
        QVERIFY(dec.popNext(f, 2000));
        QCOMPARE(f.ptsUs, int64_t(0));
        // Let the worker fill its queue, then ask for 0.1 s: 40 and 80 are due, 200 is not.
        QTest::qWait(300);
        QVERIFY(dec.popUpTo(0.1, f));
        QCOMPARE(f.ptsUs, int64_t(80000));
        QVERIFY(!dec.popUpTo(0.1, f));
    }

    void decoder_pinnedMissingBackendFallsBackToSoftware() {
        // VideoToolbox never exists here; a pinned Hardware mode must still decode.
        VideoDecoder::setMode(VideoDecoder::Mode::Hardware, prism::hwaccel::Backend::VideoToolbox);
        VideoDecoder dec;
        QVERIFY(dec.open(clip("vfr.mp4")));
        DecodedVideoFrame f;
        QVERIFY(dec.popNext(f, 2000));
        QCOMPARE(dec.activeBackendName(), QStringLiteral("Software"));
        VideoDecoder::setMode(VideoDecoder::Mode::Software, prism::hwaccel::Backend::None);
    }

    void recorder_writesH264WithChosenEncoder() {
        // Auto: whichever hardware encoder opens here, else x264. Either way the file must hold
        // real H.264 frames at the requested size.
        QVERIFY(ProgramRecorder::availableEncoders().first().id == QLatin1String("auto"));
        ProgramRecorder rec;
        const QString out = clip("rec.mkv");
        QVERIFY(rec.startRecording(out, {}, false, 320, 180));
        qInfo() << "recording with" << rec.encoderLabel();
        QImage img(320, 180, QImage::Format_RGB888);
        for (int i = 0; i < 15; ++i) {
            img.fill(QColor::fromHsv(i * 20, 200, 200));
            rec.submitFrame(img);
            QTest::qWait(34);
        }
        rec.stopRecording();
        QVERIFY(rec.capturedFrameCount() > 0);

        const MediaInfo info = MediaProbe::probe(out);
        QVERIFY(info.ok);
        QCOMPARE(info.streams.first().codecName, QStringLiteral("h264"));
        QCOMPARE(info.streams.first().width, 320);
        QCOMPARE(info.streams.first().height, 180);

        VideoDecoder dec;
        QVERIFY(dec.open(out));
        DecodedVideoFrame f;
        QVERIFY(dec.popNext(f, 2000));
    }

    void decoder_hardwareBackendsDecodeH264() {
        const QList<prism::hwaccel::Backend> backends = prism::hwaccel::availableDecodeBackends();
        if (backends.isEmpty() || !QFile::exists(clip("rec.mkv")))
            QSKIP("no hardware decoder on this machine");
        for (const prism::hwaccel::Backend backend : backends) {
            VideoDecoder::setMode(VideoDecoder::Mode::Hardware, backend);
            VideoDecoder dec;
            QVERIFY(dec.open(clip("rec.mkv")));
            DecodedVideoFrame f;
            QVERIFY(dec.popNext(f, 3000));
            qInfo() << prism::hwaccel::name(backend) << "->" << dec.activeBackendName()
                    << av_get_pix_fmt_name(AVPixelFormat(f.frame->format));
            QCOMPARE(dec.activeBackendName(), QString::fromLatin1(prism::hwaccel::name(backend)));
            // Either the surface itself (zero-copy candidate) or a readback already in NV12.
            QVERIFY(f.frame->format == AV_PIX_FMT_NV12 || f.frame->hw_frames_ctx != nullptr);
            int count = 1;
            while (dec.popNext(f, 1000))
                ++count;
            QVERIFY(count >= 10);
        }
        VideoDecoder::setMode(VideoDecoder::Mode::Software, prism::hwaccel::Backend::None);
    }

    void source_gpuPathMatchesExpectedColours() {
        if (QGuiApplication::platformName() == QLatin1String("offscreen")
            || !prism::GpuVideoUploader::instance().available())
            QSKIP("no OpenGL on this platform");
        VideoFileSource src;
        QVERIFY(src.open(clip("vfr.mp4")));
        QCOMPARE(src.frameSize(), QSize(64, 32));
        for (int i = 0; i < 4; ++i)
            QVERIFY(src.nextFrame());
        QVERIFY(src.glTexture() != 0);
        QCOMPARE(src.currentTime(), 0.2);
        // Frame 3 has luma 76 in limited range: (76 - 16) * 255 / 219 ≈ 70 on every channel.
        const uint8_t *px = src.frameData() + (16 * 64 + 32) * 3;
        for (int c = 0; c < 3; ++c)
            QVERIFY2(qAbs(int(px[c]) - 70) <= 4, qPrintable(QString::number(px[c])));

        VideoFileSource portrait;
        QVERIFY(portrait.open(clip("portrait.mp4")));
        QVERIFY(portrait.nextFrame());
        QCOMPARE(portrait.frameSize(), QSize(32, 64));
    }

    void source_hardwarePathMatchesSoftware() {
        if (QGuiApplication::platformName() == QLatin1String("offscreen")
            || !prism::GpuVideoUploader::instance().available())
            QSKIP("no OpenGL on this platform");
        const QList<prism::hwaccel::Backend> backends = prism::hwaccel::availableDecodeBackends();
        if (backends.isEmpty() || !QFile::exists(clip("rec.mkv")))
            QSKIP("no hardware decoder on this machine");

        auto centre = [](VideoFileSource &src) {
            const uint8_t *p = src.frameData() + (90 * 320 + 160) * 3;
            return QColor(p[0], p[1], p[2]);
        };
        VideoDecoder::setMode(VideoDecoder::Mode::Software, prism::hwaccel::Backend::None);
        VideoFileSource sw;
        QVERIFY(sw.open(clip("rec.mkv")));
        sw.seek(0.2);
        QVERIFY(sw.nextFrame());
        const QColor expected = centre(sw);

        for (const prism::hwaccel::Backend backend : backends) {
            VideoDecoder::setMode(VideoDecoder::Mode::Hardware, backend);
            VideoFileSource hw;
            QVERIFY(hw.open(clip("rec.mkv")));
            hw.seek(0.2);
            QVERIFY(hw.nextFrame());
            QCOMPARE(hw.currentTime(), sw.currentTime());
            const QColor got = centre(hw);
            qInfo() << prism::hwaccel::name(backend) << "upload path"
                    << int(prism::GpuVideoUploader::lastUploadPath())
                    << prism::GpuVideoUploader::lastZeroCopyDeclineReason() << got << expected;
            QVERIFY(qAbs(got.red() - expected.red()) <= 4);
            QVERIFY(qAbs(got.green() - expected.green()) <= 4);
            QVERIFY(qAbs(got.blue() - expected.blue()) <= 4);
        }
        VideoDecoder::setMode(VideoDecoder::Mode::Software, prism::hwaccel::Backend::None);
    }

    void decoder_rejectsNonVideo() {
        const QString path = clip("not-video.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("hello");
        file.close();
        VideoDecoder dec;
        QVERIFY(!dec.open(path));
    }
};

QTEST_MAIN(TestMedia)
#include "test_media.moc"
