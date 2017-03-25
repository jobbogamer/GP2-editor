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
 * first item is a vector of integer node IDs, and the second is a vector of
 * integer edge IDs.
 */
typedef QPair< QVector<int>, QVector<int> > Morphism;

enum GraphChangeType {
    MORPHISM,
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
    RULE,
    RULE_MATCH,
    RULE_MATCH_FAILED,
    RULE_APPLICATION,
    RULE_SET,
    LOOP,
    LOOP_ITERATION,
    PROCEDURE,
    IF_CONTEXT,
    TRY_CONTEXT,
    BRANCH_CONDITION,
    THEN_BRANCH,
    ELSE_BRANCH,
    OR_CONTEXT,
    OR_LEFT,
    OR_RIGHT,
    SKIP,
    BREAK,
    FAIL,
    END_CONTEXT,
    UNKNOWN
};

struct TraceStep {
    TraceStepType type;
    QVector<GraphChange> graphChanges;
};

/**
 * Converts from an XML tag name to a TraceStepType enum value.
 */
TraceStepType stepTypeFromXML(QStringRef elementName);

}

#endif // TRACESTEP_H
