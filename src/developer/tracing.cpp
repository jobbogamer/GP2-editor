#include "tracing.hpp"
#include "ui_tracing.h"

#include <QDebug>

namespace Developer {

Tracing::Tracing(QWidget *parent) :
    QWidget(parent),
    _ui(new Ui::Tracing),
    _traceRunner(0)
{
    _ui->setupUi(this);
}

Tracing::~Tracing()
{
    delete _ui;
}

/**
 * Overrides QWidget::showEvent() to signal MainWindow::graphHasFocus() when
 * the tracing tab becomes visible.
 */
void Tracing::showEvent(QShowEvent* event) {
    emit becameVisible(_ui->graphView);
}

/**
 * Overrides QWidget::hideEvent() to signal MainWindow::graphLostFocus() when
 * the tracing tab becomes invisible.
 */
void Tracing::hideEvent(QHideEvent* event) {
    emit becameHidden(_ui->graphView);
}

void Tracing::loadTracefile(QString tracefileLocation, RunConfig* runConfig, Project* project) {
    qDebug() << "Obtained tracefile " << tracefileLocation << " from run config " << runConfig->name();

    /* Create a new TraceRunner which uses the new tracefile.
     * This will overwrite any ongoing trace, but if the user has re-run
     * the program, they probably want to start from the beginning anyway. */
    if (_traceRunner) { delete _traceRunner; }
    _traceRunner = new TraceRunner(tracefileLocation,
                                   project->graph(runConfig->graph()),
                                   project->program(runConfig->program()));

    /* Load the graph into the graph view. Note that since this is just a
    pointer to the graph, any changes made to the graph will automatically
    be reflected in the graph view. */
    _ui->graphView->setGraph(_traceRunner->graph());
}

void Tracing::goToStart() {
    qDebug() << "goToStart()";
}

void Tracing::goToEnd() {
    qDebug() << "goToEnd()";
}

void Tracing::stepBack() {
    qDebug() << "stepBack()";
}

void Tracing::stepForward() {
    qDebug() << "stepForward()";
}

void Tracing::findMatch() {
    qDebug() << "findMatch()";

    // Once a match has been shown, update the Find Match button to be an Apply
    // Match button, by updating the icon, text, and connected slot.
    _ui->matchButton->setIcon(QIcon(QPixmap(":/icons/apply_match.png")));
    _ui->matchButton->setText(tr("Apply Match"));
    _ui->matchButton->setToolTip(tr("Apply the rule using the chosen match."));
    _ui->matchButton->disconnect(this);
    this->connect(_ui->matchButton, SIGNAL(clicked()), SLOT(applyMatch()));
}

void Tracing::applyMatch() {
    qDebug() << "applyMatch()";

    // When a match has been applied, change the Apply Match button back to a Find
    // Match button, by updating the icon, text, and connected slot.
    _ui->matchButton->setIcon(QIcon(QPixmap(":/icons/find_match.png")));
    _ui->matchButton->setText(tr("Find Match"));
    _ui->matchButton->setToolTip(tr("Show the selected match for the next rule."));
    _ui->matchButton->disconnect(this);
    this->connect(_ui->matchButton, SIGNAL(clicked()), SLOT(findMatch()));
}

}
