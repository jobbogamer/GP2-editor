#ifndef TRACING_H
#define TRACING_H

#include <QWidget>

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

private:
    Ui::Tracing *_ui;
};

}

#endif // TRACING_H
