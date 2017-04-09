#include "tracerunner.hpp"

#include <QDebug>
#include "translate/translate.hpp"

namespace Developer {

#define QSTRING(string) QString::fromStdString(string)

TraceRunner::TraceRunner(QString tracefilePath, Graph* graph, QVector<Token*> programTokens) :
    _graph(graph),
    _traceParser(tracefilePath),
    _traceHighlighter(programTokens),
    _initialised(false),
    _traceSteps(),
    _currentStep(-1),
    _contextStack(),
    _error()
{
    // Check that the TraceParser was initialised successfully.
    if (!_traceParser.isInitialised()) { return; }

    // Parse the first step in the trace to get started.
    TraceStep step;
    bool success = _traceParser.parseStep(&step);
    if (!success) { return; }
    _traceSteps.push_back(step);

    // If we reach this point, we have successfully opened the tracefile,
    // initialised the XML reader, and found the root <trace> element.
    _currentStep = 0;
    _initialised = true;

    // Now that the first step in the trace is prepared, update the program position
    // to highlight that first step.
    TraceStep* firstStep = &(_traceSteps[0]);
    _traceHighlighter.update(firstStep, FORWARDS);
}

Graph* TraceRunner::graph() {
    return _graph;
}

bool TraceRunner::isInitialised() {
    return _initialised;
}

bool TraceRunner::isForwardStepAvailable() {
    // If we are at the end of the trace step vector, and parsing is complete,
    // we must be at the end of the trace itself, so forward steps are not
    // available from here.
    return (! (_currentStep >= _traceSteps.size() && _traceParser.isParseComplete()));
}

bool TraceRunner::isBackwardStepAvailable() {
    // If we are at the start of the trace step vector, backwards steps are not
    // available. Whether or not parsing is complete makes no difference,
    // because parsing always starts at the beginning.
    if (_currentStep <= 0) {
        return false;
    }
    return true;
}

bool TraceRunner::isFindMatchAvailable() {
    // Find match is only available if the current step type is RULE_MATCH or RULE_MATCH_FAILED.
    if (_currentStep >= _traceSteps.size() && _traceParser.isParseComplete()) { return false; }
    TraceStep& step = _traceSteps[_currentStep];
    return (step.type == RULE_MATCH || step.type == RULE_MATCH_FAILED);
}

bool TraceRunner::isMatchApplicationAvailable() {
    // Match application is only available if the current step type is RULE_APPLICATION.
    if (_currentStep >= _traceSteps.size() && _traceParser.isParseComplete()) { return false; }
    TraceStep& step = _traceSteps[_currentStep];
    return (step.type == RULE_APPLICATION);
}

bool TraceRunner::stepForward() {
    // Sanity check...
    if (!isForwardStepAvailable()) {
        _error = "Attempted to step forwards at end of trace.";
        return false;
    }

    TraceStep& step = _traceSteps[_currentStep];
    if (step.type == RULE_APPLICATION) {
        applyCurrentStepChanges();
    }
    else if (step.endOfContext) {
        exitContext();
    }
    else {
        enterContext(step);
    }

    // Move on to the next step.
    _currentStep += 1;

    // If parsing is not complete, and the new step position does not exist in
    // the step vector, parse the next step.
    if (!_traceParser.isParseComplete() && _traceSteps.size() <= _currentStep) {
        qDebug() << "Parsing a new step";
        TraceStep step;
        bool validStep = _traceParser.parseStep(&step);
        if (!_traceParser.isParseComplete()) {
            if (validStep) {
                _traceSteps.push_back(step);
            }
            else {
                return false;
            }
        }
    }

    // We had to wait until after parsing a step to update the program position,
    // because if we had not yet parsed the next step, we would not know what
    // type of program token to search for.
    TraceStep* nextStep = (_currentStep < _traceSteps.size()) ? &(_traceSteps[_currentStep]) : 0;
    _traceHighlighter.update(nextStep, FORWARDS);

    return true;
}

bool TraceRunner::stepBackward() {
    // Sanity check...
    if (!isBackwardStepAvailable()) {
        _error = "Attempted to step backwards at beginning of trace.";
        return false;
    }

    // Move current step position backwards, then revert the changes. We have to
    // move before reverting because the "current step" refers to the step which
    // will be *applied* if stepForward() is called.
    _currentStep -= 1;

    TraceStep& step = _traceSteps[_currentStep];
    if (step.type == RULE_APPLICATION) {
        revertCurrentStepChanges();
    }
    else if (step.endOfContext) {
        enterContext(step);
    }
    else {
        exitContext();
    }

    // Now update the program position to reflect that stepping forwards from
    // here will re-apply the step we just reverted.
    TraceStep* nextStep = &(_traceSteps[_currentStep]);
    _traceHighlighter.update(nextStep, BACKWARDS);

    // We are not parsing anything because any time we go backwards, we have
    // already parsed the steps before the current one. Therefore we will never
    // encounter an error.
    return true;
}


bool TraceRunner::goToEnd() {
    // We will simply call stepForward() in a loop until we reach the end of
    // the trace (or an error occurs). We have to do this because there is no
    // way to directly jump to the output graph -- whilst a successful program
    // will have produced a .host file containing the output graph, a program
    // which ends in failure does not, so we have to step through, applying all
    // the graph changes one at a time, until we reach the end.
    bool success = true;
    while (isForwardStepAvailable() && success) {
        success = stepForward();
    }

    return success;
}


bool TraceRunner::goToStart() {
    // Since the TraceRunner only has a pointer to the graph, and does not store
    // the filename of the input graph, we will have to repeatedly step backwards
    // until we reach the start of the program. If we had the input graph file, we
    // could just replace _graph and jump directly back to step 0.
    bool success = true;
    while (isBackwardStepAvailable() && success) {
        success = stepBackward();
    }

    return success;
}


void TraceRunner::enterContext(TraceStep& context) {
    _contextStack.push(context.type);
}


void TraceRunner::exitContext() {
    _contextStack.pop();
}


/**
 * Apply all the GraphChanges in the current TraceStep. This means that the current
 * step must be a rule application.
 */
void TraceRunner::applyCurrentStepChanges() {
    // Sanity check. The way parsing works means this should never fail, but it's
    // better to make sure.
    Q_ASSERT(_currentStep >= 0 && _currentStep < _traceSteps.size());

    // Check that this is actually a rule application. There cannot be any graph
    // changes in any other type of step.
    TraceStep& step = _traceSteps[_currentStep];
    if (step.type != RULE_APPLICATION) { return; }

    // Iterate over the graph changes, applying each one in turn.
    for (QVector<GraphChange>::Iterator change = step.graphChanges.begin();
         change != step.graphChanges.end();
         change++)
    {
        switch (change->type) {
        case ADD_EDGE:
        {
            edge_t newEdge = boost::get<edge_t>(change->newItem);
            _graph->addEdge(QSTRING(newEdge.id),
                            _graph->node(QSTRING(newEdge.from)),
                            _graph->node(QSTRING(newEdge.to)),
                            QSTRING(ListToString(newEdge.label.values)),
                            QSTRING(newEdge.label.mark));
            break;
        }

        case ADD_NODE:
        {
            node_t newNode = boost::get<node_t>(change->newItem);
            _graph->addNode(QSTRING(newNode.id),
                            QSTRING(ListToString(newNode.label.values)),
                            QSTRING(newNode.label.mark),
                            newNode.isRoot);
            break;
        }

        case DELETE_EDGE:
        {
            edge_t edge = boost::get<edge_t>(change->existingItem);
            _graph->removeEdge(QSTRING(edge.id));
            break;
        }

        case DELETE_NODE:
        {
            node_t node = boost::get<node_t>(change->existingItem);

            // Before deleting the node from the graph, store its position on
            // the canvas so that it can be restored to the same position when
            // stepping backwards.
            Node* graphNode = _graph->node(QSTRING(node.id));
            node.xPos = graphNode->xPos();
            node.yPos = graphNode->yPos();
            change->existingItem = node;

            _graph->removeNode(QSTRING(node.id));
            break;
        }

        case RELABEL_EDGE:
        {
            edge_t edge = boost::get<edge_t>(change->newItem);

            // Get a pointer to the edge in the graph using its ID, so that its
            // label can be updated.
            Edge* graphEdge = _graph->edge(QSTRING(edge.id));

            // Get the label from the newItem edge and apply it to the Edge object.
            graphEdge->setLabel(QSTRING(ListToString(edge.label.values)));

            break;
        }

        case RELABEL_NODE:
        {
            node_t node = boost::get<node_t>(change->newItem);

            // Get a pointer to the node in the graph using its ID, so that its
            // label can be updated.
            Node* graphNode = _graph->node(QSTRING(node.id));

            // Get the label from the newItem node and apply it to the Node object.
            graphNode->setLabel(QSTRING(ListToString(node.label.values)));

            break;
        }

        case REMARK_EDGE:
        {
            edge_t edge = boost::get<edge_t>(change->newItem);

            // Get a pointer to the edge in the graph using its ID, so that its
            // mark can be updated.
            Edge* graphEdge = _graph->edge(QSTRING(edge.id));

            // Get the mark from the newItem edge and apply it to the Edge object.
            graphEdge->setMark(QSTRING(edge.label.mark));

            break;
        }

        case REMARK_NODE:
        {
            node_t node = boost::get<node_t>(change->newItem);

            // Get a pointer to the node in the graph using its ID, so that its
            // mark can be updated.
            Node* graphNode = _graph->node(QSTRING(node.id));

            // Get the mark from the newItem node and apply it to the Node object.
            graphNode->setMark(QSTRING(node.label.mark));

            break;
        }

        case SET_ROOT:
        {
            node_t node = boost::get<node_t>(change->newItem);

            // Get a pointer to the node in the graph using its ID, so that it
            // can be made a root node.
            Node* graphNode = _graph->node(QSTRING(node.id));
            graphNode->setIsRoot(true);

            break;
        }

        case REMOVE_ROOT:
        {
            node_t node = boost::get<node_t>(change->newItem);

            // Get a pointer to the node in the graph using its ID, so that it
            // can be made a non-root node.
            Node* graphNode = _graph->node(QSTRING(node.id));
            graphNode->setIsRoot(false);

            break;
        }

        default:
            // This is an invalid change type.
            qDebug() << "Invalid graph change in trace step at position" << _currentStep;
            continue;
        }
    }
}


/** Revert all the changes from the current TraceStep. This means that the current
 * step must be a rule application. */
void TraceRunner::revertCurrentStepChanges() {
    // Sanity check. The way parsing works means this should never fail, but it's
    // better to make sure.
    Q_ASSERT(_currentStep >= 0 && _currentStep < _traceSteps.size());

    // Check that this is actually a rule application.
    TraceStep& step = _traceSteps[_currentStep];
    if (step.type != RULE_APPLICATION) { return; }

    // Iterate over the graph changes, applying each one in turn. Note that we
    // must iterate *backwards*, because (for example) we cannot delete a node
    // if an edge was added after the node was added, so changes have to be
    // undone in reverse.
    // Qt 4.8 doesn't have QVector::rbegin() and rend() so we have to use a while
    // loop to iterate backwards.
    QVector<GraphChange>::Iterator change = step.graphChanges.end();
    while (change != step.graphChanges.begin())
    {
        // Decrement iterator at the beginning because .end() is the element "after"
        // the last element in the vector.
        change--;

        switch (change->type) {
        case ADD_EDGE:
        {
            edge_t createdEdge = boost::get<edge_t>(change->newItem);
            _graph->removeEdge(QSTRING(createdEdge.id));
            break;
        }

        case ADD_NODE:
        {
            node_t createdNode = boost::get<node_t>(change->newItem);
            _graph->removeNode(QSTRING(createdNode.id));
            break;
        }

        case DELETE_EDGE:
        {
            edge_t deletedEdge = boost::get<edge_t>(change->existingItem);
            _graph->addEdge(QSTRING(deletedEdge.id),
                            _graph->node(QSTRING(deletedEdge.from)),
                            _graph->node(QSTRING(deletedEdge.to)),
                            QSTRING(ListToString(deletedEdge.label.values)),
                            QSTRING(deletedEdge.label.mark));
            break;
        }

        case DELETE_NODE:
        {
            node_t deletedNode = boost::get<node_t>(change->existingItem);
            _graph->addNode(QSTRING(deletedNode.id),
                            QSTRING(ListToString(deletedNode.label.values)),
                            QSTRING(deletedNode.label.mark),
                            deletedNode.isRoot,
                            false,
                            QPointF(deletedNode.xPos, deletedNode.yPos));
            break;
        }

        case RELABEL_EDGE:
        {
            edge_t edge = boost::get<edge_t>(change->existingItem);

            // Get a pointer to the edge in the graph using its ID, so that its
            // label can be updated.
            Edge* graphEdge = _graph->edge(QSTRING(edge.id));

            // Get the label from the existingItem edge (since we are reverting
            // the label back to its original value) and apply it to the Edge object.
            graphEdge->setLabel(QSTRING(ListToString(edge.label.values)));

            break;
        }

        case RELABEL_NODE:
        {
            node_t node = boost::get<node_t>(change->existingItem);

            // Get a pointer to the node in the graph using its ID, so that its
            // label can be updated.
            Node* graphNode = _graph->node(QSTRING(node.id));

            // Get the label from the existingItem node (since we are reverting
            // the label back to its original value) and apply it to the Node object.
            graphNode->setLabel(QSTRING(ListToString(node.label.values)));

            break;
        }

        case REMARK_EDGE:
        {
            edge_t edge = boost::get<edge_t>(change->existingItem);

            // Get a pointer to the edge in the graph using its ID, so that its
            // label can be updated.
            Edge* graphEdge = _graph->edge(QSTRING(edge.id));

            // Get the mark from the existingItem edge (since we are reverting
            // the mark back to its original value) and apply it to the Edge object.
            graphEdge->setMark(QSTRING(edge.label.mark));

            break;
        }

        case REMARK_NODE:
        {
            node_t node = boost::get<node_t>(change->existingItem);

            // Get a pointer to the node in the graph using its ID, so that its
            // label can be updated.
            Node* graphNode = _graph->node(QSTRING(node.id));

            // Get the mark from the existingItem node (since we are reverting
            // the mark back to its original value) and apply it to the Node object.
            graphNode->setMark(QSTRING(node.label.mark));

            break;
        }

        case SET_ROOT:
        {
            node_t node = boost::get<node_t>(change->newItem);

            // Get a pointer to the node in the graph using its ID, so that it
            // can be reverted to a non-root node.
            Node* graphNode = _graph->node(QSTRING(node.id));
            graphNode->setIsRoot(false);

            break;
        }

        case REMOVE_ROOT:
        {
            node_t node = boost::get<node_t>(change->newItem);

            // Get a pointer to the node in the graph using its ID, so that it
            // can be reverted to a root node.
            Node* graphNode = _graph->node(QSTRING(node.id));
            graphNode->setIsRoot(true);

            break;
        }

        default:
            // This is an invalid change type.
            qDebug() << "Invalid graph change in trace step at position" << _currentStep;
            continue;
        }
    }
}

QString TraceRunner::getError() {
    if (_traceParser.hasError()) {
        return _traceParser.getError();
    }

    return _error;
}

}
