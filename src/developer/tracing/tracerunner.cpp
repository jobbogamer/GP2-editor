#include "tracerunner.hpp"

#include <QtGlobal>
#include <QDebug>
#include <QIODevice>

namespace Developer {

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
    if (!parseStep()) {
        return;
    }

    // If we reach this point, we have successfully opened the tracefile,
    // initialised the XML reader, and found the root <trace> element.
    _initialised = true;

    bool s = true;
    do {
        s = parseStep();
    } while(s && !_parseComplete);

    qDebug() << "Found" << _traceSteps.size() << "steps";
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
    if (!_parseComplete && !isForwardStepAvailable()) {
        qDebug() << "Parsed a new step";
        return parseStep();
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
    // already parsed the steps before the current one.
    return true;
}

bool TraceRunner::parseStep() {
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
    TraceStepType type;
    switch (tokenType) {
    case QXmlStreamReader::StartElement:
        // Get the name of the element so we can decide what type of step this is.
        type = stepTypeFromXML(_xml->name());
        qDebug() << "Found start element" << _xml->name();

        switch (type) {
        case RULE_MATCH:
        {
            // If the match failed, there is nothing more to parse, since there
            // will not be a morphism.
            if (_xml->attributes().value("success") == "false") {
                step.type = RULE_MATCH_FAILED;
                qDebug() << "Found a failed rule match";
                break;
            }

            // If the match was successful, keep parsing until the </match> end
            // element is found, to get the nodes and edges from the rule match.
            step.type = RULE_MATCH;
            tokenType = _xml->readNext();
            while (tokenType != QXmlStreamReader::EndElement && _xml->name() != "match") {
                if (_xml->name() == "node") {
                    QXmlStreamAttributes attrs = _xml->attributes();

                    // Create a node object to represent the matched node, using
                    // the id value from the XML. Note that QXmlStreamReader will
                    // return attributes as a QStringRef, hence .toAscii().constData().
                    node_t node;
                    node.id = attrs.value("id").toAscii().constData();

                    // Create a GraphChange struct and push it into the TraceStep's
                    // change vector.
                    GraphChange change;
                    change.existingItem = node;
                    step.graphChanges.push_back(change);
                }
                else if (_xml->name() == "edge") {
                    QXmlStreamAttributes attrs = _xml->attributes();

                    // Create an edge object to represent the matched edge, using
                    // the id value from the XML. Note that QXmlStreamReader will
                    // return attributes as a QStringRef, hence .toAscii().constData().
                    edge_t edge;
                    edge.id = attrs.value("id").toAscii().constData();

                    // Create a GraphChange struct and push it into the TraceStep's
                    // change vector.
                    GraphChange change;
                    change.existingItem = edge;
                    step.graphChanges.push_back(change);
                }

                tokenType = _xml->readNext();
            }
            break;
        }

        case RULE_APPLICATION:
        {
            // TODO: Scoop up all the graph changes.
            break;
        }

        default:
            // If this isn't one of the types above, it's just the start of a
            // context, so create a simple TraceStep struct.
            step.type = type;
        }
        break;

    case QXmlStreamReader::EndElement:
        // Get the name of the element so we can decide what type of step this is.
        step.type = stepTypeFromXML(_xml->name());
        qDebug() << "Found end element" << _xml->name();
        break;

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
            qDebug() << "PrematureEndOfDocument";
            return true;
        }
        else {
            // This was some other kind of parse error, so we need to return false.
            qDebug() << "ERROR!!!!";
            return false;
        }

    default:
        // We do not care about the other token types because we will never
        // encounter them.
        break;
    }

    // Push the trace step into the vector, and return true, since if we have
    // reached this point, there weren't any XML errors.    
    _traceSteps.push_back(step);
    return true;
}


void TraceRunner::applyCurrentStepChanges() {

}


void TraceRunner::revertCurrentStepChanges() {

}

QString TraceRunner::getXMLError() {
    return QObject::tr("%1\nLine %2, column %3")
            .arg(_xml->errorString())
            .arg(_xml->lineNumber())
            .arg(_xml->columnNumber());
}

}
