#ifndef TRACERUNNER_HPP
#define TRACERUNNER_HPP

#include <QXmlStreamReader>
#include <QFile>
#include <QVector>
#include <QStack>
#include <boost/optional.hpp>
#include "graph.hpp"
#include "program.hpp"
#include "tracestep.hpp"

namespace Developer {

class TraceRunner
{
public:
    TraceRunner(QString traceFile, Graph* graph, Program* program);
    ~TraceRunner();

    /**
     * Returns a pointer to the graph being modified by the program. This graph
     * will automatically be updated while stepping through the trace.
     */
    Graph* graph();

    /**
     * Call this after using the constructor to check whether the TraceRunner
     * was successfully initialised. If this method returns false, this
     * TraceRunner object is not safe to use.
     */
    bool isInitialised();

    /**
     * Returns whether or not there is another step in the trace after the
     * current one. In other words, if this method returns false, either the
     * current graph is the output graph, or the previous step resulted in
     * failure.
     */
    bool isForwardStepAvailable();

    /**
     * Returns whether or not there was a step in teh trace before the current
     * one. In other words, if this method returns false, the current graph is
     * the input graph.
     */
    bool isBackwardStepAvailable();

    /**
     * Returns whether or not the next step is a rule, meaning the match can
     * be found for that rule.
     */
    bool isFindMatchAvailable();

    /**
     * Returns whether or not there is an un-applied match (as found by the
     * findMatch() method) which can be applied by the applyMatch() function.
     */
    bool isMatchApplicationAvailable();

    /**
     * Move forward in the trace by one step, updating the state of the graph
     * if the step made any changes to it.
     * Returns true if stepping is successful, or false if an error occurs.
     */
    bool stepForward();

    /**
     * Move backward in the trace by one step, reverting the state of the graph
     * if the step had previously made any changes to it.
     * Returns true if stepping is successful, or false if an error occurs.
     */
    bool stepBackward();

    /**
     * Jump ahead to the end of the trace, updating the graph to be the output
     * graph from the program.
     * Returns true if the jump is successful, or false if an error occurs.
     */
    bool goToEnd();

    /**
     * Jump back to the start of the trace, reverting the graph to the input
     * graph from the program.
     * Returns true if the jump is successful, or false if an error occurs.
     */
    bool goToStart();

    /**
     * Returns the match morphism for the current rule, if there is one. Since a
     * morphism can be empty but still be valid, this method returns an optional,
     * which will be boost::none if the rule did not have a valid match.
     */
    boost::optional<Morphism> findMatch();

    /**
     * Moves forward in the trace by applying a rule for which a match was previously
     * found using the findMatch() function. The state of the graph will be updated.
     */
    void applyMatch();

    QString getXMLError();

private:
    Graph* _graph;
    Program* _program;
    QXmlStreamReader* _xml;
    QFile _tracefile;
    bool _initialised;
    bool _parseComplete;
    QVector<TraceStep> _traceSteps;
    int _currentStep;
    QStack<TraceStepType> _contextStack;

    bool parseStep();
    bool parseStartElement(TraceStep* step);
    bool parseGraphChange(GraphChange* change);

    label_t parseLabel(QString label, QString mark);
    edge_t parseEdge(QXmlStreamAttributes xmlAttributes);
    node_t parseNode(QXmlStreamAttributes xmlAttributes);

    void enterContext(TraceStep& context);
    void exitContext();
    void applyCurrentStepChanges();
    void revertCurrentStepChanges();
};

}

#endif // TRACERUNNER_HPP
