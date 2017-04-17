#ifndef GRAPHSNAPSHOT_HPP
#define GRAPHSNAPSHOT_HPP

#include "node.hpp"
#include "edge.hpp"

namespace Developer {

struct SnapshotNode {
    QString id;
    QString label;
    QString mark;
    bool isRoot;
    QPointF pos;
};

struct SnapshotEdge {
    QString id;
    QString label;
    QString mark;
    QString from;
    QString to;
};

struct GraphSnapshot {
    std::vector<SnapshotNode> nodes;
    std::vector<SnapshotEdge> edges;
};

}

#endif // GRAPHSNAPSHOT_HPP
