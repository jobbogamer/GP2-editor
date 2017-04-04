#include "tracing.hpp"
#include "ui_tracing.h"

#include <QDebug>
#include <QMessageBox>

namespace Developer {

Tracing::Tracing(QWidget *parent) :
    QWidget(parent),
    _ui(new Ui::Tracing),
    _traceRunner(0),
    _graphFile(0),
    _programFile(0)
{
    _ui->setupUi(this);
    _ui->graphView->graphScene()->setReadOnly(true);
}

Tracing::~Tracing()
{
    delete _ui;
    if (_graphFile) { delete _graphFile; }
    if (_programFile) { delete _programFile; }
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

    /* We need to update the graph and program filenames to use the copies
     * created for tracing, so that changes don't get written to the orignal
     * files. (e.g. Graph::AddNode() writes the new node to the actual .host
     * file, which would mean the input graph itself is being modified while
     * the trace is being executed.) */
    Graph* originalGraph = project->graph(runConfig->graph());
    QString graphPath = originalGraph->absolutePath().replace(".host", "_tracing.host");
    _graphFile = new Graph(graphPath);

    Program* originalProgram = project->program(runConfig->program());
    QString programPath = originalProgram->absolutePath().replace(".gp2", "_tracing.gp2");
    _programFile = new Program(programPath);

    // Pass the program to the program widget.
    _ui->programView->setPlainText(_programFile->program());
    _ui->programView->parse();

    _traceRunner = new TraceRunner(tracefileLocation,
                                   _graphFile,
                                   _ui->programView->tokens());

    if (!_traceRunner->isInitialised()) {
        QMessageBox::warning(
            this,
            tr("Error Loading Tracefile"),
            tr("An error occurred when loading the tracefile. See the log for details.")
        );
        return;
    }

    QVector<Token*> tokens = _ui->programView->tokens();
    for (int i = 0; i < tokens.size(); i++) {
        Token* token = tokens[i];
        qDebug() << tr("Token (%1, %2) <%3>: %4")
                    .arg(token->startPos)
                    .arg(token->endPos)
                    .arg(token->lexeme)
                    .arg(token->text);
    }

    updateUI();

    // We can now signal that tracing is ready, because we have everything
    // we need to show the tracing tab.
    emit tracingReady();
}

void Tracing::goToStart() {
    // Assuming the button is only enabled if it's possible to jump to the
    // start (i.e. we are not already at the start).
    bool success = _traceRunner->goToStart();
   if (!success) { showXMLError(); }
    updateUI();
}

void Tracing::goToEnd() {
    // Assuming the button is only enabled if it's possible to jump to the
    // end (i.e. we are not already at the end).
    bool success = _traceRunner->goToEnd();
    if (!success) { showXMLError(); }
    updateUI();
}

void Tracing::stepBack() {
    // Assuming the button is only enabled if a backwards step is available.
    bool success = _traceRunner->stepBackward();
    if (!success) { showXMLError(); }
    updateUI();
}

void Tracing::stepForward() {
    // Assuming the button is only enabled if a forward step is available.
    bool success = _traceRunner->stepForward();
    if (!success) { showXMLError(); }
    updateUI();
}

void Tracing::findMatch() {
    qDebug() << "findMatch()";
    updateUI();
}

void Tracing::applyMatch() {
    qDebug() << "applyMatch()";
    updateUI();
}

/**
 * Updates the enabled/disabled state of the control strip buttons based on the
 * state of the TraceRunner, and updates the graph view.
 */
void Tracing::updateUI() {
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

    // Update the graph view.
    _ui->graphView->setGraph(_traceRunner->graph());
}

/**
 * Show a dialog box explaining that an XML error occurred.
 */
void Tracing::showXMLError() {
    qDebug() << "XML error in tracefile:" << _traceRunner->getXMLError();
    QMessageBox::warning(
        this,
        tr("Tracing Error"),
        tr("An error occurred when reading the tracefile. See the log for details.")
    );
}

}
