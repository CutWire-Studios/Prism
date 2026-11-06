#include "app/online/updates/UpdateAsset.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace prism {

namespace {

bool isGitHubDownload(const QUrl &url)
{
    if (url.scheme() != QLatin1String("https") || url.host() != QLatin1String("github.com"))
        return false;
    if (!url.userName().isEmpty() || !url.password().isEmpty())
        return false;
    return url.path().contains(QLatin1String("/releases/download/"));
}

QString sha256Digest(const QJsonObject &asset)
{
    const QString digest = asset.value(QStringLiteral("digest")).toString();
    const QString prefix = QStringLiteral("sha256:");
    if (!digest.startsWith(prefix))
        return {};
    const QString hex = digest.mid(prefix.size()).trimmed().toLower();
    for (const QChar c : hex) {
        if (!c.isDigit() && (c < QLatin1Char('a') || c > QLatin1Char('f')))
            return {};
    }
    return hex.size() == 64 ? hex : QString();
}

} // namespace

ReleaseAsset selectReleaseAsset(const QJsonArray &assets, const QString &platform, const QString &arch)
{
    const bool windows = platform == QLatin1String("windows");
    const bool macos = platform == QLatin1String("macos");
    QString suffix;
    const bool arm64 = arch == QLatin1String("arm64") || arch == QLatin1String("aarch64");
    if (windows && arch == QLatin1String("x86_64"))
        suffix = QStringLiteral("-x64.exe");
    else if (macos && arm64)
        suffix = QStringLiteral("-arm64.dmg");
    else if (platform == QLatin1String("linux") && arch == QLatin1String("x86_64"))
        suffix = QStringLiteral("-x86_64.AppImage");
    else
        return {};

    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (!name.endsWith(suffix))
            continue;
        // The portable zip is not an installer, and a dmg is never named Setup.
        if (windows && !name.startsWith(QLatin1String("Prism-Setup-")))
            continue;
        if (!windows && !name.startsWith(QLatin1String("Prism-")))
            continue;

        const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
        if (!isGitHubDownload(url))
            continue;

        ReleaseAsset out;
        out.name = name;
        out.url = url.toString();
        out.sha256 = sha256Digest(asset);
        out.size = asset.value(QStringLiteral("size")).toVariant().toLongLong();
        return out;
    }
    return {};
}

QString parseVersionText(const QByteArray &raw)
{
    QString text = QString::fromUtf8(raw).trimmed();
    if (text.size() >= 2 && text.front() == QLatin1Char('"') && text.back() == QLatin1Char('"'))
        text = text.mid(1, text.size() - 2).trimmed();
    if (!text.isEmpty() && text.front().toLower() == QLatin1Char('v') && text.size() > 1
        && text.at(1).isDigit())
        text = text.mid(1);
    static const QRegularExpression version(QStringLiteral("^\\d+(?:\\.\\d+)*$"));
    return version.match(text).hasMatch() ? text : QString();
}

int compareVersions(const QString &a, const QString &b)
{
    const QStringList left = a.split(QLatin1Char('.'));
    const QStringList right = b.split(QLatin1Char('.'));
    for (int i = 0; i < qMax(left.size(), right.size()); ++i) {
        const int l = left.value(i).section(QLatin1Char('-'), 0, 0).toInt();
        const int r = right.value(i).section(QLatin1Char('-'), 0, 0).toInt();
        if (l != r)
            return l < r ? -1 : 1;
    }
    return 0;
}

QString sha256FromSums(const QByteArray &sums, const QString &fileName)
{
    static const QRegularExpression line(QStringLiteral("^([0-9a-fA-F]{64})\\s+\\*?(.+)$"));
    const QStringList lines = QString::fromUtf8(sums).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QRegularExpressionMatch match = line.match(raw.trimmed());
        if (match.hasMatch() && match.captured(2) == fileName)
            return match.captured(1).toLower();
    }
    return {};
}

namespace {

QStringList feedParts(const QString &feedUrl)
{
    const QStringList parts = QUrl(feedUrl).path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    // repos / owner / name / releases / latest
    if (parts.size() < 5 || parts.at(0) != QLatin1String("repos")
        || parts.at(3) != QLatin1String("releases"))
        return {};
    return parts;
}

} // namespace

QString releaseTagApiUrl(const QString &feedUrl, const QString &version)
{
    QUrl url(feedUrl);
    QString path = url.path();
    const QString latest = QStringLiteral("/releases/latest");
    if (version.isEmpty() || !path.endsWith(latest))
        return {};
    path.chop(latest.size());
    path += QStringLiteral("/releases/tags/v") + version;
    url.setPath(path);
    return url.toString();
}

QString releasePageUrl(const QString &feedUrl, const QString &version)
{
    const QStringList parts = feedParts(feedUrl);
    if (parts.isEmpty() || version.isEmpty())
        return {};
    return QStringLiteral("https://github.com/%1/%2/releases/tag/v%3")
            .arg(parts.at(1), parts.at(2), version);
}

QString releaseDownloadUrl(const QString &feedUrl, const QString &version, const QString &fileName)
{
    const QStringList parts = feedParts(feedUrl);
    if (parts.isEmpty() || version.isEmpty() || fileName.isEmpty())
        return {};
    return QStringLiteral("https://github.com/%1/%2/releases/download/v%3/%4")
            .arg(parts.at(1), parts.at(2), version, fileName);
}

QString installerFileName(const QString &platform, const QString &arch, const QString &version)
{
    if (version.isEmpty())
        return {};
    const bool arm64 = arch == QLatin1String("arm64") || arch == QLatin1String("aarch64");
    if (platform == QLatin1String("windows") && arch == QLatin1String("x86_64"))
        return QStringLiteral("Prism-Setup-%1-x64.exe").arg(version);
    if (platform == QLatin1String("macos") && arm64)
        return QStringLiteral("Prism-%1-arm64.dmg").arg(version);
    if (platform == QLatin1String("linux") && arch == QLatin1String("x86_64"))
        return QStringLiteral("Prism-%1-x86_64.AppImage").arg(version);
    return {};
}

} // namespace prism
