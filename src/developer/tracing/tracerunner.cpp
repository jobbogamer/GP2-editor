#include "tracerunner.hpp"

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
    _contextStack(),
    _tracePosition(-1)
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

    // If we reach this point, we have successfully opened the tracefile,
    // initialised the XML reader, and found the root <trace> element.
    _initialised = true;

    bool s = true;
    do {
        s = parseStep();
    } while(s && !_parseComplete);
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
    switch (tokenType) {
    case QXmlStreamReader::StartElement:
        // Get the name of the element so we can decide what type of step this is.
        step.type = stepTypeFromXML(_xml->name());
        qDebug() << "Found start element" << _xml->name();
        // TODO: If it's an <apply> element, keep parsing until the end element is
        // reached, to get the graph changez.
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

QString TraceRunner::getXMLError() {
    return QObject::tr("%1\nLine %2, column %3")
            .arg(_xml->errorString())
            .arg(_xml->lineNumber())
            .arg(_xml->columnNumber());
}

}
