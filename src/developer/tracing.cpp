#include "tracing.hpp"
#include "ui_tracing.h"

#include <QDebug>
#include <QMessageBox>

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

    if (!_traceRunner->isInitialised()) {
        QMessageBox::warning(
            this,
            tr("Error Loading Tracefile"),
            tr("An error occurred when loading the tracefile. See the log for details.")
        );
        return;
    }

    updateButtons();

    /* Load the graph into the graph view. Note that since this is just a
    pointer to the graph, any changes made to the graph will automatically
    be reflected in the graph view. */
    _ui->graphView->setGraph(_traceRunner->graph());

    // We can now signal that tracing is ready, because we have everything
    // we need to show the tracing tab.
    emit tracingReady();
}

void Tracing::goToStart() {
    qDebug() << "goToStart()";
    updateButtons();
}

void Tracing::goToEnd() {
    qDebug() << "goToEnd()";
    updateButtons();
}

void Tracing::stepBack() {
    // Assuming the button is only enabled if a backwards step is available.
    _traceRunner->stepBackward();
    updateButtons();
}

void Tracing::stepForward() {
    // Assuming the button is only enabled if a forward step is available.
    _traceRunner->stepForward();
    updateButtons();
}

void Tracing::findMatch() {
    qDebug() << "findMatch()";
    updateButtons();
}

void Tracing::applyMatch() {
    qDebug() << "applyMatch()";
    updateButtons();
}

/**
 * Updates the enabled/disabled state of the control strip buttons based on the
 * state of the TraceRunner.
 */
void Tracing::updateButtons() {
    // If backwards steps are not available, we must be at the start of the
    // trace, so there's no point jumping to the start or stepping backwards.
    bool backAvailable = _traceRunner->isBackwardStepAvailable();
    _ui->goToStartButton->setEnabled(backAvailable);
    _ui->stepBackButton->setEnabled(backAvailable);

    // If forward steps are not available, we must be at the end of the trace,
    // so there's no point jumping to the end or stepping forwards.
    bool forwardAvailable = _traceRunner->isForwardStepAvailable();
    _ui->goToEndButton->setEnabled(forwardAvailable);
    _ui->stepForwardButton->setEnabled(forwardAvailable);

    // Set up the match button based on the current state of the trace. Either
    // make it into the "find match" button, the "apply match" button, or
    // disable it altogether.
    // We will default to the "find match" icon and tooltip, and only change it
    // in the case where match application is available.
    if (_traceRunner->isMatchApplicationAvailable()) {
        _ui->matchButton->setIcon(QIcon(QPixmap(":/icons/apply_match.png")));
        _ui->matchButton->setText(tr("Apply Match"));
        _ui->matchButton->setToolTip(tr("Apply the rule using the chosen match."));
        _ui->matchButton->disconnect(this);
        this->connect(_ui->matchButton, SIGNAL(clicked()), SLOT(applyMatch()));
        _ui->matchButton->setEnabled(true);
    }
    else {
        _ui->matchButton->setIcon(QIcon(QPixmap(":/icons/find_match.png")));
        _ui->matchButton->setText(tr("Find Match"));
        _ui->matchButton->setToolTip(tr("Show the selected match for the next rule."));
        _ui->matchButton->disconnect(this);
        this->connect(_ui->matchButton, SIGNAL(clicked()), SLOT(findMatch()));
        _ui->matchButton->setEnabled(_traceRunner->isFindMatchAvailable());
    }
}

}
