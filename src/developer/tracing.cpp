#include "tracing.hpp"
#include "ui_tracing.h"

#include <QDebug>

namespace Developer {

Tracing::Tracing(QWidget *parent) :
    QWidget(parent),
    _ui(new Ui::Tracing)
{
    _ui->setupUi(this);
}

Tracing::~Tracing()
{
    delete _ui;
}


void Tracing::loadTracefile(QString tracefileLocation, RunConfig* runConfig) {
    qDebug() << "Obtained tracefile " << tracefileLocation << " from run config " << runConfig->name();
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
