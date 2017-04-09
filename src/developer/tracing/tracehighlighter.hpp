#ifndef TRACEHIGHLIGHTER_HPP
#define TRACEHIGHLIGHTER_HPP

#include <QVector>
#include <QStack>

#include "token.hpp"
#include "tracestep.hpp"

namespace Developer {


enum TraceDirection {
    FORWARDS,
    BACKWARDS
};


struct TokenReference {
    Token* token;
    int index;
};

class TraceHighlighter
{
public:
    TraceHighlighter(QVector<Token*> programTokens);

    /**
     * Unhiglight the current token and highlight the token represented by the
     * next trace step. The direction being travelled in should be passed in,
     * in order to search forwards or backwards in the program text for the next
     * token.
     * If there is no next step, because the start or end of the trace has been
     * reached, pass a null pointer.
     */
    void update(TraceStep* nextStep, TraceDirection searchDirection);

private:
    QVector<Token*> _programTokens;
    QStack<TokenReference> _tokenStack;
    TraceStep* _currentStep;

    void clearHighlights();
    void replaceCurrentHighlight(TokenReference newToken);
    void pushHighlight(TokenReference newToken);
    void popHighlight();
};

}

#endif // TRACEHIGHLIGHTER_HPP
