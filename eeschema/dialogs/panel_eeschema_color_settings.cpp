/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2020 Jon Evans <jon@craftyjon.com>
 * Copyright (C) 2020 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <regex>

#include <bitmaps.h>
#include <eeschema_settings.h>
#include <gal/gal_display_options.h>
#include <layers_id_colors_and_visibility.h>
#include <class_libentry.h>
#include <lib_polyline.h>
#include <page_info.h>
#include <panel_eeschema_color_settings.h>
#include <pgm_base.h>
#include <sch_bus_entry.h>
#include <sch_junction.h>
#include <sch_line.h>
#include <sch_no_connect.h>
#include <sch_painter.h>
#include <sch_preview_panel.h>
#include <sch_text.h>
#include <settings/color_settings.h>
#include <settings/common_settings.h>
#include <settings/settings_manager.h>
#include <title_block.h>
#include <view/view.h>
#include <page_layout/ws_proxy_view_item.h>
#include <sch_base_frame.h>
#include <widgets/color_swatch.h>


PANEL_EESCHEMA_COLOR_SETTINGS::PANEL_EESCHEMA_COLOR_SETTINGS( SCH_BASE_FRAME* aFrame,
                                                              wxWindow* aParent ) :
          PANEL_COLOR_SETTINGS( aParent ),
          m_frame( aFrame ),
          m_preview( nullptr ),
          m_page( nullptr ),
          m_titleBlock( nullptr ),
          m_ws( nullptr ),
          m_previewItems()
{
    m_colorNamespace = "schematic";

    SETTINGS_MANAGER&  mgr = Pgm().GetSettingsManager();

    mgr.ReloadColorSettings();

    COMMON_SETTINGS*   common_settings = Pgm().GetCommonSettings();
    EESCHEMA_SETTINGS* app_settings = mgr.GetAppSettings<EESCHEMA_SETTINGS>();
    COLOR_SETTINGS*    current = mgr.GetColorSettings( app_settings->m_ColorTheme );

    createThemeList( app_settings->m_ColorTheme );

    m_optOverrideColors->SetValue( current->GetOverrideSchItemColors() );

    m_currentSettings = new COLOR_SETTINGS( *current );

    for( int id = SCH_LAYER_ID_START; id < SCH_LAYER_ID_END; id++ )
        m_validLayers.push_back( id );

    m_backgroundLayer = LAYER_SCHEMATIC_BACKGROUND;

    createSwatches();

    KIGFX::GAL_DISPLAY_OPTIONS options;
    options.ReadConfig( *common_settings, app_settings->m_Window, this );
    options.m_forceDisplayCursor = false;

    auto type = static_cast<EDA_DRAW_PANEL_GAL::GAL_TYPE>( app_settings->m_Graphics.canvas_type );

    m_preview = new SCH_PREVIEW_PANEL( this, wxID_ANY, wxDefaultPosition, wxSize( -1, -1 ),
                                       options, type );
    m_preview->SetStealsFocus( false );
    m_preview->ShowScrollbars( wxSHOW_SB_NEVER, wxSHOW_SB_NEVER );
    m_preview->GetGAL()->SetAxesEnabled( false );

    m_colorsMainSizer->Add( 10, 0, 0, wxEXPAND, 5 );
    m_colorsMainSizer->Add( m_preview, 1, wxALL | wxEXPAND, 5 );
    m_colorsMainSizer->Add( 10, 0, 0, wxEXPAND, 5 );

    createPreviewItems();
    updatePreview();
    zoomFitPreview();
}


PANEL_EESCHEMA_COLOR_SETTINGS::~PANEL_EESCHEMA_COLOR_SETTINGS()
{
    delete m_page;
    delete m_titleBlock;
    delete m_ws;
    delete m_currentSettings;

    for( EDA_ITEM* item : m_previewItems )
        delete item;
}


bool PANEL_EESCHEMA_COLOR_SETTINGS::TransferDataFromWindow()
{
    m_currentSettings->SetOverrideSchItemColors( m_optOverrideColors->GetValue() );

    if( !saveCurrentTheme( true ) )
        return false;

    m_frame->GetCanvas()->GetView()->GetPainter()->GetSettings()->LoadColors( m_currentSettings );

    SETTINGS_MANAGER& settingsMgr = Pgm().GetSettingsManager();
    EESCHEMA_SETTINGS* app_settings = settingsMgr.GetAppSettings<EESCHEMA_SETTINGS>();
    app_settings->m_ColorTheme = m_currentSettings->GetFilename();

    return true;
}


bool PANEL_EESCHEMA_COLOR_SETTINGS::TransferDataToWindow()
{
    zoomFitPreview();
    return true;
}


bool PANEL_EESCHEMA_COLOR_SETTINGS::validateSave( bool aQuiet )
{
    COLOR4D bgcolor = m_currentSettings->GetColor( LAYER_SCHEMATIC_BACKGROUND );

    for( SCH_LAYER_ID layer = SCH_LAYER_ID_START; layer < SCH_LAYER_ID_END; ++layer )
    {
        if( bgcolor == m_currentSettings->GetColor( layer )
            && layer != LAYER_SCHEMATIC_BACKGROUND && layer != LAYER_SHEET_BACKGROUND )
        {
            wxString msg = _( "Some items have the same color as the background\n"
                              "and they will not be seen on the screen.  Are you\n"
                              "sure you want to use these colors?" );

            if( wxMessageBox( msg, _( "Warning" ), wxYES_NO | wxICON_QUESTION, this ) == wxNO )
                return false;

            break;
        }
    }

    return true;
}


bool PANEL_EESCHEMA_COLOR_SETTINGS::saveCurrentTheme( bool aValidate)
{
    for( auto layer : m_validLayers )
    {
        COLOR4D color = m_currentSettings->GetColor( layer );

        // Do not allow non-background layers to be completely white.
        // This ensures the BW printing recognizes that the colors should be printed black.
        if( color == COLOR4D::WHITE && layer != LAYER_SCHEMATIC_BACKGROUND
                && layer != LAYER_SHEET_BACKGROUND )
        {
            color.Darken( 0.01 );
        }

        m_currentSettings->SetColor( layer, color );
    }

    return PANEL_COLOR_SETTINGS::saveCurrentTheme( aValidate );
}


void PANEL_EESCHEMA_COLOR_SETTINGS::createSwatches()
{
    std::vector<SCH_LAYER_ID> layers;

    for( SCH_LAYER_ID i = SCH_LAYER_ID_START; i < SCH_LAYER_ID_END; ++i )
        layers.push_back( i );

    std::sort( layers.begin(), layers.end(),
               []( SCH_LAYER_ID a, SCH_LAYER_ID b )
               {
                   return LayerName( a ) < LayerName( b );
               } );

    for( int layer : layers )
    {
        wxString name = LayerName( layer );

        if( layer == LAYER_SCHEMATIC_GRID_AXES )
            name += wxS( " " ) + _( "(symbol editor only)" );

        createSwatch( layer, name );
    }

    // Give a minimal width to m_colorsListWindow, in order to always having
    // a full row shown
    int min_width = m_colorsGridSizer->GetMinSize().x;
    const int margin = 20;  // A margin around the sizer
    m_colorsListWindow->SetMinSize( wxSize( min_width + margin, -1 ) );
}


void PANEL_EESCHEMA_COLOR_SETTINGS::onNewThemeSelected()
{
    updatePreview();
}


void PANEL_EESCHEMA_COLOR_SETTINGS::createPreviewItems()
{
    KIGFX::VIEW* view = m_preview->GetView();

    m_page       = new PAGE_INFO( PAGE_INFO::Custom );
    m_titleBlock = new TITLE_BLOCK;
    m_titleBlock->SetTitle( _( "Color Preview" ) );
    m_titleBlock->SetDate( wxDateTime::Now().FormatDate() );

    m_page->SetHeightMils( 5000 );
    m_page->SetWidthMils( 6000 );

    m_ws = new KIGFX::WS_PROXY_VIEW_ITEM( (int) IU_PER_MILS, m_page, nullptr, m_titleBlock );
    m_ws->SetColorLayer( LAYER_SCHEMATIC_WORKSHEET );
    view->Add( m_ws );

    // NOTE: It would be nice to parse a schematic file here.
    // This is created from the color_settings.sch file in demos folder

    auto addItem = [&]( EDA_ITEM* aItem )
                   {
                       view->Add( aItem );
                       m_previewItems.push_back( aItem );
                   };

    std::vector<std::pair<SCH_LAYER_ID, std::pair<wxPoint, wxPoint>>> lines = {
                { LAYER_WIRE,  { { 1950, 1500 }, { 2325, 1500 } } },
                { LAYER_WIRE,  { { 1950, 2600 }, { 2350, 2600 } } },
                { LAYER_WIRE,  { { 2150, 1700 }, { 2325, 1700 } } },
                { LAYER_WIRE,  { { 2150, 2000 }, { 2150, 1700 } } },
                { LAYER_WIRE,  { { 2925, 1600 }, { 3075, 1600 } } },
                { LAYER_WIRE,  { { 3075, 1600 }, { 3075, 2000 } } },
                { LAYER_WIRE,  { { 3075, 1600 }, { 3250, 1600 } } },
                { LAYER_WIRE,  { { 3075, 2000 }, { 2150, 2000 } } },
                { LAYER_BUS,   { { 1750, 1400 }, { 1850, 1400 } } },
                { LAYER_BUS,   { { 1850, 2500 }, { 1850, 1400 } } },
                { LAYER_NOTES, { { 2350, 2125 }, { 2350, 2300 } } },
                { LAYER_NOTES, { { 2350, 2125 }, { 2950, 2125 } } },
                { LAYER_NOTES, { { 2950, 2125 }, { 2950, 2300 } } },
                { LAYER_NOTES, { { 2950, 2300 }, { 2350, 2300 } } }
            };

    for( const auto& line : lines )
    {
        SCH_LINE* wire = new SCH_LINE;
        wire->SetLayer( line.first );
        wire->SetStartPoint( wxPoint( Mils2iu( line.second.first.x ),
                                      Mils2iu( line.second.first.y ) ) );
        wire->SetEndPoint( wxPoint( Mils2iu( line.second.second.x ),
                                    Mils2iu( line.second.second.y ) ) );
        addItem( wire );
    }

    auto nc = new SCH_NO_CONNECT;
    nc->SetPosition( wxPoint( Mils2iu( 2525 ), Mils2iu( 1300 ) ) );
    addItem( nc );

    auto e1 = new SCH_BUS_WIRE_ENTRY;
    e1->SetPosition( wxPoint( Mils2iu( 1850 ), Mils2iu( 1400 ) ) );
    addItem( e1 );

    auto e2 = new SCH_BUS_WIRE_ENTRY;
    e2->SetPosition( wxPoint( Mils2iu( 1850 ), Mils2iu( 2500 ) ) );
    addItem( e2 );

    auto t1 = new SCH_TEXT( wxPoint( Mils2iu( 2850 ), Mils2iu( 2250 ) ), wxT( "PLAIN TEXT" ) );
    t1->SetLabelSpinStyle( LABEL_SPIN_STYLE::SPIN::LEFT );
    addItem( t1 );

    auto t2 = new SCH_LABEL( wxPoint( Mils2iu( 1975 ), Mils2iu( 1500 ) ), wxT( "LABEL_{0}" ) );
    t2->SetLabelSpinStyle( LABEL_SPIN_STYLE::SPIN::RIGHT );
    t2->SetIsDangling( false );
    addItem( t2 );

    auto t3 = new SCH_LABEL( wxPoint( Mils2iu( 1975 ), Mils2iu( 2600 ) ), wxT( "LABEL_{1}" ) );
    t3->SetLabelSpinStyle( LABEL_SPIN_STYLE::SPIN::RIGHT );
    t3->SetIsDangling( false );
    addItem( t3 );

    auto t4 = new SCH_GLOBALLABEL( wxPoint( Mils2iu( 1750 ), Mils2iu( 1400 ) ), wxT( "GLOBAL[3..0]" ) );
    t4->SetLabelSpinStyle( LABEL_SPIN_STYLE::SPIN::LEFT );
    t4->SetIsDangling( false );
    addItem( t4 );

    auto t5 = new SCH_HIERLABEL( wxPoint( Mils2iu( 3250 ), Mils2iu( 1600 ) ), wxT( "HIER_LABEL" ) );
    t5->SetLabelSpinStyle( LABEL_SPIN_STYLE::SPIN::RIGHT );
    t5->SetIsDangling( false );
    addItem( t5 );

    auto j = new SCH_JUNCTION( wxPoint( Mils2iu( 3075 ), Mils2iu( 1600 ) ) );
    addItem( j );

    e2->SetBrightened();
    t2->SetSelected();

    {
        LIB_PART* part = new LIB_PART( wxEmptyString );
        wxPoint p( 2625, -1600 );

        LIB_FIELD& ref = part->GetReferenceField();

        ref.SetText( wxT( "U1" ) );
        ref.SetPosition( wxPoint( Mils2iu( p.x + 30 ), Mils2iu( p.y + 260 ) ) );
        ref.SetHorizJustify( GR_TEXT_HJUSTIFY_LEFT );

        LIB_FIELD& value = part->GetValueField();

        value.SetText( wxT( "OPA604" ) );
        value.SetPosition( wxPoint( Mils2iu( p.x + 30 ), Mils2iu( p.y + 180 ) ) );
        value.SetHorizJustify( GR_TEXT_HJUSTIFY_LEFT );

        part->SetShowPinNames( true );
        part->SetShowPinNumbers( true );
        part->SetPinNameOffset( 0 );

        addItem( part );

        LIB_POLYLINE* comp_body = new LIB_POLYLINE( part );

        comp_body->SetUnit( 0 );
        comp_body->SetConvert( 0 );
        comp_body->SetWidth( Mils2iu( 10 ) );
        comp_body->SetFillMode( FILL_TYPE::FILLED_WITH_BG_BODYCOLOR );
        comp_body->AddPoint( wxPoint( Mils2iu( p.x - 200 ), Mils2iu( p.y + 200 ) ) );
        comp_body->AddPoint( wxPoint( Mils2iu( p.x + 200 ), Mils2iu( p.y ) ) );
        comp_body->AddPoint( wxPoint( Mils2iu( p.x - 200 ), Mils2iu( p.y - 200 ) ) );
        comp_body->AddPoint( wxPoint( Mils2iu( p.x - 200 ), Mils2iu( p.y + 200 ) ) );

        addItem( comp_body );

        LIB_PIN* pin = new LIB_PIN( part );

        pin->SetPosition( wxPoint( Mils2iu( p.x - 200 ), Mils2iu( p.y + 100 ) ) );
        pin->SetLength( Mils2iu( 100 ) );
        pin->SetOrientation( PIN_LEFT );
        pin->SetType( ELECTRICAL_PINTYPE::PT_INPUT );
        pin->SetNumber( wxT( "1" ) );
        pin->SetName( wxT( "-" ) );

        part->AddDrawItem( pin );

        pin = new LIB_PIN( part );

        pin->SetPosition( wxPoint( Mils2iu( p.x - 200 ), Mils2iu( p.y - 100 ) ) );
        pin->SetLength( Mils2iu( 100 ) );
        pin->SetOrientation( PIN_LEFT );
        pin->SetType( ELECTRICAL_PINTYPE::PT_INPUT );
        pin->SetNumber( wxT( "2" ) );
        pin->SetName( wxT( "+" ) );

        part->AddDrawItem( pin );

        pin = new LIB_PIN( part );

        pin->SetPosition( wxPoint( Mils2iu( p.x + 200 ), Mils2iu( p.y ) ) );
        pin->SetLength( Mils2iu( 100 ) );
        pin->SetOrientation( PIN_RIGHT );
        pin->SetType( ELECTRICAL_PINTYPE::PT_OUTPUT );
        pin->SetNumber( wxT( "3" ) );
        pin->SetName( wxT( "OUT" ) );

        part->AddDrawItem( pin );
    }

    auto s = new SCH_SHEET( nullptr, wxPoint( Mils2iu( 4000 ), Mils2iu( 1300 ) ) );
    s->SetSize( wxSize( Mils2iu( 800 ), Mils2iu( 1300 ) ) );
    s->GetFields().at( SHEETNAME ).SetText( wxT( "SHEET" ) );
    s->GetFields().at( SHEETFILENAME ).SetText( _( "/path/to/sheet" ) );
    s->AutoplaceFields( nullptr, false );
    addItem( s );

    auto sp = new SCH_SHEET_PIN( s, wxPoint( Mils2iu( 4500 ), Mils2iu( 1500 ) ), wxT( "SHEET PIN" ) );
    addItem( sp );

    zoomFitPreview();
}


void PANEL_EESCHEMA_COLOR_SETTINGS::onColorChanged()
{
    updatePreview();
}


void PANEL_EESCHEMA_COLOR_SETTINGS::ResetPanel()
{
    PANEL_COLOR_SETTINGS::ResetPanel();
    updatePreview();
}


void PANEL_EESCHEMA_COLOR_SETTINGS::updatePreview()
{
    if( !m_preview )
        return;

    KIGFX::VIEW* view = m_preview->GetView();
    auto settings = static_cast<KIGFX::SCH_RENDER_SETTINGS*>( view->GetPainter()->GetSettings() );
    settings->LoadColors( m_currentSettings );

    m_preview->GetGAL()->SetClearColor( settings->GetBackgroundColor() );

    view->UpdateAllItems( KIGFX::COLOR );
    auto rect = m_preview->GetScreenRect();
    m_preview->Refresh( true, &rect );
}


void PANEL_EESCHEMA_COLOR_SETTINGS::zoomFitPreview()
{
    auto view = m_preview->GetView();

    view->SetScale( 1.0 );
    VECTOR2D screenSize = view->ToWorld( m_preview->GetClientSize(), false );

    VECTOR2I psize( m_page->GetWidthIU(), m_page->GetHeightIU() );
    double scale = view->GetScale() / std::max( fabs( psize.x / screenSize.x ),
                                                fabs( psize.y / screenSize.y ) );

    view->SetScale( scale * 1.1 );
    view->SetCenter( m_ws->ViewBBox().Centre() );
    m_preview->ForceRefresh();
}


void PANEL_EESCHEMA_COLOR_SETTINGS::OnSize( wxSizeEvent& aEvent )
{
    zoomFitPreview();
    aEvent.Skip();
}


void PANEL_EESCHEMA_COLOR_SETTINGS::OnOverrideItemColorsClicked( wxCommandEvent& aEvent )
{
    m_currentSettings->SetOverrideSchItemColors( m_optOverrideColors->GetValue() );

    // If the theme is not overriding individual item colors then don't show them so that
    // the user doesn't get seduced into thinking they'll have some effect.
    m_labels[ LAYER_SHEET ]->Show( m_currentSettings->GetOverrideSchItemColors() );
    m_swatches[ LAYER_SHEET ]->Show( m_currentSettings->GetOverrideSchItemColors() );

    m_labels[ LAYER_SHEET_BACKGROUND ]->Show( m_currentSettings->GetOverrideSchItemColors() );
    m_swatches[ LAYER_SHEET_BACKGROUND ]->Show( m_currentSettings->GetOverrideSchItemColors() );

    m_colorsGridSizer->Layout();
    m_colorsListWindow->Layout();
}
