#pragma once

#include <QVector>

#include "core/graph/ClipGraph.h"

/// Round-robin picker for live node thumbnails. Each tick hands out the next `budget` nodes of the
/// visible set, continuing after the last node served, so every visible node is refreshed within
/// ceil(N / budget) ticks. Nodes that are not visible are never picked.
class ThumbScheduler {
public:
    /// Thumbnails refreshed per tick, and the tick period. 4 per 300 ms is ~13 refreshes/s in
    /// total however many nodes are on screen; N visible nodes each update every ceil(N/4) * 300 ms.
    static constexpr int kBudget = 4;
    static constexpr int kTickMs = 300;

    void setVisible(const QVector<NodeId> &ids) { m_visible = ids; }
    const QVector<NodeId> &visible() const { return m_visible; }

    QVector<NodeId> nextBatch(int budget = kBudget)
    {
        QVector<NodeId> out;
        if (m_visible.isEmpty() || budget <= 0)
            return out;
        // Resume after the last served node, so a changing visible set neither restarts at the
        // front nor skips nodes that were next in line.
        int start = 0;
        const int at = m_visible.indexOf(m_last);
        if (m_last != 0 && at >= 0)
            start = (at + 1) % int(m_visible.size());
        const int n = qMin(budget, int(m_visible.size()));
        for (int i = 0; i < n; ++i)
            out.append(m_visible.at((start + i) % int(m_visible.size())));
        m_last = out.last();
        return out;
    }

private:
    QVector<NodeId> m_visible;
    NodeId m_last = 0;
};
