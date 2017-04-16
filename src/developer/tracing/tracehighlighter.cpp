#include "tracehighlighter.hpp"

#include <QDebug>

#include "programtokens.hpp"

namespace Developer {

TraceHighlighter::TraceHighlighter(QVector<Token *> programTokens) :
    _programTokens(programTokens),
    _tokenStack(),
    _currentStep(0)
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
    {
        // At the start of a loop, get the next token after the previously
        // highlighted one, which will be a separator or a parenthesis,
        // and higlight it. We do this so that we can jump back to the
        // start of the loop if there's another iteration.
        // There must be a token after the current one, because we cannot be
        // starting a loop at the last token.
        if (searchDirection == FORWARDS && !nextStep->endOfContext) {
            Token* token = _programTokens[searchPos];
            foundToken.token = token;
            foundToken.index = searchPos;
            replaceCurrentHighlight(foundToken);
            break;
        }

        // At the end of a loop, look for the ! symbol.
        if (nextStep->endOfContext) {
            while (searchPos >= 0 && searchPos < _programTokens.size()) {
                Token* token = _programTokens[searchPos];
                if (token->lexeme == ProgramLexeme_Repeat) {
                    foundToken.token = token;
                    foundToken.index = searchPos;
                    replaceCurrentHighlight(foundToken);
                    break;
                }

                searchPos += (searchDirection == FORWARDS) ? 1 : -1;
            }
        }

        break;
    }

    case LOOP_ITERATION:
    {
        // At the start of a loop iteration, get the current top of the highlight
        // stack (the start of the loop), and push a copy onto the top of the stack,
        // so that we can pop it at the end of the iteration to return here.
        if ((searchDirection == FORWARDS && !nextStep->endOfContext) ||
            (searchDirection == BACKWARDS && nextStep->endOfContext)) {
            TokenReference highlighted = _tokenStack.top();
            pushHighlight(highlighted);
            break;
        }

        // At the end of a loop iteration, we want to pop the highlight
        // stack ready for the next iteration.
        if (!nextStep->loopBoundary) {
            popHighlight();
            break;
        }

        // If this is the boundary of the loop, we want to get the current
        // highlighted token, pop the token stack, and then re-highlight the
        // previous one.
        TokenReference highlighted = _tokenStack.top();
        popHighlight();
        replaceCurrentHighlight(highlighted);

        break;
    }

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
    {
        // There's no end token, so we don't need to do anything if we reach
        // the end of the context.
        if (nextStep->endOfContext) { break; }

        QString keyword = (nextStep->type == IF_CONTEXT) ? "if" : "try";

        while (searchPos >= 0 && searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];
            if (token->lexeme == ProgramLexeme_Keyword && token->text == keyword) {
                foundToken.token = token;
                foundToken.index = searchPos;
                replaceCurrentHighlight(foundToken);
                break;
            }

            searchPos += (searchDirection == FORWARDS) ? 1 : -1;
        }

        break;
    }

    case BRANCH_CONDITION:
    {
        // There are no tokens representing the condition context. However, if we are
        // searching backwards and we just came from an else block, we need to jump
        // over the then block if we executed the else).
        if (searchDirection == BACKWARDS && nextStep->endOfContext) {
            if (_currentStep->type == ELSE_BRANCH) {
                int unclosedParens = 0;
                while (searchPos >= 0 && searchPos < _programTokens.size()) {
                    Token* token = _programTokens[searchPos];

                    if (token->lexeme == ProgramLexeme_CloseParen) {
                        unclosedParens += 1;
                    }
                    else if (token->lexeme == ProgramLexeme_OpenParen) {
                        unclosedParens -= 1;
                    }

                    if (unclosedParens == 0) {
                        if (token->lexeme == ProgramLexeme_Keyword && token->text == "then") {
                            foundToken.token = token;
                            foundToken.index = searchPos;
                            replaceCurrentHighlight(foundToken);
                            break;
                        }
                    }

                    searchPos += (searchDirection == FORWARDS) ? 1 : -1;
                }
            }
        }

        break;
    }

    case THEN_BRANCH:
    {
        bool foundElse = false;
        bool foundParens = false;
        int unclosedParens = 0;

        // This token will be used in the case where we are searching backwards and
        // there isn't an else block in the if statement.
        TokenReference endOfBlock;
        endOfBlock.token = 0;
        endOfBlock.index = -1;

        while (searchPos >= 0 && searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];
            if (nextStep->endOfContext) {
                // If we're searching forward and this is the end of the context,
                // we need to jump over the else branch, becuase that won't be
                // executed.
                if (searchDirection == FORWARDS) {
                    // The else block is optional. Since we have reached the end
                    // of the then block, either the next token (the current one)
                    // is an else keyword, meaning we need to skip over the else
                    // block, or it's not, meaning there's no else block and we
                    // can just continue.
                    if (token->lexeme == ProgramLexeme_Keyword && token->text == "else") {
                        foundElse = true;
                    }
                    else {
                        // If we didn't find an else, this is the next token after
                        // the then block.
                        if (!foundElse) {
                            foundToken.token = token;
                            foundToken.index = searchPos;
                            replaceCurrentHighlight(foundToken);
                            break;
                        }
                        else {
                            // If there's a parenthesis after the else, keep going
                            // until we reach the end of the block.
                            if (token->lexeme == ProgramLexeme_OpenParen) {
                                unclosedParens += 1;
                                foundParens = true;
                            }
                            else if (token->lexeme == ProgramLexeme_CloseParen) {
                                unclosedParens -= 1;
                            }

                            // Once all the unclosed parentheses are closed (including
                            // the situation where there were none opened in the first
                            // place), we have reached the end of the else block.
                            if (unclosedParens == 0) {
                                // If we didn't find any parentheses, we should move
                                // forward by one more token, because otherwise we're
                                // highlighting the token in the else block.
                                if (!foundParens) {
                                    if (searchPos < _programTokens.size() - 1) {
                                        searchPos += 1;
                                        token = _programTokens[searchPos];
                                    }
                                    else {
                                        // There are no more tokens, don't try to
                                        // highlight anything.
                                        break;
                                    }
                                }

                                foundToken.token = token;
                                foundToken.index = searchPos;
                                replaceCurrentHighlight(foundToken);
                                break;
                            }
                        }
                    }
                }

                // If it's the end of the context and we're searching backwards,
                // try to find an else keyword. However, the else block is optional.
                if (searchDirection == BACKWARDS) {
                    // We assume this is the end of a block, but we don't yet know
                    // if it's the else block or the then block (since else is optional)
                    // so we'll store the pointer to this token, just in case we need
                    // to come back here.
                    if (endOfBlock.index == -1) {
                        endOfBlock.token = token;
                        endOfBlock.index = searchPos;
                    }

                    if (token->lexeme == ProgramLexeme_CloseParen) {
                        unclosedParens += 1;
                        foundParens = true;
                    }
                    else if (token->lexeme == ProgramLexeme_OpenParen) {
                        unclosedParens -= 1;
                    }

                    // If we have closed any opened parentheses, check for the else
                    // or then keyword.
                    if (unclosedParens == 0) {
                        if (token->lexeme == ProgramLexeme_Keyword) {
                            if (token->text == "else") {
                                foundToken.token = token;
                                foundToken.index = searchPos;
                                replaceCurrentHighlight(foundToken);
                                break;
                            }
                            else if (token->text == "then") {
                                // There wasn't an else block, so reset back to the
                                // token reference we stored earlier.
                                replaceCurrentHighlight(endOfBlock);
                                break;
                            }
                        }
                    }
                }
            }

            // If this is the start of the context, we just need to find the keyword "then".
            if (!nextStep->endOfContext) {
                if (token->lexeme == ProgramLexeme_Keyword && token->text == "then") {
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

    case ELSE_BRANCH:
    {
        // If this is a virtual else, don't do anything, unless we are going
        // backwards. In that case, highlight whatever the current searchPos is
        // because it will be a separator.
        if (nextStep->virtualStep) {
            if (searchDirection == BACKWARDS && nextStep->endOfContext) {
                foundToken.token = _programTokens[searchPos];
                foundToken.index = searchPos;
                replaceCurrentHighlight(foundToken);
            }
            break;
        }

        // There's no end token for an else branch. However, if we're searching
        // forwards and the previous step was a virtual step, mark this step as
        // virtual as well.
        if (nextStep->endOfContext) {
            if (searchDirection == FORWARDS && _currentStep->virtualStep) {
                nextStep->virtualStep = true;
            }
            break;
        }

        // The else block is optional, but we will find ourselves here anyway in
        // an else-less if statement because the compiler adds an else statement
        // which only contains a skip. However, it's perfectly valid for the user
        // to create their own else block which only contains a skip! The current
        // token will be at the end of the condition, so the next keyword will be
        // "then". We can then keep searching until we find the end of the then
        // block. Once we reach the end of that block, if the next token is "else"
        // we can continue as normal. If not, we will have to keep the highlight
        // at the end of the then block.
        int unclosedParens = 0;
        bool foundThen = false;
        bool foundParens = false;
        while (searchPos >= 0 && searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];

            // If we're searching backwards, and we know that the else isn't
            // virtual (because we would have exited early above), we can
            // just look for the else keyword.
            if (searchDirection == BACKWARDS) {
                if (token->lexeme == ProgramLexeme_Keyword && token->text == "else") {
                    foundToken.token = token;
                    foundToken.index = searchPos;
                    replaceCurrentHighlight(foundToken);
                    break;
                }
            }

            if (searchDirection == FORWARDS) {
                if (foundThen) {
                    if (token->lexeme == ProgramLexeme_OpenParen) {
                        unclosedParens += 1;
                        foundParens = true;
                    }
                    else if (token->lexeme == ProgramLexeme_CloseParen) {
                        unclosedParens -= 1;
                    }

                    if (unclosedParens == 0) {
                        if (searchPos < _programTokens.size() - 1) {
                            Token* nextToken = _programTokens[searchPos + 1];
                            foundToken.token = nextToken;
                            foundToken.index = searchPos + 1;
                            replaceCurrentHighlight(foundToken);

                            if (! (nextToken->lexeme == ProgramLexeme_Keyword && nextToken->text == "else")) {
                                nextStep->virtualStep = true;
                            }

                            break;
                        }
                        else {
                            // The if statement is at the end of the program, and there
                            // is no else block, so do nothing.
                            nextStep->virtualStep = true;
                            break;
                        }
                    }
                }
                else if (token->lexeme == ProgramLexeme_Keyword && token->text == "then") {
                    foundThen = true;
                }
            }

            searchPos += (searchDirection == FORWARDS) ? 1 : -1;
        }

        break;
    }


    case OR_CONTEXT:
    {
        // Since there are no markers at either end of an or context, just highlight
        // whatever the next token is. It will always be either a separator, a
        // parenthesis, or the = operator.
        if (searchPos >= 0 && searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];
            foundToken.token = token;
            foundToken.index = searchPos;
            replaceCurrentHighlight(foundToken);
            break;
        }

        // If we are at the end of the program, use a dummy token instead.
        else {
            foundToken.token = _tokenStack.top().token;
            foundToken.index = _programTokens.size();
            replaceCurrentHighlight(foundToken);
            break;
        }
    }

    case OR_LEFT:
    {
        // If this is the start of the context, there is nothing to do, since
        // there is no marker for the start of the left branch of an or statement.
        if (!nextStep->endOfContext) { break; }

        // If it's the end of the context, we need to jump over the right branch.
        // We do this by finding the "or" keyword. If we are going backwards, we
        // need to wait until we have seen the entire right branch of the or, by
        // looking for parentheses.
        // In the forward case, we need to find the "or" keyword and *then* wait
        // until we have seen the right branch.
        bool foundOr = false;
        bool foundParens = false;
        int unmatchedParens = 0;
        while (searchPos >= 0 && searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];

            if (searchDirection == FORWARDS) {
                if (foundOr) {
                    if (token->lexeme == ProgramLexeme_OpenParen) {
                        unmatchedParens += 1;
                        foundParens = true;
                    }
                    else if (token->lexeme == ProgramLexeme_CloseParen) {
                        unmatchedParens -= 1;
                    }

                    if (unmatchedParens == 0) {
                        // If we aren't at the end of the program, and we didn't
                        // find parentheses, move forward one extra token, so that
                        // we are highlighting the separator token, not the token
                        // in the right branch.
                        if (!foundParens) {
                            if (searchPos < _programTokens.size() - 1) {
                                Token* nextToken = _programTokens[searchPos + 1];
                                foundToken.token = nextToken;
                                foundToken.index = searchPos + 1;
                                replaceCurrentHighlight(foundToken);
                                break;
                            }
                        }
                        else {
                            foundToken.token = token;
                            foundToken.index = searchPos;
                            replaceCurrentHighlight(foundToken);
                            break;
                        }
                    }
                }
                else if (token->lexeme == ProgramLexeme_Keyword && token->text == "or") {
                    foundOr = true;
                }
            }

            if (searchDirection == BACKWARDS) {
                if (token->lexeme == ProgramLexeme_CloseParen) {
                    unmatchedParens += 1;
                    foundParens = true;
                }
                else if (token->lexeme == ProgramLexeme_OpenParen) {
                    unmatchedParens -= 1;
                }

                if (unmatchedParens == 0) {
                    if (token->lexeme == ProgramLexeme_Keyword && token->text == "or") {
                        foundToken.token = token;
                        foundToken.index = searchPos;
                        replaceCurrentHighlight(foundToken);
                        break;
                    }
                }
            }

            searchPos += (searchDirection == FORWARDS) ? 1 : -1;
        }

        break;
    }

    case OR_RIGHT:
    {
        // If this is the end of the context, there is nothing to do, since
        // there is no marker for the end of the right branch of an or statement.
        if (nextStep->endOfContext) { break; }

        int unmatchedParens = 0;
        while (searchPos >= 0 && searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];

            // If we are searching backwards, we can simply look for the "or"
            // keyword, since we do not have to jump over anything.
            if (searchDirection == BACKWARDS) {
                if (token->lexeme == ProgramLexeme_Keyword && token->text == "or") {
                    foundToken.token = token;
                    foundToken.index = searchPos;
                    replaceCurrentHighlight(foundToken);
                    break;
                }
            }

            // If we are searching forwards, we need to jump over the left branch,
            // by looking for a full block (either a single token or a matched set
            // of parentheses).
            if (searchDirection == FORWARDS) {
                if (token->lexeme == ProgramLexeme_OpenParen) {
                    unmatchedParens += 1;
                }
                else if (token->lexeme == ProgramLexeme_CloseParen) {
                    unmatchedParens -= 1;
                }

                if (unmatchedParens == 0) {
                    if (token->lexeme == ProgramLexeme_Keyword && token->text == "or") {
                        foundToken.token = token;
                        foundToken.index = searchPos;
                        replaceCurrentHighlight(foundToken);
                        break;
                    }
                }
            }

            searchPos += (searchDirection == FORWARDS) ? 1 : -1;
        }

        break;
    }

    case SKIP:
    {
        // If this step is virtual, do nothing.
        if (nextStep->virtualStep) {
            break;
        }

        // If the previous step was virtual, i.e. it was an else context without
        // a real else block in the program, mark this skip as virtual.
        if (searchDirection == FORWARDS && _currentStep->virtualStep) {
            nextStep->virtualStep = true;
            break;
        }

        while (searchPos > 0 && searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];
            if (token->lexeme == ProgramLexeme_Keyword && token->text == "skip") {
                foundToken.token = token;
                foundToken.index = searchPos;
                replaceCurrentHighlight(foundToken);
                break;
            }

            searchPos += (searchDirection == FORWARDS) ? 1 : -1;
        }

        break;
    }

    case BREAK:
    case FAIL:
    {
        QString keyword = (nextStep->type == BREAK) ? "break" : "fail";

        while (searchPos > 0 && searchPos < _programTokens.size()) {
            Token* token = _programTokens[searchPos];
            if (token->lexeme == ProgramLexeme_Keyword && token->text == keyword) {
                foundToken.token = token;
                foundToken.index = searchPos;
                replaceCurrentHighlight(foundToken);
                break;
            }

            searchPos += (searchDirection == FORWARDS) ? 1 : -1;
        }

        break;
    }

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
