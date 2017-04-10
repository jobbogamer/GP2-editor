#include "tracehighlighter.hpp"

#include <QDebug>

#include "programtokens.hpp"

namespace Developer {

TraceHighlighter::TraceHighlighter(QVector<Token *> programTokens) :
    _programTokens(programTokens),
    _tokenStack()
{
    // When starting the trace, ensure no tokens are highlighted. A token may
    // be highlighted if this is not the first run of the traced program, since
    // we are using the tokens created by the ProgramEditor object, so when this
    // object dies, the tokens remain.
    clearHighlights();
}


void TraceHighlighter::update(TraceStep *nextStep, TraceDirection searchDirection) {
    // If we are currently pointing at a procedure call, move the highlight to
    // the declaration of that procedure before searching for the next token.
    // Since a procedure can be declared pretty much anywhere, we have to
    // start the search at the beginning of the token vector.
    if (_currentStep && _currentStep->type == PROCEDURE) {
        bool lookingForNext = false;
        int searchPos = 0;
        while (searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];
            if (token->lexeme == ProgramLexeme_Declaration) {
                if ((token->text == _currentStep->contextName) || lookingForNext) {
                    // To find the actual implementation of the procedure, we have to
                    // look for the = sign after the name. When a procedure is
                    // called, the token is still tagged as a declaration (presumably
                    // to give procedure calls the same colour), and we don't want
                    // to highlight a procedure call by mistake.
                    Token* nextToken = _programTokens[searchPos + 1];
                    if (nextToken->lexeme == ProgramLexeme_DeclarationOperator) {
                        TokenReference foundToken;
                        foundToken.token = token;
                        foundToken.index = searchPos;

                        if ((searchDirection == FORWARDS && !_currentStep->endOfContext) || lookingForNext) {
                            // Since we will have to jump back to the call site, push the
                            // token onto the stack rather than replacing it.
                            pushHighlight(foundToken);
                            lookingForNext = false;
                            break;
                        }
                        else if (searchDirection == BACKWARDS && _currentStep->endOfContext) {
                            // When we are searching backwards, we have to enter the
                            // procedure at the end, not the beginning. Because the
                            // only top level program structure in GP2 is procedure
                            // delcarations, we can find the end of this procedure
                            // by searching for the next procedure declaration in the
                            // program. We set this flag to true, which signifies that
                            // we don't care that the procedure name matches, we just
                            // want to find the next procedure in the program, whatever
                            // it may be. If we push that procedure declaration onto
                            // the token stack, when we update the highlight for nextStep,
                            // the search will start at the beginning of the next procedure
                            // and work backwards, essentially entering this procedure from
                            // the end.
                            lookingForNext = true;
                        }
                    }
                }
            }
            // Even if we're stepping backwards, we started the search at the
            // beginning of the program, so we always move forwards.
            searchPos += 1;
        }

        if (lookingForNext) {
            // We were looking for the next declaration, but we reached the end
            // of the program before we found one, so put a dummy token on the
            // stack, with the index set to the end of the program so that when
            // we start searching below, the search starts at the end of the
            // program, which we have determined is also the end of the procedure.
            TokenReference dummyToken;
            dummyToken.token = _programTokens.last();
            dummyToken.index = _programTokens.size();
            pushHighlight(dummyToken);
        }
    }

    // We have to start the search at the currently highlighted token. However,
    // if the token stack is empty, we have to start at the start or the end of
    // the program tokens vector, because there is no current token.
    int searchPos;
    if (_tokenStack.empty()) {
        searchPos = (searchDirection == FORWARDS) ? 0 : (_programTokens.size() - 1);
    }
    else {
        searchPos = _tokenStack.top().index;
        searchPos += (searchDirection == FORWARDS) ? 1 : -1;
    }

    TokenReference foundToken;
    foundToken.token = 0;
    foundToken.index = -1;

    // If no step was given, unhighlight everything.
    if (!nextStep) {
        clearHighlights();

        // Put a new TokenReference on the stack which is identical to the token
        // that was highlighted at the end of the trace, but with the index
        // increased by one. This means that if we now step backwards, we start
        // the search in the correct place.
        TokenReference previousToken = _tokenStack.pop();
        TokenReference dummyToken;
        dummyToken.token = previousToken.token;
        dummyToken.index = previousToken.index + 1;
        _tokenStack.push(dummyToken);

        _currentStep = nextStep;
        return;
    }

    switch (nextStep->type) {
    case RULE_MATCH:
    case RULE_MATCH_FAILED:
    case RULE_APPLICATION:
        // We do not need to update the program position for <match> or <apply>, since
        // they are component parts of a rule in the source text.
        break;

    case RULE:
    {
        // Depending on the direction we are moving through the program, we need to
        // either ignore the end of the context or the start of it. This is because
        // a rule call is represented by a single token in the source code, so we
        // don't need to move the highlight.
        if ((searchDirection == FORWARDS  && nextStep->endOfContext) ||
            (searchDirection == BACKWARDS && !nextStep->endOfContext)) {
            break;
        }

        // For some reason, rule names are prefixed with "Main_" by the compiler,
        // so we need to remove that from the context name before searching for the
        // correct token.
        QString ruleName = nextStep->contextName.remove("Main_");

        while (searchPos >= 0 && searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];
            if (token->lexeme == ProgramLexeme_Identifier) {
                if (token->text == ruleName) {
                    foundToken.token = token;
                    foundToken.index = searchPos;
                    replaceCurrentHighlight(foundToken);
                    break;
                }
            }

            searchPos += (searchDirection == FORWARDS) ? 1 : -1;
        }

        break;
    }

    case RULE_SET:
    {
        // If this is the end of the context, we need to search for the
        // closing brace instead of the opening one.
        int lexeme = (nextStep->endOfContext) ? ProgramLexeme_CloseBrace : ProgramLexeme_OpenBrace;

        while (searchPos >= 0 && searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];
            if (token->lexeme == lexeme) {
                foundToken.token = token;
                foundToken.index = searchPos;
                replaceCurrentHighlight(foundToken);
                break;
            }

            searchPos += (searchDirection == FORWARDS) ? 1 : -1;
        }

        break;
    }

    case LOOP:
    case LOOP_ITERATION:
        break;

    case PROCEDURE:
    {
        // If we are stepping forward and this is the end of the context, or
        // we are stepping backwards and this is the start of the context, we
        // simply pop the token stack to get back to the call site.
        if ((searchDirection == FORWARDS  && nextStep->endOfContext) ||
            (searchDirection == BACKWARDS && !nextStep->endOfContext)) {
            popHighlight();
            break;
        }

        while (searchPos >= 0 && searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];
            if (token->lexeme == ProgramLexeme_Declaration) {
                if (token->text == nextStep->contextName) {
                    foundToken.token = token;
                    foundToken.index = searchPos;
                    replaceCurrentHighlight(foundToken);
                    break;
                }
            }

            searchPos += (searchDirection == FORWARDS) ? 1 : -1;
        }

        break;
    }

    case IF_CONTEXT:
    case TRY_CONTEXT:
    case BRANCH_CONDITION:
    case THEN_BRANCH:
    case ELSE_BRANCH:
    case OR_CONTEXT:
    case OR_LEFT:
    case OR_RIGHT:
    case SKIP:
    case BREAK:
    case FAIL:
    case UNKNOWN:
    default:
        qDebug() << "Unhandled step of type" << nextStep->type;
        break;
    }

    // Update our current step pointer.
    _currentStep = nextStep;
}


/**
 * Iterate over all the program tokens and ensure they are not highlighted.
 */
void TraceHighlighter::clearHighlights() {
    for (int i = 0; i < _programTokens.size(); i++) {
        Token* token = _programTokens[i];
        token->emphasise = false;
    }
}

/**
 * Pop the current token from the stack, unhighlight it, push the new one
 * on, and highlight it.
 */
void TraceHighlighter::replaceCurrentHighlight(TokenReference newToken) {
    // Pop the top of the stack, and un-highlight that token.
    if (!_tokenStack.empty()) {
        TokenReference previous = _tokenStack.pop();
        previous.token->emphasise = false;
    }

    // Highlight the new token, and push it onto the stack.
    newToken.token->emphasise = true;
    _tokenStack.push(newToken);
}


/**
 * Unhighlight the token at the top of the stack without popping it, push
 * the new one on, and highlight it.
 */
void TraceHighlighter::pushHighlight(TokenReference newToken) {
    if (!_tokenStack.empty()) {
        // Note we are *not* popping the stack here, just peeking.
        TokenReference previous = _tokenStack.top();
        previous.token->emphasise = false;
    }

    newToken.token->emphasise = true;
    _tokenStack.push(newToken);
}


/**
 * Pop the token from the top of the stack and unhighlight it. If there is
 * another token below it on the stack, highlight it.
 */
void TraceHighlighter::popHighlight() {
    // Pop and unhighlight.
    TokenReference topToken = _tokenStack.pop();
    topToken.token->emphasise = false;

    // Highlight the next token if there is one.
    if (!_tokenStack.empty()) {
        topToken = _tokenStack.top();
        topToken.token->emphasise = true;
    }
}

}
