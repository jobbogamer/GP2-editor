/*!
 * \file
 */
#include "programhighlighter.hpp"

#include <QSettings>
#include <QDebug>
#include <QVector>
#include <QTextDocument>

#include "global.hpp"

namespace Developer {

ProgramHighlighter::ProgramHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
}

void ProgramHighlighter::setTokens(QVector<Token *> tokens)
{
    _tokens = tokens;
}

void ProgramHighlighter::highlightBlock(const QString &text)
{
    if(text.isEmpty())
        return;
    int startPosition = currentBlock().position();
    int endPosition = startPosition + text.length();

    for(QVector<Token *>::iterator iter = _tokens.begin()
        ; iter != _tokens.end(); ++iter)
    {
        Token *t = *iter;
        // Is this entire block contained in this token?
        if(t->startPos <= startPosition && t->endPos >= endPosition)
        {
            setFormat(0, text.length(), format(t->lexeme, t->emphasise));
            return;
        }

        // Is the start position of this token in range?
        if(t->startPos >= startPosition && t->startPos < endPosition)
        {
            if(t->endPos > endPosition)
                setFormat(t->startPos - startPosition,
                          text.length() - (t->startPos - startPosition),
                          format(t->lexeme, t->emphasise));
            else
                setFormat(t->startPos - startPosition,
                          t->endPos - t->startPos,
                          format(t->lexeme, t->emphasise));
        }
        // If not is the end position in range?
        else if(t->endPos > startPosition && t->endPos <= endPosition)
        {
            if(t->startPos < startPosition)
                setFormat(0,
                          t->endPos - t->startPos,
                          format(t->lexeme, t->emphasise));
            else
                setFormat(t->startPos - startPosition,
                          t->endPos - t->startPos,
                          format(t->lexeme, t->emphasise));
        }
    }
}

QTextCharFormat ProgramHighlighter::format(int type, bool emphasise) const
{
    QSettings settings;
    QColor background = settings.value(
                "Editor/BackgroundColor",
                QColor(0xff,0xff,0xff,0)
                ).value<QColor>();

    // Start a character format, the majority will be on the default background
    QTextCharFormat ret;
    ret.setBackground(background);

    QFont defaultFont = EDITOR_DEFAULT_FONT;
    QFont defaultCommentFont = defaultFont;
    defaultCommentFont.setItalic(true);

    switch(type)
    {
    case ProgramLexeme_Default:
        ret.setForeground(settings.value(
                              "Editor/Types/Default/Foreground",
                              QColor(Qt::black)
                              ).value<QColor>()
                          );
        ret.setBackground(settings.value(
                              "Editor/Types/Default/Background",
                              background
                              ).value<QColor>()
                          );
        ret.setFont(settings.value(
                        "Editor/Types/Default/Font",
                        defaultFont
                        ).value<QFont>()
                    );
        break;

    case ProgramLexeme_Declaration:
        ret.setForeground(settings.value(
                              "Editor/Types/Identifier/Foreground",
                              QColor(Qt::darkGreen)
                              ).value<QColor>()
                          );
        ret.setBackground(settings.value(
                              "Editor/Types/Identifier/Background",
                              background
                              ).value<QColor>()
                          );
        ret.setFont(settings.value(
                        "Editor/Types/Identifier/Font",
                        defaultFont
                        ).value<QFont>()
                    );
        break;

    case ProgramLexeme_Identifier:
        ret.setForeground(settings.value(
                              "Editor/Types/Identifier/Foreground",
                              QColor(Qt::darkBlue)
                              ).value<QColor>()
                          );
        ret.setBackground(settings.value(
                              "Editor/Types/Identifier/Background",
                              background
                              ).value<QColor>()
                          );
        ret.setFont(settings.value(
                        "Editor/Types/Identifier/Font",
                        defaultFont
                        ).value<QFont>()
                    );
        break;

    /*case Number:
        ret.setForeground(settings.value(
                              "Editor/Types/Number/Foreground",
                              QColor(Qt::darkCyan)
                              ).value<QColor>()
                          );
        ret.setBackground(settings.value(
                              "Editor/Types/Number/Background",
                              background
                              ).value<QColor>()
                          );
        ret.setFont(settings.value(
                        "Editor/Types/Number/Font",
                        defaultFont
                        ).value<QFont>()
                    );
        break;

    case QuotationCharacter:
    case QuotedString:
        ret.setForeground(settings.value(
                              "Editor/Types/QuotedString/Foreground",
                              QColor(Qt::red)
                              ).value<QColor>()
                          );
        ret.setBackground(settings.value(
                              "Editor/Types/QuotedString/Background",
                              background
                              ).value<QColor>()
                          );
        ret.setFont(settings.value(
                        "Editor/Types/QuotedString/Font",
                        defaultFont
                        ).value<QFont>()
                    );
        break;*/

    case ProgramLexeme_DeclarationOperator:
    //case ProgramLexeme_DeclarationSeparator:
    case ProgramLexeme_Keyword:
    case ProgramLexeme_OpenParen:
    case ProgramLexeme_CloseParen:
    case ProgramLexeme_OpenBrace:
    case ProgramLexeme_CloseBrace:
    case ProgramLexeme_Repeat:
    case ProgramLexeme_StatementSeparator:
    case ProgramLexeme_RuleSeparator:
        ret.setForeground(settings.value(
                              "Editor/Types/Keyword/Foreground",
                              QColor(Qt::darkYellow)
                              ).value<QColor>()
                          );
        ret.setBackground(settings.value(
                              "Editor/Types/Keyword/Background",
                              background
                              ).value<QColor>()
                          );
        ret.setFont(settings.value(
                        "Editor/Types/Keyword/Font",
                        defaultFont
                        ).value<QFont>()
                    );
        break;

    case ProgramLexeme_Comment:
    case ProgramLexeme_CommentOpen:
    case ProgramLexeme_CommentClose:
        ret.setForeground(settings.value(
                              "Editor/Types/Comment/Foreground",
                              QColor(Qt::darkCyan)
                              ).value<QColor>()
                          );
        ret.setBackground(settings.value(
                              "Editor/Types/Comment/Background",
                              background
                              ).value<QColor>()
                          );
        ret.setFont(settings.value(
                        "Editor/Types/Comment/Font",
                        defaultCommentFont
                        ).value<QFont>()
                    );
        break;

    case ProgramLexeme_Error:
        ret.setForeground(settings.value(
                              "Editor/Types/Error/Foreground",
                              QColor(Qt::darkRed)
                              ).value<QColor>()
                          );
        ret.setBackground(settings.value(
                              "Editor/Types/Error/Background",
                              QColor(0xff,0xcc,0xcc) // light red
                              ).value<QColor>()
                          );
        ret.setFont(settings.value(
                        "Editor/Types/Error/Font",
                        defaultFont
                        ).value<QFont>()
                    );
        // Return early because we don't want to check the highlight property.
        return ret;

    default:
        qDebug() << "ProgramHighlighter::format(): Unknown type passed in: "
                 << type;
        ret.setForeground(settings.value(
                              "Editor/Types/Error/Foreground",
                              QColor(Qt::darkGray)
                              ).value<QColor>()
                          );
        ret.setBackground(settings.value(
                              "Editor/Types/Error/Background",
                              QColor(0xff,0xff,0xcc) // light yellow
                              ).value<QColor>()
                          );
        ret.setFont(settings.value(
                        "Editor/Types/Error/Font",
                        defaultFont
                        ).value<QFont>()
                    );
        // Return early because we don't want to check the highlight property.
        return ret;
    }

    // Now check the highlight property. If it is true, we will override the
    // background colour that was previously set and use the highlight colour.
    if (emphasise) {
        QColor emphasisColour = settings.value(
            "Editor/Types/Emphasis/Background",
            QColor(144, 249, 114) // pale green
            ).value<QColor>();

        ret.setBackground(emphasisColour);
    }

    return ret;
}

}
