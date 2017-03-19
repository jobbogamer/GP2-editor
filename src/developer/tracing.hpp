#ifndef TRACING_H
#define TRACING_H

#include <QWidget>
#include "runconfig.hpp"
#include "tracing/tracerunner.hpp"
#include "project.hpp"
#include "graphview/graphwidget.hpp"

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

    virtual void showEvent(QShowEvent* event);
    virtual void hideEvent(QHideEvent* event);

public slots:
    void goToStart();
    void goToEnd();
    void stepBack();
    void stepForward();
    void findMatch();
    void applyMatch();

    void loadTracefile(QString tracefileLocation, RunConfig* runConfig, Project *project);

signals:
    void becameVisible(GraphWidget* widget);
    void becameHidden(GraphWidget* widget);

private:
    Ui::Tracing* _ui;
    TraceRunner* _traceRunner;
};

}

#endif // TRACING_H
