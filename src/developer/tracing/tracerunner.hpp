#ifndef TRACERUNNER_HPP
#define TRACERUNNER_HPP

#include <QXmlStreamReader>

namespace Developer {

class TraceRunner
{
public:
    TraceRunner();

private:
    Graph* _graph;
    QString _program;
    QXmlStreamReader _xml;
};

}

#endif // TRACERUNNER_HPP
