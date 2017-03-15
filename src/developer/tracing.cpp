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
}

void Tracing::applyMatch() {
    qDebug() << "applyMatch()";

}

}
