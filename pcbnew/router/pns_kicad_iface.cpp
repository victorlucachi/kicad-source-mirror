/*
 * KiRouter - a push-and-(sometimes-)shove PCB router
 *
 * Copyright (C) 2013-2016 CERN
 * Copyright (C) 2016-2020 KiCad Developers, see AUTHORS.txt for contributors.
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
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

#include <undo_redo_container.h>
#include <class_board.h>
#include <board_connected_item.h>
#include <fp_text.h>
#include <class_module.h>
#include <class_track.h>
#include <class_zone.h>
#include <pcb_shape.h>
#include <pcb_text.h>
#include <board_commit.h>
#include <layers_id_colors_and_visibility.h>
#include <geometry/convex_hull.h>
#include <confirm.h>

#include <view/view.h>
#include <view/view_item.h>
#include <view/view_group.h>

#include <pcb_painter.h>

#include <geometry/shape.h>
#include <geometry/shape_line_chain.h>
#include <geometry/shape_arc.h>
#include <geometry/shape_simple.h>

#include <drc/drc_rule.h>
#include <drc/drc_engine.h>

#include <memory>

#include <advanced_config.h>

#include "tools/pcb_tool_base.h"

#include "pns_kicad_iface.h"

#include "pns_arc.h"
#include "pns_routing_settings.h"
#include "pns_sizes_settings.h"
#include "pns_item.h"
#include "pns_solid.h"
#include "pns_segment.h"
#include "pns_node.h"
#include "pns_router.h"
#include "pns_debug_decorator.h"
#include "router_preview_item.h"

typedef VECTOR2I::extended_type ecoord;

class PNS_PCBNEW_RULE_RESOLVER : public PNS::RULE_RESOLVER
{
public:
    PNS_PCBNEW_RULE_RESOLVER( BOARD* aBoard, PNS::ROUTER_IFACE* aRouterIface );
    virtual ~PNS_PCBNEW_RULE_RESOLVER();

    virtual bool CollideHoles( const PNS::ITEM* aA, const PNS::ITEM* aB,
                               bool aNeedMTV, VECTOR2I* aMTV ) const override;

    virtual int Clearance( const PNS::ITEM* aA, const PNS::ITEM* aB ) override;
    virtual int DpCoupledNet( int aNet ) override;
    virtual int DpNetPolarity( int aNet ) override;
    virtual bool DpNetPair( const PNS::ITEM* aItem, int& aNetP, int& aNetN ) override;
    virtual bool IsDiffPair( const PNS::ITEM* aA, const PNS::ITEM* aB ) override;

    virtual bool QueryConstraint( PNS::CONSTRAINT_TYPE aType, const PNS::ITEM* aItemA, const PNS::ITEM* aItemB, int aLayer, PNS::CONSTRAINT* aConstraint ) override;
    virtual wxString NetName( int aNet ) override;

private:
    int holeRadius( const PNS::ITEM* aItem ) const;
    int matchDpSuffix( const wxString& aNetName, wxString& aComplementNet, wxString& aBaseDpName );

private:
    PNS::ROUTER_IFACE* m_routerIface;
    BOARD*             m_board;
    TRACK              m_dummyTrack;
    ARC                m_dummyArc;
    VIA                m_dummyVia;

    std::map<std::pair<const PNS::ITEM*, const PNS::ITEM*>, int> m_clearanceCache;
};


PNS_PCBNEW_RULE_RESOLVER::PNS_PCBNEW_RULE_RESOLVER( BOARD* aBoard,
                                                    PNS::ROUTER_IFACE* aRouterIface ) :
    m_routerIface( aRouterIface ),
    m_board( aBoard ),
    m_dummyTrack( aBoard ),
    m_dummyArc( aBoard ),
    m_dummyVia( aBoard )
{
}


PNS_PCBNEW_RULE_RESOLVER::~PNS_PCBNEW_RULE_RESOLVER()
{
}


int PNS_PCBNEW_RULE_RESOLVER::holeRadius( const PNS::ITEM* aItem ) const
{
    if( aItem->Kind() == PNS::ITEM::SOLID_T )
    {
        const D_PAD* pad = dynamic_cast<const D_PAD*>( aItem->Parent() );

        if( pad && pad->GetDrillShape() == PAD_DRILL_SHAPE_CIRCLE )
            return pad->GetDrillSize().x / 2;
    }
    else if( aItem->Kind() == PNS::ITEM::VIA_T )
    {
        const ::VIA* via = dynamic_cast<const ::VIA*>( aItem->Parent() );

        if( via )
            return via->GetDrillValue() / 2;
    }

    return 0;
}


bool PNS_PCBNEW_RULE_RESOLVER::CollideHoles( const PNS::ITEM* aA, const PNS::ITEM* aB,
                                             bool aNeedMTV, VECTOR2I* aMTV ) const
{
    VECTOR2I pos_a = aA->Shape()->Centre();
    VECTOR2I pos_b = aB->Shape()->Centre();

    // Holes with identical locations are allowable
    if( pos_a == pos_b )
        return false;

    int radius_a = holeRadius( aA );
    int radius_b = holeRadius( aB );

    // Do both objects have holes?
    if( radius_a > 0 && radius_b > 0 )
    {
        int holeToHoleMin = m_board->GetDesignSettings().m_HoleToHoleMin;

        ecoord min_dist = holeToHoleMin + radius_a + radius_b;
        ecoord min_dist_sq = min_dist * min_dist;

        const VECTOR2I delta = pos_b - pos_a;

        ecoord dist_sq = delta.SquaredEuclideanNorm();

        if( dist_sq == 0 || dist_sq < min_dist_sq )
        {
            if( aNeedMTV )
                *aMTV = delta.Resize( min_dist - sqrt( dist_sq ) + 3 );  // fixme: apparent rounding error

            return true;
        }
    }

    return false;
}


bool PNS_PCBNEW_RULE_RESOLVER::IsDiffPair( const PNS::ITEM* aA, const PNS::ITEM* aB )
{
    int net_p, net_n;

    if( !DpNetPair( aA, net_p, net_n ) )
        return false;

    if( aA->Net() == net_p && aB->Net() == net_n )
        return true;
    if( aB->Net() == net_p && aA->Net() == net_n )
        return true;

    return false;
}



bool PNS_PCBNEW_RULE_RESOLVER::QueryConstraint( PNS::CONSTRAINT_TYPE aType,
                                                const PNS::ITEM* aItemA, const PNS::ITEM* aItemB,
                                                int aLayer, PNS::CONSTRAINT* aConstraint )
{
    std::shared_ptr<DRC_ENGINE> drcEngine = m_board->GetDesignSettings().m_DRCEngine;

    if( !drcEngine )
        return false;

    DRC_CONSTRAINT_TYPE_T hostType;

    switch ( aType )
    {
        case PNS::CONSTRAINT_TYPE::CT_CLEARANCE:     hostType = CLEARANCE_CONSTRAINT;     break;
        case PNS::CONSTRAINT_TYPE::CT_WIDTH:         hostType = TRACK_WIDTH_CONSTRAINT;   break;
        case PNS::CONSTRAINT_TYPE::CT_DIFF_PAIR_GAP: hostType = DIFF_PAIR_GAP_CONSTRAINT; break;
        case PNS::CONSTRAINT_TYPE::CT_LENGTH:        hostType = LENGTH_CONSTRAINT;        break;
        case PNS::CONSTRAINT_TYPE::CT_VIA_DIAMETER:  hostType = VIA_DIAMETER_CONSTRAINT;  break;
        case PNS::CONSTRAINT_TYPE::CT_VIA_HOLE:      hostType = HOLE_SIZE_CONSTRAINT;     break;
        default:                                     return false; // should not happen
    }

    BOARD_ITEM*    parentA = aItemA ? aItemA->Parent() : nullptr;
    BOARD_ITEM*    parentB = aItemB ? aItemB->Parent() : nullptr;
    DRC_CONSTRAINT hostConstraint;

    // A track being routed may not have a BOARD_ITEM associated yet.
    if( aItemA && !parentA )
    {
        switch( aItemA->Kind() )
        {
        case PNS::ITEM::ARC_T:     parentA = &m_dummyArc;   break;
        case PNS::ITEM::VIA_T:     parentA = &m_dummyVia;   break;
        case PNS::ITEM::SEGMENT_T: parentA = &m_dummyTrack; break;
        case PNS::ITEM::LINE_T:    parentA = &m_dummyTrack; break;
        default: break;
        }

        if( parentA )
        {
            parentA->SetLayer( (PCB_LAYER_ID) aLayer );
            static_cast<BOARD_CONNECTED_ITEM*>( parentA )->SetNetCode( aItemA->Net() );
        }
    }

    if( aItemB && !parentB )
    {
        switch( aItemB->Kind() )
        {
        case PNS::ITEM::ARC_T:     parentB = &m_dummyArc;   break;
        case PNS::ITEM::VIA_T:     parentB = &m_dummyVia;   break;
        case PNS::ITEM::SEGMENT_T: parentB = &m_dummyTrack; break;
        case PNS::ITEM::LINE_T:    parentB = &m_dummyTrack; break;
        default: break;
        }

        if( parentB )
        {
            parentB->SetLayer( (PCB_LAYER_ID) aLayer );
            static_cast<BOARD_CONNECTED_ITEM*>( parentB )->SetNetCode( aItemB->Net() );
        }
    }

    if( parentA )
    {
        hostConstraint = drcEngine->EvalRulesForItems( hostType, parentA, parentB,
                                                       (PCB_LAYER_ID) aLayer );
    }

    if( hostConstraint.IsNull() )
        return false;

    switch ( aType )
    {
        case PNS::CONSTRAINT_TYPE::CT_CLEARANCE:
        case PNS::CONSTRAINT_TYPE::CT_WIDTH:
        case PNS::CONSTRAINT_TYPE::CT_DIFF_PAIR_GAP:
        case PNS::CONSTRAINT_TYPE::CT_VIA_DIAMETER:
        case PNS::CONSTRAINT_TYPE::CT_VIA_HOLE:
            aConstraint->m_Value = hostConstraint.GetValue();
            aConstraint->m_RuleName = hostConstraint.GetName();
            aConstraint->m_Type = aType;
            return true;

        default:
            return false;
       }
}


int PNS_PCBNEW_RULE_RESOLVER::Clearance( const PNS::ITEM* aA, const PNS::ITEM* aB )
{
    std::pair<const PNS::ITEM*, const PNS::ITEM*> key( aA, aB );
    auto it = m_clearanceCache.find( key );

    if( it != m_clearanceCache.end() )
        return it->second;

    PNS::CONSTRAINT constraint;
    bool ok = false;
    int rv = 0;

    if( aB && IsDiffPair( aA, aB ) )
    {
        // for diff pairs, we use the gap value for shoving/dragging
        if( QueryConstraint( PNS::CONSTRAINT_TYPE::CT_DIFF_PAIR_GAP, aA, aB, aA->Layer(),
                             &constraint ) )
        {
            rv = constraint.m_Value.Opt();
            ok = true;
        }
    }

    if( !ok )
    {
        if( QueryConstraint( PNS::CONSTRAINT_TYPE::CT_CLEARANCE, aA, aB, aA->Layer(),
                             &constraint ) )
        {
            rv = constraint.m_Value.Min();
            ok = true;
        }
    }

    // still no valid clearance rule? fall back to global minimum.
    if( !ok )
        rv = m_board->GetDesignSettings().m_MinClearance;

    m_clearanceCache[ key ] = rv;

    return rv;
}


bool PNS_KICAD_IFACE_BASE::inheritTrackWidth( PNS::ITEM* aItem, int* aInheritedWidth )
{
    VECTOR2I p;

    assert( aItem->Owner() != NULL );

    switch( aItem->Kind() )
    {
    case PNS::ITEM::VIA_T:
        p = static_cast<PNS::VIA*>( aItem )->Pos();
        break;

    case PNS::ITEM::SOLID_T:
        p = static_cast<PNS::SOLID*>( aItem )->Pos();
        break;

    case PNS::ITEM::SEGMENT_T:
        *aInheritedWidth = static_cast<PNS::SEGMENT*>( aItem )->Width();
        return true;

    default:
        return false;
    }

    PNS::JOINT* jt = static_cast<PNS::NODE*>( aItem->Owner() )->FindJoint( p, aItem );

    assert( jt != NULL );

    int mval = INT_MAX;

    PNS::ITEM_SET linkedSegs = jt->Links();
    linkedSegs.ExcludeItem( aItem ).FilterKinds( PNS::ITEM::SEGMENT_T );

    for( PNS::ITEM* item : linkedSegs.Items() )
    {
        int w = static_cast<PNS::SEGMENT*>( item )->Width();
        mval = std::min( w, mval );
    }

    if( mval == INT_MAX )
        return false;

    *aInheritedWidth = mval;
    return true;
}


bool PNS_KICAD_IFACE_BASE::ImportSizes( PNS::SIZES_SETTINGS& aSizes, PNS::ITEM* aStartItem, int aNet )
{
    BOARD_DESIGN_SETTINGS& bds = m_board->GetDesignSettings();
    PNS::CONSTRAINT        constraint;

    int  trackWidth = bds.m_TrackMinWidth;
    bool found = false;

    if( bds.m_UseConnectedTrackWidth && aStartItem != nullptr )
    {
        found = inheritTrackWidth( aStartItem, &trackWidth );
    }

    if( !found && bds.UseNetClassTrack() && aStartItem )
    {
        if( m_ruleResolver->QueryConstraint( PNS::CONSTRAINT_TYPE::CT_WIDTH, aStartItem, nullptr,
                                             aStartItem->Layer(), &constraint ) )
        {
            trackWidth = constraint.m_Value.OptThenMin();
            found = true;    // Note: allowed to override anything, including bds.m_TrackMinWidth
        }
    }

    if( !found )
    {
        trackWidth = bds.GetCurrentTrackWidth();
    }

    aSizes.SetTrackWidth( trackWidth );

    int viaDiameter = bds.m_ViasMinSize;
    int viaDrill = bds.m_MinThroughDrill;

    if( bds.UseNetClassVia() && aStartItem )   // netclass value
    {
        if( m_ruleResolver->QueryConstraint( PNS::CONSTRAINT_TYPE::CT_VIA_DIAMETER, aStartItem,
                                             nullptr, aStartItem->Layer(), &constraint ) )
        {
            viaDiameter = constraint.m_Value.OptThenMin();
        }

        if( m_ruleResolver->QueryConstraint( PNS::CONSTRAINT_TYPE::CT_VIA_HOLE, aStartItem,
                                             nullptr, aStartItem->Layer(), &constraint ) )
        {
            viaDrill = constraint.m_Value.OptThenMin();
        }
    }
    else
    {
        viaDiameter = bds.GetCurrentViaSize();
        viaDrill    = bds.GetCurrentViaDrill();
    }

    aSizes.SetViaDiameter( viaDiameter );
    aSizes.SetViaDrill( viaDrill );

    int diffPairWidth = bds.m_TrackMinWidth;
    int diffPairGap = bds.m_MinClearance;
    int diffPairViaGap = bds.m_MinClearance;

    if( bds.UseNetClassDiffPair() && aStartItem )
    {
        if( m_ruleResolver->QueryConstraint( PNS::CONSTRAINT_TYPE::CT_WIDTH, aStartItem,
                                             nullptr, aStartItem->Layer(), &constraint ) )
        {
            diffPairWidth = constraint.m_Value.OptThenMin();
        }

        if( m_ruleResolver->QueryConstraint( PNS::CONSTRAINT_TYPE::CT_DIFF_PAIR_GAP, aStartItem,
                                             nullptr, aStartItem->Layer(), &constraint ) )
        {
            diffPairGap = constraint.m_Value.OptThenMin();
            diffPairViaGap = constraint.m_Value.OptThenMin();
        }
    }
    else if( bds.UseCustomDiffPairDimensions() )
    {
        diffPairWidth  = bds.GetCustomDiffPairWidth();
        diffPairGap    = bds.GetCustomDiffPairGap();
        diffPairViaGap = bds.GetCustomDiffPairViaGap();
    }

    //printf( "DPWidth: %d gap %d\n", diffPairWidth, diffPairGap );

    aSizes.SetDiffPairWidth( diffPairWidth );
    aSizes.SetDiffPairGap( diffPairGap );
    aSizes.SetDiffPairViaGap( diffPairViaGap );

    aSizes.ClearLayerPairs();

    return true;
}


int PNS_PCBNEW_RULE_RESOLVER::matchDpSuffix( const wxString& aNetName, wxString& aComplementNet,
                                             wxString& aBaseDpName )
{
    int rv = 0;

    if( aNetName.EndsWith( "+" ) )
    {
        aComplementNet = "-";
        rv = 1;
    }
    else if( aNetName.EndsWith( "P" ) )
    {
        aComplementNet = "N";
        rv = 1;
    }
    else if( aNetName.EndsWith( "-" ) )
    {
        aComplementNet = "+";
        rv = -1;
    }
    else if( aNetName.EndsWith( "N" ) )
    {
        aComplementNet = "P";
        rv = -1;
    }
    // Match P followed by 2 digits
    else if( aNetName.Right( 2 ).IsNumber() && aNetName.Right( 3 ).Left( 1 ) == "P" )
    {
        aComplementNet = "N" + aNetName.Right( 2 );
        rv = 1;
    }
    // Match P followed by 1 digit
    else if( aNetName.Right( 1 ).IsNumber() && aNetName.Right( 2 ).Left( 1 ) == "P" )
    {
        aComplementNet = "N" + aNetName.Right( 1 );
        rv = 1;
    }
    // Match N followed by 2 digits
    else if( aNetName.Right( 2 ).IsNumber() && aNetName.Right( 3 ).Left( 1 ) == "N" )
    {
        aComplementNet = "P" + aNetName.Right( 2 );
        rv = -1;
    }
    // Match N followed by 1 digit
    else if( aNetName.Right( 1 ).IsNumber() && aNetName.Right( 2 ).Left( 1 ) == "N" )
    {
        aComplementNet = "P" + aNetName.Right( 1 );
        rv = -1;
    }
    if( rv != 0 )
    {
        aBaseDpName = aNetName.Left( aNetName.Length() - aComplementNet.Length() );
        aComplementNet = aBaseDpName + aComplementNet;
    }

    return rv;
}


int PNS_PCBNEW_RULE_RESOLVER::DpCoupledNet( int aNet )
{
    wxString refName = m_board->FindNet( aNet )->GetNetname();
    wxString dummy, coupledNetName;

    if( matchDpSuffix( refName, coupledNetName, dummy ) )
    {
        NETINFO_ITEM* net = m_board->FindNet( coupledNetName );

        if( !net )
            return -1;

        return net->GetNet();
    }

    return -1;
}


wxString PNS_PCBNEW_RULE_RESOLVER::NetName( int aNet )
{
    return m_board->FindNet( aNet )->GetNetname();
}


int PNS_PCBNEW_RULE_RESOLVER::DpNetPolarity( int aNet )
{
    wxString refName = m_board->FindNet( aNet )->GetNetname();
    wxString dummy1, dummy2;

    return matchDpSuffix( refName, dummy1, dummy2 );
}


bool PNS_PCBNEW_RULE_RESOLVER::DpNetPair( const PNS::ITEM* aItem, int& aNetP, int& aNetN )
{
    if( !aItem || !aItem->Parent() || !aItem->Parent()->IsConnected() )
        return false;

    BOARD_CONNECTED_ITEM* cItem = static_cast<BOARD_CONNECTED_ITEM*>( aItem->Parent() );
    NETINFO_ITEM*         netInfo = cItem->GetNet();

    if( !netInfo )
        return false;

    wxString netNameP = netInfo->GetNetname();
    wxString netNameN, netNameCoupled, netNameBase;

    int r = matchDpSuffix( netNameP, netNameCoupled, netNameBase );

    if( r == 0 )
    {
        return false;
    }
    else if( r == 1 )
    {
        netNameN = netNameCoupled;
    }
    else
    {
        netNameN = netNameP;
        netNameP = netNameCoupled;
    }

//    wxLogTrace( "PNS","p %s n %s base %s\n", (const char *)netNameP.c_str(), (const char *)netNameN.c_str(), (const char *)netNameBase.c_str() );

    NETINFO_ITEM* netInfoP = m_board->FindNet( netNameP );
    NETINFO_ITEM* netInfoN = m_board->FindNet( netNameN );

    //wxLogTrace( "PNS","ip %p in %p\n", netInfoP, netInfoN);

    if( !netInfoP || !netInfoN )
        return false;

    aNetP = netInfoP->GetNet();
    aNetN = netInfoN->GetNet();

    return true;
}


class PNS_PCBNEW_DEBUG_DECORATOR: public PNS::DEBUG_DECORATOR
{
public:
    PNS_PCBNEW_DEBUG_DECORATOR( KIGFX::VIEW* aView = NULL ): PNS::DEBUG_DECORATOR(),
        m_view( NULL ), m_items( NULL )
    {
        SetView( aView );
    }

    ~PNS_PCBNEW_DEBUG_DECORATOR()
    {
        Clear();
        delete m_items;
    }

    void SetView( KIGFX::VIEW* aView )
    {
        Clear();
        delete m_items;
        m_items = NULL;
        m_view = aView;

        if( m_view == NULL )
            return;

        m_items = new KIGFX::VIEW_GROUP( m_view );
        m_items->SetLayer( LAYER_SELECT_OVERLAY ) ;
        m_view->Add( m_items );
    }

    void AddPoint( VECTOR2I aP, int aColor,  const std::string aName = "") override
    {
        SHAPE_LINE_CHAIN l;

        l.Append( aP - VECTOR2I( -50000, -50000 ) );
        l.Append( aP + VECTOR2I( -50000, -50000 ) );

        AddLine( l, aColor, 10000 );

        l.Clear();
        l.Append( aP - VECTOR2I( 50000, -50000 ) );
        l.Append( aP + VECTOR2I( 50000, -50000 ) );

        AddLine( l, aColor, 10000 );
    }

    void AddBox( BOX2I aB, int aColor,  const std::string aName = "" ) override
    {
        SHAPE_LINE_CHAIN l;

        VECTOR2I o = aB.GetOrigin();
        VECTOR2I s = aB.GetSize();

        l.Append( o );
        l.Append( o.x + s.x, o.y );
        l.Append( o.x + s.x, o.y + s.y );
        l.Append( o.x, o.y + s.y );
        l.Append( o );

        AddLine( l, aColor, 10000 );
    }

    void AddSegment( SEG aS, int aColor,  const std::string aName = "") override
    {
        SHAPE_LINE_CHAIN l;

        l.Append( aS.A );
        l.Append( aS.B );

        AddLine( l, aColor, 10000 );
    }

    void AddDirections( VECTOR2D aP, int aMask, int aColor,  const std::string aName = "") override
    {
        BOX2I b( aP - VECTOR2I( 10000, 10000 ), VECTOR2I( 20000, 20000 ) );

        AddBox( b, aColor );
        for( int i = 0; i < 8; i++ )
        {
            if( ( 1 << i ) & aMask )
            {
                VECTOR2I v = DIRECTION_45( ( DIRECTION_45::Directions ) i ).ToVector() * 100000;
                AddSegment( SEG( aP, aP + v ), aColor );
            }
        }
    }

    void AddLine( const SHAPE_LINE_CHAIN& aLine, int aType, int aWidth,  const std::string aName = "" ) override
    {
        if( !m_view )
            return;

        ROUTER_PREVIEW_ITEM* pitem = new ROUTER_PREVIEW_ITEM( NULL, m_view );

        pitem->Line( aLine, aWidth, aType );
        m_items->Add( pitem ); // Should not be needed, as m_items has been passed as a parent group in alloc;
        m_view->Update( m_items );
    }

    void Clear() override
    {
        if( m_view && m_items )
        {
            m_items->FreeItems();
            m_view->Update( m_items );
        }
    }

private:
    KIGFX::VIEW* m_view;
    KIGFX::VIEW_GROUP* m_items;
};


PNS::DEBUG_DECORATOR* PNS_KICAD_IFACE_BASE::GetDebugDecorator()
{
    return m_debugDecorator;
}


PNS_KICAD_IFACE_BASE::PNS_KICAD_IFACE_BASE()
{
    m_ruleResolver = nullptr;
    m_board = nullptr;
    m_world = nullptr;
    m_debugDecorator = nullptr;
}


PNS_KICAD_IFACE::PNS_KICAD_IFACE()
{
    m_tool = nullptr;
    m_view = nullptr;
    m_previewItems = nullptr;
    m_dispOptions = nullptr;
}


PNS_KICAD_IFACE_BASE::~PNS_KICAD_IFACE_BASE()
{
}


PNS_KICAD_IFACE::~PNS_KICAD_IFACE()
{
    delete m_ruleResolver;
    delete m_debugDecorator;

     if( m_previewItems )
    {
        m_previewItems->FreeItems();
        delete m_previewItems;
    }
}


std::unique_ptr<PNS::SOLID> PNS_KICAD_IFACE_BASE::syncPad( D_PAD* aPad )
{
    LAYER_RANGE layers( 0, MAX_CU_LAYERS - 1 );

    // ignore non-copper pads except for those with holes
    if( ( aPad->GetLayerSet() & LSET::AllCuMask() ).none() && aPad->GetDrillSize().x == 0 )
        return NULL;

    switch( aPad->GetAttribute() )
    {
    case PAD_ATTRIB_PTH:
    case PAD_ATTRIB_NPTH:
        break;

    case PAD_ATTRIB_CONN:
    case PAD_ATTRIB_SMD:
        {
            LSET lmsk = aPad->GetLayerSet();
            bool is_copper = false;

            for( int i = 0; i < MAX_CU_LAYERS; i++ )
            {
                if( lmsk[i] )
                {
                    is_copper = true;

                    if( aPad->GetAttribute() != PAD_ATTRIB_NPTH )
                        layers = LAYER_RANGE( i );

                    break;
                }
            }

            if( !is_copper )
                return NULL;
        }
        break;

    default:
        wxLogTrace( "PNS", "unsupported pad type 0x%x", aPad->GetAttribute() );
        return NULL;
    }

    std::unique_ptr<PNS::SOLID> solid = std::make_unique<PNS::SOLID>();

    if( aPad->GetDrillSize().x > 0 )
    {
        SHAPE_SEGMENT* slot = (SHAPE_SEGMENT*) aPad->GetEffectiveHoleShape()->Clone();

        if( aPad->GetAttribute() != PAD_ATTRIB_NPTH )
        {
            BOARD_DESIGN_SETTINGS& bds = m_board->GetDesignSettings();
            slot->SetWidth( slot->GetWidth() + bds.GetHolePlatingThickness() * 2 );
        }

        solid->SetAlternateShape( slot );
    }

    if( aPad->GetAttribute() == PAD_ATTRIB_NPTH )
        solid->SetRoutable( false );

    solid->SetLayers( layers );
    solid->SetNet( aPad->GetNetCode() );
    solid->SetParent( aPad );
    solid->SetPadToDie( aPad->GetPadToDieLength() );

    wxPoint wx_c = aPad->ShapePos();
    wxPoint offset = aPad->GetOffset();

    VECTOR2I c( wx_c.x, wx_c.y );

    RotatePoint( &offset, aPad->GetOrientation() );

    solid->SetPos( VECTOR2I( c.x - offset.x, c.y - offset.y ) );
    solid->SetOffset( VECTOR2I( offset.x, offset.y ) );


    auto shapes = std::dynamic_pointer_cast<SHAPE_COMPOUND>( aPad->GetEffectiveShape() );

    if( shapes && shapes->Size() == 1 )
    {
        solid->SetShape( shapes->Clone() );
    }
    else
    {
        // Fixme (but not urgent). For complex pad shapes, we pass a single simple polygon to the
        // router, otherwise it won't know how to correctly build walkaround 'hulls' for the pad
        // primitives - it can recognize only simple shapes, but not COMPOUNDs made of multiple shapes.
        // The proper way to fix this would be to implement SHAPE_COMPOUND::ConvertToSimplePolygon(),
        // but the complexity of pad polygonization code (see D_PAD::GetEffectivePolygon), including approximation
        // error handling makes me slightly scared to do it right now.

        const std::shared_ptr<SHAPE_POLY_SET>& outline = aPad->GetEffectivePolygon();
        SHAPE_SIMPLE*                          shape = new SHAPE_SIMPLE();

        for( auto iter = outline->CIterate( 0 ); iter; iter++ )
            shape->Append( *iter );

        solid->SetShape( shape );
    }

    return solid;
}


std::unique_ptr<PNS::SEGMENT> PNS_KICAD_IFACE_BASE::syncTrack( TRACK* aTrack )
{
    auto segment = std::make_unique<PNS::SEGMENT>( SEG( aTrack->GetStart(), aTrack->GetEnd() ),
                                                     aTrack->GetNetCode() );

    segment->SetWidth( aTrack->GetWidth() );
    segment->SetLayers( LAYER_RANGE( aTrack->GetLayer() ) );
    segment->SetParent( aTrack );

    if( aTrack->IsLocked() )
        segment->Mark( PNS::MK_LOCKED );

    return segment;
}


std::unique_ptr<PNS::ARC> PNS_KICAD_IFACE_BASE::syncArc( ARC* aArc )
{
    auto arc = std::make_unique<PNS::ARC>( SHAPE_ARC( aArc->GetStart(), aArc->GetMid(), aArc->GetEnd(),
                                                      aArc->GetWidth() ),
                                           aArc->GetNetCode() );

    arc->SetLayers( LAYER_RANGE( aArc->GetLayer() ) );
    arc->SetParent( aArc );

    if( aArc->IsLocked() )
        arc->Mark( PNS::MK_LOCKED );

    return arc;
}


std::unique_ptr<PNS::VIA> PNS_KICAD_IFACE_BASE::syncVia( VIA* aVia )
{
    std::vector<std::unique_ptr<PNS::VIA>> retval;

    PCB_LAYER_ID top, bottom;
    aVia->LayerPair( &top, &bottom );

    auto via = std::make_unique<PNS::VIA>( aVia->GetPosition(),
                                           LAYER_RANGE( aVia->TopLayer(), aVia->BottomLayer() ),
                                           aVia->GetWidth(),
                                           aVia->GetDrillValue(),
                                           aVia->GetNetCode(),
                                           aVia->GetViaType() );

    via->SetParent( aVia );

    if( aVia->IsLocked() )
        via->Mark( PNS::MK_LOCKED );

    return std::move( via );
}


bool PNS_KICAD_IFACE_BASE::syncZone( PNS::NODE* aWorld, ZONE_CONTAINER* aZone,
                                     SHAPE_POLY_SET* aBoardOutline )
{
    SHAPE_POLY_SET poly;

    // TODO handle no-via restriction
    if( !aZone->GetIsRuleArea() || !aZone->GetDoNotAllowTracks() )
        return false;

    LSET      layers = aZone->GetLayerSet();
    EDA_UNITS units = EDA_UNITS::MILLIMETRES;       // TODO: get real units

    for( int layer = F_Cu; layer <= B_Cu; layer++ )
    {
        if( ! layers[ layer ] )
            continue;

        aZone->BuildSmoothedPoly( poly, ToLAYER_ID( layer ), aBoardOutline );
        poly.CacheTriangulation();

        if( !poly.IsTriangulationUpToDate() )
        {
            KIDIALOG dlg( nullptr, wxString::Format( _( "%s is malformed." ),
                                                     aZone->GetSelectMenuText( units ) ),
                          KIDIALOG::KD_WARNING );
            dlg.ShowDetailedText( wxString::Format( _( "This zone cannot be handled by the track "
                                                       "layout tool.\n"
                                                       "Please verify it is not a "
                                                       "self-intersecting polygon." ) ) );
            dlg.DoNotShowCheckbox( __FILE__, __LINE__ );
            dlg.ShowModal();

            return false;
        }

        for( int outline = 0; outline < poly.OutlineCount(); outline++ )
        {
            const SHAPE_POLY_SET::TRIANGULATED_POLYGON* tri = poly.TriangulatedPolygon( outline );

            for( size_t i = 0; i < tri->GetTriangleCount(); i++)
            {
                VECTOR2I a, b, c;
                tri->GetTriangle( i, a, b, c );
                SHAPE_SIMPLE* triShape = new SHAPE_SIMPLE;

                triShape->Append( a );
                triShape->Append( b );
                triShape->Append( c );

                std::unique_ptr<PNS::SOLID> solid = std::make_unique<PNS::SOLID>();

                solid->SetLayer( layer );
                solid->SetNet( -1 );
                solid->SetParent( aZone );
                solid->SetShape( triShape );
                solid->SetRoutable( false );

                aWorld->Add( std::move( solid ) );
            }
        }
    }

    return true;
}


bool PNS_KICAD_IFACE_BASE::syncTextItem( PNS::NODE* aWorld, EDA_TEXT* aText, PCB_LAYER_ID aLayer )
{
    if( !IsCopperLayer( aLayer ) )
        return false;

    int textWidth = aText->GetEffectiveTextPenWidth();
    std::vector<wxPoint> textShape;

    aText->TransformTextShapeToSegmentList( textShape );

    if( textShape.size() < 2 )
        return false;

    for( size_t jj = 0; jj < textShape.size(); jj += 2 )
    {
        VECTOR2I start( textShape[jj] );
        VECTOR2I end( textShape[jj+1] );
        std::unique_ptr<PNS::SOLID> solid = std::make_unique<PNS::SOLID>();

        solid->SetLayer( aLayer );
        solid->SetNet( -1 );
        solid->SetParent( dynamic_cast<BOARD_ITEM*>( aText ) );
        solid->SetShape( new SHAPE_SEGMENT( start, end, textWidth ) );
        solid->SetRoutable( false );

        aWorld->Add( std::move( solid ) );
    }

    return true;

    /* A coarser (but faster) method:
     *
    SHAPE_POLY_SET outline;
    SHAPE_SIMPLE* shape = new SHAPE_SIMPLE();

    aText->TransformBoundingBoxWithClearanceToPolygon( &outline, 0 );

    for( auto iter = outline.CIterate( 0 ); iter; iter++ )
        shape->Append( *iter );

    solid->SetShape( shape );

    solid->SetLayer( aLayer );
    solid->SetNet( -1 );
    solid->SetParent( nullptr );
    solid->SetRoutable( false );
    aWorld->Add( std::move( solid ) );
    return true;
     */
}


bool PNS_KICAD_IFACE_BASE::syncGraphicalItem( PNS::NODE* aWorld, PCB_SHAPE* aItem )
{
    if( aItem->GetLayer() != Edge_Cuts && !IsCopperLayer( aItem->GetLayer() ) )
        return false;

    // TODO: where do we handle filled polygons on copper layers?
    if( aItem->GetShape() == S_POLYGON && aItem->IsPolygonFilled() )
        return false;

    for( SHAPE* shape : aItem->MakeEffectiveShapes() )
    {
        std::unique_ptr<PNS::SOLID> solid = std::make_unique<PNS::SOLID>();

        if( aItem->GetLayer() == Edge_Cuts )
            solid->SetLayers( LAYER_RANGE( F_Cu, B_Cu ) );
        else
            solid->SetLayer( aItem->GetLayer() );

        solid->SetNet( -1 );
        solid->SetParent( aItem );
        solid->SetShape( shape );
        solid->SetRoutable( false );

        aWorld->Add( std::move( solid ) );
    }

    return true;
}


void PNS_KICAD_IFACE_BASE::SetBoard( BOARD* aBoard )
{
    m_board = aBoard;
    wxLogTrace( "PNS", "m_board = %p", m_board );
}


bool PNS_KICAD_IFACE::IsAnyLayerVisible( const LAYER_RANGE& aLayer ) const
{
    if( !m_view )
        return false;

    for( int i = aLayer.Start(); i <= aLayer.End(); i++ )
        if( m_view->IsLayerVisible( i ) )
            return true;

    return false;
}


bool PNS_KICAD_IFACE::IsOnLayer( const PNS::ITEM* aItem, int aLayer ) const
{
    /// Default is all layers
    if( aLayer < 0 )
        return true;

    if( aItem->Parent() )
    {
        switch( aItem->Parent()->Type() )
        {
        case PCB_VIA_T:
        {
            const VIA* via = static_cast<const VIA*>( aItem->Parent() );

            return via->FlashLayer( static_cast<PCB_LAYER_ID>( aLayer ));
        }

        case PCB_PAD_T:
        {
            const D_PAD* pad = static_cast<const D_PAD*>( aItem->Parent() );

            return pad->FlashLayer( static_cast<PCB_LAYER_ID>( aLayer ));
        }

        default:
            break;
        }
    }

    return aItem->Layers().Overlaps( aLayer );
}


bool PNS_KICAD_IFACE::IsItemVisible( const PNS::ITEM* aItem ) const
{
    // by default, all items are visible (new ones created by the router have parent == NULL
    // as they have not been committed yet to the BOARD)
    if( !m_view || !aItem->Parent() )
        return true;

    BOARD_ITEM*      item = aItem->Parent();
    bool             isOnVisibleLayer = true;
    RENDER_SETTINGS* settings = m_view->GetPainter()->GetSettings();

    if( settings->GetHighContrast() )
        isOnVisibleLayer = item->IsOnLayer( settings->GetPrimaryHighContrastLayer() );

    if( m_view->IsVisible( item ) && isOnVisibleLayer
            && item->ViewGetLOD( item->GetLayer(), m_view ) < m_view->GetScale() )
    {
        return true;
    }

    // Items hidden in the router are not hidden on the board
    if( m_hiddenItems.find( item ) != m_hiddenItems.end() )
        return true;

    return false;
}


void PNS_KICAD_IFACE_BASE::SyncWorld( PNS::NODE *aWorld )
{
    int worstPadClearance = 0;

    m_world = aWorld;

    if( !m_board )
    {
        wxLogTrace( "PNS", "No board attached, aborting sync." );
        return;
    }

    for( BOARD_ITEM* gitem : m_board->Drawings() )
    {
        if ( gitem->Type() == PCB_SHAPE_T )
        {
            syncGraphicalItem( aWorld, static_cast<PCB_SHAPE*>( gitem ) );
        }
        else if( gitem->Type() == PCB_TEXT_T )
        {
            syncTextItem( aWorld, static_cast<PCB_TEXT*>( gitem ), gitem->GetLayer() );
        }
    }

    SHAPE_POLY_SET  buffer;
    SHAPE_POLY_SET* boardOutline = nullptr;

    if( m_board->GetBoardPolygonOutlines( buffer ) )
        boardOutline = &buffer;

    for( ZONE_CONTAINER* zone : m_board->Zones() )
    {
        syncZone( aWorld, zone, boardOutline );
    }

    for( MODULE* module : m_board->Modules() )
    {
        for( D_PAD* pad : module->Pads() )
        {
            if( std::unique_ptr<PNS::SOLID> solid = syncPad( pad ) )
                aWorld->Add( std::move( solid ) );

            worstPadClearance = std::max( worstPadClearance, pad->GetLocalClearance() );
        }

        syncTextItem( aWorld, &module->Reference(), module->Reference().GetLayer() );
        syncTextItem( aWorld, &module->Value(), module->Value().GetLayer() );

        for( MODULE_ZONE_CONTAINER* zone : module->Zones() )
            syncZone( aWorld, zone, boardOutline );

        if( module->IsNetTie() )
            continue;

        for( BOARD_ITEM* mgitem : module->GraphicalItems() )
        {
            if( mgitem->Type() == PCB_FP_SHAPE_T )
            {
                syncGraphicalItem( aWorld, static_cast<PCB_SHAPE*>( mgitem ) );
            }
            else if( mgitem->Type() == PCB_FP_TEXT_T )
            {
                syncTextItem( aWorld, static_cast<FP_TEXT*>( mgitem ), mgitem->GetLayer() );
            }
        }
    }

    for( TRACK* t : m_board->Tracks() )
    {
        KICAD_T type = t->Type();

        if( type == PCB_TRACE_T )
        {
            if( auto segment = syncTrack( t ) )
                aWorld->Add( std::move( segment ) );
        }
        else if( type == PCB_ARC_T )
        {
            if( auto arc = syncArc( static_cast<ARC*>( t ) ) )
                aWorld->Add( std::move( arc ) );
        }
        else if( type == PCB_VIA_T )
        {
            if( auto via = syncVia( static_cast<VIA*>( t ) ) )
                aWorld->Add( std::move( via ) );
        }
    }

    int worstRuleClearance = m_board->GetDesignSettings().GetBiggestClearanceValue();

    // NB: if this were ever to become a long-lived object we would need to dirty its
    // clearance cache here....
    delete m_ruleResolver;
    m_ruleResolver = new PNS_PCBNEW_RULE_RESOLVER( m_board, this );

    aWorld->SetRuleResolver( m_ruleResolver );
    aWorld->SetMaxClearance( 4 * std::max(worstPadClearance, worstRuleClearance ) );
}


void PNS_KICAD_IFACE::EraseView()
{
    for( auto item : m_hiddenItems )
        m_view->SetVisible( item, true );

    m_hiddenItems.clear();

    if( m_previewItems )
    {
        m_previewItems->FreeItems();
        m_view->Update( m_previewItems );
    }

    if( m_debugDecorator )
        m_debugDecorator->Clear();
}

void PNS_KICAD_IFACE_BASE::SetDebugDecorator( PNS::DEBUG_DECORATOR *aDec )
{
    m_debugDecorator = aDec;
}

void PNS_KICAD_IFACE::DisplayItem( const PNS::ITEM* aItem, int aColor, int aClearance, bool aEdit )
{
    wxLogTrace( "PNS", "DisplayItem %p", aItem );

    ROUTER_PREVIEW_ITEM* pitem = new ROUTER_PREVIEW_ITEM( aItem, m_view );

    if( aColor >= 0 )
        pitem->SetColor( KIGFX::COLOR4D( aColor ) );

    if( aClearance >= 0 )
    {
        pitem->SetClearance( aClearance );

        switch( m_dispOptions->m_ShowTrackClearanceMode )
        {
        case PCB_DISPLAY_OPTIONS::DO_NOT_SHOW_CLEARANCE:
            pitem->ShowTrackClearance( false );
            pitem->ShowViaClearance( false );
            break;
        case PCB_DISPLAY_OPTIONS::SHOW_CLEARANCE_ALWAYS:
        case PCB_DISPLAY_OPTIONS::SHOW_CLEARANCE_NEW_AND_EDITED_TRACKS_AND_VIA_AREAS:
            pitem->ShowTrackClearance( true );
            pitem->ShowViaClearance( true );
            break;

        case PCB_DISPLAY_OPTIONS::SHOW_CLEARANCE_NEW_TRACKS_AND_VIA_AREAS:
            pitem->ShowTrackClearance( !aEdit );
            pitem->ShowViaClearance( !aEdit );
            break;

        case PCB_DISPLAY_OPTIONS::SHOW_CLEARANCE_NEW_TRACKS:
            pitem->ShowTrackClearance( !aEdit );
            pitem->ShowViaClearance( false );
            break;
        }
    }


    m_previewItems->Add( pitem );
    m_view->Update( m_previewItems );
}


void PNS_KICAD_IFACE::DisplayRatline( const SHAPE_LINE_CHAIN& aRatline, int aColor )
{
    ROUTER_PREVIEW_ITEM* pitem = new ROUTER_PREVIEW_ITEM( nullptr, m_view );
    pitem->Line( aRatline, 10000, aColor );
    m_previewItems->Add( pitem );
    m_view->Update( m_previewItems );
}


void PNS_KICAD_IFACE::HideItem( PNS::ITEM* aItem )
{
    BOARD_ITEM* parent = aItem->Parent();

    if( parent )
    {
        if( m_view->IsVisible( parent ) )
            m_hiddenItems.insert( parent );

        m_view->SetVisible( parent, false );
        m_view->Update( parent, KIGFX::APPEARANCE );
    }
}


void PNS_KICAD_IFACE_BASE::RemoveItem( PNS::ITEM* aItem )
{

}


void PNS_KICAD_IFACE::RemoveItem( PNS::ITEM* aItem )
{
    BOARD_ITEM* parent = aItem->Parent();

    if ( aItem->OfKind(PNS::ITEM::SOLID_T) )
    {
        D_PAD*   pad = static_cast<D_PAD*>( parent );
        VECTOR2I pos = static_cast<PNS::SOLID*>( aItem )->Pos();

        m_moduleOffsets[ pad ].p_old = pos;
        return;
    }

    if( parent )
    {
        m_commit->Remove( parent );
    }
}


void PNS_KICAD_IFACE_BASE::AddItem( PNS::ITEM* aItem )
{

}


void PNS_KICAD_IFACE::AddItem( PNS::ITEM* aItem )
{
    BOARD_CONNECTED_ITEM* newBI = NULL;

    switch( aItem->Kind() )
    {
    case PNS::ITEM::ARC_T:
    {
        auto arc = static_cast<PNS::ARC*>( aItem );
        ARC* new_arc = new ARC( m_board, static_cast<const SHAPE_ARC*>( arc->Shape() ) );
        new_arc->SetWidth( arc->Width() );
        new_arc->SetLayer( ToLAYER_ID( arc->Layers().Start() ) );
        new_arc->SetNetCode( std::max<int>( 0, arc->Net() ) );
        newBI = new_arc;
        break;
    }

    case PNS::ITEM::SEGMENT_T:
    {
        PNS::SEGMENT* seg = static_cast<PNS::SEGMENT*>( aItem );
        TRACK* track = new TRACK( m_board );
        const SEG& s = seg->Seg();
        track->SetStart( wxPoint( s.A.x, s.A.y ) );
        track->SetEnd( wxPoint( s.B.x, s.B.y ) );
        track->SetWidth( seg->Width() );
        track->SetLayer( ToLAYER_ID( seg->Layers().Start() ) );
        track->SetNetCode( seg->Net() > 0 ? seg->Net() : 0 );
        newBI = track;
        break;
    }

    case PNS::ITEM::VIA_T:
    {
        VIA* via_board = new VIA( m_board );
        PNS::VIA* via = static_cast<PNS::VIA*>( aItem );
        via_board->SetPosition( wxPoint( via->Pos().x, via->Pos().y ) );
        via_board->SetWidth( via->Diameter() );
        via_board->SetDrill( via->Drill() );
        via_board->SetNetCode( via->Net() > 0 ? via->Net() : 0 );
        via_board->SetViaType( via->ViaType() ); // MUST be before SetLayerPair()
        via_board->SetLayerPair( ToLAYER_ID( via->Layers().Start() ),
                                 ToLAYER_ID( via->Layers().End() ) );
        newBI = via_board;
        break;
    }

    case PNS::ITEM::SOLID_T:
    {
        D_PAD*   pad = static_cast<D_PAD*>( aItem->Parent() );
        VECTOR2I pos = static_cast<PNS::SOLID*>( aItem )->Pos();

        m_moduleOffsets[ pad ].p_new = pos;
        return;
    }

    default:
        break;
    }

    if( newBI )
    {
        //newBI->SetLocalRatsnestVisible( m_dispOptions->m_ShowGlobalRatsnest );
        aItem->SetParent( newBI );
        newBI->ClearFlags();

        m_commit->Add( newBI );
    }
}


void PNS_KICAD_IFACE::Commit()
{
    std::set<MODULE*> processedMods;

    EraseView();

    for( auto mo : m_moduleOffsets )
    {
        auto offset = mo.second.p_new - mo.second.p_old;
        auto mod = mo.first->GetParent();

        VECTOR2I p_orig = mod->GetPosition();
        VECTOR2I p_new = p_orig + offset;

        if( processedMods.find( mod ) != processedMods.end() )
            continue;

        processedMods.insert( mod );
        m_commit->Modify( mod );
        mod->SetPosition( wxPoint( p_new.x, p_new.y ));
    }

    m_moduleOffsets.clear();

    m_commit->Push( _( "Interactive Router" ) );
    m_commit = std::make_unique<BOARD_COMMIT>( m_tool );
}


void PNS_KICAD_IFACE::SetView( KIGFX::VIEW* aView )
{
    wxLogTrace( "PNS", "SetView %p", aView );

    if( m_previewItems )
    {
        m_previewItems->FreeItems();
        delete m_previewItems;
    }

    m_view = aView;
    m_previewItems = new KIGFX::VIEW_GROUP( m_view );
    m_previewItems->SetLayer( LAYER_SELECT_OVERLAY ) ;

    if(m_view)
        m_view->Add( m_previewItems );

    delete m_debugDecorator;

    auto dec = new PNS_PCBNEW_DEBUG_DECORATOR();
    m_debugDecorator = dec;

    if( ADVANCED_CFG::GetCfg().m_ShowRouterDebugGraphics )
        dec->SetView( m_view );
}


void PNS_KICAD_IFACE::UpdateNet( int aNetCode )
{
    wxLogTrace( "PNS", "Update-net %d", aNetCode );

}

PNS::RULE_RESOLVER* PNS_KICAD_IFACE_BASE::GetRuleResolver()
{
    return m_ruleResolver;
}


void PNS_KICAD_IFACE::SetHostTool( PCB_TOOL_BASE* aTool )
{
    m_tool = aTool;
    m_commit = std::make_unique<BOARD_COMMIT>( m_tool );
}

void PNS_KICAD_IFACE::SetDisplayOptions( const PCB_DISPLAY_OPTIONS* aDispOptions )
{
    m_dispOptions = aDispOptions;
}
