#include "tracing.hpp"
#include "ui_tracing.h"

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

}
