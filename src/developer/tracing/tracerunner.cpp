#include "tracerunner.hpp"

#include <QDebug>

namespace Developer {

TraceRunner::TraceRunner(QString traceFile, Graph* graph, Program* program) :
    _graph(graph),
    _program(program)
{
    qDebug() << "Starting trace using graph: " << graph->toString();
    qDebug() << "and program: " << program->program();
}

Graph* TraceRunner::graph() {
    return _graph;
}

}
