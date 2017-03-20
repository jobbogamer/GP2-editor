#ifndef TRACERUNNER_HPP
#define TRACERUNNER_HPP

#include <QXmlStreamReader>
#include <QFile>
#include "graph.hpp"
#include "program.hpp"

namespace Developer {

class TraceRunner
{
public:
    TraceRunner(QString traceFile, Graph* graph, Program* program);
    ~TraceRunner();
    Graph* graph();

    /**
     * Call this after using the constructor to check whether the TraceRunner
     * was successfully initialised. If this method returns false, this
     * TraceRunner object is not safe to use.
     */
    bool isInitialised();

private:
    Graph* _graph;
    Program* _program;
    QXmlStreamReader* _xml;
    QFile _tracefile;
    bool _initialised;
};

}

#endif // TRACERUNNER_HPP
