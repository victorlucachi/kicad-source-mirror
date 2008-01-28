/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
 * Copyright (C) 2007-2008 SoftPLC Corporation, Dick Hollenbeck <dick@softplc.com>
 * Copyright (C) 2007 Kicad Developers, see change_log.txt for contributors.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html 
 * or you may search the http://www.gnu.org website for the version 2 license, 
 * or you may write to the Free Software Foundation, Inc., 
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef SPECCTRA_H_
#define SPECCTRA_H_

 
//  see http://www.boost.org/libs/ptr_container/doc/ptr_sequence_adapter.html
#include <boost/ptr_container/ptr_vector.hpp>

#include "fctsys.h"
#include "dsn.h"



/**
    This source file implements export and import capabilities to the 
    specctra dsn file format.  The grammar for that file format is documented
    fairly well.  There are classes for each major type of descriptor in the
    spec.
    
    Since there are so many classes in here, it may be helpful to generate 
    the Doxygen directory:
    
    $ cd <kicadSourceRoot>
    $ doxygen
    
    Then you can view the html documentation in the <kicadSourceRoot>/doxygen
    directory.  The main class in this file is SPECCTRA_DB and its main
    functions are LoadPCB(), LoadSESSION(), and ExportPCB().
    
    Wide use is made of boost::ptr_vector<> and std::vector<> template classes.
    If the contained object is small, then std::vector tends to be used.
    If the contained object is large, variable size, or would require writing
    an assignment operator() or copy constructore, then boost::ptr_vector
    cannot be beat.
*/    
namespace DSN {
    
class SPECCTRA_DB;


/**
 * Class OUTPUTFORMATTER
 * is an interface (abstract class) used to output ASCII text.  The destination
 * of the ASCII text is up to the implementer.
 */
class OUTPUTFORMATTER
{

// When used on a C++ function, we must account for the "this" pointer,
// so increase the STRING-INDEX and FIRST-TO_CHECK by one.
// See http://docs.freebsd.org/info/gcc/gcc.info.Function_Attributes.html
// Then to get format checking during the compile, compile with -Wall or -Wformat
#define PRINTF_FUNC       __attribute__ ((format (printf, 3, 4)))
    
public:
    
    /**
     * Function print
     * formats and writes text to the output stream.
     *
     * @param nestLevel The multiple of spaces to preceed the output with. 
     * @param fmt A printf() style format string.
     * @param ... a variable list of parameters that will get blended into 
     *  the output under control of the format string.
     * @return int - the number of characters output.
     * @throw IOError, if there is a problem outputting, such as a full disk.
     */
    virtual int PRINTF_FUNC Print( int nestLevel, const char* fmt, ... ) throw( IOError ) = 0;

    /**
     * Function GetQuoteChar
     * returns the quote character as a single character string for a given 
     * input wrapee string.  Often the return value is "" the null string if
     * there are no delimiters in the input string.  If you want the quote_char
     * to be assuredly not "", then pass in "(" as the wrappee.
     * @param wrapee A string that might need wrapping on each end.
     * @return const char* - the quote_char as a single character string, or ""
     *   if the wrapee does not need to be wrapped.
     */
    virtual const char* GetQuoteChar( const char* wrapee ) = 0;
};
 

struct POINT
{
    double  x;
    double  y;
    
    POINT() { x=0.0; y=0.0; }
    
    POINT( double aX, double aY ) :
        x(aX), y(aY)
    {
    }
    
    bool operator==( const POINT& other ) const
    {
        return x==other.x && y==other.y;
    }
    
    bool operator!=( const POINT& other ) const
    {
        return !( *this == other );
    }
    
    POINT& operator+=( const POINT& other )
    {
        x += other.x;
        y += other.y;
        return *this;
    }
    
    POINT& operator=( const POINT& other )
    {
        x = other.x;
        if( x == -0.0 )     // correct -0.0 so output looks nice.
            x = 0.0;
        y = other.y;
        if( y == -0.0 )
            y = 0.0;
        return *this;        
    }

    
    /**
     * Function Format
     * writes this object as ASCII out to an OUTPUTFORMATTER according to the 
     * SPECCTRA DSN format.
     * @param out The formatter to write to.
     * @param nestLevel A multiple of the number of spaces to preceed the output with.
     * @throw IOError if a system error writing the output, such as a full disk.
     */
    void Format( OUTPUTFORMATTER* out, int nestLevel ) const throw( IOError )
    {
        out->Print( nestLevel, " %.6g %.6g", x, y ); 
    }
};

typedef std::vector<std::string>    STRINGS;
typedef std::vector<POINT>          POINTS;

struct PROPERTY
{
    std::string name;
    std::string value;

    /**
     * Function Format
     * writes this object as ASCII out to an OUTPUTFORMATTER according to the 
     * SPECCTRA DSN format.
     * @param out The formatter to write to.
     * @param nestLevel A multiple of the number of spaces to preceed the output with.
     * @throw IOError if a system error writing the output, such as a full disk.
     */
    void Format( OUTPUTFORMATTER* out, int nestLevel ) const throw( IOError )
    {
        const char* quoteName  = out->GetQuoteChar( name.c_str() );
        const char* quoteValue = out->GetQuoteChar( value.c_str() );
        
        out->Print( nestLevel, "(%s%s%s %s%s%s)\n", 
                   quoteName,  name.c_str(), quoteName,
                   quoteValue, value.c_str(), quoteValue );
    }
};
typedef std::vector<PROPERTY>       PROPERTIES;
    

/**
 * Class ELEM
 * is a base class for any DSN element class. 
 * See class ELEM_HOLDER also.
 */ 
class ELEM
{
    friend class SPECCTRA_DB;
    
protected:    
    DSN_T           type;
    ELEM*           parent;
    
public:

    ELEM( DSN_T aType, ELEM* aParent = 0 );
    
    virtual ~ELEM();
    
    DSN_T   Type() { return type; }

    
    /**
     * Function GetUnits
     * returns the units for this section.  Derived classes may override this
     * to check for section specific overrides.
     * @return DSN_T - one of the allowed values to &lt;unit_descriptor&gt;
     */
    virtual DSN_T   GetUnits()
    {
        if( parent )
            return parent->GetUnits();
        
        return T_inch;
    }

    
    /**
     * Function Format
     * writes this object as ASCII out to an OUTPUTFORMATTER according to the 
     * SPECCTRA DSN format.
     * @param out The formatter to write to.
     * @param nestLevel A multiple of the number of spaces to preceed the output with.
     * @throw IOError if a system error writing the output, such as a full disk.
     */
    virtual void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError );

    
    /**
     * Function FormatContents
     * writes the contents as ASCII out to an OUTPUTFORMATTER according to the 
     * SPECCTRA DSN format.  This is the same as Format() except that the outer
     * wrapper is not included.
     * @param out The formatter to write to.
     * @param nestLevel A multiple of the number of spaces to preceed the output with.
     * @throw IOError if a system error writing the output, such as a full disk.
     */
    virtual void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        // overridden in ELEM_HOLDER
    }
    
    void SetParent( ELEM* aParent )
    {
        parent = aParent;
    }
};


/**
 * Class ELEM_HOLDER
 * is a holder for any DSN class.  It can contain other
 * class instances, including classes derived from this class.
 */ 
class ELEM_HOLDER : public ELEM
{
    friend class SPECCTRA_DB;
    
    typedef boost::ptr_vector<ELEM> ELEM_ARRAY;
    
    ELEM_ARRAY      kids;      ///< ELEM pointers

public:    
    
    ELEM_HOLDER( DSN_T aType, ELEM* aParent = 0 ) :
        ELEM( aType, aParent )
    {
    }

    virtual void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError );
    
    
    //-----< list operations >--------------------------------------------

    /**
     * Function FindElem
     * finds a particular instance number of a given type of ELEM.
     * @param aType The type of ELEM to find
     * @param instanceNum The instance number of to find: 0 for first, 1 for second, etc.
     * @return int - The index into the kids array or -1 if not found.
     */
    int FindElem( DSN_T aType, int instanceNum = 0 );

    
    /**
     * Function Length
     * returns the number of ELEMs in this ELEM.
     * @return int - the count of children
     */
    int     Length() const
    {
        return kids.size();
    }
    
    void    Append( ELEM* aElem )
    {
        kids.push_back( aElem );
    }

    ELEM*   Replace( int aIndex, ELEM* aElem )
    {
        ELEM_ARRAY::auto_type ret = kids.replace( aIndex, aElem );
        return ret.release();
    }

    ELEM*   Remove( int aIndex )
    {
        ELEM_ARRAY::auto_type ret = kids.release( kids.begin()+aIndex );
        return ret.release();
    }

    void    Insert( int aIndex, ELEM* aElem )
    {
        kids.insert( kids.begin()+aIndex, aElem );
    }
    
    ELEM*   At( int aIndex )
    {
        // we have varying sized objects and are using polymorphism, so we
        // must return a pointer not a reference.
        return &kids[aIndex];
    }
    
    ELEM* operator[]( int aIndex )
    {
        return At( aIndex );
    }
    
    void    Delete( int aIndex )
    {
        kids.erase( kids.begin()+aIndex );
    }
};


/**
 * Class PARSER
 * is simply a configuration record per the SPECCTRA DSN file spec.
 * It is not actually a parser, but rather corresponds to &lt;parser_descriptor&gt;
 */
class PARSER : public ELEM
{
    friend class SPECCTRA_DB;

    char        string_quote;
    bool        space_in_quoted_tokens;
    bool        case_sensitive;
    bool        wires_include_testpoint;
    bool        routes_include_testpoint;
    bool        routes_include_guides;
    bool        routes_include_image_conductor;
    bool        via_rotate_first;
    bool        generated_by_freeroute;
    
    std::string const_id1;
    std::string const_id2;
    
    std::string host_cad;
    std::string host_version;

    
public:

    PARSER( ELEM* aParent );

    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError );
};


/**
 * Class UNIT_RES
 * is a holder for either a T_unit or T_resolution object which are usually
 * mutually exclusive in the dsn grammar, except within the T_pcb level.
 */
class UNIT_RES : public ELEM
{
    friend class SPECCTRA_DB;
    
    DSN_T       units;
    int         value;

public:
    UNIT_RES( ELEM* aParent, DSN_T aType ) :
        ELEM( aType, aParent )
    {
        units = T_inch;
        value = 2540000;
    }

    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        if( type == T_unit )
            out->Print( nestLevel, "(%s %s)\n", LEXER::GetTokenText( Type() ), 
                       LEXER::GetTokenText(units) ); 

        else    // T_resolution            
            out->Print( nestLevel, "(%s %s %d)\n", LEXER::GetTokenText( Type() ), 
                       LEXER::GetTokenText(units), value ); 
    }
    
    DSN_T   GetUnits()
    {
        return units;
    }
};


class RECTANGLE : public ELEM
{
    friend class SPECCTRA_DB;
    
    std::string     layer_id;

    POINT           point0;
    POINT           point1;
    
public:

    RECTANGLE( ELEM* aParent ) :
        ELEM( T_rect, aParent )
    {
    }

    void SetLayerId( const char* aLayerId )
    {
        layer_id = aLayerId;
    }
    
    void SetCorners( const POINT& aPoint0, const POINT& aPoint1 )
    {
        point0 = aPoint0;
        point1 = aPoint1;
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* newline = nestLevel ? "\n" : "";
        
        const char* quote = out->GetQuoteChar( layer_id.c_str() );
        
        out->Print( nestLevel, "(%s %s%s%s %.6g %.6g %.6g %.6g)%s", 
                   LEXER::GetTokenText( Type() ),
                   quote, layer_id.c_str(), quote,
                   point0.x, point0.y,
                   point1.x, point1.y,
                   newline );
    }
};


/**
 * Class RULE
 * corresponds to the &lt;rule_descriptor&gt; in the specctra dsn spec.
 */
class RULE : public ELEM
{
    friend class SPECCTRA_DB;

    STRINGS     rules;      ///< rules are saved in std::string form.

public:

    RULE( ELEM* aParent, DSN_T aType ) :
        ELEM( aType, aParent )
    {
    }

    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        out->Print( nestLevel, "(%s", LEXER::GetTokenText( Type() ) );
        
        bool singleLine;
        
        if( rules.size() == 1 )
        {
            singleLine = true;
            out->Print( 0, " %s)", rules.begin()->c_str() );
        }
        
        else
        {
            out->Print( 0, "\n" );
            singleLine = false;
            for( STRINGS::const_iterator i = rules.begin();  i!=rules.end(); ++i )
                out->Print( nestLevel+1, "%s\n", i->c_str() );
            out->Print( nestLevel, ")" );
        }
        
        if( nestLevel || !singleLine )
            out->Print( 0, "\n" );
    }
};


#if 0
class PLACE_RULE : public RULE
{
    friend class SPECCTRA_DB;
    
    DSN_T       object_type;
    DSN_T       image_type;
    
    /*  T_spacing, T_permit_orient, T_permit_side & T_opposite_side are
        all stored in the kids list.
    */
    
public:

    PLACE_RULE( ELEM* aParent ) :
        RULE( aParent, T_place_rule )
    {
        object_type = T_NONE;
        image_type  = T_NONE;
    }

    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        if( object_type != T_NONE )
        {
            if( object_type == T_pcb )
                out->Print( nestLevel, "(object_type %s", 
                                         LEXER::GetTokenText( object_type ) );
            else
                out->Print( nestLevel, "(object_type image_set %s",
                                         LEXER::GetTokenText( object_type ) );
            
            if( image_type != T_NONE )
                out->Print( 0, " (image_type %s)", LEXER::GetTokenText( image_type ) );
            out->Print( 0, ")\n" ); 
        }
        
        RULE::FormatContents( out, nestLevel );
    }
};
#endif


class LAYER_RULE : public ELEM
{
    friend class SPECCTRA_DB;
    
    STRINGS         layer_ids;
    RULE*           rule;
    
public:

    LAYER_RULE( ELEM* aParent ) :
        ELEM( T_layer_rule, aParent )
    {
        rule = 0;
    }
    ~LAYER_RULE()
    {
        delete rule;
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        out->Print( nestLevel, "(%s", LEXER::GetTokenText( Type() ) );
        
        for( STRINGS::const_iterator i=layer_ids.begin();  i!=layer_ids.end();  ++i )
        {
            const char* quote = out->GetQuoteChar( i->c_str() );
            out->Print( 0, " %s%s%s", quote, i->c_str(), quote );
        }
        out->Print( 0 , "\n" );
        
        if( rule )
            rule->Format( out, nestLevel+1 );
        
        out->Print( nestLevel, ")\n" );
    }
};

typedef boost::ptr_vector<LAYER_RULE>   LAYER_RULES;


/**
 * Class PATH
 * supports both the &lt;path_descriptor&gt; and the &lt;polygon_descriptor&gt; per
 * the specctra dsn spec.
 */
class PATH : public ELEM
{
    friend class SPECCTRA_DB;
    
    std::string     layer_id;
    double          aperture_width;

    POINTS          points;                   
    DSN_T           aperture_type;
    
public:

    PATH( ELEM* aParent, DSN_T aType = T_path ) :
        ELEM( aType, aParent )
    {
        aperture_width = 0.0;
        aperture_type  = T_round;
    }
    
    void AppendPoint( const POINT& aPoint )
    {
        points.push_back( aPoint );
    }
    
    void SetLayerId( const char* aLayerId )
    {
        layer_id = aLayerId;
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* newline = nestLevel ? "\n" : "";
        
        const char* quote = out->GetQuoteChar( layer_id.c_str() );
        
        const int RIGHTMARGIN = 80;        
        int perLine = out->Print( nestLevel, "(%s %s%s%s %.6g", 
                               LEXER::GetTokenText( Type() ),
                               quote, layer_id.c_str(), quote, 
                               aperture_width );

        for( unsigned i=0;  i<points.size();  ++i )
        {
            if( perLine > RIGHTMARGIN )
            {
                out->Print( 0, "\n" );
                perLine = out->Print( nestLevel+1, "%s", "" );
                newline = "\n";
            }
            else
                perLine += out->Print( 0, "  " );
            
            perLine += out->Print( 0, "%.6g %.6g", points[i].x, points[i].y );
        }

        if( aperture_type == T_square )
        {
            out->Print( 0, "\n" );
            out->Print( nestLevel+1, "(aperture_type square))\n" );
        }
        else
            out->Print( 0, ")%s", newline ); 
    }
};
typedef boost::ptr_vector<PATH> PATHS;


class BOUNDARY : public ELEM
{
    friend class SPECCTRA_DB;
 
    // only one or the other of these two is used, not both
    PATHS           paths;
    RECTANGLE*      rectangle;
    
    
public:

    BOUNDARY( ELEM* aParent, DSN_T aType = T_boundary ) :
        ELEM( aType, aParent )
    {
        rectangle = 0;
    }
    
    ~BOUNDARY()
    {
        delete rectangle;
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        out->Print( nestLevel, "(%s\n", LEXER::GetTokenText( Type() ) );

        if( rectangle )
            rectangle->Format( out, nestLevel+1 );
        else
        {
            for( PATHS::iterator i=paths.begin();  i!=paths.end();  ++i )
                i->Format( out, nestLevel+1 );
        }
        
        out->Print( nestLevel, ")\n" ); 
    }
};


class CIRCLE : public ELEM
{
    friend class SPECCTRA_DB;
    
    std::string layer_id;
    
    double      diameter;
    POINT       vertex;     // POINT's constructor sets to (0,0)
    
public:    
    CIRCLE( ELEM* aParent ) :
        ELEM( T_circle, aParent )
    {
        diameter = 0.0;
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* newline = nestLevel ? "\n" : "";
        
        const char* quote = out->GetQuoteChar( layer_id.c_str() );
        out->Print( nestLevel, "(%s %s%s%s %.6g", LEXER::GetTokenText( Type() ),
                                quote, layer_id.c_str(), quote,
                                diameter );

        if( vertex.x!=0.0 || vertex.y!=0.0 )
            out->Print( 0, " %.6g %.6g)%s", vertex.x, vertex.y, newline );
        else
            out->Print( 0, ")%s", newline );
    }
    
    void SetLayerId( const char* aLayerId )
    {
        layer_id = aLayerId;
    }
    
    void SetDiameter( double aDiameter )
    {
        diameter = aDiameter;
    }
    
    void SetVertex( const POINT& aVertex )
    {
        vertex = aVertex;
    }
};


class QARC : public ELEM
{
    friend class SPECCTRA_DB;
    
    std::string layer_id;
    double      aperture_width;
    POINT       vertex[3];

public:    
    QARC( ELEM* aParent ) :
        ELEM( T_qarc, aParent )
    {
        aperture_width = 0.0;
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* newline = nestLevel ? "\n" : "";
        
        const char* quote = out->GetQuoteChar( layer_id.c_str() );
        out->Print( nestLevel, "(%s %s%s%s %.6g", LEXER::GetTokenText( Type() ) ,
                                 quote, layer_id.c_str(), quote,
                                 aperture_width);
        
        for( int i=0;  i<3;  ++i )
            out->Print( 0, "  %.6g %.6g",  vertex[i].x, vertex[i].y );
        
        out->Print( 0, ")%s", newline );
    }
    
    void SetLayerId( const char* aLayerId )
    {
        layer_id = aLayerId;
    }
    void SetStart( const POINT& aStart )
    {
        vertex[0] = aStart;    
    }
    void SetEnd( const POINT& aEnd )
    {
        vertex[1] = aEnd;
    }
    void SetCenter( const POINT& aCenter )
    {
        vertex[2] = aCenter;
    }
};


class WINDOW : public ELEM
{
    friend class SPECCTRA_DB;

    //----- only one of these is used, like a union -----
    PATH*           path;           ///< used for both path and polygon
    RECTANGLE*      rectangle;
    CIRCLE*         circle;
    QARC*           qarc;
    //---------------------------------------------------

public:
    
    WINDOW( ELEM* aParent ) :
        ELEM( T_window, aParent )
    {
        path = 0;
        rectangle = 0;
        circle = 0;
        qarc = 0;
    }
    
    ~WINDOW()
    {
        delete path;
        delete rectangle;
        delete circle;
        delete qarc;
    }
    
    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        // these are mutually exclusive
        if( rectangle )
            rectangle->Format( out, nestLevel );
        
        else if( path )
            path->Format( out, nestLevel );
        
        else if( circle )
            circle->Format( out, nestLevel );

        else if( qarc )
            qarc->Format( out, nestLevel );
    }
};

typedef boost::ptr_vector<WINDOW>   WINDOWS;


/**
 * Class KEEPOUT
 * is used for <keepout_descriptor> and <plane_descriptor>.
 */
class KEEPOUT : public ELEM
{
    friend class SPECCTRA_DB;

protected:    
    std::string     name;
    int             sequence_number;
    RULE*           rules;
    RULE*           place_rules;
    
    WINDOWS         windows;
    
    //----- only one of these is used, like a union -----
    PATH*           path;           ///< used for both path and polygon
    RECTANGLE*      rectangle;
    CIRCLE*         circle;
    QARC*           qarc;
    //---------------------------------------------------
    
public:

    /**
     * Constructor KEEPOUT
     * requires a DSN_T because this class is used for T_place_keepout, T_via_keepout,
     * T_wire_keepout, T_bend_keepout, and T_elongate_keepout as well as T_keepout.
     */
    KEEPOUT( ELEM* aParent, DSN_T aType ) :
        ELEM( aType, aParent )
    {
        rules = 0;
        place_rules = 0;

        path = 0;
        rectangle = 0;
        circle = 0;
        qarc = 0;
        
        sequence_number = -1;
    }
    
    ~KEEPOUT()
    {
        delete rules;
        delete place_rules;

        delete path;
        delete rectangle;
        delete circle;
        delete qarc;
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        out->Print( nestLevel, "(%s\n", LEXER::GetTokenText( Type() ) );

        if( name.size() )
        {
            const char* quote = out->GetQuoteChar( name.c_str() );
            out->Print( nestLevel+1, "%s%s%s\n", quote, name.c_str(), quote );
        }

        if( sequence_number != -1 )
            out->Print( nestLevel+1, "(sequence_number %d)\n", sequence_number );
        
        // these are mutually exclusive
        if( rectangle )
            rectangle->Format( out, nestLevel+1 );
        
        else if( path )
            path->Format( out, nestLevel+1 );
        
        else if( circle )
            circle->Format( out, nestLevel+1 );

        else if( qarc )
            qarc->Format( out, nestLevel+1 );

        if( rules )
            rules->Format( out, nestLevel+1 );
        
        if( place_rules )
            place_rules->Format( out, nestLevel+1 );

        for( WINDOWS::iterator i=windows.begin();  i!=windows.end();  ++i )
            i->Format( out, nestLevel+1 );
        
        out->Print( nestLevel, ")\n" ); 
    }
};
typedef boost::ptr_vector<KEEPOUT>  KEEPOUTS;


/**
 * Class VIA
 * corresponds to the &lt;via_descriptor&gt; in the specctra dsn spec.
 */
class VIA : public ELEM
{
    friend class SPECCTRA_DB;

    STRINGS     padstacks;
    STRINGS     spares;

public:

    VIA( ELEM* aParent ) :
        ELEM( T_via, aParent )
    {
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const int RIGHTMARGIN = 80;
        int perLine = out->Print( nestLevel, "(%s", LEXER::GetTokenText( Type() ) );
        
        for( STRINGS::iterator i=padstacks.begin();  i!=padstacks.end();  ++i )
        {
            if( perLine > RIGHTMARGIN )
            {
                out->Print( 0, "\n" );
                perLine = out->Print( nestLevel+1, "%s", "");
            }

            const char* quote = out->GetQuoteChar( i->c_str() );
            perLine += out->Print( 0, " %s%s%s", quote, i->c_str(), quote );
        }
        
        if( spares.size() )
        {
            out->Print( 0, "\n" );
            
            perLine = out->Print( nestLevel+1, "(spare" );

            for( STRINGS::iterator i=spares.begin();  i!=spares.end();  ++i )
            {
                if( perLine > RIGHTMARGIN )
                {
                    out->Print( 0, "\n" );
                    perLine = out->Print( nestLevel+2, "%s", "");
                }
                const char* quote = out->GetQuoteChar( i->c_str() );
                perLine += out->Print( 0, " %s%s%s", quote, i->c_str(), quote );
            }
            
            out->Print( 0, ")" );
        }
        
        out->Print( 0, ")\n" );
    }
};


class CLASSES : public ELEM
{
    friend class SPECCTRA_DB;

    STRINGS         class_ids;
    
public:
    CLASSES( ELEM* aParent ) :
        ELEM( T_classes, aParent )
    {
    }
    
    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        for( STRINGS::iterator i=class_ids.begin();  i!=class_ids.end();  ++i )
        {
            const char* quote = out->GetQuoteChar( i->c_str() );
            out->Print( nestLevel, "%s%s%s\n", quote, i->c_str(), quote );
        }
    }
};


class CLASS_CLASS : public ELEM_HOLDER
{
    friend class SPECCTRA_DB;
    
    CLASSES*        classes;
    
    /*  rule | layer_rule are put into the kids container.
    */
    

public:
    
    /**
     * Constructor CLASS_CLASS
     * @param aType May be either T_class_class or T_region_class_class
     */
    CLASS_CLASS( ELEM* aParent, DSN_T aType ) :
        ELEM_HOLDER( aType, aParent )
    {
        classes = 0;
    }
    
    ~CLASS_CLASS()
    {
        delete classes;
    }

    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        if( classes )
            classes->Format( out, nestLevel );

        // format the kids
        ELEM_HOLDER::FormatContents( out, nestLevel );
    }
};


class CONTROL : public ELEM_HOLDER
{
    friend class SPECCTRA_DB;

    bool    via_at_smd;
    bool    via_at_smd_grid_on;
    
public:
    CONTROL( ELEM* aParent ) :
        ELEM_HOLDER( T_control, aParent )
    {
        via_at_smd = false;
        via_at_smd_grid_on = false;
    }
   
    ~CONTROL()
    {
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        out->Print( nestLevel, "(%s\n", LEXER::GetTokenText( Type() ) );

        //if( via_at_smd )
        {
            out->Print( nestLevel+1, "(via_at_smd %s", via_at_smd ? "on" : "off" );
            if( via_at_smd_grid_on )
                out->Print( 0, " grid %s", via_at_smd_grid_on ? "on" : "off" );
            
            out->Print( 0, ")\n" );
        }

        for( int i=0;  i<Length();  ++i )
        {
            At(i)->Format( out, nestLevel+1 );
        }
        
        out->Print( nestLevel, ")\n" ); 
    }
};


class LAYER : public ELEM
{
    friend class SPECCTRA_DB;
    
    std::string name;
    DSN_T       layer_type; ///< one of: T_signal, T_power, T_mixed, T_jumper
    int         direction;
    int         cost;       ///< [forbidden | high | medium | low | free | <positive_integer > | -1]
    int         cost_type;  ///< T_length | T_way 
    RULE*       rules;
    STRINGS     use_net;
    
    PROPERTIES  properties;
    
public:

    LAYER( ELEM* aParent ) :
        ELEM( T_layer, aParent )
    {
        layer_type = T_signal;
        direction  = -1;
        cost       = -1;
        cost_type  = -1;
        
        rules = 0;
    }
    
    ~LAYER()
    {
        delete rules;
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* quote = out->GetQuoteChar( name.c_str() );
        
        out->Print( nestLevel, "(%s %s%s%s\n", LEXER::GetTokenText( Type() ),
                       quote, name.c_str(), quote );

        out->Print( nestLevel+1, "(type %s)\n", LEXER::GetTokenText( layer_type ) );

        if( properties.size() )
        {
            out->Print( nestLevel+1, "(property \n" );
            
            for( PROPERTIES::iterator i = properties.begin();  i != properties.end();  ++i )
            {
                i->Format( out, nestLevel+2 );
            }
            out->Print( nestLevel+1, ")\n" );
        }
        
        if( direction != -1 )
            out->Print( nestLevel+1, "(direction %s)\n", 
                       LEXER::GetTokenText( (DSN_T)direction ) );

        if( rules )
            rules->Format( out, nestLevel+1 );
        
        if( cost != -1 )
        {
            if( cost < 0 )
                out->Print( nestLevel+1, "(cost %d", -cost );   // positive integer, stored as negative
            else
                out->Print( nestLevel+1, "(cost %s", LEXER::GetTokenText( (DSN_T)cost ) );
            
            if( cost_type != -1 )
                out->Print( 0, " (type %s)", LEXER::GetTokenText( (DSN_T)cost_type ) );
            
            out->Print( 0, ")\n" );
        }

        if( use_net.size() )
        {
            out->Print( nestLevel+1, "(use_net" );
            for( STRINGS::const_iterator i = use_net.begin();  i!=use_net.end(); ++i )
            {
                const char* quote = out->GetQuoteChar( i->c_str() );
                out->Print( 0, " %s%s%s",  quote, i->c_str(), quote );
            }
            out->Print( 0, ")\n" );
        }
        
        out->Print( nestLevel, ")\n" ); 
    }
};


class LAYER_PAIR : public ELEM
{
    friend class SPECCTRA_DB;

    std::string     layer_id0;
    std::string     layer_id1;

    double          layer_weight;    
    
public:
    LAYER_PAIR( ELEM* aParent ) :
        ELEM( T_layer_pair, aParent )
    {
        layer_weight = 0.0;
    }

    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* quote0 = out->GetQuoteChar( layer_id0.c_str() );
        const char* quote1 = out->GetQuoteChar( layer_id1.c_str() );
        
        out->Print( nestLevel, "(%s %s%s%s %s%s%s %.6g)\n", LEXER::GetTokenText( Type() ),
               quote0, layer_id0.c_str(), quote0,
               quote1, layer_id1.c_str(), quote1,
               layer_weight );
    }
};


class LAYER_NOISE_WEIGHT : public ELEM
{
    friend class SPECCTRA_DB;
    
    typedef boost::ptr_vector<LAYER_PAIR>  LAYER_PAIRS;
    LAYER_PAIRS     layer_pairs;
    
public:

    LAYER_NOISE_WEIGHT( ELEM* aParent ) :
        ELEM( T_layer_noise_weight, aParent )
    {
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        out->Print( nestLevel, "(%s\n", LEXER::GetTokenText( Type() ) );
        
        for( LAYER_PAIRS::iterator i=layer_pairs.begin(); i!=layer_pairs.end();  ++i )
            i->Format( out, nestLevel+1 );
        
        out->Print( nestLevel, ")\n" );
    }
};


/**
 * Class COPPER_PLANE
 * corresponds to a &lt;plane_descriptor&gt; in the specctra dsn spec.
 */
class COPPER_PLANE : public KEEPOUT
{
    friend class SPECCTRA_DB;

public:    
    COPPER_PLANE( ELEM* aParent ) :
        KEEPOUT( aParent, T_plane )
    {}
};
typedef boost::ptr_vector<COPPER_PLANE>    COPPER_PLANES;


/**
 * Class TOKPROP
 * is a container for a single property whose value is another DSN_T token.
 * The name of the property is obtained from the DSN_T Type().
 */
class TOKPROP : public ELEM
{
    friend class SPECCTRA_DB;

    DSN_T       value;

public:

    TOKPROP( ELEM* aParent, DSN_T aType ) :
        ELEM( aType, aParent )
    {
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        out->Print( nestLevel, "(%s %s)\n", LEXER::GetTokenText( Type() ),
                   LEXER::GetTokenText( value ) );
    }
};


/**
 * Class STRINGPROP
 * is a container for a single property whose value is a string.
 * The name of the property is obtained from the DSN_T.
 */
class STRINGPROP : public ELEM
{
    friend class SPECCTRA_DB;

    std::string     value;

public:

    STRINGPROP( ELEM* aParent, DSN_T aType ) :
        ELEM( aType, aParent )
    {
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* quote = out->GetQuoteChar( value.c_str() );
        
        out->Print( nestLevel, "(%s %s%s%s)\n", LEXER::GetTokenText( Type() ),
                                quote, value.c_str(), quote );
    }
};


class REGION : public ELEM_HOLDER
{
    friend class SPECCTRA_DB;

    std::string     region_id;

    //-----<mutually exclusive>--------------------------------------
    RECTANGLE*      rectangle;
    PATH*           polygon;
    //-----</mutually exclusive>-------------------------------------

    /* region_net | region_class | region_class_class are all mutually
       exclusive and are put into the kids container.
    */
    
    RULE*           rules;    

public:
    REGION( ELEM* aParent ) :
        ELEM_HOLDER( T_region, aParent )
    {
        rectangle = 0;
        polygon = 0;
        rules = 0;
    }

    ~REGION()
    {
        delete rectangle;
        delete polygon;
        delete rules;
    }
    
    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        if( region_id.size() )
        {
            const char* quote = out->GetQuoteChar( region_id.c_str() );
            out->Print( nestLevel, "%s%s%s\n", quote, region_id.c_str(), quote );
        }
        
        if( rectangle )
            rectangle->Format( out, nestLevel );
        
        if( polygon )
            polygon->Format( out, nestLevel );

        ELEM_HOLDER::FormatContents( out, nestLevel );
        
        if( rules )
            rules->Format( out, nestLevel );
    }
};


class GRID : public ELEM
{
    friend class SPECCTRA_DB;

    DSN_T       grid_type;      ///< T_via | T_wire | T_via_keepout | T_place | T_snap

    double      dimension;
    
    DSN_T       direction;      ///< T_x | T_y | -1 for both
    
    double      offset;
    
    DSN_T       image_type;
    
public:
    
    GRID( ELEM* aParent ) :
        ELEM( T_grid, aParent )
    {
        grid_type = T_via;
        direction = T_NONE;
        dimension = 0.0;
        offset    = 0.0;
        image_type= T_NONE;
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        out->Print( nestLevel, "(%s %s %.6g",
                   LEXER::GetTokenText( Type() ),
                   LEXER::GetTokenText( grid_type ), dimension );
     
        if( grid_type == T_place )
        {
            if( image_type==T_smd || image_type==T_pin )
                out->Print( 0, " (image_type %s)", LEXER::GetTokenText( image_type ) );
        }
        else
        {
            if( direction==T_x || direction==T_y )
                out->Print( 0, " (direction %s)", LEXER::GetTokenText( direction ) );
        }
        
        if( offset != 0.0 )
            out->Print( 0, " (offset %.6g)", offset );
        
        out->Print( 0, ")\n");
    }
};


class STRUCTURE : public ELEM_HOLDER
{
    friend class SPECCTRA_DB;
    
    UNIT_RES*   unit;
    
    typedef boost::ptr_vector<LAYER>    LAYERS;
    LAYERS      layers;

    LAYER_NOISE_WEIGHT*  layer_noise_weight;
    
    BOUNDARY*   boundary;
    BOUNDARY*   place_boundary;
    VIA*        via;
    CONTROL*    control;
    RULE*       rules;
    
    KEEPOUTS    keepouts;

    COPPER_PLANES      planes;

    typedef boost::ptr_vector<REGION>   REGIONS;
    REGIONS     regions;
    
    RULE*       place_rules;
    
    typedef boost::ptr_vector<GRID>     GRIDS;
    GRIDS       grids;
    
public:

    STRUCTURE( ELEM* aParent ) :
        ELEM_HOLDER( T_structure, aParent )
    {
        unit = 0;
        layer_noise_weight = 0;
        boundary = 0;
        place_boundary = 0;
        via = 0;
        control = 0;
        rules = 0;
        place_rules = 0;
    }
    
    ~STRUCTURE()
    {
        delete unit;
        delete layer_noise_weight;
        delete boundary;
        delete place_boundary;
        delete via;
        delete control;
        delete rules;
        delete place_rules;
    }
    
    void SetBOUNDARY( BOUNDARY *aBoundary )
    {
        delete boundary;
        boundary = aBoundary;
        if( boundary )
        {
            boundary->SetParent( this );
        }
    }
    
    void SetPlaceBOUNDARY( BOUNDARY *aBoundary )
    {
        delete place_boundary;
        place_boundary = aBoundary;
        if( place_boundary )
            place_boundary->SetParent( this );
    }
    
    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        if( unit )
            unit->Format( out, nestLevel );
        
        for( LAYERS::iterator i=layers.begin();  i!=layers.end();  ++i )
            i->Format( out, nestLevel ); 

        if( layer_noise_weight )
            layer_noise_weight->Format( out, nestLevel );
        
        if( boundary )
            boundary->Format( out, nestLevel );

        if( place_boundary )
            place_boundary->Format( out, nestLevel );

        for( COPPER_PLANES::iterator i=planes.begin();  i!=planes.end();  ++i )
            i->Format( out, nestLevel );

        for( REGIONS::iterator i=regions.begin();  i!=regions.end();  ++i )
            i->Format( out, nestLevel );
        
        for( KEEPOUTS::iterator i=keepouts.begin();  i!=keepouts.end();  ++i )
            i->Format( out, nestLevel );
        
        if( via )
            via->Format( out, nestLevel );
        
        if( control )
            control->Format( out, nestLevel );
        
        for( int i=0; i<Length();  ++i )
        {
            At(i)->Format( out, nestLevel );
        }
        
        if( rules )
            rules->Format( out, nestLevel );

        if( place_rules )
            place_rules->Format( out, nestLevel );
        
        for( GRIDS::iterator i=grids.begin();  i!=grids.end();  ++i )
            i->Format( out, nestLevel );
    }
    
    DSN_T   GetUnits()
    {
        if( unit )
            return unit->GetUnits();
        
        return ELEM::GetUnits();
    }
};


class PLACE : public ELEM
{
    friend class SPECCTRA_DB;
    
    std::string     component_id;       ///< reference designator
    
    DSN_T           side;
    
    bool            isRotated;
    double          rotation;
    
    bool            hasVertex;
    POINT           vertex;
    
    DSN_T           mirror;
    DSN_T           status;

    std::string     logical_part;
    
    RULE*           place_rules;
    
    PROPERTIES      properties;
    
    DSN_T           lock_type;

    //-----<mutually exclusive>--------------    
    RULE*           rules;
    REGION*         region;
    //-----</mutually exclusive>-------------    

    std::string     part_number;
    
public:

    PLACE( ELEM* aParent ) :
        ELEM( T_place, aParent )
    {
        side = T_front;
        isRotated = false;
        hasVertex = false;
        
        mirror = T_NONE;
        status = T_NONE;
        place_rules = 0;
        
        lock_type = T_NONE;
        rules = 0;
        region = 0;
    }

    ~PLACE()
    {
        delete place_rules;
        delete rules;
        delete region;
    }
    
    void SetVertex( const POINT& aVertex )
    {
        vertex = aVertex;
        hasVertex = true;
    }

    void SetRotation( double aRotation )
    {
        rotation = aRotation;
        isRotated = (aRotation != 0.0);
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError );
};


class COMPONENT : public ELEM
{
    friend class SPECCTRA_DB;
    
    std::string     image_id;
    
    typedef boost::ptr_vector<PLACE>    PLACES;
    PLACES          places;
    
public:
    COMPONENT( ELEM* aParent ) :
        ELEM( T_component, aParent )
    {
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* quote = out->GetQuoteChar( image_id.c_str() );
        out->Print( nestLevel, "(%s %s%s%s\n", LEXER::GetTokenText( Type() ),
                                quote, image_id.c_str(), quote );

        for( PLACES::iterator i=places.begin();  i!=places.end();  ++i )
            i->Format( out, nestLevel+1 );
        
        out->Print( nestLevel, ")\n" );
    }
};


class PLACEMENT : public ELEM
{
    friend class SPECCTRA_DB;

    UNIT_RES*   unit;
    
    DSN_T       flip_style;
    
    typedef boost::ptr_vector<COMPONENT> COMPONENTS;
    COMPONENTS  components;
    
public:    
    PLACEMENT( ELEM* aParent ) :
        ELEM( T_placement, aParent )
    {
        unit = 0;
        flip_style = T_NONE;
    }

    ~PLACEMENT()
    {
        delete unit;
    }
    
    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        if( unit )
            unit->Format( out, nestLevel );
        
        if( flip_style != T_NONE )
        {
            out->Print( nestLevel, "(place_control (flip_style %s))\n",
                       LEXER::GetTokenText( flip_style ) );
        }
        
        for( COMPONENTS::iterator i=components.begin();  i!=components.end();  ++i )
            i->Format( out, nestLevel );
    }
    
    DSN_T   GetUnits()
    {
        if( unit )
            return unit->GetUnits();
        
        return ELEM::GetUnits();
    }
};


/**
 * Class SHAPE
 * corresponds to the "(shape ..)" element in the specctra dsn spec.
 * It is not a &lt;shape_descriptor&gt;, which is one of things that this
 * elements contains, i.e. in its "shape" field.  This class also implements
 * the "(outline ...)" element as a dual personality.
 */
class SHAPE : public ELEM
{
    friend class SPECCTRA_DB;

    DSN_T           connect;
    
    /*  <shape_descriptor >::=
        [<rectangle_descriptor> |
        <circle_descriptor> |
        <polygon_descriptor> |
        <path_descriptor> |
        <qarc_descriptor> ]
    */
    ELEM*           shape;
    
    WINDOWS         windows;
    
public:
    SHAPE( ELEM* aParent, DSN_T aType = T_shape ) :
        ELEM( aType, aParent )
    {
        connect = T_on;
        shape = 0;
    }
    ~SHAPE()
    {
        delete shape;
    }

    void SetShape( ELEM* aShape )
    {
        delete shape;
        shape = aShape;
        
        if( aShape )
        {
            wxASSERT(aShape->Type()==T_rect || aShape->Type()==T_circle 
                     || aShape->Type()==T_qarc || aShape->Type()==T_path 
                     || aShape->Type()==T_polygon);
            
            aShape->SetParent( this );            
        }
    }
    
    void SetConnect( DSN_T aConnect )
    {
        connect = aConnect;
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        out->Print( nestLevel, "(%s ", LEXER::GetTokenText( Type() ) );
        
        if( shape )
            shape->Format( out, 0 );
        
        if( connect == T_off )
            out->Print( 0, "(connect %s)", LEXER::GetTokenText( connect ) );

        if( windows.size() )
        {
            out->Print( 0, "\n" );
            
            for( WINDOWS::iterator i=windows.begin();  i!=windows.end();  ++i )
                i->Format( out, nestLevel+1 );
            
            out->Print( nestLevel, ")\n" );            
        }
        else
            out->Print( 0, ")\n" );
    }
};


class PIN : public ELEM
{
    friend class SPECCTRA_DB;

    std::string     padstack_id;
    double          rotation;
    bool            isRotated;
    std::string     pin_id;
    POINT           vertex;
    
public:
    PIN( ELEM* aParent ) :
        ELEM( T_pin, aParent )
    {
        rotation = 0.0;
        isRotated = false;
    }

    void SetRotation( double aRotation )
    {
        rotation = aRotation;
        isRotated = (aRotation != 0.0);
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* quote = out->GetQuoteChar( padstack_id.c_str() );
        if( isRotated )
            out->Print( nestLevel, "(pin %s%s%s (rotate %.6g)", 
                                     quote, padstack_id.c_str(), quote,
                                     rotation
                                     );
        else
            out->Print( nestLevel, "(pin %s%s%s", quote, padstack_id.c_str(), quote );
        
        quote = out->GetQuoteChar( pin_id.c_str() );
        out->Print( 0, " %s%s%s %.6g %.6g)\n", quote, pin_id.c_str(), quote, 
                   vertex.x, vertex.y );
    }
};


class IMAGE : public ELEM_HOLDER
{
    friend class SPECCTRA_DB;
    
    std::string     image_id;
    DSN_T           side;
    UNIT_RES*       unit;
    
    /*  The grammar spec says only one outline is supported, but I am seeing
        *.dsn examples with multiple outlines.  So the outlines will go into 
        the kids list. 
    */
    
    typedef boost::ptr_vector<PIN>  PINS;
    PINS            pins;

    RULE*           rules;
    RULE*           place_rules;

    KEEPOUTS        keepouts;
    
public:
    
    IMAGE( ELEM* aParent ) :
        ELEM_HOLDER( T_image, aParent )
    {
        side = T_both;
        unit = 0;
        rules = 0;
        place_rules = 0;
    }
    ~IMAGE()
    {
        delete unit;
        delete rules;
        delete place_rules;
    }

    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* quote = out->GetQuoteChar( image_id.c_str() );
        
        out->Print( nestLevel, "(%s %s%s%s", LEXER::GetTokenText( Type() ),
                                quote, image_id.c_str(), quote );

        if( side != T_both )
            out->Print( 0, " (side %s)", LEXER::GetTokenText( side ) );
        
        out->Print( 0, "\n");
        
        if( unit )
            unit->Format( out, nestLevel+1 );

        // format the kids, which in this class are the shapes
        ELEM_HOLDER::FormatContents( out, nestLevel+1 );
    
        for( PINS::iterator i=pins.begin();  i!=pins.end();  ++i )
            i->Format( out, nestLevel+1 );

        if( rules )
            rules->Format( out, nestLevel+1 );
        
        if( place_rules )
            place_rules->Format( out, nestLevel+1 );

        for( KEEPOUTS::iterator i=keepouts.begin();  i!=keepouts.end();  ++i )
            i->Format( out, nestLevel+1 );
        
        out->Print( nestLevel, ")\n" );
    }
    
    
    DSN_T   GetUnits()
    {
        if( unit )
            return unit->GetUnits();
        
        return ELEM::GetUnits();
    }
};


class PADSTACK : public ELEM_HOLDER
{
    friend class SPECCTRA_DB;

    std::string     padstack_id;    
    UNIT_RES*       unit;

    /* The shapes are stored in the kids list */

    DSN_T           rotate;
    DSN_T           absolute;
    DSN_T           attach;
    std::string     via_id;
    
    RULE*           rules;

public:
    
    PADSTACK( ELEM* aParent ) :
        ELEM_HOLDER( T_padstack, aParent )
    {
        unit = 0;
        rotate = T_on;
        absolute = T_off;
        rules = 0;
        attach = T_off;
    }
    ~PADSTACK()
    {
        delete unit;
        delete rules;
    }

    void SetPadstackId( const char* aPadstackId )
    {
        padstack_id = aPadstackId;
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* quote = out->GetQuoteChar( padstack_id.c_str() );
        
        out->Print( nestLevel, "(%s %s%s%s\n", LEXER::GetTokenText( Type() ),
                                quote, padstack_id.c_str(), quote );

        if( unit )
            unit->Format( out, nestLevel+1 );

        // format the kids, which in this class are the shapes
        ELEM_HOLDER::FormatContents( out, nestLevel+1 );

        out->Print( nestLevel+1, "%s", "" );
        
        // spec for <attach_descriptor> says default is on, so
        // print the off condition to override this.        
        if( attach == T_off )
            out->Print( 0, "(attach off)" );
        else if( attach == T_on )
        {
            const char* quote = out->GetQuoteChar( via_id.c_str() );
            out->Print( 0, "(attach on (use_via %s%s%s))",
                       quote, via_id.c_str(), quote );
        }
        
        if( rotate == T_off )   // print the non-default
            out->Print( 0, "(rotate %s)", LEXER::GetTokenText( rotate ) );

        if( absolute == T_on )  // print the non-default
            out->Print( 0, "(absolute %s)", LEXER::GetTokenText( absolute ) );

        out->Print( 0, "\n" );
        
        if( rules )
            rules->Format( out, nestLevel+1 );
        
        out->Print( nestLevel, ")\n" );
    }
    
    DSN_T   GetUnits()
    {
        if( unit )
            return unit->GetUnits();
        
        return ELEM::GetUnits();
    }
};
typedef boost::ptr_vector<PADSTACK> PADSTACKS;


/**
 * Class LIBRARY
 * corresponds to the &lt;library_descriptor&gt; in the specctra dsn specification.
 * Only unit_descriptor, image_descriptors, and padstack_descriptors are 
 * included as children at this time.
 */
class LIBRARY : public ELEM
{
    friend class SPECCTRA_DB;

    UNIT_RES*       unit;
    
    typedef boost::ptr_vector<IMAGE>    IMAGES;
    IMAGES          images;

    PADSTACKS       padstacks;

public:

    LIBRARY( ELEM* aParent, DSN_T aType = T_library ) :
        ELEM( aType, aParent )
    {
        unit = 0;
    }
    ~LIBRARY()
    {
        delete unit;
    }
    
    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        if( unit )
            unit->Format( out, nestLevel );
        
        for( IMAGES::iterator i=images.begin();  i!=images.end();  ++i )
            i->Format( out, nestLevel );

        for( PADSTACKS::iterator i=padstacks.begin();  i!=padstacks.end();  ++i )
            i->Format( out, nestLevel );
    }
    
    DSN_T   GetUnits()
    {
        if( unit )
            return unit->GetUnits();
        
        return ELEM::GetUnits();
    }
    
    void AddPadstack( PADSTACK* aPadstack )
    {
        padstacks.push_back( aPadstack );
    }
};


/**
 * Class PIN_REF
 * corresponds to the &lt;pin_reference&gt; definition in the specctra dsn spec.
 */
class PIN_REF : public ELEM
{
    friend class SPECCTRA_DB;

    std::string     component_id;
    std::string     pin_id;
    
public:

    PIN_REF( ELEM* aParent ) :
        ELEM( T_pin, aParent )
    {
    }

    /**
     * Function FormatIt
     * is like Format() but is not virual and returns the number of characters
     * that were output.
     */
    int FormatIt( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        // only print the newline if there is a nest level, and make
        // the quotes unconditional on this one.
        const char* newline = nestLevel ? "\n" : "";
        
        return out->Print( nestLevel, "\"%s\"-\"%s\"%s", 
                component_id.c_str(), pin_id.c_str(), newline );
    }
};
typedef std::vector<PIN_REF>   PIN_REFS;


class FROMTO : public ELEM
{
    friend class SPECCTRA_DB;
    
    std::string     fromText;
    std::string     toText;
    
    DSN_T           fromto_type;
    std::string     net_id;
    RULE*           rules;
//    std::string     circuit;
    LAYER_RULES     layer_rules;
    

public:    
    FROMTO( ELEM* aParent ) :
        ELEM( T_fromto, aParent )
    {
        rules = 0;
        fromto_type  = T_NONE;
    }
    ~FROMTO()
    {
        delete rules;
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        // no quoting on these two, the lexer preserved the quotes on input
        out->Print( nestLevel, "(%s %s %s ", 
                 LEXER::GetTokenText( Type() ), fromText.c_str(), toText.c_str() );

        if( fromto_type != T_NONE )
            out->Print( 0, "(type %s)", LEXER::GetTokenText( fromto_type ) );
        
        if( net_id.size() )
        {
            const char* quote = out->GetQuoteChar( net_id.c_str() );
            out->Print( 0, "(net %s%s%s)",  quote, net_id.c_str(), quote );
        }

        bool singleLine = true;
        
        if( rules || layer_rules.size() )
        {
            out->Print( 0, "\n" );
            singleLine = false;
        }
        
        if( rules )
            rules->Format( out, nestLevel+1 );
        
        /*
        if( circuit.size() )
            out->Print( nestLevel, "%s\n", circuit.c_str() );
        */
     
        for( LAYER_RULES::iterator i=layer_rules.begin();  i!=layer_rules.end();  ++i )
            i->Format( out, nestLevel+1 );
        
        out->Print( singleLine ? 0 : nestLevel, ")" );
        if( nestLevel || !singleLine )
            out->Print( 0, "\n" );
    }
};
typedef boost::ptr_vector<FROMTO>       FROMTOS;


/**
 * Class COMP_ORDER
 * corresponds to the &lt;component_order_descriptor&gt;
 */
class COMP_ORDER : public ELEM
{
    friend class SPECCTRA_DB;

    STRINGS         placement_ids;
    
public:    
    COMP_ORDER( ELEM* aParent ) :
        ELEM( T_comp_order, aParent )
    {
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        out->Print( nestLevel, "(%s", LEXER::GetTokenText( Type() ) );
        
        for( STRINGS::iterator i=placement_ids.begin();  i!=placement_ids.end();  ++i )
        {
            const char* quote = out->GetQuoteChar( i->c_str() );
            out->Print( 0, " %s%s%s", quote, i->c_str(), quote );
        }
        
        out->Print( 0, ")" );
        if( nestLevel )
            out->Print( 0, "\n" );
    }
};


class NET : public ELEM
{
    friend class SPECCTRA_DB;

    std::string     net_id;
    bool            unassigned;
    int             net_number;

    DSN_T           pins_type;      ///< T_pins | T_order
    
    PIN_REFS        pins;

    DSN_T           type;           ///< T_fix | T_normal
    
    DSN_T           supply;         ///< T_power | T_ground
    
    RULE*           rules;
    
    LAYER_RULES     layer_rules;
    
    FROMTOS         fromtos;
    
    COMP_ORDER*     comp_order;
    
public:

    NET( ELEM* aParent ) :
        ELEM( T_net, aParent )
    {
        unassigned = false;
        net_number = T_NONE;
        pins_type = T_pins;
        
        type = T_NONE;
        supply = T_NONE;
        
        rules = 0;
        comp_order = 0;
    }
    
    ~NET()
    {
        delete rules;
        delete comp_order;
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* quote = out->GetQuoteChar( net_id.c_str() );
        
        out->Print( nestLevel, "(%s %s%s%s ", LEXER::GetTokenText( Type() ), 
                   quote, net_id.c_str(), quote );
        
        if( unassigned )
            out->Print( 0, "(unassigned)" );
        
        if( net_number != T_NONE )
            out->Print( 0, "(net_number %d)", net_number );
        
        out->Print( 0, "\n" );

                
        const int RIGHTMARGIN = 80;
        int perLine = out->Print( nestLevel+1, "(%s", LEXER::GetTokenText( pins_type ) );
        
        for( PIN_REFS::iterator i=pins.begin();  i!=pins.end();  ++i )
        {
            if( perLine > RIGHTMARGIN )
            {
                out->Print( 0, "\n");
                perLine = out->Print( nestLevel+2, "%s", "" );
            }
            else
                perLine += out->Print( 0, " " );
            
            perLine += i->FormatIt( out, 0 );
        }
        out->Print( 0, ")\n" );

        if( comp_order )
            comp_order->Format( out, nestLevel+1 );
        
        if( type != T_NONE )
            out->Print( nestLevel+1, "(type %s)\n", LEXER::GetTokenText( type ) );
        
        if( rules )
            rules->Format( out, nestLevel+1 );
        
        for( LAYER_RULES::iterator i=layer_rules.begin();  i!=layer_rules.end();  ++i )
            i->Format( out, nestLevel+1 );

        for( FROMTOS::iterator i=fromtos.begin();  i!=fromtos.end();  ++i )
            i->Format( out, nestLevel+1 );
        
        out->Print( nestLevel, ")\n" );
    }
};


class TOPOLOGY : public ELEM
{
    friend class SPECCTRA_DB;
    
    FROMTOS         fromtos;
  
    typedef boost::ptr_vector<COMP_ORDER>   COMP_ORDERS;
    COMP_ORDERS     comp_orders;

public:    
    TOPOLOGY( ELEM* aParent ) :
        ELEM( T_topology, aParent )
    {
    }

    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        for( FROMTOS::iterator i=fromtos.begin();  i!=fromtos.end();  ++i )
            i->Format( out, nestLevel );
        
        for( COMP_ORDERS::iterator i=comp_orders.begin();  i!=comp_orders.end();  ++i )
            i->Format( out, nestLevel );
    }
};


class CLASS : public ELEM
{
    friend class SPECCTRA_DB;
    
    std::string     class_id;
    
    STRINGS         net_ids;
    
    /// <circuit_descriptor> list
    STRINGS         circuit;
    
    RULE*           rules;
    
    LAYER_RULES     layer_rules;
    
    TOPOLOGY*       topology;
    
public:

    CLASS( ELEM* aParent ) :
        ELEM( T_class, aParent )
    {
        rules = 0;
        topology = 0;
    }
    ~CLASS()
    {
        delete rules;
        delete topology;
    }

    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const int RIGHTMARGIN = 80;
        
        const char* quote = out->GetQuoteChar( class_id.c_str() );
        
        int perLine = out->Print( nestLevel, "(%s %s%s%s", 
                              LEXER::GetTokenText( Type() ),
                              quote, class_id.c_str(), quote );

        for( STRINGS::iterator i=net_ids.begin();  i!=net_ids.end();  ++i )
        {
            if( perLine > RIGHTMARGIN )
            {
                out->Print( 0, "\n" );
                perLine = out->Print( nestLevel+1, "%s", "" );
            }
            
            quote = out->GetQuoteChar( i->c_str() );
            perLine += out->Print( 0, " %s%s%s", quote, i->c_str(), quote );
        }
        
        bool newLine = false;
        if( circuit.size() || layer_rules.size() || topology )
        {
            out->Print( 0, "\n" );
            newLine = true;
        }

        for( STRINGS::iterator i=circuit.begin();  i!=circuit.end();  ++i )
            out->Print( nestLevel+1, "%s\n", i->c_str() );

        for( LAYER_RULES::iterator i=layer_rules.begin();  i!=layer_rules.end();  ++i )
            i->Format( out, nestLevel+1 );
        
        if( topology )
            topology->Format( out, nestLevel+1 );
        
        out->Print( newLine ? nestLevel : 0, ")\n" );
    }
};
    

class NETWORK : public ELEM
{
    friend class SPECCTRA_DB;

    typedef boost::ptr_vector<NET>  NETS;
    NETS        nets;
    
    typedef boost::ptr_vector<CLASS> CLASSLIST;
    CLASSLIST   classes;
    
    
public:

    NETWORK( ELEM* aParent ) :
        ELEM( T_network, aParent )
    {
    }
    
    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        for( NETS::iterator i=nets.begin();  i!=nets.end();  ++i )
            i->Format( out, nestLevel );
        
        for( CLASSLIST::iterator i=classes.begin();  i!=classes.end();  ++i )
            i->Format( out, nestLevel );
    }
};


class CONNECT : public ELEM
{
public:
    CONNECT( ELEM* parent ) :
        ELEM( T_connect, parent ) {}
};


/**
 * Class WIRE
 * corresponds to &lt;wire_shape_descriptor&gt; in the specctra dsn spec.
 */
class WIRE : public ELEM
{
    friend class SPECCTRA_DB;

    //----- only one of these is used, like a union -----
    PATH*           path;           ///< used for both path and polygon
    RECTANGLE*      rectangle;
    CIRCLE*         circle;
    QARC*           qarc;
    //---------------------------------------------------
    
    std::string     net_id;
    int             turret;
    DSN_T           type;
    DSN_T           attr;
    std::string     shield;
    WINDOWS         windows;
    CONNECT*        connect;
    bool            supply;
    
public:
    WIRE( ELEM* aParent ) :
        ELEM( T_wire, aParent )
    {
        path = 0;
        rectangle = 0;
        circle = 0;
        qarc = 0;
        connect = 0;
        
        turret = -1;
        type = T_NONE;
        attr = T_NONE;
        supply = false;
    }
    
    ~WIRE()
    {
        delete path;
        delete rectangle;
        delete circle;
        delete qarc;
        delete connect;
    }

    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        // these are mutually exclusive
        if( rectangle )
            rectangle->Format( out, nestLevel );
        
        else if( path )
            path->Format( out, nestLevel );
        
        else if( circle )
            circle->Format( out, nestLevel );

        else if( qarc )
            qarc->Format( out, nestLevel );
        
        if( net_id.size() )
        {
            const char* quote = out->GetQuoteChar( net_id.c_str() );
            out->Print( nestLevel, "(net %s%s%s)\n", 
                       quote, net_id.c_str(), quote );
        }
        
        if( turret >= 0 )
            out->Print( nestLevel, "(turrent %d)\n", turret );

        if( type != T_NONE )
            out->Print( nestLevel, "(type %s)\n", LEXER::GetTokenText( type ) );
        
        if( attr != T_NONE )
            out->Print( nestLevel, "(attr %s)\n", LEXER::GetTokenText( attr ) );
        
        if( shield.size() )
        {
            const char* quote = out->GetQuoteChar( shield.c_str() );
            out->Print( nestLevel, "(shield %s%s%s)\n", 
                       quote, shield.c_str(), quote );
        }
        
        for( WINDOWS::iterator i=windows.begin();  i!=windows.end();  ++i )
            i->Format( out, nestLevel );

        if( connect )
            connect->Format( out, nestLevel );
        
        if( supply )
            out->Print( nestLevel, "(supply)\n" );
    }
};
typedef boost::ptr_vector<WIRE>     WIRES;


/**
 * Class WIRE_VIA
 * corresponds to &lt;wire_via_descriptor&gt; in the specctra dsn spec.
 */
class WIRE_VIA : public ELEM
{
    friend class SPECCTRA_DB;

    std::string     padstack_id;
    POINTS          vertexes;
    std::string     net_id;
    int             via_number;
    DSN_T           type;
    DSN_T           attr;
    std::string     virtual_pin_name;
    STRINGS         contact_layers;
    bool            supply;

    
public:
    WIRE_VIA( ELEM* aParent ) :
        ELEM( T_via, aParent )
    {
        via_number = -1;
        type = T_NONE;
        attr = T_NONE;
        supply = false;
    }

    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* quote = out->GetQuoteChar( padstack_id.c_str() );
        
        const int RIGHTMARGIN = 80;        
        int perLine = out->Print( nestLevel, "(%s %s%s%s", 
                       LEXER::GetTokenText( Type() ),
                       quote, padstack_id.c_str(), quote );

        for( POINTS::iterator i=vertexes.begin();  i!=vertexes.end();  ++i )
        {
            if( perLine > RIGHTMARGIN )
            {
                out->Print( 0, "\n" );
                perLine = out->Print( nestLevel+1, "%s", "" );
            }
            else
                perLine += out->Print( 0, "  " );
            
            perLine += out->Print( 0, "%.6g %.6g", i->x, i->y ); 
        }
        
        if( net_id.size() || via_number!=-1 || type!=T_NONE || attr!=T_NONE || supply)
            out->Print( 0, " " );
        
        if( net_id.size() )
        {
            if( perLine > RIGHTMARGIN )
            {
                out->Print( 0, "\n" );
                perLine = out->Print( nestLevel+1, "%s", "" );
            }
            const char* quote = out->GetQuoteChar( net_id.c_str() );
            perLine += out->Print( 0, "(net %s%s%s)", quote, net_id.c_str(), quote ); 
        }

        if( via_number != -1 )
        {
            if( perLine > RIGHTMARGIN )
            {
                out->Print( 0, "\n" );
                perLine = out->Print( nestLevel+1, "%s", "" );
            }
            perLine += out->Print( 0, "(via_number %d)", via_number );
        }
        
        if( type != T_NONE )
        {
            if( perLine > RIGHTMARGIN )
            {
                out->Print( 0, "\n" );
                perLine = out->Print( nestLevel+1, "%s", "" );
            }
            perLine += out->Print( 0, "(type %s)", LEXER::GetTokenText( type ) );
        }
        
        if( attr != T_NONE )
        {
            if( perLine > RIGHTMARGIN )
            {
                out->Print( 0, "\n" );
                perLine = out->Print( nestLevel+1, "%s", "" );
            }
            if( attr == T_virtual_pin )
            {
                const char* quote = out->GetQuoteChar( virtual_pin_name.c_str() );
                perLine += out->Print( 0, "(attr virtual_pin %s%s%s)",
                           quote, virtual_pin_name.c_str(), quote );
            }
            else
                perLine += out->Print( 0, "(attr %s)", LEXER::GetTokenText( attr ) );
        }

        if( supply )
        {
            if( perLine > RIGHTMARGIN )
            {
                out->Print( 0, "\n" );
                perLine = out->Print( nestLevel+1, "%s", "" );
            }
            perLine += out->Print( 0, "(supply)" );
        }
        
        if( contact_layers.size() )
        {
            out->Print( 0, "\n" );
            out->Print( nestLevel+1, "(contact\n" );
            
            for( STRINGS::iterator i=contact_layers.begin();  i!=contact_layers.end();  ++i )
            {
                const char* quote = out->GetQuoteChar( i->c_str() );
                out->Print( nestLevel+2, "%s%s%s\n", quote, i->c_str(), quote );
            }
            out->Print( nestLevel+1, "))\n" );
        }
        else
            out->Print( 0, ")\n" );
    }
};
typedef boost::ptr_vector<WIRE_VIA>      WIRE_VIAS;


/**
 * Class WIRING
 * corresponds to &lt;wiring_descriptor&gt; in the specctra dsn spec.
 */
class WIRING : public ELEM
{
    friend class SPECCTRA_DB;
    
    UNIT_RES*   unit;
    WIRES       wires;
    WIRE_VIAS   wire_vias;    

public:

    WIRING( ELEM* aParent ) :
        ELEM( T_wiring, aParent )
    {
        unit = 0;
    }
    ~WIRING()
    {
        delete unit;
    }

    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        if( unit )
            unit->Format( out, nestLevel );

        for( WIRES::iterator i=wires.begin();  i!=wires.end();  ++i )
            i->Format( out, nestLevel );
        
        for( WIRE_VIAS::iterator i=wire_vias.begin();  i!=wire_vias.end();  ++i )
            i->Format( out, nestLevel );
    }

    DSN_T   GetUnits()
    {
        if( unit )
            return unit->GetUnits();
        
        return ELEM::GetUnits();
    }
};


class PCB : public ELEM
{
    friend class SPECCTRA_DB;

    std::string     pcbname;    
    PARSER*         parser;
    UNIT_RES*       resolution;
    UNIT_RES*       unit;
    STRUCTURE*      structure;
    PLACEMENT*      placement;
    LIBRARY*        library;
    NETWORK*        network;
    WIRING*         wiring;
    
public:
    
    PCB( ELEM* aParent = 0 ) :
        ELEM( T_pcb, aParent )
    {
        parser = 0;
        resolution = 0;
        unit = 0;
        structure = 0;
        placement = 0;
        library = 0;
        network = 0;
        wiring = 0;
    }
    
    ~PCB()
    {
        delete parser;
        delete resolution;
        delete unit;
        delete structure;
        delete placement;
        delete library;
        delete network;
        delete wiring;
    }
    
    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* quote = out->GetQuoteChar( pcbname.c_str() );
        
        out->Print( nestLevel, "(%s %s%s%s\n", LEXER::GetTokenText( Type() ),
                                quote, pcbname.c_str(), quote );
        
        if( parser )
            parser->Format( out, nestLevel+1 );
        
        if( resolution )
            resolution->Format( out, nestLevel+1 );

        if( unit )
            unit->Format( out, nestLevel+1 );

        if( structure )
            structure->Format( out, nestLevel+1 );
        
        if( placement )
            placement->Format( out, nestLevel+1 );
        
        if( library )
            library->Format( out, nestLevel+1 );

        if( network )
            network->Format( out, nestLevel+1 );
        
        if( wiring )
            wiring->Format( out, nestLevel+1 );
        
        out->Print( nestLevel, ")\n" ); 
    }
    
    DSN_T   GetUnits()
    {
        if( unit )
            return unit->GetUnits();
        
        if( resolution )
            return resolution->GetUnits();
        
        return ELEM::GetUnits();
    }
};


class ANCESTOR : public ELEM
{
    friend class SPECCTRA_DB;

    std::string     filename;    
    std::string     comment;
    time_t          time_stamp;

    
public:
    ANCESTOR( ELEM* aParent ) :
        ELEM( T_ancestor, aParent )
    {
        time_stamp = time(NULL);
    }

    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        char    temp[80];
        struct  tm* tmp;
        
        tmp = localtime( &time_stamp );
        strftime( temp, sizeof(temp), "%b %d %H : %M : %S %Y", tmp );
        
        // format the time first to temp
        // filename may be empty, so quote it just in case.
        out->Print( nestLevel, "(%s \"%s\" (created_time %s)\n", 
                     LEXER::GetTokenText( Type() ),
                     filename.c_str(),
                     temp );
        
        if( comment.size() )
        {
            const char* quote = out->GetQuoteChar( comment.c_str() );
            out->Print( nestLevel+1, "(comment %s%s%s)\n", 
                       quote, comment.c_str(), quote );
        }
        
        out->Print( nestLevel, ")\n" );
    }
};
typedef boost::ptr_vector<ANCESTOR>     ANCESTORS;


class HISTORY : public ELEM
{
    friend class SPECCTRA_DB;

    ANCESTORS       ancestors;
    time_t          time_stamp;   
    STRINGS         comments;
    
public:

    HISTORY( ELEM* aParent ) :
        ELEM( T_history, aParent )
    {
        time_stamp = time(NULL);
    }
    
    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        for( ANCESTORS::iterator i=ancestors.begin();  i!=ancestors.end();  ++i )
            i->Format( out, nestLevel );
        
        char    temp[80];
        struct  tm* tmp;
        
        tmp = localtime( &time_stamp );
        strftime( temp, sizeof(temp), "%b %d %H : %M : %S %Y", tmp );
    
        // format the time first to temp
        out->Print( nestLevel, "(self (created_time %s)\n", temp );
        
        for( STRINGS::iterator i=comments.begin();  i!=comments.end();  ++i )
        {
            const char* quote = out->GetQuoteChar( i->c_str() );
            out->Print( nestLevel+1, "(comment %s%s%s)\n", 
                       quote, i->c_str(), quote );
        }
        
        out->Print( nestLevel, ")\n" );
    }
};


/**
 * Class SUPPLY_PIN
 * corresponds to the &lt;supply_pin_descriptor&gt; in the specctra dsn spec.
*/ 
class SUPPLY_PIN : public ELEM
{
    friend class SPECCTRA_DB;
    
    PIN_REFS        pin_refs;
    std::string     net_id;

public:
    SUPPLY_PIN( ELEM* aParent ) :
        ELEM( T_supply_pin, aParent )
    {
    }

    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        bool singleLine = pin_refs.size() <= 1;
        out->Print( nestLevel, "(%s", LEXER::GetTokenText( Type() ) );
        
        if( singleLine )
        {
            out->Print( 0, "%s", " " );
            pin_refs.begin()->Format( out, 0 );
        }
        else
        {
            for( PIN_REFS::iterator i=pin_refs.begin();  i!=pin_refs.end();  ++i )
                i->FormatIt( out, nestLevel+1 );
        }
        
        if( net_id.size() )
        {
            const char* newline = singleLine ? "" : "\n";
        
            const char* quote = out->GetQuoteChar( net_id.c_str() );
            out->Print( singleLine ? 0 : nestLevel+1, 
                       " (net %s%s%s)%s", quote, net_id.c_str(), quote, newline ); 
        }
        
        out->Print( singleLine ? 0 : nestLevel, ")\n");
    }
};
typedef boost::ptr_vector<SUPPLY_PIN>   SUPPLY_PINS;


/**
 * Class NET_OUT
 * corresponds to the &lt;net_out_descriptor&gt; of the specctra dsn spec.
 */
class NET_OUT : public ELEM
{
    friend class SPECCTRA_DB;
    
    std::string     net_id;
    int             net_number;
    RULE*           rules;
    WIRES           wires;
    WIRE_VIAS       wire_vias;
    SUPPLY_PINS     supply_pins;    
    

public:
    NET_OUT( ELEM* aParent ) :
        ELEM( T_net_out, aParent )
    {
        rules = 0;
        net_number = -1;
    }
    ~NET_OUT()
    {
        delete rules;
    }

    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* quote = out->GetQuoteChar( net_id.c_str() );
        
        // cannot use Type() here, it is T_net_out and we need "(net "  
        out->Print( nestLevel, "(net %s%s%s\n",    
                   quote, net_id.c_str(), quote );

        if( net_number>= 0 )
            out->Print( nestLevel+1, "(net_number %d)\n", net_number );
        
        if( rules )
            rules->Format( out, nestLevel+1 ); 
        
        for( WIRES::iterator i=wires.begin();  i!=wires.end();  ++i )
            i->Format( out, nestLevel+1 );
        
        for( WIRE_VIAS::iterator i=wire_vias.begin();  i!=wire_vias.end();  ++i )
            i->Format( out, nestLevel+1 );
        
        for( SUPPLY_PINS::iterator i=supply_pins.begin();  i!=supply_pins.end();  ++i )
            i->Format( out, nestLevel+1 );
        
        out->Print( nestLevel, ")\n" );
    }
};
typedef boost::ptr_vector<NET_OUT>      NET_OUTS;


class ROUTE : public ELEM
{
    friend class SPECCTRA_DB;

    UNIT_RES*       resolution;
    PARSER*         parser;
    STRUCTURE*      structure;
    LIBRARY*        library;
    NET_OUTS        net_outs;
//    TEST_POINTS*    test_points;    

public:
    
    ROUTE( ELEM* aParent ) :
        ELEM( T_route, aParent )
    {
        resolution = 0;
        parser = 0;
        structure = 0;
        library = 0;
    }
    ~ROUTE()
    {
        delete resolution;
        delete parser;
        delete structure;
        delete library;
//        delete test_points;
    }
    
    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        if( resolution )
            resolution->Format( out, nestLevel );
        
        if( parser )
            parser->Format( out, nestLevel );
        
        if( structure )
            structure->Format( out, nestLevel );
        
        if( library )
            library->Format( out, nestLevel );
        
        if( net_outs.size() )
        {
            out->Print( nestLevel, "(network_out\n" );
            for( NET_OUTS::iterator i=net_outs.begin();  i!=net_outs.end();  ++i )
                i->Format( out, nestLevel+1 );
            out->Print( nestLevel, ")\n" );
        }
        
//        if( test_poinst )
//            test_points->Format( out, nestLevel );
    }
};


/**
 * Struct PIN_PAIR
 * is used within the WAS_IS class below to hold a pair of PIN_REFs and
 * corresponds to the (pins was is) construct within the specctra dsn spec.
 */
struct PIN_PAIR
{
    PIN_PAIR( ELEM* aParent = 0 ) :
        was( aParent ),
        is( aParent )
    {
    }
    
    PIN_REF     was;
    PIN_REF     is;
};
typedef std::vector<PIN_PAIR>   PIN_PAIRS;


/**
 * Class WAS_IS
 * corresponds to the &lt;was_is_descriptor&gt; in the specctra dsn spec.
 */
class WAS_IS : public ELEM
{
    friend class SPECCTRA_DB;

    PIN_PAIRS       pin_pairs;

public:
    WAS_IS( ELEM* aParent ) :
        ELEM( T_was_is, aParent )
    {
    }
    
    void FormatContents( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        for( PIN_PAIRS::iterator i=pin_pairs.begin();  i!=pin_pairs.end();  ++i )
        {
            out->Print( nestLevel, "(pins " );
            i->was.Format( out, 0 );
            out->Print( 0, " " );
            i->is.Format( out, 0 );
            out->Print( 0, ")\n" );
        }
    }
};


/**
 * Class SESSION
 * corresponds to the &lt;session_file_descriptor&gt; in the specctra dsn spec.
 */
class SESSION : public ELEM
{
    friend class SPECCTRA_DB;

    std::string     session_id;
    std::string     base_design;
    
    HISTORY*        history;    
    STRUCTURE*      structure;
    PLACEMENT*      placement;
    WAS_IS*         was_is;
    ROUTE*          route;

/*  not supported:
    FLOOR_PLAN*         floor_plan;
    NET_PIN_CHANGES*    net_pin_changes;
    SWAP_HISTORY*       swap_history;
*/

public:
    
    SESSION( ELEM* aParent = 0 ) :
        ELEM( T_pcb, aParent )
    {
        history = 0;
        structure = 0;
        placement = 0;
        was_is = 0;
        route = 0;
    }
    ~SESSION()
    {
        delete history;
        delete structure;
        delete placement;
        delete was_is;
        delete route;
    }

    void Format( OUTPUTFORMATTER* out, int nestLevel ) throw( IOError )
    {
        const char* quote = out->GetQuoteChar( session_id.c_str() );
        out->Print( nestLevel, "(%s %s%s%s\n", LEXER::GetTokenText( Type() ),
                                quote, session_id.c_str(), quote );
        
        out->Print( nestLevel+1, "(base_design \"%s\")\n", base_design.c_str() );
        
        if( history )
            history->Format( out, nestLevel+1 );
        
        if( structure )
            structure->Format( out, nestLevel+1 );
        
        if( placement )
            placement->Format( out, nestLevel+1 );

        if( was_is )
            was_is->Format( out, nestLevel+1 );
        
        if( route )
            route->Format( out, nestLevel+1 );
        
        out->Print( nestLevel, ")\n" );
    }
};


/**
 * Class SPECCTRA_DB
 * holds a DSN data tree, usually coming from a DSN file.
 */
class SPECCTRA_DB : public OUTPUTFORMATTER
{
    LEXER*      lexer;
    
    PCB*        pcb;

    SESSION*    session;    

    FILE*       fp;

    wxString    filename;
    
    std::string quote_char;
    
    
    /**
     * Function nextTok
     * returns the next token from the lexer.
     */
    DSN_T   nextTok();

    
    /**
     * Function isSymbol
     * tests a token to see if it is a symbol.  This means it cannot be a
     * special delimiter character such as T_LEFT, T_RIGHT, T_QUOTE, etc.  It may
     * however, coincidentally match a keyword and still be a symbol.
     */
    static bool isSymbol( DSN_T aTok );

    
    /**
     * Function needLEFT
     * calls nextTok() and then verifies that the token read in is a T_LEFT.
     * If it is not, an IOError is thrown.
     * @throw IOError, if the next token is not a T_LEFT
     */
    void needLEFT() throw( IOError );

    /**
     * Function needRIGHT
     * calls nextTok() and then verifies that the token read in is a T_RIGHT.
     * If it is not, an IOError is thrown.
     * @throw IOError, if the next token is not a T_RIGHT
     */
    void needRIGHT() throw( IOError );

    /**
     * Function needSYMBOL
     * calls nextTok() and then verifies that the token read in 
     * satisfies bool isSymbol().
     * If not, an IOError is thrown.
     * @throw IOError, if the next token does not satisfy isSymbol()
     */
    void needSYMBOL() throw( IOError );

    /**
     * Function readCOMPnPIN
     * reads a &lt;pin_reference&gt; and splits it into the two parts which are
     * on either side of the hyphen.  This function is specialized because
     * pin_reference may or may not be using double quotes.  Both of these
     * are legal:  U2-14 or "U2"-"14".  The lexer treats the first one as a 
     * single T_SYMBOL, so in that case we have to split it into two here.
     * <p>
     * The caller should have already read in the first token comprizing the 
     * pin_reference and it will be tested through lexer->CurTok().
     *
     * @param component_id Where to put the text preceeding the '-' hyphen.
     * @param pin_d Where to put the text which trails the '-'.
     * @throw IOError, if the next token or two do no make up a pin_reference,
     * or there is an error reading from the input stream.
     */
    void readCOMPnPIN( std::string* component_id, std::string* pid_id ) throw( IOError );


    /**
     * Function readTIME
     * reads a &lt;time_stamp&gt; which consists of 8 lexer tokens:
     * "month date hour : minute : second year".
     * This function is specialized because time_stamps occur more than
     * once in a session file.
     * <p>
     * The caller should not have already read in the first token comprizing the 
     * time stamp.
     *
     * @param time_stamp Where to put the parsed time value.
     * @throw IOError, if the next token or 8 do no make up a time stamp,
     * or there is an error reading from the input stream.
     */
    void readTIME( time_t* time_stamp ) throw( IOError );

    
    /**
     * Function expecting
     * throws an IOError exception with an input file specific error message.
     * @param DSN_T The token type which was expected at the current input location.
     * @throw IOError with the location within the input file of the problem.
     */
    void expecting( DSN_T ) throw( IOError );
    void expecting( const char* text ) throw( IOError );
    void unexpected( DSN_T aTok ) throw( IOError );
    void unexpected( const char* text ) throw( IOError );
    
    void doPCB( PCB* growth ) throw(IOError);
    void doPARSER( PARSER* growth ) throw(IOError);
    void doRESOLUTION( UNIT_RES* growth ) throw(IOError);
    void doUNIT( UNIT_RES* growth ) throw( IOError );    
    void doSTRUCTURE( STRUCTURE* growth ) throw( IOError );
    void doLAYER_NOISE_WEIGHT( LAYER_NOISE_WEIGHT* growth ) throw( IOError );
    void doLAYER_PAIR( LAYER_PAIR* growth ) throw( IOError );    
    void doBOUNDARY( BOUNDARY* growth ) throw( IOError );
    void doRECTANGLE( RECTANGLE* growth ) throw( IOError );
    void doPATH( PATH* growth ) throw( IOError );
    void doSTRINGPROP( STRINGPROP* growth ) throw( IOError );
    void doTOKPROP( TOKPROP* growth ) throw( IOError );
    void doVIA( VIA* growth ) throw( IOError );
    void doCONTROL( CONTROL* growth ) throw( IOError );    
    void doLAYER( LAYER* growth ) throw( IOError );
    void doRULE( RULE* growth ) throw( IOError );
    void doKEEPOUT( KEEPOUT* growth ) throw( IOError );
    void doCIRCLE( CIRCLE* growth ) throw( IOError );
    void doQARC( QARC* growth ) throw( IOError );
    void doWINDOW( WINDOW* growth ) throw( IOError );
    void doREGION( REGION* growth ) throw( IOError );
    void doCLASS_CLASS( CLASS_CLASS* growth ) throw( IOError );    
    void doLAYER_RULE( LAYER_RULE* growth ) throw( IOError );
    void doCLASSES( CLASSES* growth ) throw( IOError );
    void doGRID( GRID* growth ) throw( IOError );
    void doPLACE( PLACE* growth ) throw( IOError );
    void doCOMPONENT( COMPONENT* growth ) throw( IOError );
    void doPLACEMENT( PLACEMENT* growth ) throw( IOError );
    void doPROPERTIES( PROPERTIES* growth ) throw( IOError );
    void doPADSTACK( PADSTACK* growth ) throw( IOError );
    void doSHAPE( SHAPE* growth ) throw( IOError );
    void doIMAGE( IMAGE* growth ) throw( IOError );
    void doLIBRARY( LIBRARY* growth ) throw( IOError );
    void doPIN( PIN* growth ) throw( IOError );
    void doNET( NET* growth ) throw( IOError );
    void doNETWORK( NETWORK* growth ) throw( IOError );
    void doCLASS( CLASS* growth ) throw( IOError );
    void doTOPOLOGY( TOPOLOGY* growth ) throw( IOError );
    void doFROMTO( FROMTO* growth ) throw( IOError );
    void doCOMP_ORDER( COMP_ORDER* growth ) throw( IOError );
    void doWIRE( WIRE* growth ) throw( IOError );
    void doWIRE_VIA( WIRE_VIA* growth ) throw( IOError );
    void doWIRING( WIRING* growth ) throw( IOError );
    void doSESSION( SESSION* growth ) throw( IOError );
    void doANCESTOR( ANCESTOR* growth ) throw( IOError );
    void doHISTORY( HISTORY* growth ) throw( IOError );
    void doROUTE( ROUTE* growth ) throw( IOError );
    void doWAS_IS( WAS_IS* growth ) throw( IOError );
    void doNET_OUT( NET_OUT* growth ) throw( IOError );
    void doSUPPLY_PIN( SUPPLY_PIN* growth ) throw( IOError );    

    
public:

    SPECCTRA_DB()
    {
        lexer = 0;
        pcb   = 0;
        session = 0;
        fp    = 0;
        quote_char += '"';
    }

    ~SPECCTRA_DB()
    {
        delete lexer;
        delete pcb;
        delete session;
        
        if( fp )
            fclose( fp );
    }

    
    //-----<OUTPUTFORMATTER>-------------------------------------------------
    int PRINTF_FUNC Print( int nestLevel, const char* fmt, ... ) throw( IOError );
    
    const char* GetQuoteChar( const char* wrapee ); 
    //-----</OUTPUTFORMATTER>------------------------------------------------

    /**
     * Function MakePCB
     * makes a PCB with all the default ELEMs and parts on the heap.
     */
    static PCB* MakePCB();

    
    /**
     * Function SetPCB
     * deletes any existing PCB and replaces it with the given one.
     */
    void SetPCB( PCB* aPcb )
    {
        delete pcb;
        pcb = aPcb;
    }

    /**
     * Function SetSESSION
     * deletes any existing SESSION and replaces it with the given one.
     */
    void SetSESSION( SESSION* aSession )
    {
        delete session;
        session = aSession;
    }
    
    
    /**
     * Function LoadPCB
     * is a recursive descent parser for a SPECCTRA DSN "design" file.
     * A design file is nearly a full description of a PCB (seems to be 
     * missing only the silkscreen stuff).
     *
     * @param filename The name of the dsn file to load.
     * @throw IOError if there is a lexer or parser error. 
     */
    void LoadPCB( const wxString& filename ) throw( IOError );

    
    /**
     * Function LoadSESSION
     * is a recursive descent parser for a SPECCTRA DSN "session" file.
     * A session file is file that is fed back from the router to the layout
     * tool (PCBNEW) and should be used to update a BOARD object with the new
     * tracks, vias, and component locations. 
     *
     * @param filename The name of the dsn file to load.
     * @throw IOError if there is a lexer or parser error. 
     */
    void LoadSESSION( const wxString& filename ) throw( IOError );

    
    void ThrowIOError( const wxChar* fmt, ... ) throw( IOError );
    
    
    /**
     * Function ExportPCB
     * writes the internal PCB instance out as a SPECTRA DSN format file.
     *
     * @param aFilename The file to save to.
     * @param aNameChange If true, causes the pcb's name to change to "aFilename" 
     *          and also to to be changed in the output file.
     * @throw IOError, if an i/o error occurs saving the file.
     */
    void ExportPCB( wxString aFilename,  bool aNameChange=false ) throw( IOError );

    
    /**
     * Function FromBOARD
     * adds the entire BOARD to the PCB but does not write it out.  Note that
     * the BOARD given to this function must have all the MODULEs on the component
     * side of the BOARD.
     *
     * See void WinEDA_PcbFrame::ExportToSPECCTRA( wxCommandEvent& event )
     * for how this can be done before calling this function.  
     * @todo
     * I would have liked to put the flipping logic into the ExportToSPECCTRA()
     * function directly, but for some strange reason, 
     * void Change_Side_Module( MODULE* Module, wxDC* DC ) is a member of 
     * of class WinEDA_BasePcbFrame rather than class BOARD.
     *
     * @param aBoard The BOARD to convert to a PCB.
     */
    void FromBOARD( BOARD* aBoard );
    
    
    /**
     * Function ExportSESSION
     * writes the internal SESSION instance out as a SPECTRA DSN format file.
     *
     * @param aFilename The file to save to.
     */
    void ExportSESSION( wxString aFilename );
};


}           // namespace DSN

#endif      // SPECCTRA_H_

//EOF
