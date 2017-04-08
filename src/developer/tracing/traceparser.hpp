#ifndef TRACEPARSER_H
#define TRACEPARSER_H

#include <QXmlStreamReader>
#include <QFile>
#include <QDebug>
#include <QStack>

#include "tracestep.hpp"

namespace Developer {

class TraceParser
{
public:
    TraceParser(QString tracefilePath);
    ~TraceParser();

    /**
     * Whether or not this TraceParser object is ready to use. None of this
     * object's other methods should be called if this returns false.
     */
    bool isInitialised();

    /**
     * Returns true when all XML tokens in the tracefile have been consumed.
     */
    bool isParseComplete();

    /**
     * Returns true when the underlying XML parser encounters an error.
     */
    bool hasError();

    /**
     * Return the error given by the XML parser, if there is one.
     */
    QString getError();

    /**
     * Parse the next element of the tracefile XML to get the next TraceStep.
     */
    bool parseStep(TraceStep* step);

private:
    QXmlStreamReader* _xml;
    QFile _tracefile;
    bool _initialised;
    bool _parseComplete;
    QStack<QString> _unmatchedContextNames;

    bool parseStartElement(TraceStep* step);
    bool parseEndElement(TraceStep* step);
    bool parseGraphChange(GraphChange* change);

    label_t parseLabel(QString label, QString mark);
    edge_t parseEdge(QXmlStreamAttributes xmlAttributes);
    node_t parseNode(QXmlStreamAttributes xmlAttributes);

    TraceStepType stepTypeFromTagName(QStringRef tagName);
    GraphChangeType graphChangeTypeFromTagName(QStringRef tagName);
};

}

#endif // TRACEPARSER_H
