#ifndef TRACESTEP_H
#define TRACESTEP_H

#include <QVector>
#include <QPair>
#include <boost/variant.hpp>
#include "node.hpp"
#include "edge.hpp"
#include "parsertypes.hpp"

namespace Developer {

typedef boost::variant<edge_t, node_t> graph_item_t;

/**
 * A Morphism is a pair of vectors representing a rule match morphism. The
 * first item is a vector of node_t objects, and the second is a vector of
 * edge_t objects.
 */
typedef QPair< QVector<node_t>, QVector<edge_t> > Morphism;

enum GraphChangeType {
    ADD_EDGE,
    ADD_NODE,
    DELETE_EDGE,
    DELETE_NODE,
    RELABEL_EDGE,
    RELABEL_NODE,
    REMARK_EDGE,
    REMARK_NODE,
    SET_ROOT
};

struct GraphChange {
    GraphChangeType type;
    graph_item_t existingItem;
    graph_item_t newItem;
};

enum TraceStepType {
    RULE_APPLICATION,
    RULE_SET,
    LOOP,
    PROCEDURE,
    IF_CONDITION,
    TRY_CONDITION,
    THEN_BRANCH,
    ELSE_BRANCH,
    OR_LEFT,
    OR_RIGHT,
    SKIP,
    BREAK,
    FAIL
};

struct TraceStep {
    TraceStepType type;
    QVector<GraphChange> graphChanges;
};

}

#endif // TRACESTEP_H
