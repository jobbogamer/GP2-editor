/*!
 * \file
 */
#ifndef TOKEN_HPP
#define TOKEN_HPP

#include <QString>

namespace Developer {

/*!
 * \brief The Token struct describes a match of a token to a particular
 *  substring within the document
 */
struct Token
{
    //! The start position of this token
    int startPos;
    //! The end position of this token
    int endPos;
    //! The type of token this instance represents
    int lexeme;
    //! Choose whether to highlight the background of this token, such as when tracing
    bool emphasise;
    //! The actual matched text
    QString text;
    //!  \brief A description of this token, this is primarily used to contain
    //! error messages for Error type tokens
    QString description;

    Token():
        startPos(0),
        endPos(0),
        lexeme(0),
        emphasise(false),
        text(QString()),
        description(QString())
    {}
};

}

#endif // TOKEN_HPP
