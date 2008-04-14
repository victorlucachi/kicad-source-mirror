/////////////////////////////////////////////////////////////////////////////

// Name:        svg_print.cpp
// Purpose:
// Author:      jean-pierre Charras
// Modified by:
// Created:     05/02/2006 11:05:19
// RCS-ID:
// Copyright:   License GNU
// Licence:
/////////////////////////////////////////////////////////////////////////////

// Generated by DialogBlocks (unregistered), 05/02/2006 11:05:19

#if defined (__GNUG__) && !defined (NO_GCC_PRAGMA)
#pragma implementation "svg_print.h"
#endif

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

////@begin includes
////@end includes

#include <ctype.h>
#include "wx/metafile.h"
#include "wx/image.h"

#include "fctsys.h"
#include "gr_basic.h"

#include "common.h"

#include "dcsvg.h"
#include "svg_print.h"

#ifdef EESCHEMA
#include "program.h"
#endif

////@begin XPM images
////@end XPM images

extern BASE_SCREEN* ActiveScreen;
#ifdef EESCHEMA
#define WIDTH_MAX_VALUE 100
#else
#define WIDTH_MAX_VALUE 1000
#endif
#define WIDTH_MIN_VALUE 1

// Variables locales
static int  s_SVGPenMinWidth;       /* Minimum pen width (in internal units) for printing */

static int  Select_PrintAll     = FALSE;
static bool Print_Sheet_Ref     = TRUE;
static int  s_PlotBlackAndWhite = 0;


/*******************************************************/
void WinEDA_DrawFrame::SVG_Print( wxCommandEvent& event )
/*******************************************************/

/* Prepare les structures de donnees de gestion de l'impression
  * et affiche la fenetre de dialogue de gestion de l'impression des feuilles
 */
{
    // Arret des commandes en cours
    if( DrawPanel->ManageCurseur && DrawPanel->ForceCloseManageCurseur )
    {
        wxClientDC dc( DrawPanel );

        DrawPanel->PrepareDC( dc );
        DrawPanel->ForceCloseManageCurseur( DrawPanel, &dc );
    }
    SetToolID( 0, wxCURSOR_ARROW, wxEmptyString );

    WinEDA_PrintSVGFrame frame( this );

    frame.ShowModal();
}


/*!
 * WinEDA_PrintSVGFrame type definition
 */

IMPLEMENT_DYNAMIC_CLASS( WinEDA_PrintSVGFrame, wxDialog )

/*!
 * WinEDA_PrintSVGFrame event table definition
 */

BEGIN_EVENT_TABLE( WinEDA_PrintSVGFrame, wxDialog )

////@begin WinEDA_PrintSVGFrame event table entries
EVT_CLOSE( WinEDA_PrintSVGFrame::OnCloseWindow )

EVT_RADIOBOX( ID_RADIOBOX_SETPRINTMODE, WinEDA_PrintSVGFrame::OnRadioboxSetprintmodeSelected )

EVT_BUTTON( ID_PRINT_EXECUTE, WinEDA_PrintSVGFrame::OnPrintExecuteClick )

EVT_BUTTON( wxID_CLOSE, WinEDA_PrintSVGFrame::OnCloseClick )

////@end WinEDA_PrintSVGFrame event table entries

END_EVENT_TABLE()

/*!
 * WinEDA_PrintSVGFrame constructors
 */

WinEDA_PrintSVGFrame::WinEDA_PrintSVGFrame()
{
}


WinEDA_PrintSVGFrame::WinEDA_PrintSVGFrame( WinEDA_DrawFrame* parent,
                                            wxWindowID id,
                                            const wxString& caption,
                                            const wxPoint& pos,
                                            const wxSize& size,
                                            long style )
{
    m_Parent = parent;
    m_ImageXSize_mm = 270;
    wxConfig* Config = m_Parent->m_Parent->m_EDA_Config;
    if( Config )
    {
        Config->Read( wxT( "PlotSVGPenWidth" ), &s_SVGPenMinWidth );
        Config->Read( wxT( "PlotSVGModeColor" ), &s_PlotBlackAndWhite );
    }

    Create( parent, id, caption, pos, size, style );
}


/*!
 * WinEDA_PrintSVGFrame creator
 */

bool WinEDA_PrintSVGFrame::Create( wxWindow* parent,
                                   wxWindowID id,
                                   const wxString& caption,
                                   const wxPoint& pos,
                                   const wxSize& size,
                                   long style )
{
////@begin WinEDA_PrintSVGFrame member initialisation
    m_DialogPenWidthSizer = NULL;
    m_ModeColorOption = NULL;
    m_Print_Sheet_Ref = NULL;
    m_PagesOption  = NULL;
    m_FileNameCtrl = NULL;
    m_MessagesBox  = NULL;

////@end WinEDA_PrintSVGFrame member initialisation

////@begin WinEDA_PrintSVGFrame creation
    SetExtraStyle( GetExtraStyle() | wxWS_EX_BLOCK_EVENTS );
    wxDialog::Create( parent, id, caption, pos, size, style );

    CreateControls();
    if( GetSizer() )
    {
        GetSizer()->SetSizeHints( this );
    }
    Centre();

////@end WinEDA_PrintSVGFrame creation
    return true;
}


/*!
 * Control creation for WinEDA_PrintSVGFrame
 */

void WinEDA_PrintSVGFrame::CreateControls()
{
    SetFont( *g_DialogFont );

////@begin WinEDA_PrintSVGFrame content construction
    // Generated by DialogBlocks, 24/01/2007 19:00:23 (unregistered)

    WinEDA_PrintSVGFrame* itemDialog1 = this;

    wxBoxSizer*           itemBoxSizer2 = new   wxBoxSizer( wxVERTICAL );

    itemDialog1->SetSizer( itemBoxSizer2 );

    wxBoxSizer* itemBoxSizer3 = new             wxBoxSizer( wxHORIZONTAL );

    itemBoxSizer2->Add( itemBoxSizer3, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 5 );

    wxBoxSizer* itemBoxSizer4 = new             wxBoxSizer( wxVERTICAL );

    itemBoxSizer3->Add( itemBoxSizer4, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

    m_DialogPenWidthSizer = new                 wxBoxSizer( wxVERTICAL );

    itemBoxSizer4->Add( m_DialogPenWidthSizer, 0, wxGROW | wxALL, 5 );

    wxString m_ModeColorOptionStrings[] = {
        _( "Color" ),
        _( "Black and White" )
    };
    m_ModeColorOption = new wxRadioBox( itemDialog1, ID_RADIOBOX_SETPRINTMODE, _(
                                            "Print mode" ), wxDefaultPosition, wxDefaultSize, 2,
                                        m_ModeColorOptionStrings, 1,
                                        wxRA_SPECIFY_COLS );

    m_ModeColorOption->SetSelection( 0 );
    itemBoxSizer4->Add( m_ModeColorOption, 0, wxALIGN_LEFT | wxALL, 5 );

    m_Print_Sheet_Ref = new wxCheckBox( itemDialog1, ID_CHECKBOX, _(
                                            "Print Sheet Ref" ), wxDefaultPosition, wxDefaultSize,
                                        wxCHK_2STATE );

    m_Print_Sheet_Ref->SetValue( false );
    itemBoxSizer4->Add( m_Print_Sheet_Ref, 0, wxALIGN_LEFT | wxALL, 5 );

    itemBoxSizer3->Add( 5, 5, 0, wxALIGN_CENTER_VERTICAL | wxALL, 15 );

    wxBoxSizer* itemBoxSizer9 = new wxBoxSizer( wxVERTICAL );

    itemBoxSizer3->Add( itemBoxSizer9, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );

    wxString m_PagesOptionStrings[] = {
        _( "Current" ),
        _( "All" )
    };
    m_PagesOption = new             wxRadioBox( itemDialog1, ID_RADIOBOX1, _(
                                                    "Page Print:" ), wxDefaultPosition,
                                                wxDefaultSize, 2, m_PagesOptionStrings, 1,
                                                wxRA_SPECIFY_COLS );

    m_PagesOption->SetSelection( 0 );
    itemBoxSizer9->Add( m_PagesOption, 0, wxALIGN_LEFT | wxALL, 5 );

    wxButton* itemButton11 = new    wxButton( itemDialog1, ID_PRINT_EXECUTE, _(
                                                  "Create &File" ), wxDefaultPosition,
              wxDefaultSize, 0 );

    itemButton11->SetDefault();
    itemButton11->SetForegroundColour( wxColour( 0, 128, 0 ) );
    itemBoxSizer9->Add( itemButton11, 0, wxGROW | wxALL, 5 );

    wxButton* itemButton12 = new            wxButton( itemDialog1, wxID_CLOSE, _(
                                                          "&Close" ), wxDefaultPosition,
              wxDefaultSize, 0 );

    itemButton12->SetForegroundColour( wxColour( 0, 0, 198 ) );
    itemBoxSizer9->Add( itemButton12, 0, wxGROW | wxALL, 5 );

    wxStaticText* itemStaticText13 = new    wxStaticText( itemDialog1, wxID_STATIC, _(
                                                              "Filename:" ), wxDefaultPosition,
                  wxDefaultSize, 0 );

    itemBoxSizer2->Add( itemStaticText13,
        0,
        wxALIGN_LEFT | wxLEFT | wxRIGHT | wxTOP | wxADJUST_MINSIZE,
        5 );

    m_FileNameCtrl = new                    wxTextCtrl( itemDialog1, ID_TEXTCTRL, _T(
                                                            "" ), wxDefaultPosition, wxDefaultSize,
                                                        0 );

    itemBoxSizer2->Add( m_FileNameCtrl, 0, wxGROW | wxALL, 5 );

    wxStaticText* itemStaticText15 = new    wxStaticText( itemDialog1, wxID_STATIC, _(
                                                              "Messages:" ), wxDefaultPosition,
                  wxDefaultSize, 0 );

    itemBoxSizer2->Add( itemStaticText15,
        0,
        wxALIGN_LEFT | wxLEFT | wxRIGHT | wxTOP | wxADJUST_MINSIZE,
        5 );

    m_MessagesBox = new                     wxTextCtrl( itemDialog1, ID_TEXTCTRL1, _T(
                                                            "" ), wxDefaultPosition, wxSize( -1,
                                                            100 ), wxTE_MULTILINE | wxTE_READONLY );

    itemBoxSizer2->Add( m_MessagesBox, 0, wxGROW | wxALL, 5 );

    // Set validators
    m_ModeColorOption->SetValidator( wxGenericValidator( &s_PlotBlackAndWhite ) );
    m_Print_Sheet_Ref->SetValidator( wxGenericValidator( &Print_Sheet_Ref ) );

////@end WinEDA_PrintSVGFrame content construction

    m_DialogPenWidth = new WinEDA_ValueCtrl( this, _(
                                                 "Pen width mini" ), s_SVGPenMinWidth,
                                             g_UnitMetric, m_DialogPenWidthSizer,
                                             m_Parent->m_InternalUnits );
}


/*!
 * Should we show tooltips?
 */

bool WinEDA_PrintSVGFrame::ShowToolTips()
{
    return true;
}


/*!
 * Get bitmap resources
 */

wxBitmap WinEDA_PrintSVGFrame::GetBitmapResource( const wxString& name )
{
    // Bitmap retrieval
////@begin WinEDA_PrintSVGFrame bitmap retrieval
    wxUnusedVar( name );
    return wxNullBitmap;

////@end WinEDA_PrintSVGFrame bitmap retrieval
}


/*!
 * Get icon resources
 */

wxIcon WinEDA_PrintSVGFrame::GetIconResource( const wxString& name )
{
    // Icon retrieval
////@begin WinEDA_PrintSVGFrame icon retrieval
    wxUnusedVar( name );
    return wxNullIcon;

////@end WinEDA_PrintSVGFrame icon retrieval
}


/******************************************************/
wxString WinEDA_PrintSVGFrame::ReturnFullFileName()
/******************************************************/
{
    wxString name, ext;

    name = m_Parent->GetScreen()->m_FileName;
    ChangeFileNameExt( name, wxT( ".svg" ) );
    return name;
}


/********************************************/
void WinEDA_PrintSVGFrame::SetPenWidth()
/********************************************/
{
    s_SVGPenMinWidth = m_DialogPenWidth->GetValue();

    if( s_SVGPenMinWidth > WIDTH_MAX_VALUE )
    {
        s_SVGPenMinWidth = WIDTH_MAX_VALUE;
    }

    if( s_SVGPenMinWidth < WIDTH_MIN_VALUE )
    {
        s_SVGPenMinWidth = WIDTH_MIN_VALUE;
    }

    m_DialogPenWidth->SetValue( s_SVGPenMinWidth );
}


/**************************************************************/
void WinEDA_PrintSVGFrame::PrintSVGDoc( wxCommandEvent& event )
/**************************************************************/

/* Called on activate "Print CURRENT" button
 */
{
    bool     print_ref = TRUE;
    wxString msg;

    Select_PrintAll = FALSE;
    if( m_PagesOption && (m_PagesOption->GetSelection() == 1) )
        Select_PrintAll = TRUE;

    if( (m_Print_Sheet_Ref == NULL) || (m_Print_Sheet_Ref->GetValue() == FALSE) )
        print_ref = FALSE;

    SetPenWidth();


    BASE_SCREEN* screen    = m_Parent->GetScreen();
    BASE_SCREEN* oldscreen = screen;
#ifndef EESCHEMA
    if( Select_PrintAll )
        while( screen->Pback )
            screen = (BASE_SCREEN*) screen->Pback;

#endif

    if( (m_Parent->m_Ident == PCB_FRAME) || (m_Parent->m_Ident == GERBER_FRAME) )
    {
        if( Select_PrintAll )
        {
            m_PrintMaskLayer = 0xFFFFFFFF;
        }
        else
            m_PrintMaskLayer = 1;
    }

    if( screen == NULL )
        return;

#ifdef EESCHEMA
    if( Select_PrintAll && m_Parent->m_Ident == SCHEMATIC_FRAME )
    {
        EDA_ScreenList ScreenList;
        for( SCH_SCREEN* schscreen = ScreenList.GetFirst(); schscreen != NULL;
            schscreen = ScreenList.GetNext() )
        {
            /* Create all files *.svg */
            ( (WinEDA_SchematicFrame*) m_Parent )->SetScreen( schscreen );
            wxString FullFileName = schscreen->m_FileName;
            ChangeFileNameExt( FullFileName, wxT( ".svg" ) );
            bool     success = DrawPage( FullFileName, schscreen );
            msg = _( "Create file " ) + FullFileName;
            if( !success )
                msg += _( " error" );
            msg += wxT( "\n" );
            m_MessagesBox->AppendText( msg );
        }
    }
    else
#endif
    {
        wxString FullFileName = m_FileNameCtrl->GetValue();
        if( FullFileName.IsEmpty() )
        {
            FullFileName = screen->m_FileName;
            ChangeFileNameExt( FullFileName, wxT( ".svg" ) );
        }
        bool     success = DrawPage( FullFileName, screen );
        msg = _( "Create file " ) + FullFileName;
        if( !success )
            msg += _( " error" );
        msg += wxT( "\n" );
        m_MessagesBox->AppendText( msg );
    }
    ActiveScreen = oldscreen;
}


/*****************************************************************/
bool WinEDA_PrintSVGFrame::DrawPage( const wxString& FullFileName, BASE_SCREEN* screen )
/*****************************************************************/

/*
  * Routine effective d'impression
 */
{
    int     tmpzoom;
    wxPoint tmp_startvisu;
    wxSize  SheetSize;  // Sheet size in internal units
    wxPoint old_org;
    float   dpi;
    bool    success = TRUE;

    /* modification des cadrages et reglages locaux */
    tmp_startvisu = screen->m_StartVisu;
    tmpzoom = screen->GetZoom();
    old_org = screen->m_DrawOrg;
    screen->m_DrawOrg.x   = screen->m_DrawOrg.y = 0;
    screen->m_StartVisu.x = screen->m_StartVisu.y = 0;
    SheetSize    = screen->m_CurrentSheetDesc->m_Size;  // size in 1/1000 inch
    SheetSize.x *= m_Parent->m_InternalUnits / 1000;
    SheetSize.y *= m_Parent->m_InternalUnits / 1000;    // size in pixels

    screen->SetZoom( 1 );
    dpi = (float) SheetSize.x * 25.4 / m_ImageXSize_mm;

    WinEDA_DrawPanel* panel = m_Parent->DrawPanel;

    wxSVGFileDC dc( FullFileName, SheetSize.x, SheetSize.y, dpi );

    if( !dc.Ok() )
    {
        DisplayError( this, wxT( "SVGprint error: wxSVGFileDC not OK" ) );
        success = FALSE;
    }
    else
    {
        EDA_Rect tmp = panel->m_ClipBox;
        GRResetPenAndBrush( &dc );
        s_SVGPenMinWidth = m_DialogPenWidth->GetValue();
        SetPenMinWidth( s_SVGPenMinWidth );
        GRForceBlackPen( m_ModeColorOption->GetSelection() == 0 ? FALSE : TRUE );


        panel->m_ClipBox.SetX( 0 ); panel->m_ClipBox.SetY( 0 );
        panel->m_ClipBox.SetWidth( 0x7FFFFF0 ); panel->m_ClipBox.SetHeight( 0x7FFFFF0 );

        g_IsPrinting = TRUE;
        setlocale( LC_NUMERIC, "C" );   // Switch the locale to standard C (needed to print floating point numbers like 1.3)
        panel->PrintPage( &dc, m_Print_Sheet_Ref, m_PrintMaskLayer );
        setlocale( LC_NUMERIC, "" );    // revert to the current  locale
        g_IsPrinting     = FALSE;
        panel->m_ClipBox = tmp;
    }


    GRForceBlackPen( FALSE );
    SetPenMinWidth( 1 );

    screen->m_StartVisu = tmp_startvisu;
    screen->m_DrawOrg   = old_org;
    screen->SetZoom( tmpzoom );

    return success;
}


/*!
 * wxEVT_COMMAND_BUTTON_CLICKED event handler for ID_PRINT_EXECUTE
 */

void WinEDA_PrintSVGFrame::OnPrintExecuteClick( wxCommandEvent& event )
{
    PrintSVGDoc( event );
}


/*!
 * wxEVT_COMMAND_BUTTON_CLICKED event handler for wxID_CLOSE
 */

void WinEDA_PrintSVGFrame::OnCloseClick( wxCommandEvent& event )
{
    Close( TRUE );
}


/*!
 * wxEVT_CLOSE_WINDOW event handler for ID_DIALOG
 */

void WinEDA_PrintSVGFrame::OnCloseWindow( wxCloseEvent& event )
{
    wxConfig* Config = m_Parent->m_Parent->m_EDA_Config;

    if( Config )
    {
        s_PlotBlackAndWhite = m_ModeColorOption->GetSelection();
        Config->Write( wxT( "PlotSVGPenWidth" ), s_SVGPenMinWidth );
        Config->Write( wxT( "PlotSVGModeColor" ), s_PlotBlackAndWhite );
    }
    event.Skip();
}


/*!
 * wxEVT_COMMAND_RADIOBOX_SELECTED event handler for ID_RADIOBOX_SETPRINTMODE
 */

void WinEDA_PrintSVGFrame::OnRadioboxSetprintmodeSelected( wxCommandEvent& event )
{
    s_PlotBlackAndWhite = m_ModeColorOption->GetSelection();
    event.Skip();
}
