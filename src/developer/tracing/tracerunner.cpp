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

    if (_xml->readNextStartElement()) {

    }
    else {
        // readNextStartElement() returns false if a closing tag is reached or
        // an error occurs.
        if (_xml->hasError()) {
            return false;
        }
        else {
            // There was no error, a closing tag was reached.
            step.type = END_CONTEXT;
            step.graphChanges.clear();
        }
    }

    // Check again whether we have reached the end of the file, since the
    // element we just parsed could have been the last element.
    if (_xml->atEnd()) {
        _parseComplete = true;
    }

    // Push the trace step into the vector, and return true, since if we have
    // reached this point, there weren't any XML errors.
    _traceSteps.push_back(step);
    return true;
}

QString TraceRunner::getXMLError() {
    return _xml->errorString();
}

}
