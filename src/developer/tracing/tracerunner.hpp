#ifndef TRACERUNNER_HPP
#define TRACERUNNER_HPP

#include <QXmlStreamReader>
#include "graph.hpp"
#include "program.hpp"

namespace Developer {

class TraceRunner
{
public:
    TraceRunner(QString traceFile, Graph* graph, Program* program);

private:
    Graph* _graph;
    Program* _program;
    QXmlStreamReader _xml;
};

}

#endif // TRACERUNNER_HPP
