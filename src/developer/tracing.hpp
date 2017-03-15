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

public slots:
    void goToStart();
    void goToEnd();
    void stepBack();
    void stepForward();
    void findMatch();
    void applyMatch();

private:
    Ui::Tracing *_ui;
};

}

#endif // TRACING_H
