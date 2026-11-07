#include "app/nodes/NodeCatalog.h"
#include "core/graph/ClipGraph.h"

#include <QtTest>

class TestNodeCatalog : public QObject {
    Q_OBJECT

    static int indexOf(const QList<QuickAddEntry> &l, const QString &name) {
        for (int i = 0; i < l.size(); ++i)
            if (l.at(i).name.compare(name, Qt::CaseInsensitive) == 0)
                return i;
        return -1;
    }

private slots:
    void idsAreUniqueAndDescribed() {
        QSet<QString> ids;
        for (const QuickAddEntry &e : NodeCatalog::quickAddCatalogue()) {
            QVERIFY2(!e.description.trimmed().isEmpty(), qPrintable(e.id));
            QVERIFY2(!e.name.isEmpty() && !e.category.isEmpty(), qPrintable(e.id));
            QVERIFY2(!ids.contains(e.id), qPrintable(e.id));
            ids.insert(e.id);
        }
        QVERIFY(ids.contains("process:blend"));
        QVERIFY(ids.contains("source:camera"));
    }

    void scoringOrder() {
        const auto r = NodeCatalog::quickAddSearch("blu");
        const int blur = indexOf(r, "Blur");
        const int directional = indexOf(r, "Directional blur");
        QCOMPARE(blur, 0);
        QVERIFY(directional > blur);
        int firstDescriptionOnly = -1;
        for (int i = 0; i < r.size(); ++i) {
            const QuickAddEntry &e = r.at(i);
            if (!e.name.toLower().contains("blu") && !e.category.toLower().contains("blu")) {
                firstDescriptionOnly = i;
                break;
            }
        }
        QVERIFY(firstDescriptionOnly > directional);
        QCOMPARE(NodeCatalog::quickAddScore("BLU", r.first()), NodeCatalog::quickAddScore("blu", r.first()));
    }

    void emptyQueryGroupsByCategory() {
        const auto r = NodeCatalog::quickAddSearch({});
        QCOMPARE(r.size(), NodeCatalog::quickAddCatalogue().size());
        QSet<QString> closed;
        for (int i = 1; i < r.size(); ++i) {
            if (r.at(i).category != r.at(i - 1).category) {
                QVERIFY(!closed.contains(r.at(i).category));
                closed.insert(r.at(i - 1).category);
            }
        }
    }

    void compatibilityFilter() {
        const int chainOut = int(ClipGraph::PortKind::ChainOut);
        const auto r = NodeCatalog::quickAddSearch({}, chainOut, true);
        QVERIFY(!r.isEmpty());
        for (const QuickAddEntry &e : r)
            QVERIFY2(e.inKinds.contains(int(ClipGraph::PortKind::ChainIn))
                     || e.inKinds.contains(int(ClipGraph::PortKind::RerouteIn)), qPrintable(e.id));
        QVERIFY(indexOf(r, "Blur") >= 0);
        QCOMPARE(indexOf(r, "Camera"), -1);
        QCOMPARE(indexOf(r, "Mixer"), -1);
        QCOMPARE(indexOf(r, "LFO"), -1);

        const auto fromInput = NodeCatalog::quickAddSearch({}, int(ClipGraph::PortKind::ChainIn), false);
        QVERIFY(indexOf(fromInput, "Camera") >= 0);
        QVERIFY(indexOf(fromInput, "Blur") >= 0);
        QCOMPARE(indexOf(fromInput, "Mixer"), -1);
    }
};

QTEST_MAIN(TestNodeCatalog)
#include "test_nodecatalog.moc"
