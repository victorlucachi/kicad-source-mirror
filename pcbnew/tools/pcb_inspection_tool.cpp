/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2019-2020 KiCad Developers, see AUTHORS.txt for contributors.
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

#include <bitmaps.h>
#include <class_pcb_group.h>
#include <tool/tool_manager.h>
#include <tools/selection_tool.h>
#include <tools/pcbnew_picker_tool.h>
#include <tools/edit_tool.h>
#include <pcb_painter.h>
#include <connectivity/connectivity_data.h>
#include <profile.h>
#include <dialogs/wx_html_report_box.h>
#include <drc/drc_engine.h>
#include <dialogs/panel_setup_rules_base.h>
#include <dialogs/dialog_constraints_reporter.h>
#include "pcb_inspection_tool.h"


void DIALOG_INSPECTION_REPORTER::OnErrorLinkClicked( wxHtmlLinkEvent& event )
{
    m_frame->ShowBoardSetupDialog( _( "Rules" ) );
}


PCB_INSPECTION_TOOL::PCB_INSPECTION_TOOL() :
        PCB_TOOL_BASE( "pcbnew.InspectionTool" ),
        m_frame( nullptr )
{
    m_probingSchToPcb = false;
    m_lastNetcode = -1;
    m_dynamicData = nullptr;
}


class NET_CONTEXT_MENU : public ACTION_MENU
{
public:
    NET_CONTEXT_MENU() : ACTION_MENU( true )
    {
        SetIcon( ratsnest_xpm );
        SetTitle( _( "Net Tools" ) );

        Add( PCB_ACTIONS::showNet );
        Add( PCB_ACTIONS::hideNet );
        // Add( PCB_ACTIONS::highlightNet );
    }

private:
    ACTION_MENU* create() const override
    {
        return new NET_CONTEXT_MENU();
    }
};


bool PCB_INSPECTION_TOOL::Init()
{
    SELECTION_TOOL* selectionTool = m_toolMgr->GetTool<SELECTION_TOOL>();

    auto netSubMenu = std::make_shared<NET_CONTEXT_MENU>();
    netSubMenu->SetTool( this );

    static KICAD_T connectedTypes[] = { PCB_TRACE_T, PCB_VIA_T, PCB_ARC_T, PCB_PAD_T,
                                        PCB_ZONE_AREA_T, EOT };

    CONDITIONAL_MENU& menu = selectionTool->GetToolMenu().GetMenu();

    selectionTool->GetToolMenu().AddSubMenu( netSubMenu );

    menu.AddMenu( netSubMenu.get(), SELECTION_CONDITIONS::OnlyTypes( connectedTypes ), 200 );
    menu.AddItem( PCB_ACTIONS::inspectClearance, SELECTION_CONDITIONS::Count( 2 ), 200 );

    return true;
}


void PCB_INSPECTION_TOOL::Reset( RESET_REASON aReason )
{
    m_frame = getEditFrame<PCB_EDIT_FRAME>();
}


int PCB_INSPECTION_TOOL::ShowStatisticsDialog( const TOOL_EVENT& aEvent )
{
    DIALOG_BOARD_STATISTICS dialog( m_frame );
    dialog.ShowModal();
    return 0;
}


void PCB_INSPECTION_TOOL::reportZoneConnection( ZONE_CONTAINER* aZone, D_PAD* aPad, REPORTER* r )
{
    ENUM_MAP<ZONE_CONNECTION> connectionEnum = ENUM_MAP<ZONE_CONNECTION>::Instance();
    wxString                  source;
    ZONE_CONNECTION           connection = aZone->GetPadConnection( aPad, &source );

    r->Report( "" );

    r->Report( wxString::Format( _( "Zone connection type: %s." ),
                                 connectionEnum.ToString( aZone->GetPadConnection() ) ) );

    if( source != _( "zone" ) )
    {
        r->Report( wxString::Format( _( "Overridden by %s; connection type: %s." ),
                                     source,
                                     connectionEnum.ToString( connection ) ) );
    }

    // Resolve complex connection types into simple types
    if( connection == ZONE_CONNECTION::THT_THERMAL )
    {
        if( aPad->GetAttribute() == PAD_ATTRIB_PTH )
        {
            connection = ZONE_CONNECTION::THERMAL;
        }
        else
        {
            connection = ZONE_CONNECTION::FULL;
            r->Report( wxString::Format( _( "Pad is not a PTH pad; connection will be: %s." ),
                                         connectionEnum.ToString( ZONE_CONNECTION::FULL ) ) );
        }
    }

    r->Report( "" );

    // Process simple connection types
    if( connection == ZONE_CONNECTION::THERMAL )
    {
        int gap = aZone->GetThermalReliefGap();

        r->Report( wxString::Format( _( "Zone thermal relief: %s." ),
                                     StringFromValue( r->GetUnits(), gap, true ) ) );

        gap = aZone->GetThermalReliefGap( aPad, &source );

        if( source != _( "zone" ) )
        {
            r->Report( wxString::Format( _( "Overridden by %s; thermal relief: %s." ),
                                         source,
                                         StringFromValue( r->GetUnits(), gap, true ) ) );
        }
    }
    else if( connection == ZONE_CONNECTION::NONE )
    {
        int clearance = aZone->GetLocalClearance();

        r->Report( wxString::Format( _( "Zone clearance: %s." ),
                                     StringFromValue( r->GetUnits(), clearance, true ) ) );

        if( aZone->GetThermalReliefGap( aPad ) > clearance )
        {
            clearance = aZone->GetThermalReliefGap( aPad, &source );

            if( source != _( "zone" ) )
            {
                r->Report( wxString::Format( _( "Overridden by larger thermal relief from %s;"
                                                "clearance: %s." ),
                                             source,
                                             StringFromValue( r->GetUnits(), clearance, true ) ) );
            }
        }
    }
    else
    {
        r->Report( wxString::Format( _( "Clearance: %s." ),
                                     StringFromValue( r->GetUnits(), 0, true ) ) );
    }
}


void PCB_INSPECTION_TOOL::reportClearance( DRC_CONSTRAINT_TYPE_T aClearanceType,
                                           PCB_LAYER_ID aLayer, BOARD_ITEM* aA, BOARD_ITEM* aB,
                                           REPORTER* r )
{
    r->Report( "" );

    DRC_ENGINE drcEngine( m_frame->GetBoard(), &m_frame->GetBoard()->GetDesignSettings() );

    try
    {
        drcEngine.InitEngine( m_frame->GetDesignRulesPath() );
    }
    catch( PARSE_ERROR& pe )
    {
        r->Report( "" );
        r->Report( _( "Report incomplete: could not compile design rules.  " )
                   + "<a href='boardsetup'>" + _( "Show design rules." ) + "</a>" );
        return;
    }

    int clearance = 0;

    if( aClearanceType == CLEARANCE_CONSTRAINT )
    {
        auto edgeConstraint = drcEngine.EvalRulesForItems( EDGE_CLEARANCE_CONSTRAINT, aA, aB,
                                                           aLayer, r );

        clearance = edgeConstraint.m_Value.HasMin() ? edgeConstraint.m_Value.Min() : 0;
    }

    auto constraint = drcEngine.EvalRulesForItems( aClearanceType, aA, aB, aLayer, r );

    if( constraint.m_Value.HasMin() && constraint.m_Value.Min() > clearance )
        clearance = constraint.m_Value.Min();

    wxString clearanceStr = StringFromValue( r->GetUnits(), clearance, true );

    r->Report( "" );
    r->Report( wxString::Format( _( "Resolved clearance: %s." ), clearanceStr ) );
}


int PCB_INSPECTION_TOOL::InspectClearance( const TOOL_EVENT& aEvent )
{
    SELECTION_TOOL*         selTool = m_toolMgr->GetTool<SELECTION_TOOL>();
    const PCBNEW_SELECTION& selection = selTool->GetSelection();
    PCB_LAYER_ID            layer = m_frame->GetActiveLayer();

    if( selection.Size() != 2 )
    {
        m_frame->ShowInfoBarError( _( "Select two items for a clearance resolution report." ) );
        return 0;
    }

    if( m_inspectClearanceDialog == nullptr )
    {
        m_inspectClearanceDialog = std::make_unique<DIALOG_INSPECTION_REPORTER>( m_frame );
        m_inspectClearanceDialog->SetTitle( _( "Clearance Report" ) );

        m_inspectClearanceDialog->Connect( wxEVT_CLOSE_WINDOW,
                    wxCommandEventHandler( PCB_INSPECTION_TOOL::onInspectClearanceDialogClosed ),
                    nullptr, this );
    }

    WX_HTML_REPORT_BOX* r = m_inspectClearanceDialog->m_Reporter;
    r->SetUnits( m_frame->GetUserUnits() );
    r->Clear();

    BOARD_ITEM* a = static_cast<BOARD_ITEM*>( selection.GetItem( 0 ) );
    BOARD_ITEM* b = static_cast<BOARD_ITEM*>( selection.GetItem( 1 ) );

    if( a->Type() == PCB_TRACE_T || a->Type() == PCB_ARC_T )
        layer = a->GetLayer();
    else if( b->Type() == PCB_TRACE_T || b->Type() == PCB_ARC_T )
        layer = b->GetLayer();
    else if( a->Type() == PCB_PAD_T && static_cast<D_PAD*>( a )->GetAttribute() == PAD_ATTRIB_SMD )
    {
        D_PAD* pad = static_cast<D_PAD*>( a );

        if( pad->GetAttribute() == PAD_ATTRIB_SMD && pad->IsOnLayer( F_Cu ) )
            layer = F_Cu;
        else
            layer = B_Cu;
    }
    else if( b->Type() == PCB_PAD_T )
    {
        D_PAD* pad = static_cast<D_PAD*>( b );

        if( pad->GetAttribute() == PAD_ATTRIB_SMD && pad->IsOnLayer( F_Cu ) )
            layer = F_Cu;
        else
            layer = B_Cu;
    }

    if( a->Type() != PCB_ZONE_AREA_T && b->Type() == PCB_ZONE_AREA_T )
        std::swap( a, b );
    else if( !a->IsConnected() && b->IsConnected() )
        std::swap( a, b );

    auto getItemDescription =
            [&]( BOARD_ITEM* aItem )
            {
                wxString s = aItem->GetSelectMenuText( r->GetUnits() );

                if( auto* cItem = dynamic_cast<BOARD_CONNECTED_ITEM*>( aItem ) )
                    s += wxS( " " ) + wxString::Format( _( "[netclass %s]" ), cItem->GetNetClassName() );

                return s;
            };

    if( layer == F_SilkS || layer == B_SilkS )
    {
        r->Report( "<h7>" + _( "Silkscreen clearance resolution for:" ) + "</h7>" );

        r->Report( wxString::Format( "<ul><li>%s %s</li><li>%s</li><li>%s</li></ul>",
                                     _( "Layer" ),
                                     m_frame->GetBoard()->GetLayerName( layer ),
                                     getItemDescription( a ),
                                     getItemDescription( b ) ) );

        reportClearance( SILK_CLEARANCE_CONSTRAINT, layer, a, b, r );
    }
    else if( !( a->GetLayerSet() & LSET( 2, layer, Edge_Cuts ) ).any() )
    {
        r->Report( wxString::Format( _( "%s not present on layer %s.  No clearance defined." ),
                                     a->GetSelectMenuText( r->GetUnits() ),
                                     m_frame->GetBoard()->GetLayerName( layer ) ) );
    }
    else if( !( b->GetLayerSet() & LSET( 2, layer, Edge_Cuts ) ).any() )
    {
        r->Report( wxString::Format( _( "%s not present on layer %s.  No clearance defined." ),
                                     b->GetSelectMenuText( r->GetUnits() ),
                                     m_frame->GetBoard()->GetLayerName( layer ) ) );
    }
    else
    {
        r->Report( "<h7>" + _( "Clearance resolution for:" ) + "</h7>" );

        r->Report( wxString::Format( "<ul><li>%s %s</li><li>%s</li><li>%s</li></ul>",
                                     _( "Layer" ),
                                     m_frame->GetBoard()->GetLayerName( layer ),
                                     getItemDescription( a ),
                                     getItemDescription( b ) ) );

        BOARD_CONNECTED_ITEM* ac = dynamic_cast<BOARD_CONNECTED_ITEM*>( a );
        BOARD_CONNECTED_ITEM* bc = dynamic_cast<BOARD_CONNECTED_ITEM*>( b );

        if( ac && bc && ac->GetNetCode() > 0 && ac->GetNetCode() == bc->GetNetCode() )
        {
            // Same nets....

            if( ac->Type() == PCB_ZONE_AREA_T && bc->Type() == PCB_PAD_T )
            {
                reportZoneConnection( static_cast<ZONE_CONTAINER*>( ac ),
                                      static_cast<D_PAD*>( bc ), r );
            }
            else
            {
                r->Report( _( "Items belong to the same net. Clearance is 0." ) );
            }
        }
        else
        {
            // Different nets (or one or both unconnected)....
            reportClearance( CLEARANCE_CONSTRAINT, layer, a, b, r );
        }
    }

    r->Flush();

    m_inspectClearanceDialog->Raise();
    m_inspectClearanceDialog->Show( true );
    return 0;
}


int PCB_INSPECTION_TOOL::InspectConstraints( const TOOL_EVENT& aEvent )
{
    SELECTION_TOOL*         selTool = m_toolMgr->GetTool<SELECTION_TOOL>();
    const PCBNEW_SELECTION& selection = selTool->GetSelection();

    if( selection.Size() != 1 )
    {
        m_frame->ShowInfoBarError( _( "Select an item for a constraints resolution report." ) );
        return 0;
    }

    if( m_inspectConstraintsDialog == nullptr )
    {
        m_inspectConstraintsDialog = std::make_unique<DIALOG_CONSTRAINTS_REPORTER>( m_frame );
        m_inspectConstraintsDialog->SetTitle( _( "Constraints Report" ) );

        m_inspectConstraintsDialog->Connect( wxEVT_CLOSE_WINDOW,
                    wxCommandEventHandler( PCB_INSPECTION_TOOL::onInspectConstraintsDialogClosed ),
                    nullptr, this );
    }

    m_inspectConstraintsDialog->DeleteAllPages();

    BOARD_ITEM* item = static_cast<BOARD_ITEM*>( selection.GetItem( 0 ) );
    DRC_ENGINE  drcEngine( m_frame->GetBoard(), &m_frame->GetBoard()->GetDesignSettings() );
    bool        compileError = false;

    try
    {
        drcEngine.InitEngine( m_frame->GetDesignRulesPath() );
    }
    catch( PARSE_ERROR& pe )
    {
        compileError = true;
    }

    for( ZONE_CONTAINER* zone : m_frame->GetBoard()->Zones() )
        zone->CacheBoundingBox();

    for( MODULE* module : m_frame->GetBoard()->Modules() )
    {
        for( ZONE_CONTAINER* zone : module->Zones() )
            zone->CacheBoundingBox();

        module->BuildPolyCourtyards();
    }

    if( item->Type() == PCB_TRACE_T )
    {
        WX_HTML_REPORT_BOX* r = m_inspectConstraintsDialog->AddPage( _( "Track Width" ) );

        r->Report( "<h7>" + _( "Track width resolution for:" ) + "</h7>" );
        r->Report( "<ul><li>" + item->GetSelectMenuText( r->GetUnits() ) + "</li></ul>" );
        r->Report( "" );

        if( compileError )
        {
            r->Report( "" );
            r->Report( _( "Report incomplete: could not compile design rules.  " )
                       + "<a href='boardsetup'>" + _( "Show design rules." ) + "</a>" );
        }
        else
        {
            auto constraint = drcEngine.EvalRulesForItems( TRACK_WIDTH_CONSTRAINT, item, nullptr,
                                                           UNDEFINED_LAYER, r );

            wxString min = _( "undefined" );
            wxString max = _( "undefined" );

            if( constraint.m_Value.HasMin() )
                min = StringFromValue( r->GetUnits(), constraint.m_Value.Min(), true );

            if( constraint.m_Value.HasMax() )
                max = StringFromValue( r->GetUnits(), constraint.m_Value.Max(), true );

            r->Report( "" );
            r->Report( wxString::Format( _( "Width constraints: min %s max %s." ),
                                         min,
                                         max ) );
        }

        r->Flush();
    }

    if( item->Type() == PCB_VIA_T )
    {
        WX_HTML_REPORT_BOX* r = m_inspectConstraintsDialog->AddPage( _( "Via Diameter" ) );

        r->Report( "<h7>" + _( "Via diameter resolution for:" ) + "</h7>" );
        r->Report( "<ul><li>" + item->GetSelectMenuText( r->GetUnits() ) + "</li></ul>" );
        r->Report( "" );

        if( compileError )
        {
            r->Report( "" );
            r->Report( _( "Report incomplete: could not compile design rules.  " )
                       + "<a href='boardsetup'>" + _( "Show design rules." ) + "</a>" );
        }
        else
        {
            auto constraint = drcEngine.EvalRulesForItems( VIA_DIAMETER_CONSTRAINT, item, nullptr,
                                                           UNDEFINED_LAYER, r );

            wxString min = _( "undefined" );
            wxString max = _( "undefined" );

            if( constraint.m_Value.HasMin() )
                min = StringFromValue( r->GetUnits(), constraint.m_Value.Min(), true );

            if( constraint.m_Value.HasMax() )
                max = StringFromValue( r->GetUnits(), constraint.m_Value.Max(), true );

            r->Report( "" );
            r->Report( wxString::Format( _( "Diameter constraints: min %s max %s." ),
                                         min,
                                         max ) );
        }

        r->Flush();

        r = m_inspectConstraintsDialog->AddPage( _( "Via Annular Width" ) );

        r->Report( "<h7>" + _( "Via annular width resolution for:" ) + "</h7>" );
        r->Report( "<ul><li>" + item->GetSelectMenuText( r->GetUnits() ) + "</li></ul>" );
        r->Report( "" );

        if( compileError )
        {
            r->Report( "" );
            r->Report( _( "Report incomplete: could not compile design rules.  " )
                       + "<a href='boardsetup'>" + _( "Show design rules." ) + "</a>" );
        }
        else
        {
            auto constraint = drcEngine.EvalRulesForItems( ANNULAR_WIDTH_CONSTRAINT, item, nullptr,
                                                           UNDEFINED_LAYER, r );

            wxString min = _( "undefined" );
            wxString max = _( "undefined" );

            if( constraint.m_Value.HasMin() )
                min = StringFromValue( r->GetUnits(), constraint.m_Value.Min(), true );

            if( constraint.m_Value.HasMax() )
                max = StringFromValue( r->GetUnits(), constraint.m_Value.Max(), true );

            r->Report( "" );
            r->Report( wxString::Format( _( "Annular width constraints: min %s max %s." ),
                                         min,
                                         max ) );
        }

        r->Flush();
    }

    if( ( item->Type() == PCB_PAD_T && static_cast<D_PAD*>( item )->GetDrillSize().x > 0 )
            || item->Type() == PCB_VIA_T )
    {
        WX_HTML_REPORT_BOX* r = m_inspectConstraintsDialog->AddPage( _( "Hole Size" ) );

        r->Report( "<h7>" + _( "Hole diameter resolution for:" ) + "</h7>" );
        r->Report( "<ul><li>" + item->GetSelectMenuText( r->GetUnits() ) + "</li></ul>" );
        r->Report( "" );

        if( compileError )
        {
            r->Report( "" );
            r->Report( _( "Report incomplete: could not compile design rules.  " )
                       + "<a href='boardsetup'>" + _( "Show design rules." ) + "</a>" );
        }
        else
        {
            auto constraint = drcEngine.EvalRulesForItems( HOLE_SIZE_CONSTRAINT, item, nullptr,
                                                           UNDEFINED_LAYER, r );

            wxString min = _( "undefined" );

            if( constraint.m_Value.HasMin() )
                min = StringFromValue( r->GetUnits(), constraint.m_Value.Min(), true );

            r->Report( "" );
            r->Report( wxString::Format( _( "Hole constraint: min %s." ), min ) );
        }

        r->Flush();
    }

    WX_HTML_REPORT_BOX* r = m_inspectConstraintsDialog->AddPage( _( "Keepouts" ) );

    r->Report( "<h7>" + _( "Keepout resolution for:" ) + "</h7>" );
    r->Report( "<ul><li>" + item->GetSelectMenuText( r->GetUnits() ) + "</li></ul>" );
    r->Report( "" );

    if( compileError )
    {
        r->Report( "" );
        r->Report( _( "Report incomplete: could not compile design rules.  " )
                   + "<a href='boardsetup'>" + _( "Show design rules." ) + "</a>" );
    }
    else
    {
        auto constraint = drcEngine.EvalRulesForItems( DISALLOW_CONSTRAINT, item, nullptr,
                                                       UNDEFINED_LAYER, r );

        r->Report( "" );

        if( constraint.m_DisallowFlags )
            r->Report( _( "Item <b>disallowed</b> at current location." ) );
        else
            r->Report( _( "Item allowed at current location." ) );
    }

    r->Flush();

    m_inspectConstraintsDialog->FinishInitialization();
    m_inspectConstraintsDialog->Raise();
    m_inspectConstraintsDialog->Show( true );
    return 0;
}


int PCB_INSPECTION_TOOL::CrossProbePcbToSch( const TOOL_EVENT& aEvent )
{
    // Don't get in an infinite loop PCB -> SCH -> PCB -> SCH -> ...
    if( m_probingSchToPcb )
        return 0;

    SELECTION_TOOL*         selTool = m_toolMgr->GetTool<SELECTION_TOOL>();
    const PCBNEW_SELECTION& selection = selTool->GetSelection();

    if( selection.Size() == 1 )
        m_frame->SendMessageToEESCHEMA( static_cast<BOARD_ITEM*>( selection.Front() ) );
    else
        m_frame->SendMessageToEESCHEMA( nullptr );

    m_frame->Redraw3Dview();

    return 0;
}


int PCB_INSPECTION_TOOL::HighlightItem( const TOOL_EVENT& aEvent )
{
    BOARD_ITEM* item = aEvent.Parameter<BOARD_ITEM*>();

    m_probingSchToPcb = true;   // recursion guard
    {
        m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );

        if( item )
            m_toolMgr->RunAction( PCB_ACTIONS::selectItem, true, (void*) item );
    }
    m_probingSchToPcb = false;

    bool request3DviewRedraw = true;

    if( item )
        if( item->Type() != PCB_MODULE_T )
            request3DviewRedraw = false;

    if( request3DviewRedraw )
        m_frame->Redraw3Dview();

    return 0;
}


/**
 * Look for a BOARD_CONNECTED_ITEM in a given spot and if one is found - it enables
 * highlight for its net.
 *
 * @param aPosition is the point where an item is expected (world coordinates).
 * @param aUseSelection is true if we should use the current selection to pick the netcode
 */
 bool PCB_INSPECTION_TOOL::highlightNet( const VECTOR2D& aPosition, bool aUseSelection )
{
    BOARD*                  board         = static_cast<BOARD*>( m_toolMgr->GetModel() );
    KIGFX::RENDER_SETTINGS* settings      = getView()->GetPainter()->GetSettings();
    SELECTION_TOOL*         selectionTool = m_toolMgr->GetTool<SELECTION_TOOL>();

    int net = -1;
    bool enableHighlight = false;

    if( aUseSelection )
    {
        const PCBNEW_SELECTION& selection = selectionTool->GetSelection();

        for( auto item : selection )
        {
            if( auto ci = dyn_cast<BOARD_CONNECTED_ITEM*>( item ) )
            {
                int item_net = ci->GetNetCode();

                if( net < 0 )
                    net = item_net;
                else if( net != item_net )  // more than one net selected: do nothing
                    return false;
            }
        }

        enableHighlight = ( net >= 0 && !settings->GetHighlightNetCodes().count( net ) );
    }

    // If we didn't get a net to highlight from the selection, use the cursor
    if( net < 0 )
    {
        GENERAL_COLLECTORS_GUIDE guide = m_frame->GetCollectorsGuide();
        GENERAL_COLLECTOR collector;

        PCB_LAYER_ID activeLayer = static_cast<PCB_LAYER_ID>( view()->GetTopLayer() );
        guide.SetPreferredLayer( activeLayer );

        // Find a connected item for which we are going to highlight a net
        collector.Collect( board, GENERAL_COLLECTOR::PadsOrTracks, (wxPoint) aPosition, guide );

        if( collector.GetCount() == 0 )
            collector.Collect( board, GENERAL_COLLECTOR::Zones, (wxPoint) aPosition, guide );

        // Apply the active selection filter
        selectionTool->FilterCollectedItems( collector );

        // Clear the previous highlight
        m_frame->SendMessageToEESCHEMA( nullptr );

        bool         highContrast  = settings->GetHighContrast();
        PCB_LAYER_ID contrastLayer = settings->GetPrimaryHighContrastLayer();

        for( int i = collector.GetCount() - 1; i >= 0; i-- )
        {
            LSET itemLayers = collector[i]->GetLayerSet();

            if( ( itemLayers & LSET::AllCuMask() ).none() ||
                ( highContrast && !itemLayers.Contains( contrastLayer ) ) )
            {
                collector.Remove( i );
                continue;
            }
        }

        enableHighlight = ( collector.GetCount() > 0 );

        // Obtain net code for the clicked item
        if( enableHighlight )
        {
            BOARD_CONNECTED_ITEM* targetItem = static_cast<BOARD_CONNECTED_ITEM*>( collector[0] );

            if( targetItem->Type() == PCB_PAD_T )
                m_frame->SendMessageToEESCHEMA( targetItem );

            net = targetItem->GetNetCode();
        }
    }

    auto& netcodes = settings->GetHighlightNetCodes();

    // Toggle highlight when the same net was picked
    if( net > 0 && netcodes.count( net ) )
        enableHighlight = !settings->IsHighlightEnabled();

    if( enableHighlight != settings->IsHighlightEnabled()
            || !netcodes.count( net ) )
    {
        if( !netcodes.empty() )
            m_lastNetcode = *netcodes.begin();

        settings->SetHighlight( enableHighlight, net );
        m_toolMgr->GetView()->UpdateAllLayersColor();
    }

    // Store the highlighted netcode in the current board (for dialogs for instance)
    if( enableHighlight && net >= 0 )
    {
        board->SetHighLightNet( net );
        board->HighLightON();

        NETINFO_ITEM* netinfo = board->FindNet( net );

        if( netinfo )
        {
            MSG_PANEL_ITEMS items;
            netinfo->GetMsgPanelInfo( m_frame, items );
            m_frame->SetMsgPanel( items );
            m_frame->SendCrossProbeNetName( netinfo->GetNetname() );
        }
    }
    else
    {
        board->ResetNetHighLight();
        m_frame->SetMsgPanel( board );
        m_frame->SendCrossProbeNetName( "" );
    }

    return true;
}


int PCB_INSPECTION_TOOL::HighlightNet( const TOOL_EVENT& aEvent )
{
    int                     netcode     = aEvent.Parameter<intptr_t>();
    KIGFX::RENDER_SETTINGS* settings    = m_toolMgr->GetView()->GetPainter()->GetSettings();
    const std::set<int>&    highlighted = settings->GetHighlightNetCodes();

    if( netcode > 0 )
    {
        m_lastNetcode = highlighted.empty() ? -1 : *highlighted.begin();
        settings->SetHighlight( true, netcode );
        m_toolMgr->GetView()->UpdateAllLayersColor();
    }
    else if( aEvent.IsAction( &PCB_ACTIONS::toggleLastNetHighlight ) )
    {
        int temp = highlighted.empty() ? -1 : *highlighted.begin();
        settings->SetHighlight( true, m_lastNetcode );
        m_toolMgr->GetView()->UpdateAllLayersColor();
        m_lastNetcode = temp;
    }
    else    // Highlight the net belonging to the item under the cursor
    {
        highlightNet( getViewControls()->GetMousePosition(), false );
    }

    return 0;
}


int PCB_INSPECTION_TOOL::ClearHighlight( const TOOL_EVENT& aEvent )
{
    BOARD*                  board = static_cast<BOARD*>( m_toolMgr->GetModel() );
    KIGFX::RENDER_SETTINGS* settings = m_toolMgr->GetView()->GetPainter()->GetSettings();

    board->ResetNetHighLight();
    settings->SetHighlight( false );
    m_toolMgr->GetView()->UpdateAllLayersColor();
    m_frame->SetMsgPanel( board );
    m_frame->SendCrossProbeNetName( "" );
    return 0;
}


int PCB_INSPECTION_TOOL::HighlightNetTool( const TOOL_EVENT& aEvent )
{
    std::string         tool = aEvent.GetCommandStr().get();
    PCBNEW_PICKER_TOOL* picker = m_toolMgr->GetTool<PCBNEW_PICKER_TOOL>();

    // Deactivate other tools; particularly important if another PICKER is currently running
    Activate();

    // If the keyboard hotkey was triggered and we are already in the highlight tool, behave
    // the same as a left-click.  Otherwise highlight the net of the selected item(s), or if
    // there is no selection, then behave like a ctrl-left-click.
    if( aEvent.IsAction( &PCB_ACTIONS::highlightNetSelection ) )
    {
        bool use_selection = m_frame->IsCurrentTool( PCB_ACTIONS::highlightNetTool );
        highlightNet( getViewControls()->GetMousePosition(), use_selection );
    }

    picker->SetClickHandler(
        [this] ( const VECTOR2D& pt ) -> bool
        {
            highlightNet( pt, false );
            return true;
        } );

    picker->SetLayerSet( LSET::AllCuMask() );

    m_toolMgr->RunAction( ACTIONS::pickerTool, true, &tool );

    return 0;
}


int PCB_INSPECTION_TOOL::LocalRatsnestTool( const TOOL_EVENT& aEvent )
{
    std::string          tool = aEvent.GetCommandStr().get();
    PCBNEW_PICKER_TOOL*  picker = m_toolMgr->GetTool<PCBNEW_PICKER_TOOL>();
    BOARD*               board = getModel<BOARD>();
    auto&                opt = displayOptions();

    // Deactivate other tools; particularly important if another PICKER is currently running
    Activate();

    picker->SetClickHandler(
        [this, board, opt]( const VECTOR2D& pt ) -> bool
        {
            SELECTION_TOOL* selectionTool = m_toolMgr->GetTool<SELECTION_TOOL>();

            m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );
            m_toolMgr->RunAction( PCB_ACTIONS::selectionCursor, true, EDIT_TOOL::PadFilter );
            PCBNEW_SELECTION& selection = selectionTool->GetSelection();

            if( selection.Empty() )
            {
                m_toolMgr->RunAction( PCB_ACTIONS::selectionCursor, true,
                                      EDIT_TOOL::FootprintFilter );
                selection = selectionTool->GetSelection();
            }

            if( selection.Empty() )
            {
                // Clear the previous local ratsnest if we click off all items
                for( MODULE* fp : board->Modules() )
                {
                    for( D_PAD* pad : fp->Pads() )
                        pad->SetLocalRatsnestVisible( opt.m_ShowGlobalRatsnest );
                }
            }
            else
            {
                for( EDA_ITEM* item : selection )
                {
                    if( D_PAD* pad = dyn_cast<D_PAD*>(item) )
                    {
                        pad->SetLocalRatsnestVisible( !pad->GetLocalRatsnestVisible() );
                    }
                    else if( MODULE* fp = dyn_cast<MODULE*>( item) )
                    {
                        if( !fp->Pads().empty() )
                        {
                            bool enable = !fp->Pads()[0]->GetLocalRatsnestVisible();

                            for( D_PAD* childPad : fp->Pads() )
                                childPad->SetLocalRatsnestVisible( enable );
                        }
                    }
                }
            }

            m_toolMgr->GetView()->MarkTargetDirty( KIGFX::TARGET_OVERLAY );

            return true;
        } );

    picker->SetFinalizeHandler(
        [board, opt] ( int aCondition )
        {
            if( aCondition != PCBNEW_PICKER_TOOL::END_ACTIVATE )
            {
                for( MODULE* fp : board->Modules() )
                {
                    for( D_PAD* pad : fp->Pads() )
                        pad->SetLocalRatsnestVisible( opt.m_ShowGlobalRatsnest );
                }
            }
        } );

    m_toolMgr->RunAction( ACTIONS::pickerTool, true, &tool );

    return 0;
}


int PCB_INSPECTION_TOOL::UpdateSelectionRatsnest( const TOOL_EVENT& aEvent )
{
    VECTOR2I  delta;

    // If we have passed the simple move vector, we can update without recalculation
    if( aEvent.Parameter<VECTOR2I*>() )
    {
        delta = *aEvent.Parameter<VECTOR2I*>();
        delete aEvent.Parameter<VECTOR2I*>();
    }
    else
    {
        // We can delete the existing map to force a recalculation
        delete m_dynamicData;
        m_dynamicData = nullptr;
    }

    auto selectionTool = m_toolMgr->GetTool<SELECTION_TOOL>();
    auto& selection = selectionTool->GetSelection();
    auto connectivity = getModel<BOARD>()->GetConnectivity();

    if( selection.Empty() )
    {
        connectivity->ClearDynamicRatsnest();
        delete m_dynamicData;
        m_dynamicData = nullptr;
    }
    else
    {
        calculateSelectionRatsnest( delta );
    }

    return 0;
}


int PCB_INSPECTION_TOOL::HideDynamicRatsnest( const TOOL_EVENT& aEvent )
{
    getModel<BOARD>()->GetConnectivity()->ClearDynamicRatsnest();
    delete m_dynamicData;
    m_dynamicData = nullptr;

    return 0;
}


void PCB_INSPECTION_TOOL::calculateSelectionRatsnest( const VECTOR2I& aDelta )
{
    SELECTION_TOOL* selectionTool = m_toolMgr->GetTool<SELECTION_TOOL>();
    SELECTION& selection = selectionTool->GetSelection();
    std::shared_ptr<CONNECTIVITY_DATA> connectivity = board()->GetConnectivity();
    std::vector<BOARD_ITEM*> items;
    std::deque<EDA_ITEM*> queued_items( selection.begin(), selection.end() );

    for( std::size_t i = 0; i < queued_items.size(); ++i )
    {
        BOARD_ITEM* item = static_cast<BOARD_ITEM*>( queued_items[i] );

        if( item->Type() == PCB_MODULE_T )
        {
            for( D_PAD* pad : static_cast<MODULE*>( item )->Pads() )
            {
                if( pad->GetLocalRatsnestVisible() || displayOptions().m_ShowModuleRatsnest )
                    items.push_back( pad );
            }
        }
        else if( item->Type() == PCB_GROUP_T )
        {
            PCB_GROUP *group = static_cast<PCB_GROUP*>( item );
            group->RunOnDescendants( [ &queued_items ]( BOARD_ITEM *aItem )
                                     {
                                         queued_items.push_back( aItem );
                                     } );
        }
        else if( BOARD_CONNECTED_ITEM* boardItem = dyn_cast<BOARD_CONNECTED_ITEM*>( item ) )
        {
            if( boardItem->GetLocalRatsnestVisible() || displayOptions().m_ShowModuleRatsnest )
                items.push_back( boardItem );
        }
    }

    if( items.empty() || std::none_of( items.begin(), items.end(),
                                       []( const BOARD_ITEM* aItem )
                                       {
                                           return( aItem->Type() == PCB_TRACE_T
                                                    || aItem->Type() == PCB_PAD_T
                                                    || aItem->Type() == PCB_ARC_T
                                                    || aItem->Type() == PCB_ZONE_AREA_T
                                                    || aItem->Type() == PCB_MODULE_T
                                                    || aItem->Type() == PCB_VIA_T );
                                       } ) )
    {
        return;
    }

    if( !m_dynamicData )
    {
        m_dynamicData = new CONNECTIVITY_DATA( items, true );
        connectivity->BlockRatsnestItems( items );
    }
    else
    {
        m_dynamicData->Move( aDelta );
    }

    connectivity->ComputeDynamicRatsnest( items, m_dynamicData );
}


int PCB_INSPECTION_TOOL::ListNets( const TOOL_EVENT& aEvent )
{
    if( m_listNetsDialog == nullptr )
    {
        m_listNetsDialog =
                std::make_unique<DIALOG_NET_INSPECTOR>( m_frame, m_listNetsDialogSettings );

        m_listNetsDialog->Connect( wxEVT_CLOSE_WINDOW,
                wxCommandEventHandler( PCB_INSPECTION_TOOL::onListNetsDialogClosed ), nullptr,
                this );

        m_listNetsDialog->Connect( wxEVT_BUTTON,
                wxCommandEventHandler( PCB_INSPECTION_TOOL::onListNetsDialogClosed ), nullptr,
                this );
    }

    m_listNetsDialog->Raise();
    m_listNetsDialog->Show( true );
    return 0;
}


void PCB_INSPECTION_TOOL::onListNetsDialogClosed( wxCommandEvent& event )
{
    m_listNetsDialogSettings = m_listNetsDialog->Settings();

    m_listNetsDialog->Disconnect( wxEVT_CLOSE_WINDOW,
            wxCommandEventHandler( PCB_INSPECTION_TOOL::onListNetsDialogClosed ), nullptr, this );

    m_listNetsDialog->Disconnect( wxEVT_BUTTON,
            wxCommandEventHandler( PCB_INSPECTION_TOOL::onListNetsDialogClosed ), nullptr, this );

    m_listNetsDialog->Destroy();
    m_listNetsDialog.release();
}


void PCB_INSPECTION_TOOL::onInspectClearanceDialogClosed( wxCommandEvent& event )
{
    m_inspectClearanceDialog->Disconnect( wxEVT_CLOSE_WINDOW,
            wxCommandEventHandler( PCB_INSPECTION_TOOL::onInspectClearanceDialogClosed ), nullptr, this );

    m_inspectClearanceDialog->Destroy();
    m_inspectClearanceDialog.release();
}


void PCB_INSPECTION_TOOL::onInspectConstraintsDialogClosed( wxCommandEvent& event )
{
    m_inspectConstraintsDialog->Disconnect( wxEVT_CLOSE_WINDOW,
            wxCommandEventHandler( PCB_INSPECTION_TOOL::onInspectConstraintsDialogClosed ), nullptr, this );

    m_inspectConstraintsDialog->Destroy();
    m_inspectConstraintsDialog.release();
}


int PCB_INSPECTION_TOOL::HideNet( const TOOL_EVENT& aEvent )
{
    doHideNet( aEvent.Parameter<intptr_t>(), true );
    return 0;
}


int PCB_INSPECTION_TOOL::ShowNet( const TOOL_EVENT& aEvent )
{
    doHideNet( aEvent.Parameter<intptr_t>(), false );
    return 0;
}


void PCB_INSPECTION_TOOL::doHideNet( int aNetCode, bool aHide )
{
    KIGFX::PCB_RENDER_SETTINGS* rs = static_cast<KIGFX::PCB_RENDER_SETTINGS*>(
            m_toolMgr->GetView()->GetPainter()->GetSettings() );

    SELECTION_TOOL* selectionTool = m_toolMgr->GetTool<SELECTION_TOOL>();
    SELECTION&      selection     = selectionTool->GetSelection();

    if( aNetCode <= 0 && !selection.Empty() )
    {
        for( EDA_ITEM* item : selection )
        {
            if( BOARD_CONNECTED_ITEM* bci = dynamic_cast<BOARD_CONNECTED_ITEM*>( item ) )
            {
                if( bci->GetNetCode() > 0 )
                    doHideNet( bci->GetNetCode(), aHide );
            }
        }

        return;
    }

    if( aHide )
        rs->GetHiddenNets().insert( aNetCode );
    else
        rs->GetHiddenNets().erase( aNetCode );

    m_frame->GetCanvas()->RedrawRatsnest();
    m_frame->GetCanvas()->Refresh();
}


void PCB_INSPECTION_TOOL::setTransitions()
{
    Go( &PCB_INSPECTION_TOOL::CrossProbePcbToSch,     EVENTS::SelectedEvent );
    Go( &PCB_INSPECTION_TOOL::CrossProbePcbToSch,     EVENTS::UnselectedEvent );
    Go( &PCB_INSPECTION_TOOL::CrossProbePcbToSch,     EVENTS::ClearedEvent );

    Go( &PCB_INSPECTION_TOOL::LocalRatsnestTool,      PCB_ACTIONS::localRatsnestTool.MakeEvent() );
    Go( &PCB_INSPECTION_TOOL::HideDynamicRatsnest,    PCB_ACTIONS::hideDynamicRatsnest.MakeEvent() );
    Go( &PCB_INSPECTION_TOOL::UpdateSelectionRatsnest,PCB_ACTIONS::updateLocalRatsnest.MakeEvent() );

    Go( &PCB_INSPECTION_TOOL::ListNets,               PCB_ACTIONS::listNets.MakeEvent() );
    Go( &PCB_INSPECTION_TOOL::ShowStatisticsDialog,   PCB_ACTIONS::boardStatistics.MakeEvent() );
    Go( &PCB_INSPECTION_TOOL::InspectClearance,       PCB_ACTIONS::inspectClearance.MakeEvent() );
    Go( &PCB_INSPECTION_TOOL::InspectConstraints,     PCB_ACTIONS::inspectConstraints.MakeEvent() );

    Go( &PCB_INSPECTION_TOOL::HighlightNet,           PCB_ACTIONS::highlightNet.MakeEvent() );
    Go( &PCB_INSPECTION_TOOL::HighlightNet,           PCB_ACTIONS::highlightNetSelection.MakeEvent() );
    Go( &PCB_INSPECTION_TOOL::HighlightNet,           PCB_ACTIONS::toggleLastNetHighlight.MakeEvent() );
    Go( &PCB_INSPECTION_TOOL::ClearHighlight,         PCB_ACTIONS::clearHighlight.MakeEvent() );
    Go( &PCB_INSPECTION_TOOL::HighlightNetTool,       PCB_ACTIONS::highlightNetTool.MakeEvent() );
    Go( &PCB_INSPECTION_TOOL::ClearHighlight,         ACTIONS::cancelInteractive.MakeEvent() );
    Go( &PCB_INSPECTION_TOOL::HighlightItem,          PCB_ACTIONS::highlightItem.MakeEvent() );

    Go( &PCB_INSPECTION_TOOL::HideNet,                PCB_ACTIONS::hideNet.MakeEvent() );
    Go( &PCB_INSPECTION_TOOL::ShowNet,                PCB_ACTIONS::showNet.MakeEvent() );
}
