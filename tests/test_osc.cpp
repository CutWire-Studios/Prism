#include "ui/remote/OscProtocol.h"

#include <QtEndian>
#include <cstring>
#include <QtTest>

using OscProtocol::OscMessage;

namespace {

QByteArray pad(QByteArray s)
{
    s.append('\0');
    while (s.size() % 4)
        s.append('\0');
    return s;
}

QByteArray be32(quint32 v)
{
    char buf[4];
    qToBigEndian(v, buf);
    return QByteArray(buf, 4);
}

QByteArray bundle(const QList<QByteArray> &elements)
{
    QByteArray out = pad("#bundle") + QByteArray(8, '\0');
    for (const QByteArray &e : elements)
        out += be32(quint32(e.size())) + e;
    return out;
}

} // namespace

class TestOsc : public QObject {
    Q_OBJECT

private slots:
    void encode_layout() {
        const QByteArray bytes = OscProtocol::encode({QStringLiteral("/prism/cut"), {}});
        QCOMPARE(bytes, pad("/prism/cut") + pad(","));
    }

    void roundTrip_intFloatString() {
        const OscMessage in{QStringLiteral("/prism/a/slot"), {3, 0.5f, QStringLiteral("blackout")}};
        const QVector<OscMessage> out = OscProtocol::decode(OscProtocol::encode(in));
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].address, in.address);
        QCOMPARE(out[0].args.size(), 3);
        QCOMPARE(out[0].args[0].typeId(), int(QMetaType::Int));
        QCOMPARE(out[0].args[0].toInt(), 3);
        QCOMPARE(out[0].args[1].typeId(), int(QMetaType::Float));
        QCOMPARE(out[0].args[1].toFloat(), 0.5f);
        QCOMPARE(out[0].args[2].toString(), QStringLiteral("blackout"));
    }

    void decode_doubleInt64Bools() {
        QByteArray d(8, '\0');
        const double value = 2.5;
        quint64 bits;
        std::memcpy(&bits, &value, sizeof bits);
        qToBigEndian(bits, d.data());
        QByteArray h(8, '\0');
        qToBigEndian<qint64>(-7, h.data());

        const QByteArray packet = pad("/x") + pad(",dhTFN") + d + h;
        const QVector<OscMessage> out = OscProtocol::decode(packet);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].args.size(), 5);
        QCOMPARE(out[0].args[0].toDouble(), 2.5);
        QCOMPARE(out[0].args[1].toLongLong(), qint64(-7));
        QCOMPARE(out[0].args[2].toBool(), true);
        QCOMPARE(out[0].args[3].toBool(), false);
        QVERIFY(!out[0].args[4].isValid());
    }

    void decode_noTypeTag() {
        const QVector<OscMessage> out = OscProtocol::decode(pad("/prism/auto"));
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].address, QStringLiteral("/prism/auto"));
        QVERIFY(out[0].args.isEmpty());
    }

    void decode_nestedBundle() {
        const QByteArray cut = OscProtocol::encode({QStringLiteral("/prism/cut"), {}});
        const QByteArray fader = OscProtocol::encode({QStringLiteral("/prism/fader"), {1.0f}});
        const QVector<OscMessage> out = OscProtocol::decode(bundle({cut, bundle({fader})}));
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[0].address, QStringLiteral("/prism/cut"));
        QCOMPARE(out[1].address, QStringLiteral("/prism/fader"));
    }

    void decode_rejectsMalformed_data() {
        QTest::addColumn<QByteArray>("packet");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("unaligned") << QByteArray("/ab");
        QTest::newRow("no slash") << pad("abc");
        QTest::newRow("unterminated") << QByteArray("/abc/def");
        QTest::newRow("bad tags") << pad("/x") + pad("ii");
        QTest::newRow("truncated int") << pad("/x") + pad(",ii") + be32(1);
        QTest::newRow("unknown tag") << pad("/x") + pad(",b") + be32(0);
        QTest::newRow("bundle overrun") << pad("#bundle") + QByteArray(8, '\0') + be32(64) + pad("/x");
        QTest::newRow("bundle zero size") << pad("#bundle") + QByteArray(8, '\0') + be32(0);
    }

    void decode_rejectsMalformed() {
        QFETCH(QByteArray, packet);
        QVERIFY(OscProtocol::decode(packet).isEmpty());
    }

    void decode_badBundleElementDropsAll() {
        const QByteArray good = OscProtocol::encode({QStringLiteral("/prism/cut"), {}});
        QVERIFY(OscProtocol::decode(bundle({good, pad("bad")})).isEmpty());
    }
};

QTEST_MAIN(TestOsc)
#include "test_osc.moc"
