#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class ClipManager;

/// The entries (sub-folders, then clips) of one folder of the media bin, filtered by a search
/// string. ClipManager has no change signal, so the model polls it and resets on a difference.
class AssetLibraryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString folderId READ folderId WRITE setFolderId NOTIFY folderIdChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(QString folderName READ folderName NOTIFY folderIdChanged)
    Q_PROPERTY(QString parentFolderId READ parentFolderId NOTIFY folderIdChanged)
    Q_PROPERTY(int clipCount READ clipCount NOTIFY changed)

public:
    enum Role { KeyRole = Qt::UserRole + 1, NameRole, IsFolderRole, SuffixRole, IsAudioRole };

    explicit AssetLibraryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString folderId() const { return m_folderId; }
    void setFolderId(const QString &id);
    QString search() const { return m_search; }
    void setSearch(const QString &s);
    QString folderName() const;
    QString parentFolderId() const;
    int clipCount() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString createFolder(const QString &name);
    Q_INVOKABLE bool renameFolder(const QString &id, const QString &name);
    Q_INVOKABLE void removeFolder(const QString &id);
    Q_INVOKABLE void removeClip(const QString &path);
    Q_INVOKABLE bool moveClip(const QString &path, const QString &folderId);
    Q_INVOKABLE quint64 addToGraph(const QString &path);

signals:
    void folderIdChanged();
    void searchChanged();
    void changed();

private:
    struct Entry {
        QString key;
        QString name;
        bool folder = false;
    };

    ClipManager *clips() const;
    QList<Entry> collect() const;
    QString signature() const;

    QList<Entry> m_entries;
    QString m_folderId;
    QString m_search;
    QString m_signature;
    QTimer m_poll;
};
