#include "traceparser.hpp"

#include <QStringList>

namespace Developer {

#define ATTRIBUTE_AS_ASCII(attributes, name) attributes.value(name).toAscii().constData()

TraceParser::TraceParser(QString tracefilePath) :
    _tracefile(tracefilePath),
    _initialised(false),
    _parseComplete(false),
    _unmatchedContextNames()
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
            qDebug() << "TraceRunner expected a <trace> element but got \"" << rootName << "\" instead";
            return;
        }
    }
    else {
        qDebug() << "TraceRunner expected a <trace> element but got an empty tracefile";
        return;
    }

    // We can now signal that this object is ready to use.
    _initialised = true;
}


TraceParser::~TraceParser() {
    if (_xml) { delete _xml; }
    _tracefile.close();
}


bool TraceParser::isInitialised() {
    return _initialised;
}


bool TraceParser::isParseComplete() {
    return _parseComplete;
}


bool TraceParser::hasError() {
    return _xml->hasError();
}


QString TraceParser::getError() {
    return QObject::tr("Line %2, column %3: %1")
            .arg(_xml->errorString())
            .arg(_xml->lineNumber())
            .arg(_xml->columnNumber());
}


bool TraceParser::parseStep(TraceStep *step) {
    // Set sensible defaults.
    step->contextName = "";
    step->endOfContext = false;
    step->loopBoundary = false;
    step->virtualStep = false;
    step->type = UNKNOWN;

    // We always want to parse one step, no matter how much irrelevant XML
    // text comes before it, so we loop round until a step is traced or an
    // error occurs.
    while (true) {
        if (_parseComplete) {
            // We already saw the end of the trace, so there's nothing else
            // to do. Return false because we didn't create a valid TraceStep.
            return false;
        }

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
        switch (tokenType) {
        case QXmlStreamReader::StartElement:
        {
            bool validStep = parseStartElement(step);
            if (validStep) { return true; }
            break;
        }

        case QXmlStreamReader::EndElement:
        {
            bool validStep = parseEndElement(step);
            if (validStep) { return true; }
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
    }
}

/**
 * Treat the current XML token as a start element, and parse it. Returns true
 * if TraceStep contains a valid step.
 */
bool TraceParser::parseStartElement(TraceStep *step) {
    // Get the name of the element so we can decide what type of step this is.
    TraceStepType type = stepTypeFromTagName(_xml->name());
    qDebug() << "Found start element" << _xml->name();

    QXmlStreamReader::TokenType tokenType;
    switch (type) {
    case RULE_MATCH:
        step->type = (_xml->attributes().value("success") == "true") ? RULE_MATCH : RULE_MATCH_FAILED;

        // Keep parsing until the </match> end element is found, to get the nodes
        // and edges from the rule match (if there are any).
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
        // If this isn't one of the types above, it's the start of a
        // context.
        step->type = type;

        // If the context has a name, record it in the TraceStep. Only rules
        // and procedures have names.
        // We also push the name onto the unmatched names stack, so that it
        // can be retrieved when ending the context.
        if (step->type == RULE || step->type == PROCEDURE) {
            QXmlStreamAttributes attrs = _xml->attributes();
            QString name = ATTRIBUTE_AS_ASCII(attrs, "name");
            step->contextName = ATTRIBUTE_AS_ASCII(attrs, "name");
            _unmatchedContextNames.push(name);
        }
    }

    return true;
}


/**
 * Treat the current XML token as an end element, and parse it. Returns true
 * if TraceStep contains a valid step.
 */
bool TraceParser::parseEndElement(TraceStep *step) {
    // Get the name of the element so we can decide what type of step this is.
    QStringRef name = _xml->name();

    // If the tag is the </trace> tag, this is the end of the document, so
    // mark the parse as complete.
    if (name == "trace") {
        _parseComplete = true;
        return false;
    }

    // If the end element is a context, we want to add a TraceStep of
    // marking the end of that context. Otherwise, we want to ignore it.
    // This is because QXmlStreamReader counts tags such as <break /> as
    // an open tag *and* an end tag, so we have to ignore any which aren't
    // contexts, because we didn't open a context for the corresponding start element.
    TraceStepType type = stepTypeFromTagName(name);
    if (type == SKIP  ||
        type == BREAK ||
        type == FAIL  ||
        type == UNKNOWN)
    {
        return false;
    }

    qDebug() << "Found end of context" << name;
    step->type = type;
    step->endOfContext = true;

    // If this is the end of a rule or procedure context, get name of the
    // context from the top of the unmatched name stack. This way, we can
    // store the name in this TraceStep, so that program highlighting is easier
    // when stepping backwards.
    if (step->type == RULE || step->type == PROCEDURE) {
        step->contextName = _unmatchedContextNames.pop();
    }

    return true;
}


/**
 * Parses the current XML token as a GraphChange. Returns true if the
 * GraphChange object is valid.
 */
bool TraceParser::parseGraphChange(GraphChange* change) {
    // Determine which type of change this is, then use that to decide
    // how to parse the element.
    change->type = graphChangeTypeFromTagName(_xml->name());

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


edge_t TraceParser::parseEdge(QXmlStreamAttributes xmlAttributes) {
    edge_t edge;
    edge.id    = ATTRIBUTE_AS_ASCII(xmlAttributes, "id");
    edge.from  = ATTRIBUTE_AS_ASCII(xmlAttributes, "source");
    edge.to    = ATTRIBUTE_AS_ASCII(xmlAttributes, "target");
    edge.label = parseLabel(ATTRIBUTE_AS_ASCII(xmlAttributes, "label"),
                            ATTRIBUTE_AS_ASCII(xmlAttributes, "mark"));
    return edge;
}


node_t TraceParser::parseNode(QXmlStreamAttributes xmlAttributes) {
    node_t node;
    node.id     = ATTRIBUTE_AS_ASCII(xmlAttributes, "id");
    node.isRoot = (ATTRIBUTE_AS_ASCII(xmlAttributes, "root") == "true");
    node.label  = parseLabel(ATTRIBUTE_AS_ASCII(xmlAttributes, "label"),
                             ATTRIBUTE_AS_ASCII(xmlAttributes, "mark"));
    return node;
}


label_t TraceParser::parseLabel(QString label, QString mark) {
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


/**
 * Converts from an XML tag name to a TraceStepType enum value.
 */
TraceStepType TraceParser::stepTypeFromTagName(QStringRef tagName) {
    if      (tagName == "rule")        { return RULE; }
    else if (tagName == "match")       { return RULE_MATCH; }
    else if (tagName == "apply")       { return RULE_APPLICATION; }
    else if (tagName == "ruleset")     { return RULE_SET; }
    else if (tagName == "loop")        { return LOOP; }
    else if (tagName == "iteration")   { return LOOP_ITERATION; }
    else if (tagName == "procedure")   { return PROCEDURE; }
    else if (tagName == "if")          { return IF_CONTEXT; }
    else if (tagName == "try")         { return TRY_CONTEXT; }
    else if (tagName == "condition")   { return BRANCH_CONDITION; }
    else if (tagName == "then")        { return THEN_BRANCH; }
    else if (tagName == "else")        { return ELSE_BRANCH; }
    else if (tagName == "or")          { return OR_CONTEXT; }
    else if (tagName == "leftBranch")  { return OR_LEFT; }
    else if (tagName == "rightBranch") { return OR_RIGHT; }
    else if (tagName == "skip")        { return SKIP; }
    else if (tagName == "break")       { return BREAK; }
    else if (tagName == "fail")        { return FAIL; }

    else { return UNKNOWN; }
}


/**
 * Converts from an XML tag name to a GraphChangeType enum value.
 */
GraphChangeType TraceParser::graphChangeTypeFromTagName(QStringRef tagName) {
    if      (tagName == "createEdge")  { return ADD_EDGE; }
    else if (tagName == "createNode")  { return ADD_NODE; }
    else if (tagName == "deleteEdge")  { return DELETE_EDGE; }
    else if (tagName == "deleteNode")  { return DELETE_NODE; }
    else if (tagName == "relabelEdge") { return RELABEL_EDGE; }
    else if (tagName == "relabelNode") { return RELABEL_NODE; }
    else if (tagName == "remarkEdge")  { return REMARK_EDGE; }
    else if (tagName == "remarkNode")  { return REMARK_NODE; }
    else if (tagName == "setRoot")     { return SET_ROOT; }
    else if (tagName == "removeRoot")  { return REMOVE_ROOT; }

    else { return INVALID; }
}


}
