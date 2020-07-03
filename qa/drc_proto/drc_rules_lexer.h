
/* Do not modify this file it was automatically generated by the
 * TokenList2DsnLexer CMake script.
 */

#ifndef DRC_RULES_LEXER_H_
#define DRC_RULES_LEXER_H_

#include <dsnlexer.h>

/**
 * C++ does not put enum _values_ in separate namespaces unless the enum itself
 * is in a separate namespace.  All the token enums must be in separate namespaces
 * otherwise the C++ compiler will eventually complain if it sees more than one
 * DSNLEXER in the same compilation unit, say by mutliple header file inclusion.
 * Plus this also enables re-use of the same enum name T.  A typedef can always be used
 * to clarify which enum T is in play should that ever be a problem.  This is
 * unlikely since Parse() functions will usually only be exposed to one header
 * file like this one.  But if there is a problem, then use:
 *   typedef DRCRULE_T::T T;
 * within that problem area.
 */
namespace DRCRULE_T
{
    /// enum T contains all this lexer's tokens.
    enum T
    {
        // these first few are negative special ones for syntax, and are
        // inherited from DSNLEXER.
        T_NONE          = DSN_NONE,
        T_COMMENT       = DSN_COMMENT,
        T_STRING_QUOTE  = DSN_STRING_QUOTE,
        T_QUOTE_DEF     = DSN_QUOTE_DEF,
        T_DASH          = DSN_DASH,
        T_SYMBOL        = DSN_SYMBOL,
        T_NUMBER        = DSN_NUMBER,
        T_RIGHT         = DSN_RIGHT,        // right bracket: ')'
        T_LEFT          = DSN_LEFT,         // left bracket:  '('
        T_STRING        = DSN_STRING,       // a quoted string, stripped of the quotes
        T_EOF           = DSN_EOF,          // special case for end of file

        T_allow = 0,
        T_condition,
        T_enable,
        T_error,
        T_expression,
        T_ignore,
        T_info,
        T_max,
        T_min,
        T_name,
        T_opt,
        T_priority,
        T_rule,
        T_severity,
        T_type,
        T_version,
        T_warning
    };
}   // namespace DRCRULE_T


/**
 * Class DRC_RULES_LEXER
 * is an automatically generated class using the TokenList2DnsLexer.cmake
 * technology, based on keywords provided by file:
 *    /home/twl/Kicad-dev/kicad-git-dev/common/drc_rules.keywords
 */
class DRC_RULES_LEXER : public DSNLEXER
{
    /// Auto generated lexer keywords table and length:
    static const KEYWORD  keywords[];
    static const unsigned keyword_count;

public:
    /**
     * Constructor ( const std::string&, const wxString& )
     * @param aSExpression is (utf8) text possibly from the clipboard that you want to parse.
     * @param aSource is a description of the origin of @a aSExpression, such as a filename.
     *   If left empty, then _("clipboard") is used.
     */
    DRC_RULES_LEXER( const std::string& aSExpression, const wxString& aSource = wxEmptyString ) :
        DSNLEXER( keywords, keyword_count, aSExpression, aSource )
    {
    }

    /**
     * Constructor ( FILE* )
     * takes @a aFile already opened for reading and @a aFilename as parameters.
     * The opened file is assumed to be positioned at the beginning of the file
     * for purposes of accurate line number reporting in error messages.  The
     * FILE is closed by this instance when its destructor is called.
     * @param aFile is a FILE already opened for reading.
     * @param aFilename is the name of the opened file, needed for error reporting.
     */
    DRC_RULES_LEXER( FILE* aFile, const wxString& aFilename ) :
        DSNLEXER( keywords, keyword_count, aFile, aFilename )
    {
    }

    /**
     * Constructor ( LINE_READER* )
     * initializes a lexer and prepares to read from @a aLineReader which
     * is assumed ready, and may be in use by other DSNLEXERs also.  No ownership
     * is taken of @a aLineReader. This enables it to be used by other lexers also.
     * The transition between grammars in such a case, must happen on a text
     * line boundary, not within the same line of text.
     *
     * @param aLineReader is any subclassed instance of LINE_READER, such as
     *  STRING_LINE_READER or FILE_LINE_READER.  No ownership is taken of aLineReader.
     */
    DRC_RULES_LEXER( LINE_READER* aLineReader ) :
        DSNLEXER( keywords, keyword_count, aLineReader )
    {
    }

    /**
     * Function TokenName
     * returns the name of the token in ASCII form.
     */
    static const char* TokenName( DRCRULE_T::T aTok );

    /**
     * Function NextTok
     * returns the next token found in the input file or T_EOF when reaching
     * the end of file.  Users should wrap this function to return an enum
     * to aid in grammar debugging while running under a debugger, but leave
     * this lower level function returning an int (so the enum does not collide
     * with another usage).
     * @return DRCRULE_T::T - the type of token found next.
     * @throw IO_ERROR - only if the LINE_READER throws it.
     */
    DRCRULE_T::T NextTok()
    {
        return (DRCRULE_T::T) DSNLEXER::NextTok();
    }

    /**
     * Function NeedSYMBOL
     * calls NextTok() and then verifies that the token read in
     * satisfies bool IsSymbol().
     * If not, an IO_ERROR is thrown.
     * @return int - the actual token read in.
     * @throw IO_ERROR, if the next token does not satisfy IsSymbol()
     */
    DRCRULE_T::T NeedSYMBOL()
    {
        return (DRCRULE_T::T) DSNLEXER::NeedSYMBOL();
    }

    /**
     * Function NeedSYMBOLorNUMBER
     * calls NextTok() and then verifies that the token read in
     * satisfies bool IsSymbol() or tok==T_NUMBER.
     * If not, an IO_ERROR is thrown.
     * @return int - the actual token read in.
     * @throw IO_ERROR, if the next token does not satisfy the above test
     */
    DRCRULE_T::T NeedSYMBOLorNUMBER()
    {
        return (DRCRULE_T::T) DSNLEXER::NeedSYMBOLorNUMBER();
    }

    /**
     * Function CurTok
     * returns whatever NextTok() returned the last time it was called.
     */
    DRCRULE_T::T CurTok()
    {
        return (DRCRULE_T::T) DSNLEXER::CurTok();
    }

    /**
     * Function PrevTok
     * returns whatever NextTok() returned the 2nd to last time it was called.
     */
    DRCRULE_T::T PrevTok()
    {
        return (DRCRULE_T::T) DSNLEXER::PrevTok();
    }
};

// example usage

/**
 * Class _PARSER
 * holds data and functions pertinent to parsing a S-expression file .
 *
class DRC_RULES_PARSER : public DRC_RULES_LEXER
{

};
*/

#endif   // DRC_RULES_LEXER_H_