#ifndef TRACING_H
#define TRACING_H

#include <QWidget>
#include "runconfig.hpp"
#include "tracing/tracerunner.hpp"
#include "project.hpp"

namespace Ui {
class Tracing;
}

namespace Developer {

class Tracing : public QWidget
{
    Q_OBJECT

public:
    explicit Tracing(QWidget *parent = 0);
    ~Tracing();

public slots:
    void goToStart();
    void goToEnd();
    void stepBack();
    void stepForward();
    void findMatch();
    void applyMatch();

    void loadTracefile(QString tracefileLocation, RunConfig* runConfig, Project *project);

private:
    Ui::Tracing* _ui;
    TraceRunner* _traceRunner;
};

}

#endif // TRACING_H
