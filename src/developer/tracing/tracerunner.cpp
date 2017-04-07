#include "tracerunner.hpp"

#include <QtGlobal>
#include <QDebug>
#include <QIODevice>
#include "translate/translate.hpp"
#include "programtokens.hpp"

namespace Developer {

#define ATTRIBUTE_AS_ASCII(attributes, name) attributes.value(name).toAscii().constData()

#define QSTRING(string) QString::fromStdString(string)

TraceRunner::TraceRunner(QString traceFile, Graph* graph, QVector<Token*> programTokens) :
    _graph(graph),
    _programTokens(programTokens),
    _tracefile(traceFile),
    _initialised(false),
    _parseComplete(false),
    _traceSteps(),
    _currentStep(-1),
    _tokenStack(),
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

    // Ensure that no tokens are currently highlighted. This may occur if, for example,
    // a traced program is run twice.
    removeHighlights();

    // Parse the first step in the trace to get started.
    bool success = parseStep();
    if (!success) { return; }

    // If we reach this point, we have successfully opened the tracefile,
    // initialised the XML reader, and found the root <trace> element.
    _currentStep = 0;
    _initialised = true;

    // Now that the first step in the trace is prepared, update the program position
    // to highlight that first step.
    updateProgramPosition(false);
}

TraceRunner::~TraceRunner() {
    if (_xml) { delete _xml; }
    _tracefile.close();
}

Graph* TraceRunner::graph() {
    return _graph;
}

Token* TraceRunner::currentToken() {
    return _tokenStack.top().token;
}

bool TraceRunner::isInitialised() {
    return _initialised;
}

bool TraceRunner::isForwardStepAvailable() {
    // If we are at the end of the trace step vector, and parsing is complete,
    // we must be at the end of the trace itself, so forward steps are not
    // available from here.
    return (! (_currentStep >= _traceSteps.size() && _parseComplete));
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
    if (_currentStep >= _traceSteps.size() && _parseComplete) { return false; }
    TraceStep& step = _traceSteps[_currentStep];
    return (step.type == RULE_MATCH || step.type == RULE_MATCH_FAILED);
}

bool TraceRunner::isMatchApplicationAvailable() {
    // Match application is only available if the current step type is RULE_APPLICATION.
    if (_currentStep >= _traceSteps.size() && _parseComplete) { return false; }
    TraceStep& step = _traceSteps[_currentStep];
    return (step.type == RULE_APPLICATION);
}

bool TraceRunner::stepForward() {
    // Sanity check...
    Q_ASSERT(isForwardStepAvailable());

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
    if (!_parseComplete && _traceSteps.size() <= _currentStep) {
        qDebug() << "Parsing a new step";
        bool success = parseStep();
        if (!success) {
            return false;
        }
    }

    // We had to wait until after parsing a step to update the program position,
    // because if we had not yet parsed the next step, we would not know what
    // type of program token to search for.
    updateProgramPosition(false);

    return true;
}

bool TraceRunner::stepBackward() {
    // Sanity check...
    Q_ASSERT(isBackwardStepAvailable());

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
    updateProgramPosition(true);

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

        // Set sensible defaults.
        step.contextName = "";
        step.endOfContext = false;
        step.type = UNKNOWN;

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
            // marking the end of that context. Otherwise, we want to ignore it.
            // This is because QXmlStreamReader counts tags such as <node /> as
            // an open tag *and* an end tag, so we have to ignore any which aren't
            // contexts, because we didn't open a context for the corresponding start element.
            TraceStepType type = stepTypeFromXML(name);
            if (type == SKIP  ||
                type == BREAK ||
                type == FAIL  ||
                type == UNKNOWN)
            {
                continue;
            }

            qDebug() << "Found end of context" << name;
            step.type = type;
            step.endOfContext = true;

            // If this is the end of a rule context, search back in the trace steps to
            // find the start of the rule context. It must be the most recently added one,
            // since rules cannot be nested. This way, we can get the name of the rule to
            // store in this TraceStep, so that program highlighting is easier when
            // stepping backwards.
            if (step.type == RULE) {
                for (int i = _traceSteps.size() - 1; i >= 0; i--) {
                    TraceStep& oldStep = _traceSteps[i];
                    if (oldStep.type == RULE) {
                        step.contextName = oldStep.contextName;
                        break;
                    }
                }
            }

            // If this is the end of a procedure, we need to record the name of that
            // procedure so that we know where to jump to when stepping over it in
            // reverse. This is slightly more complicated than finding the start of
            // the rule above, because procedures can be nested. For example, if the
            // trace looks like this:
            //
            // <procedure name="outer">
            //     <procedure name="inner">
            //          ...
            //     </procedure>
            // </procedure>
            //
            // then we cannot just take the first procedure TraceStep we find, because
            // we will incorrectly record the outer procedure as "inner". So while
            // searching backwards, we have to count the number of other closed contexts
            // we come across, and only record the name once we are sure we have skipped
            // the open context for every close context.
            if (step.type == PROCEDURE) {
                int nestedContexts = 0;
                for (int i = _traceSteps.size() - 1; i >= 0; i--) {
                    TraceStep& oldStep = _traceSteps[i];
                    if (oldStep.type == PROCEDURE) {
                        if (oldStep.endOfContext) {
                            nestedContexts += 1;
                        }
                        else if (nestedContexts > 0) {
                            nestedContexts -= 1;
                        }
                        else {
                            step.contextName = oldStep.contextName;
                            break;
                        }
                    }
                }
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

        // If the step we added was a <match> context, we also have to add a
        // corresponding ending </match> context. This is because the </match> token
        // was already consumed, so we won't parse it in the next forward step.
        if (step.type == RULE_MATCH || step.type == RULE_MATCH_FAILED) {
            TraceStep matchEnd;
            matchEnd.type = step.type;
            matchEnd.endOfContext = true;
            _traceSteps.push_back(matchEnd);
        }

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

        // If the context has a name, record it in the TraceStep.
        QXmlStreamAttributes attrs = _xml->attributes();
        QString nameValue = ATTRIBUTE_AS_ASCII(attrs, "name");
        if (nameValue.length() > 0) {
            step->contextName = nameValue;
        }
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


void TraceRunner::enterContext(TraceStep& context) {
    _contextStack.push(context.type);
}


void TraceRunner::exitContext() {
    _contextStack.pop();
}


void TraceRunner::updateProgramPosition(bool backwards) {
    // If we are currently pointing at a procedure call, move the highlight to
    // the declaration of that procedure before searching for the next token.
    // Since a procedure can be declared pretty much anywhere, we have to
    // start the search at the beginning of the token vector.
    if (_currentStep > 0) {
        TraceStep& previousStep = _traceSteps[_currentStep - 1];
        if (previousStep.type == PROCEDURE && !previousStep.endOfContext) {
            int searchPos = 0;
            while (searchPos < _programTokens.size()) {
                Token* token = _programTokens[searchPos];
                if (token->lexeme == ProgramLexeme_Declaration) {
                    if (token->text == previousStep.contextName) {
                        // To find the actual implementation of the procedure, we have to
                        // look for the = sign after the name. When a procedure is
                        // called, the token is still tagged as a declaration (presumably
                        // to give procedure calls a different colour), and we don't want
                        // to highlight a procedure call by mistake.
                        Token* nextToken = _programTokens[searchPos + 1];
                        if (nextToken->lexeme == ProgramLexeme_DeclarationOperator) {
                            TokenReference foundToken;
                            foundToken.token = token;
                            foundToken.index = searchPos;

                            // Since we will have to jump back to the call site, push the
                            // token onto the stack rather than replacing it.
                            pushHighlight(foundToken);
                            break;
                        }
                    }
                }

                // Even if we're stepping backwards, we started the search at the
                // beginning of the program, so we always move forwards.
                searchPos += 1;
            }
        }
    }

    // We have to start the search at the currently highlighted token. However,
    // if the token stack is empty, we have to start at the start or the end of
    // the program tokens vector, because there is no current token.
    int searchPos;
    if (_tokenStack.empty()) {
        searchPos = (backwards) ? (_programTokens.size() - 1) : 0;
    }
    else {
        searchPos = (backwards) ? (_tokenStack.top().index - 1) : (_tokenStack.top().index + 1);
    }

    TokenReference foundToken;
    foundToken.token = 0;
    foundToken.index = -1;

    // If we are at the end of the trace, highlight nothing and return early.
    if (_currentStep >= _traceSteps.size()) {
        removeHighlights();
        _tokenStack.pop();
        return;
    }

    // Get the current step so we can find it in the source text to highlight.
    TraceStep& step = _traceSteps[_currentStep];

    switch (step.type) {
    case RULE_MATCH:
    case RULE_MATCH_FAILED:
    case RULE_APPLICATION:
        // We do not need to update the program position for <match> or <apply>, since
        // they are component parts of a rule in the source text.
        break;

    case RULE:
    {
        // Depending on the direction we are moving through the program, we need to
        // either ignore the end of the context or the start of it. This is because
        // a rule call is represented by a single token in the source code, so we
        // don't need to move the highlight.
        if (backwards != step.endOfContext) {
            break;
        }

        // For some reason, rule names are prefixed with "Main_" by the compiler,
        // so we need to remove that from the context name before searching for the
        // correct token.
        QString ruleName = step.contextName.remove("Main_");

        while (searchPos >= 0 && searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];
            if (token->lexeme == ProgramLexeme_Identifier) {
                if (token->text == ruleName) {
                    foundToken.token = token;
                    foundToken.index = searchPos;
                    replaceCurrentHighlight(foundToken);
                    break;
                }
            }

            if (backwards) { searchPos -= 1; }
            else           { searchPos += 1; }
        }

        break;
    }

    case RULE_SET:
    {
        // If this is the end of the context, we need to search for the
        // closing brace instead of the opening one.
        int lexeme = (step.endOfContext) ? ProgramLexeme_CloseBrace : ProgramLexeme_OpenBrace;

        while (searchPos >= 0 && searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];
            if (token->lexeme == lexeme) {
                foundToken.token = token;
                foundToken.index = searchPos;
                replaceCurrentHighlight(foundToken);
                break;
            }

            if (backwards) { searchPos -= 1; }
            else           { searchPos += 1; }
        }

        break;
    }

    case LOOP:
    case LOOP_ITERATION:
        break;

    case PROCEDURE:
    {
        while (searchPos >= 0 && searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];
            if (token->lexeme == ProgramLexeme_Declaration) {
                if (token->text == step.contextName) {
                    foundToken.token = token;
                    foundToken.index = searchPos;
                    replaceCurrentHighlight(foundToken);
                    break;
                }
            }

            if (backwards) { searchPos -= 1; }
            else           { searchPos += 1; }
        }

        break;
    }

    case IF_CONTEXT:
    case TRY_CONTEXT:
    case BRANCH_CONDITION:
    case THEN_BRANCH:
    case ELSE_BRANCH:
    case OR_CONTEXT:
    case OR_LEFT:
    case OR_RIGHT:
    case SKIP:
    case BREAK:
    case FAIL:
    case UNKNOWN:
    default:
        qDebug() << "Unhandled step of type" << step.type;
        break;
    }
}


void TraceRunner::replaceCurrentHighlight(TokenReference newToken) {
    // Pop the top of the stack, and un-highlight that token.
    if (!_tokenStack.empty()) {
        TokenReference previous = _tokenStack.pop();
        previous.token->emphasise = false;
    }

    // Highlight the new token, and push it onto the stack.
    newToken.token->emphasise = true;
    _tokenStack.push(newToken);
}


void TraceRunner::pushHighlight(TokenReference newToken) {
    if (!_tokenStack.empty()) {
        // Note we are *not* popping the stack here, just peeking.
        TokenReference previous = _tokenStack.top();
        previous.token->emphasise = false;
    }

    newToken.token->emphasise = true;
    _tokenStack.push(newToken);
}


void TraceRunner::popHighlight() {
    TokenReference topToken = _tokenStack.pop();
    topToken.token->emphasise = false;

    if (!_tokenStack.empty()) {
        topToken = _tokenStack.top();
        topToken.token->emphasise = true;
    }
}


void TraceRunner::removeHighlights() {
    for (int i = 0; i < _programTokens.size(); i++) {
        Token* token = _programTokens[i];
        token->emphasise = false;
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


QString TraceRunner::getXMLError() {
    return QObject::tr("Line %2, column %3: %1")
            .arg(_xml->errorString())
            .arg(_xml->lineNumber())
            .arg(_xml->columnNumber());
}

}
