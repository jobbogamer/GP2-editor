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
    _snapshotStack(),
    _loopSuccessStack(),
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

    // Remove the message from the info bar, since it will no longer be relevant
    // once we move on to the next step. If there is a message related to the next
    // step, it will be set below.
    _infoBarMessage = "";

    TraceStep& step = _traceSteps[_currentStep];

    // If this is the end of a rule context, check whether the previous step was a
    // failed rule match. If it was, we need to check if we're in a loop, because
    // that means we need to revert to the previous graph snapshot.
    if (step.type == RULE && step.endOfContext) {
        TraceStep& previousStep = _traceSteps[_currentStep - 1];
        if (previousStep.type == RULE_MATCH_FAILED) {
            // The only time we *don't* want to restore the snapshot in a loop is
            // if we are in a branch condition inside a loop, because failure in
            // there does not count as failing the loop. So we search down the context
            // stack, and if we reach a loop iteration context *before* reaching a
            // branch condition context, we restore the snapshot. Otherwise, we just
            // pop the snapshot without applying it.
            bool foundBranch = false;
            bool foundLoop = false;
            QStack<TraceStepType> stackCopy = _contextStack;
            while (!stackCopy.empty() && !foundLoop) {
                TraceStepType contextType = stackCopy.pop();
                foundBranch = (contextType == BRANCH_CONDITION) || foundBranch;
                foundLoop = (contextType == LOOP_ITERATION);
            }

            if (foundLoop && !foundBranch) {
                // We want to take a snapshot here and store it in the current trace step,
                // so that if we get back here backwards, we can restore the graph to its
                // state from before we restore the snapshot below.
                step.snapshot = takeSnapshot();
                step.hasSnapshot = true;

                // Now we revert the graph back to its previous state.
                GraphSnapshot snapshot = _snapshotStack.pop();
                restoreSnapshot(snapshot);
                _infoBarMessage = "Graph has been reverted to the previous snapshot.";

                // Mark the current loop as failed.
                _loopSuccessStack.pop();
                _loopSuccessStack.push(false);
            }
        }
    }

    if (step.type == RULE_APPLICATION) {
        applyCurrentStepChanges();
    }
    else if (step.type == RULE_MATCH_FAILED) {
        // We want to tell the user that the rule failed to match. The rule name
        // is not stored in the match step, so we have to go back one step to get
        // the RULE context.
        TraceStep& previousStep = _traceSteps[_currentStep - 1];
        _infoBarMessage = "No match found for rule " + previousStep.contextName + ".";        
    }
    // We don't want to treat rule matches as contexts.
    else if (step.type != RULE_MATCH) {
        if (step.endOfContext) {
            exitContext(FORWARDS);
        }
        else {
            enterContext(step, FORWARDS);
        }
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
                // If this is the start of a loop iteration and the previous step
                // was the start of the loop, set the loopBoundary flag.
                // Do this before pushing the new step, so that _traceSteps.last()
                // returns what we want.
                if (step.type == LOOP_ITERATION && !step.endOfContext) {
                    TraceStep& previousStep = _traceSteps.last();
                    if (previousStep.type == LOOP) {
                        step.loopBoundary = true;
                    }
                }

                _traceSteps.push_back(step);

                // If this is the end of a loop, set the loopBoundary flag on the
                // previous end of iteration.
                if (step.type == LOOP && step.endOfContext) {
                    for (int i = _traceSteps.size() - 1; i >= 0; i--) {
                        TraceStep& previousStep = _traceSteps[i];
                        if (previousStep.type == LOOP_ITERATION && previousStep.endOfContext) {
                            previousStep.loopBoundary = true;
                            break;
                        }
                    }
                }
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

    // Remove the message from the info bar, since it will no longer be relevant
    // once we move on to the next step. If there is a message related to the next
    // step, it will be set below.
    _infoBarMessage = "";

    // Move current step position backwards, then revert the changes. We have to
    // move before reverting because the "current step" refers to the step which
    // will be *applied* if stepForward() is called.
    _currentStep -= 1;

    TraceStep& step = _traceSteps[_currentStep];
    if (step.type == RULE_APPLICATION) {
        revertCurrentStepChanges();
    }
    else if (step.type != RULE_MATCH && step.type != RULE_MATCH_FAILED) {
        if (step.endOfContext) {
            enterContext(step, BACKWARDS);
        }
        else {
            exitContext(BACKWARDS);
        }

        // If the step has a snapshot associated with it, restore that snapshot.
        // The step will only have a snapshot if backtracking occurred here when
        // searching forwards.
        if (step.hasSnapshot) {
            restoreSnapshot(step.snapshot);
            _infoBarMessage = "Backtracking previously occurred here. Reverted the graph back to its state before backtracking.";
        }
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


void TraceRunner::enterContext(TraceStep& context, TraceDirection direction) {
    // We only want to bother with snapshots if we are going forwards.
    if (direction == FORWARDS) {
        // If we are entering an if, try, or loop iteration context, we want to store
        // a snapshot of the current graph, so that we can restore it if necessary.
        // We will need to restore it at the end of the condition context (for an if),
        // at the start of the else context (for a try), or if something in the loop
        // body fails (for a loop).
        if (context.type == IF_CONTEXT || context.type == TRY_CONTEXT || context.type == LOOP_ITERATION) {
            GraphSnapshot snapshot = takeSnapshot();
            _snapshotStack.push(snapshot);

            _infoBarMessage = "Graph snapshot taken. Graph will be reverted to this point ";
            if (context.type == IF_CONTEXT) {
                _infoBarMessage += "after the branch condition is evaluated.";
            }
            else if (context.type == TRY_CONTEXT) {
                _infoBarMessage += "if the branch condition fails.";
            }
            else {
                _infoBarMessage += "if a rule in the loop fails.";

                // Assume that this loop iteration will succeed.
                _loopSuccessStack.push(true);
            }
        }

        // If we are entering a then context and the previous context was an if context,
        // we need to restore the snapshot.
        if (context.type == THEN_BRANCH) {
            TraceStepType previousContext = _contextStack.top();
            if (previousContext == IF_CONTEXT) {
                // We want to store a snapshot of the graph's current state here, so that
                // if we reach this point backwards, we can put the graph back to how it
                // was before we restore the snapshot below.
                context.snapshot = takeSnapshot();
                context.hasSnapshot = true;

                // Now restore the graph back to its previous state.
                GraphSnapshot snapshot = _snapshotStack.pop();
                restoreSnapshot(snapshot);
                _infoBarMessage = "Graph has been reverted to the previous snapshot.";
            }

            // If the previous context was a try, we still need to pop the snapshot off the
            // stack, but we don't want to apply it, because the then branch of a try does
            // not revert changes.
            if (previousContext == TRY_CONTEXT) {
                _snapshotStack.pop();
            }
        }

        // If we are entering an else context, we need to restore the snapshot. (This is
        // true for both if and try contexts).
        if (context.type == ELSE_BRANCH) {
            // We want to store a snapshot of the graph's current state here, so that
            // if we reach this point backwards, we can put the graph back to how it
            // was before we restore the snapshot below.
            context.snapshot = takeSnapshot();
            context.hasSnapshot = true;

            // Now restore the graph back to its previous state.
            GraphSnapshot snapshot = _snapshotStack.pop();
            restoreSnapshot(snapshot);
            _infoBarMessage = "Graph has been reverted to the previous snapshot.";
        }
    }

    _contextStack.push(context.type);
}


void TraceRunner::exitContext(TraceDirection direction) {
    TraceStepType contextType = _contextStack.pop();

    // If we are exiting a loop iteration forwards, we need to check whether to pop a
    // snapshot off the stack. We need to do this in the case no rules failed during
    // the entire loop iteration. If the loop failed, we will have already restored a
    // snapshot, so there's nothing to do.
    if (contextType == LOOP_ITERATION && direction == FORWARDS) {
        bool loopSuccess = _loopSuccessStack.pop();
        if (loopSuccess) {
            _snapshotStack.pop();
        }
    }
}


GraphSnapshot TraceRunner::takeSnapshot() {
    GraphSnapshot snapshot;
    std::vector<Node*> nodes = _graph->nodes();
    std::vector<Edge*> edges = _graph->edges();

    for (size_t i = 0; i < nodes.size(); i++) {
        SnapshotNode copy;
        Node* node = nodes[i];

        copy.id = node->id();
        copy.label = node->label();
        copy.mark = node->mark();
        copy.isRoot = node->isRoot();
        copy.pos = node->pos();

        snapshot.nodes.push_back(copy);
    }

    for (size_t i = 0; i < edges.size(); i++) {
        SnapshotEdge copy;
        Edge* edge = edges[i];

        copy.id = edge->id();
        copy.label = edge->label();
        copy.mark = edge->mark();
        copy.from = edge->from()->id();
        copy.to = edge->to()->id();

        snapshot.edges.push_back(copy);
    }

    return snapshot;
}


void TraceRunner::restoreSnapshot(GraphSnapshot snapshot) {
   // Remove all nodes and edges from the current graph.
    std::vector<Node*> oldNodes = _graph->nodes();
    std::vector<Edge*> oldEdges = _graph->edges();

    for (size_t i = 0; i < oldEdges.size(); i++) {
        _graph->removeEdge(oldEdges[i]->id());
    }

    for (size_t i = 0; i < oldNodes.size(); i++) {
        _graph->removeNode(oldNodes[i]->id());
    }

    // And add all the nodes and edges from the snapshot.
    std::vector<SnapshotNode> newNodes = snapshot.nodes;
    std::vector<SnapshotEdge> newEdges = snapshot.edges;

    for (size_t i = 0; i < newNodes.size(); i++) {
        SnapshotNode node = newNodes[i];
        _graph->addNode(node.id,
                        node.label,
                        node.mark,
                        node.isRoot,
                        false,
                        node.pos);
    }

    for (size_t i = 0; i < newEdges.size(); i++) {
        SnapshotEdge edge = newEdges[i];
        Node* from = _graph->node(edge.from);
        Node* to = _graph->node(edge.to);
        _graph->addEdge(edge.id,
                        from,
                        to,
                        edge.label,
                        edge.mark);
    }
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


QString TraceRunner::getInfoBarMessage() {
    return _infoBarMessage;
}


QString TraceRunner::getError() {
    if (_traceParser.hasError()) {
        return _traceParser.getError();
    }

    return _error;
}

}
