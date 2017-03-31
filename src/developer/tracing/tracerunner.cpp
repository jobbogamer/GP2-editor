#include "tracerunner.hpp"

#include <QtGlobal>
#include <QDebug>
#include <QIODevice>
#include "translate/translate.hpp"

namespace Developer {

#define ATTRIBUTE_AS_ASCII(attributes, name) attributes.value(name).toAscii().constData()

#define QSTRING(string) QString::fromStdString(string)

TraceRunner::TraceRunner(QString traceFile, Graph* graph, Program* program) :
    _graph(graph),
    _program(program),
    _tracefile(traceFile),
    _initialised(false),
    _parseComplete(false),
    _traceSteps(),
    _currentStep(-1),
    _contextStack()
{
    // Attempt to open the tracefile.
    if (!_tracefile.open(QFile::ReadOnly | QFile::Text)) {
        qDebug() << "Error reading tracefile:";
        qDebug() << _tracefile.errorString();
        return;
    }

    // Initialise the XML reader.
    _xml = new QXmlStreamReader(&_tracefile);

    // Ensure that the top level XML element is <trace>, otherwise we can be
    // fairly sure this isn't a GP2 tracefile.
    if (_xml->readNextStartElement()) {
        QStringRef rootName = _xml->name();
        if (rootName != "trace") {
            qDebug() << "TraceRunner expected a <trace> element but got <" << rootName << "> instead";
            return;
        }
    }
    else {
        qDebug() << "TraceRunner expected a <trace> element but got an empty tracefile";
        return;
    }

    // Parse the first step in the trace to get started.
    bool success = parseStep();
    if (!success) { return; }

    // If we reach this point, we have successfully opened the tracefile,
    // initialised the XML reader, and found the root <trace> element.
    _currentStep = 0;
    _initialised = true;
}

TraceRunner::~TraceRunner() {
    if (_xml) { delete _xml; }
    _tracefile.close();
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
    if (_currentStep >= _traceSteps.size() && _parseComplete) {
        return false;
    }
    return true;
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
    // Find match is only available if the current context is <rule>.
    if (!_contextStack.empty() && _contextStack.top() == RULE) {
        return true;
    }
    return false;
}

bool TraceRunner::isMatchApplicationAvailable() {
    // Match application is only availabe if the current context is <match>.
    if (!_contextStack.empty() && _contextStack.top() == RULE_MATCH) {
        return true;
    }
    return false;
}

bool TraceRunner::stepForward() {
    // Sanity check...
    Q_ASSERT(isForwardStepAvailable());

    // Apply the changes from the current step, then advance the step position.
    applyCurrentStepChanges();
    _currentStep += 1;

    qDebug() << "Stepped forward, current position is " << _currentStep;

    // If parsing is not complete, and the new step position does not exist in
    // the step vector, parse the next step.
    if (!_parseComplete && _traceSteps.size() <= _currentStep) {
        qDebug() << "Parsing a new step";
        bool success = parseStep();
        if (!success) {
            return false;
        }
    }

    // If we are not parsing anything, we can return true.
    return true;
}

bool TraceRunner::stepBackward() {
    // Sanity check...
    Q_ASSERT(isBackwardStepAvailable());

    // Move current step position backwards, then revert the changes. We have to
    // move before reverting because the "current step" refers to the step which
    // will be *applied* if stepForward() is called.
    _currentStep -= 1;
    revertCurrentStepChanges();

    qDebug() << "Stepped backwards, current position is " << _currentStep;

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


bool TraceRunner::parseStep() {
    // We always want to parse one step, no matter how much irrelevant XML
    // text comes before it, so we loop round until a step is traced or an
    // error occurs.
    while (true) {
        // As parsing continues, this TraceStep object will be updated to reflect
        // the parsed step. It will be pushed into the steps vector at the end of
        // this method.
        TraceStep step;

        if (_xml->atEnd()) {
            // If we have reached the end of the tracefile, there is no more parsing
            // to do. Mark parsing as complete and return true, since the end of the
            // file is not an error.
            _parseComplete = true;
            return true;
        }

        if (_xml->hasError()) {
            // All we can do here is return false. The caller will have to use the
            // getXMLError() method to get the details.
            return false;
        }

        // Read the next XML token, then decide what to do based on the token type.
        QXmlStreamReader::TokenType tokenType = _xml->readNext();
        bool success = true;
        switch (tokenType) {
        case QXmlStreamReader::StartElement:
            success = parseStartElement(&step);
            if (!success) { return false; }
            break;

        case QXmlStreamReader::EndElement:
        {
            // Get the name of the element so we can decide what type of step this is.
            QStringRef name = _xml->name();

            // If the end element is a context, we want to add a TraceStep of
            // type END_CONTEXT. Otherwise, we want to ignore it. This is because
            // QXmlStreamReader counts tags such as <node /> as an open tag and an
            // end tag, so we have to ignore any which aren't contexts, because we
            // didn't open a context for the corresponding start element.
            if (name == "rule" ||
                name == "match" ||
                name == "apply" ||
                name == "ruleset" ||
                name == "loop" ||
                name == "iteration" ||
                name == "procedure" ||
                name == "if" ||
                name == "try" ||
                name == "condition" ||
                name == "then" ||
                name == "else" ||
                name == "or" ||
                name == "leftBranch" ||
                name == "rightBranch")
            {
                qDebug() << "Found end of context" << name;
                step.type = END_CONTEXT;
            }
            else {
                // If this isn't the end of a context, we want to ignore it, and not
                // add the step to the vector, so go back to the start of the loop
                // to try parsing the next element.
                continue;
            }
            break;
        }

        case QXmlStreamReader::EndDocument:
            // We have reached the end of the XML file. We don't want to add any steps
            // to the vector, because this doesn't count as a step, so we'll return
            // early here.
            qDebug() << "Found end of document";
            _parseComplete = true;
            return true;

        case QXmlStreamReader::Invalid:
            // A parse error has occurred. We will check the type of error here. If it
            // is PrematureEndOfDocumentError, we will absorb the error and mark the
            // parse as complete; this error will occur if a nonterminating program has
            // been traced and killed with ^C.
            if (_xml->error() == QXmlStreamReader::PrematureEndOfDocumentError) {
                _parseComplete = true;
                // We don't want to push a trace step for an invalid element, so return
                // early here. We're returning true because we're not classing this as
                // an error.
                qDebug() << "Tracefile is incomplete; parsing will end here";
                return true;
            }
            else {
                // This was some other kind of parse error, so we need to return false.
                return false;
            }

        default:
            // We do not care about the other token types because we will never
            // encounter them. We will jump back to the top of the loop to try
            // parsing the next token.
            continue;
        }

        // Push the trace step into the vector, and return true, since if we have
        // reached this point, there weren't any XML errors.
        _traceSteps.push_back(step);
        return true;
    }
}


/**
 * Parse the current StartElement which _xml has reached. The resulting
 * TraceStep object will be placed into *step.
 */
bool TraceRunner::parseStartElement(TraceStep* step) {
    // Get the name of the element so we can decide what type of step this is.
    TraceStepType type = stepTypeFromXML(_xml->name());
    qDebug() << "Found start element" << _xml->name();

    QXmlStreamReader::TokenType tokenType;
    switch (type) {
    case RULE_MATCH:
        // If the match failed, there is nothing more to parse, since there
        // will not be a morphism.
        if (_xml->attributes().value("success") == "false") {
            step->type = RULE_MATCH_FAILED;
            qDebug() << "Found a failed rule match";
            break;
        }

        // If the match was successful, keep parsing until the </match> end
        // element is found, to get the nodes and edges from the rule match.
        step->type = RULE_MATCH;
        tokenType = _xml->readNext();
        while (! (tokenType == QXmlStreamReader::EndElement && _xml->name() == "match") ) {
            // Stop if an invalid element is reached.
            if (tokenType == QXmlStreamReader::Invalid) {
                qDebug() << "Found invalid XML token at line" << _xml->lineNumber() << ", column" << _xml->columnNumber();
                qDebug() << _xml->tokenString();
                return false;
            }

            // Only StartElements should be parsed, since each item in a rule match
            // has on StartElement and one EndElement.
            if (tokenType != QXmlStreamReader::StartElement) {
                tokenType = _xml->readNext();
                continue;
            }

            // Add a graph change item to the vector. We will use the existingItem
            // field in GraphChange because it doesn't matter which we use, we just
            // need to be consistent.
            if (_xml->name() == "node") {
                QXmlStreamAttributes attrs = _xml->attributes();

                // Create a node object to represent the matched node, using
                // the id value from the XML.
                node_t node;
                node.id = ATTRIBUTE_AS_ASCII(attrs, "id");

                // Create a GraphChange struct and push it into the TraceStep's
                // change vector.
                GraphChange change;
                change.existingItem = node;
                change.type = MORPHISM;
                step->graphChanges.push_back(change);
            }
            else if (_xml->name() == "edge") {
                QXmlStreamAttributes attrs = _xml->attributes();

                // Create an edge object to represent the matched edge, using
                // the id value from the XML.
                edge_t edge;
                edge.id = ATTRIBUTE_AS_ASCII(attrs, "id");

                // Create a GraphChange struct and push it into the TraceStep's
                // change vector.
                GraphChange change;
                change.type = MORPHISM;
                change.existingItem = edge;
                step->graphChanges.push_back(change);
            }

            tokenType = _xml->readNext();
        }
        qDebug() << "Found <match> with" << step->graphChanges.size() << "items";
        break;

    case RULE_APPLICATION:
        step->type = RULE_APPLICATION;

        // Keep parsing until the </apply> end element is found in order to add
        // all the graph changes to the vector.
        tokenType = _xml->readNext();
        while (! (tokenType == QXmlStreamReader::EndElement && _xml->name() == "apply") ) {
            // Stop if an invalid element is reached.
            if (tokenType == QXmlStreamReader::Invalid) {
                qDebug() << "Found invalid XML token at line" << _xml->lineNumber() << ", column" << _xml->columnNumber();
                qDebug() << _xml->tokenString();
                return false;
            }

            // Only StartElements should be parsed, since each item in a rule application
            // has on StartElement and one EndElement.
            if (tokenType != QXmlStreamReader::StartElement) {
                tokenType = _xml->readNext();
                continue;
            }

            // Parse the graph change and add it to the vector if parsing was
            // successful. If not, skip this element.
            GraphChange change;
            bool success = parseGraphChange(&change);
            if (success) {
                step->graphChanges.push_back(change);
            }

            tokenType = _xml->readNext();
        }
        qDebug() << "Found <apply> with" << step->graphChanges.size() << "graph changes";
        break;

    case UNKNOWN:
        qDebug() << "Unknown XML element type found at line"
                 << _xml->lineNumber() << ", column" << _xml->columnNumber()
                 << ":" << _xml->name();
        return false;

    default:
        // If this isn't one of the types above, it's just the start of a
        // context, so create a simple TraceStep struct.
        step->type = type;
    }

    return true;
}


bool TraceRunner::parseGraphChange(GraphChange *change) {
    // Determine which type of change this is, then use that to decide
    // how to parse the element.
    change->type = changeTypeFromXML(_xml->name());

    QXmlStreamAttributes attrs = _xml->attributes();
    switch (change->type) {
    case ADD_EDGE:
    {
        // We don't need to set anything in existingItem, but we do need
        // to record the details of the new edge.
        change->newItem = parseEdge(attrs);
        return true;
    }

    case ADD_NODE:
    {
        // We don't need to set anything in existingItem, but we do need
        // to record the details of the new edge.
        change->newItem = parseNode(attrs);
        return true;
    }

    case DELETE_EDGE:
    {
        // We need to record the details of the edge into existingItem so
        // that it can be recreated if we step backwards.
        change->existingItem = parseEdge(attrs);
        return true;
    }

    case DELETE_NODE:
    {
        // We need to record the details of the node into existingItem so
        // that it can be recreated if we step backwards.
        change->existingItem = parseNode(attrs);
        return true;
    }

    case RELABEL_EDGE:
    {
        edge_t oldEdge, newEdge;
        oldEdge.id = ATTRIBUTE_AS_ASCII(attrs, "id");
        oldEdge.label = parseLabel(ATTRIBUTE_AS_ASCII(attrs, "old"), "");
        newEdge.id = oldEdge.id;  // The ID can't have changed
        newEdge.label = parseLabel(ATTRIBUTE_AS_ASCII(attrs, "new"), "");
        change->existingItem = oldEdge;
        change->newItem = newEdge;
        return true;
    }

    case RELABEL_NODE:
    {
        node_t oldNode, newNode;
        oldNode.id = ATTRIBUTE_AS_ASCII(attrs, "id");
        oldNode.label = parseLabel(ATTRIBUTE_AS_ASCII(attrs, "old"), "");
        newNode.id = oldNode.id;  // The ID can't have changed
        newNode.label = parseLabel(ATTRIBUTE_AS_ASCII(attrs, "new"), "");
        change->existingItem = oldNode;
        change->newItem = newNode;
        return true;
    }

    case REMARK_EDGE:
    {
        edge_t oldEdge, newEdge;
        oldEdge.id = ATTRIBUTE_AS_ASCII(attrs, "id");
        oldEdge.label = parseLabel("", ATTRIBUTE_AS_ASCII(attrs, "old"));
        newEdge.id = oldEdge.id;  // The ID can't have changed
        newEdge.label = parseLabel("", ATTRIBUTE_AS_ASCII(attrs, "new"));
        change->existingItem = oldEdge;
        change->newItem = newEdge;
        return true;
    }

    case REMARK_NODE:
    {
        node_t oldNode, newNode;
        oldNode.id = ATTRIBUTE_AS_ASCII(attrs, "id");
        oldNode.label = parseLabel("", ATTRIBUTE_AS_ASCII(attrs, "old"));
        newNode.id = oldNode.id;  // The ID can't have changed
        newNode.label = parseLabel("", ATTRIBUTE_AS_ASCII(attrs, "new"));
        change->existingItem = oldNode;
        change->newItem = newNode;
        return true;
    }

    case SET_ROOT:
    {
        node_t oldNode, newNode;
        oldNode.id = ATTRIBUTE_AS_ASCII(attrs, "id");
        oldNode.isRoot = false;
        newNode.id = oldNode.id; // The ID can't have changed
        newNode.isRoot = true;
        change->existingItem = oldNode;
        change->newItem = newNode;
        return true;
    }

    case REMOVE_ROOT:
    {
        node_t oldNode, newNode;
        oldNode.id = ATTRIBUTE_AS_ASCII(attrs, "id");
        oldNode.isRoot = true;
        newNode.id = oldNode.id; // The ID can't have changed
        newNode.isRoot = false;
        change->existingItem = oldNode;
        change->newItem = newNode;
        return true;
    }

    default:
        // This isn't one of the recognised change types.
        return false;
    }
}


edge_t TraceRunner::parseEdge(QXmlStreamAttributes xmlAttributes) {
    edge_t edge;
    edge.id    = ATTRIBUTE_AS_ASCII(xmlAttributes, "id");
    edge.from  = ATTRIBUTE_AS_ASCII(xmlAttributes, "source");
    edge.to    = ATTRIBUTE_AS_ASCII(xmlAttributes, "target");
    edge.label = parseLabel(ATTRIBUTE_AS_ASCII(xmlAttributes, "label"),
                            ATTRIBUTE_AS_ASCII(xmlAttributes, "mark"));
    return edge;
}


node_t TraceRunner::parseNode(QXmlStreamAttributes xmlAttributes) {
    node_t node;
    node.id     = ATTRIBUTE_AS_ASCII(xmlAttributes, "id");
    node.isRoot = (ATTRIBUTE_AS_ASCII(xmlAttributes, "root") == "true");
    node.label  = parseLabel(ATTRIBUTE_AS_ASCII(xmlAttributes, "label"),
                             ATTRIBUTE_AS_ASCII(xmlAttributes, "mark"));
    return node;
}


label_t TraceRunner::parseLabel(QString label, QString mark) {
    label_t result;

    // Parse the mark -- this will be a number but we need to convert it into
    // a string (none, red, green, blue, grey, dashed).
    if      (mark == "1") { result.mark = "red";    }
    else if (mark == "2") { result.mark = "green";  }
    else if (mark == "3") { result.mark = "blue";   }
    else if (mark == "4") { result.mark = "dashed"; }
    else                  { result.mark = "none"; }

    // Now parse the label itself. This will be a string containing one or
    // more atoms separated by : symbols.
    QStringList items = label.split(":");
    for (QList<QString>::iterator iter = items.begin(); iter != items.end(); iter++) {
        atom_t atom = (*iter).toStdString();
        result.values.push_back(atom);
    }

    return result;
}


void TraceRunner::enterContext(TraceStepType context) {
    _contextStack.push(context);
    // TODO: Update the program positon
}


void TraceRunner::exitContext() {
    // TraceStepType context = _contextStack.pop();
    // TODO: Update the program position
}


/**
 * Apply all the GraphChanges in the current TraceStep. This means that the current
 * step must be a rule application.
 */
void TraceRunner::applyCurrentStepChanges() {
    // Sanity check. The way parsing works means this should never fail, but it's
    // better to make sure.
    Q_ASSERT(_currentStep >= 0 && _currentStep < _traceSteps.size());

    // Check that this is actually a rule application.
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


QString TraceRunner::getXMLError() {
    return QObject::tr("Line %2, column %3: %1")
            .arg(_xml->errorString())
            .arg(_xml->lineNumber())
            .arg(_xml->columnNumber());
}

}
