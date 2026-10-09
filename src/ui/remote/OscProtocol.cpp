#include "ui/remote/OscProtocol.h"

#include <QtEndian>
#include <cstring>

namespace OscProtocol {

namespace {

constexpr int kMaxBundleDepth = 8;

int padded(int n) { return (n + 3) & ~3; }

bool readString(const QByteArray &data, int &pos, QString &out)
{
    const int end = data.indexOf('\0', pos);
    if (end < 0)
        return false;
    const int next = pos + padded(end - pos + 1);
    if (next > data.size())
        return false;
    out = QString::fromUtf8(data.constData() + pos, end - pos);
    pos = next;
    return true;
}

bool readMessage(const QByteArray &data, QVector<OscMessage> &out)
{
    int pos = 0;
    OscMessage msg;
    if (!readString(data, pos, msg.address) || !msg.address.startsWith(QLatin1Char('/')))
        return false;

    if (pos >= data.size()) {
        out.append(msg);
        return true;
    }

    QString tags;
    if (!readString(data, pos, tags) || !tags.startsWith(QLatin1Char(',')))
        return false;

    const char *raw = data.constData();
    auto need = [&](int bytes) { return pos + bytes <= data.size(); };

    for (int i = 1; i < tags.size(); ++i) {
        switch (tags.at(i).toLatin1()) {
        case 'i':
            if (!need(4)) return false;
            msg.args.append(qFromBigEndian<qint32>(raw + pos));
            pos += 4;
            break;
        case 'f': {
            if (!need(4)) return false;
            const quint32 bits = qFromBigEndian<quint32>(raw + pos);
            float f;
            std::memcpy(&f, &bits, sizeof f);
            msg.args.append(f);
            pos += 4;
            break;
        }
        case 'h':
            if (!need(8)) return false;
            msg.args.append(qFromBigEndian<qint64>(raw + pos));
            pos += 8;
            break;
        case 'd': {
            if (!need(8)) return false;
            const quint64 bits = qFromBigEndian<quint64>(raw + pos);
            double d;
            std::memcpy(&d, &bits, sizeof d);
            msg.args.append(d);
            pos += 8;
            break;
        }
        case 's': {
            QString s;
            if (!readString(data, pos, s)) return false;
            msg.args.append(s);
            break;
        }
        case 'T': msg.args.append(true); break;
        case 'F': msg.args.append(false); break;
        case 'N': msg.args.append(QVariant()); break;
        default:
            return false;
        }
    }
    out.append(msg);
    return true;
}

bool readPacket(const QByteArray &data, QVector<OscMessage> &out, int depth)
{
    if (data.isEmpty() || data.size() % 4 != 0)
        return false;
    if (!data.startsWith("#bundle"))
        return readMessage(data, out);
    if (depth >= kMaxBundleDepth || data.size() < 16 || data.at(7) != '\0')
        return false;

    int pos = 16; // "#bundle\0" + 8-byte timetag
    while (pos < data.size()) {
        if (pos + 4 > data.size())
            return false;
        const qint32 size = qFromBigEndian<qint32>(data.constData() + pos);
        pos += 4;
        if (size <= 0 || size > data.size() - pos)
            return false;
        if (!readPacket(data.mid(pos, size), out, depth + 1))
            return false;
        pos += size;
    }
    return true;
}

void writeString(QByteArray &out, const QString &s)
{
    const QByteArray utf8 = s.toUtf8();
    out.append(utf8);
    out.append(padded(utf8.size() + 1) - utf8.size(), '\0');
}

void writeInt(QByteArray &out, quint32 v)
{
    char buf[4];
    qToBigEndian(v, buf);
    out.append(buf, 4);
}

} // namespace

QVector<OscMessage> decode(const QByteArray &packet)
{
    QVector<OscMessage> out;
    if (!readPacket(packet, out, 0))
        out.clear();
    return out;
}

QByteArray encode(const OscMessage &message)
{
    QString tags = QStringLiteral(",");
    QByteArray body;
    for (const QVariant &arg : message.args) {
        switch (arg.typeId()) {
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::Bool:
            tags += QLatin1Char('i');
            writeInt(body, quint32(arg.toInt()));
            break;
        case QMetaType::Float:
        case QMetaType::Double: {
            tags += QLatin1Char('f');
            const float f = arg.toFloat();
            quint32 bits;
            std::memcpy(&bits, &f, sizeof bits);
            writeInt(body, bits);
            break;
        }
        default:
            tags += QLatin1Char('s');
            writeString(body, arg.toString());
            break;
        }
    }

    QByteArray out;
    writeString(out, message.address);
    writeString(out, tags);
    out.append(body);
    return out;
}

} // namespace OscProtocol
