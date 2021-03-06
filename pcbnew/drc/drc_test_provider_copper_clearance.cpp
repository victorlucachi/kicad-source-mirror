/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2004-2020 KiCad Developers.
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

#include <common.h>
#include <class_board.h>
#include <pcb_shape.h>
#include <class_pad.h>
#include <class_track.h>

#include <geometry/seg.h>
#include <geometry/shape_poly_set.h>
#include <geometry/shape_rect.h>
#include <geometry/shape_segment.h>
#include <geometry/shape_null.h>

#include <drc/drc_engine.h>
#include <drc/drc_rtree.h>
#include <drc/drc_item.h>
#include <drc/drc_rule.h>
#include <drc/drc_test_provider_clearance_base.h>
#include <class_dimension.h>

/*
    Copper clearance test. Checks all copper items (pads, vias, tracks, drawings, zones) for their electrical clearance.
    Errors generated:
    - DRCE_CLEARANCE
    - DRCE_TRACKS_CROSSING
    - DRCE_ZONES_INTERSECT
    - DRCE_SHORTING_ITEMS
*/

class DRC_TEST_PROVIDER_COPPER_CLEARANCE : public DRC_TEST_PROVIDER_CLEARANCE_BASE
{
public:
    DRC_TEST_PROVIDER_COPPER_CLEARANCE () :
            DRC_TEST_PROVIDER_CLEARANCE_BASE(),
            m_drcEpsilon( 0 )
    {
    }

    virtual ~DRC_TEST_PROVIDER_COPPER_CLEARANCE()
    {
    }

    virtual bool Run() override;

    virtual const wxString GetName() const override
    {
        return "clearance";
    };

    virtual const wxString GetDescription() const override
    {
        return "Tests copper item clearance";
    }

    virtual std::set<DRC_CONSTRAINT_TYPE_T> GetConstraintTypes() const override;

    int GetNumPhases() const override;

private:
    bool testTrackAgainstItem( TRACK* track, SHAPE* trackShape, PCB_LAYER_ID layer,
                               BOARD_ITEM* other );

    void testTrackClearances();

    bool testPadAgainstItem( D_PAD* pad, SHAPE* padShape, PCB_LAYER_ID layer, BOARD_ITEM* other );

    void testPadClearances();

    void testZones();

    void testItemAgainstZones( BOARD_ITEM* aItem, PCB_LAYER_ID aLayer );

private:
    DRC_RTREE m_copperTree;
    int       m_drcEpsilon;

    std::vector<ZONE_CONTAINER*>                          m_zones;
    std::map<ZONE_CONTAINER*, std::unique_ptr<DRC_RTREE>> m_zoneTrees;

};


bool DRC_TEST_PROVIDER_COPPER_CLEARANCE::Run()
{
    m_board = m_drcEngine->GetBoard();
    DRC_CONSTRAINT worstClearanceConstraint;

    if( m_drcEngine->QueryWorstConstraint( CLEARANCE_CONSTRAINT, worstClearanceConstraint ) )
    {
        m_largestClearance = worstClearanceConstraint.GetValue().Min();
    }
    else
    {
        reportAux( "No Clearance constraints found..." );
        return false;
    }

    m_drcEpsilon = m_board->GetDesignSettings().GetDRCEpsilon();

    m_zones.clear();

    for( ZONE_CONTAINER* zone : m_board->Zones() )
    {
        if( !zone->GetIsRuleArea() )
            m_zones.push_back( zone );
    }

    for( MODULE* footprint : m_board->Modules() )
    {
        for( ZONE_CONTAINER* zone : footprint->Zones() )
        {
            if( !zone->GetIsRuleArea() )
                m_zones.push_back( zone );
        }
    }

    reportAux( "Worst clearance : %d nm", m_largestClearance );

    // This is the number of tests between 2 calls to the progress bar
    size_t delta = 50;
    size_t count = 0;
    size_t ii = 0;

    m_copperTree.clear();

    auto countItems =
            [&]( BOARD_ITEM* item ) -> bool
            {
                ++count;
                return true;
            };

    auto addToCopperTree =
            [&]( BOARD_ITEM* item ) -> bool
            {
                if( !reportProgress( ii++, count, delta ) )
                    return false;

                item->ClearFlags( SKIP_STRUCT );

                if( item->Type() == PCB_FP_TEXT_T && !static_cast<FP_TEXT*>( item )->IsVisible() )
                    return true;

                m_copperTree.Insert( item, m_largestClearance );
                return true;
            };

    if( !reportPhase( _( "Gathering copper items..." ) ) )
        return false;

    static const std::vector<KICAD_T> itemTypes = {
        PCB_TRACE_T, PCB_ARC_T, PCB_VIA_T, PCB_PAD_T, PCB_SHAPE_T, PCB_FP_SHAPE_T,
        PCB_TEXT_T, PCB_FP_TEXT_T, PCB_DIMENSION_T, PCB_DIM_ALIGNED_T, PCB_DIM_LEADER_T,
        PCB_DIM_CENTER_T,  PCB_DIM_ORTHOGONAL_T
    };

    forEachGeometryItem( itemTypes, LSET::AllCuMask(), countItems );
    forEachGeometryItem( itemTypes, LSET::AllCuMask(), addToCopperTree );

    if( !reportPhase( _( "Tessellating copper zones..." ) ) )
        return false;

    delta = 5;
    ii = 0;
    m_zoneTrees.clear();

    for( ZONE_CONTAINER* zone : m_zones )
    {
        if( !reportProgress( ii++, m_zones.size(), delta ) )
            break;

        zone->CacheBoundingBox();
        m_zoneTrees[ zone ] = std::make_unique<DRC_RTREE>();

        for( int layer : zone->GetLayerSet().Seq() )
        {
            if( IsCopperLayer( layer ) )
                m_zoneTrees[ zone ]->Insert( zone, layer );
        }

    }

    reportAux( "Testing %d copper items and %d zones...", count, m_zones.size() );

    if( !reportPhase( _( "Checking track & via clearances..." ) ) )
        return false;

    testTrackClearances();

    if( !reportPhase( _( "Checking pad clearances..." ) ) )
        return false;

    testPadClearances();

    if( !reportPhase( _( "Checking copper zone clearances..." ) ) )
        return false;

    testZones();

    reportRuleStatistics();

    return true;
}

static std::shared_ptr<SHAPE> getShape( BOARD_ITEM* aItem, PCB_LAYER_ID aLayer )
{
    if( aItem->Type() == PCB_PAD_T && !static_cast<D_PAD*>( aItem )->FlashLayer( aLayer ) )
    {
        D_PAD* aPad = static_cast<D_PAD*>( aItem );

        if( aPad->GetAttribute() == PAD_ATTRIB_PTH )
        {
            BOARD_DESIGN_SETTINGS& bds = aPad->GetBoard()->GetDesignSettings();

            // Note: drill size represents finish size, which means the actual holes size is the
            // plating thickness larger.
            auto hole = static_cast<SHAPE_SEGMENT*>( aPad->GetEffectiveHoleShape()->Clone() );
            hole->SetWidth( hole->GetWidth() + bds.GetHolePlatingThickness() );
            return std::make_shared<SHAPE_SEGMENT>( *hole );
        }

        return std::make_shared<SHAPE_NULL>();
    }

    return aItem->GetEffectiveShape( aLayer );
}


bool DRC_TEST_PROVIDER_COPPER_CLEARANCE::testTrackAgainstItem( TRACK* track, SHAPE* trackShape,
                                                               PCB_LAYER_ID layer,
                                                               BOARD_ITEM* other )
{
    if( m_drcEngine->IsErrorLimitExceeded( DRCE_CLEARANCE ) )
        return false;

    auto     constraint = m_drcEngine->EvalRulesForItems( CLEARANCE_CONSTRAINT, track, other,
                                                          layer );
    int      minClearance = constraint.GetValue().Min();
    int      actual;
    VECTOR2I pos;

    accountCheck( constraint );

    // Special processing for track:track intersections
    if( track->Type() == PCB_TRACE_T && other->Type() == PCB_TRACE_T )
    {
        SEG trackSeg( track->GetStart(), track->GetEnd() );
        SEG otherSeg( track->GetStart(), track->GetEnd() );

        if( OPT_VECTOR2I intersection = trackSeg.Intersect( otherSeg ) )
        {
            std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( DRCE_TRACKS_CROSSING );
            drcItem->SetItems( track, other );
            drcItem->SetViolatingRule( constraint.GetParentRule() );

            reportViolation( drcItem, (wxPoint) intersection.get() );
            return true;
        }
    }

    std::shared_ptr<SHAPE> otherShape = getShape( other, layer );

    if( trackShape->Collide( otherShape.get(), minClearance - m_drcEpsilon, &actual, &pos ) )
    {
        std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_CLEARANCE );

        m_msg.Printf( _( "(%s clearance %s; actual %s)" ),
                      constraint.GetName(),
                      MessageTextFromValue( userUnits(), minClearance ),
                      MessageTextFromValue( userUnits(), actual ) );

        drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + m_msg );
        drce->SetItems( track, other );
        drce->SetViolatingRule( constraint.GetParentRule() );

        reportViolation( drce, (wxPoint) pos );

        if( !m_drcEngine->GetReportAllTrackErrors() )
            return false;
    }

    return true;
}


void DRC_TEST_PROVIDER_COPPER_CLEARANCE::testItemAgainstZones( BOARD_ITEM* aItem,
                                                               PCB_LAYER_ID aLayer )
{
    for( ZONE_CONTAINER* zone : m_zones )
    {
        if( m_drcEngine->IsErrorLimitExceeded( DRCE_CLEARANCE ) )
            break;

        if( !zone->GetLayerSet().test( aLayer ) )
            continue;

        if( zone->GetNetCode() && aItem->IsConnected() )
        {
            if( zone->GetNetCode() == static_cast<BOARD_CONNECTED_ITEM*>( aItem )->GetNetCode() )
                continue;
        }

        if( aItem->GetBoundingBox().Intersects( zone->GetCachedBoundingBox() ) )
        {
            auto constraint = m_drcEngine->EvalRulesForItems( CLEARANCE_CONSTRAINT, aItem, zone,
                                                              aLayer );
            int        clearance = constraint.GetValue().Min();
            int        actual;
            VECTOR2I   pos;
            DRC_RTREE* zoneTree = m_zoneTrees[ zone ].get();

            if( zoneTree->QueryColliding( aItem, aLayer, clearance - m_drcEpsilon, &actual, &pos ) )
            {
                std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_CLEARANCE );

                m_msg.Printf( _( "(%s clearance %s; actual %s)" ),
                              constraint.GetName(),
                              MessageTextFromValue( userUnits(), clearance ),
                              MessageTextFromValue( userUnits(), actual ) );

                drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + m_msg );
                drce->SetItems( aItem, zone );
                drce->SetViolatingRule( constraint.GetParentRule() );

                reportViolation( drce, (wxPoint) pos );
            }
        }
    }
}


void DRC_TEST_PROVIDER_COPPER_CLEARANCE::testTrackClearances()
{
    // This is the number of tests between 2 calls to the progress bar
    const int delta = 25;
    int       ii = 0;

    reportAux( "Testing %d tracks & vias...", m_board->Tracks().size() );

    for( TRACK* track : m_board->Tracks() )
    {
        if( !reportProgress( ii++, m_board->Tracks().size(), delta ) )
            break;

        for( PCB_LAYER_ID layer : track->GetLayerSet().Seq() )
        {
            std::shared_ptr<SHAPE> trackShape = track->GetEffectiveShape( layer );

            m_copperTree.QueryColliding( track, layer, layer,
                    // Filter:
                    [&]( BOARD_ITEM* other ) -> bool
                    {
                        if( other->HasFlag( SKIP_STRUCT ) )
                            return false;

                        auto otherCItem = dynamic_cast<BOARD_CONNECTED_ITEM*>( other );

                        if( otherCItem && otherCItem->GetNetCode() == track->GetNetCode() )
                            return false;

                        return true;
                    },
                    // Visitor:
                    [&]( BOARD_ITEM* other ) -> bool
                    {
                        return testTrackAgainstItem( track, trackShape.get(), layer, other );
                    },
                    m_largestClearance );

            testItemAgainstZones( track, layer );
        }

        track->SetFlags( SKIP_STRUCT );
    }
}


bool DRC_TEST_PROVIDER_COPPER_CLEARANCE::testPadAgainstItem( D_PAD* pad, SHAPE* padShape,
                                                             PCB_LAYER_ID layer,
                                                             BOARD_ITEM* other )
{
    bool testClearance = !m_drcEngine->IsErrorLimitExceeded( DRCE_CLEARANCE );
    bool testShorting = !m_drcEngine->IsErrorLimitExceeded( DRCE_SHORTING_ITEMS );
    bool testHoles = !m_drcEngine->IsErrorLimitExceeded( DRCE_HOLE_CLEARANCE );

    // Graphic items are allowed to act as net-ties within their own footprint
    if( other->Type() == PCB_FP_SHAPE_T && pad->GetParent() == other->GetParent() )
        testClearance = false;

    if( !testClearance && !testShorting && !testHoles )
        return false;

    std::shared_ptr<SHAPE> otherShape = getShape( other, layer );
    DRC_CONSTRAINT         constraint;
    int                    clearance;
    int                    actual;
    VECTOR2I               pos;

    if( other->Type() == PCB_PAD_T )
    {
        auto otherPad = static_cast<D_PAD*>( other );

        // If pads are equivalent (ie: from the same footprint with the same pad number)...
        if( pad->SameLogicalPadAs( otherPad ) )
        {
            // ...and have nets, then they must be the same net
            if( pad->GetNetCode() && otherPad->GetNetCode()
                    && pad->GetNetCode() != otherPad->GetNetCode()
                    && testShorting )
            {
                std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_SHORTING_ITEMS );

                m_msg.Printf( _( "(nets %s and %s)" ),
                              pad->GetNetname(),
                              otherPad->GetNetname() );

                drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + m_msg );
                drce->SetItems( pad, otherPad );

                reportViolation( drce, otherPad->GetPosition());
            }

            return true;
        }

        if( testHoles )
        {
            if( ( pad->FlashLayer( layer ) && otherPad->GetDrillSize().x )
             || ( pad->GetDrillSize().x && otherPad->FlashLayer( layer ) ) )
            {
                constraint = m_drcEngine->EvalRulesForItems( HOLE_CLEARANCE_CONSTRAINT, pad,
                                                             otherPad );
                clearance = constraint.GetValue().Min();

                accountCheck( constraint.GetParentRule() );

                if( padShape->Collide( otherShape.get(), clearance - m_drcEpsilon, &actual, &pos ) )
                {
                    std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_HOLE_CLEARANCE );

                    m_msg.Printf( _( "(%s clearance %s; actual %s)" ),
                                  constraint.GetName(),
                                  MessageTextFromValue( userUnits(), clearance ),
                                  MessageTextFromValue( userUnits(), actual ) );

                    drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + m_msg );
                    drce->SetItems( pad, other );
                    drce->SetViolatingRule( constraint.GetParentRule() );

                    reportViolation( drce, (wxPoint) pos );
                }
            }
        }

        // Pads of the same (defined) net get a waiver on clearance tests
        if( pad->GetNetCode() && otherPad->GetNetCode() == pad->GetNetCode() )
            testClearance = false;
    }

    if( testClearance )
    {
        constraint = m_drcEngine->EvalRulesForItems( CLEARANCE_CONSTRAINT, pad, other, layer );
        clearance = constraint.GetValue().Min();

        accountCheck( constraint );

        if( padShape->Collide( otherShape.get(), clearance - m_drcEpsilon, &actual, &pos ) )
        {
            std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_CLEARANCE );

            m_msg.Printf( _( "(%s clearance %s; actual %s)" ),
                          constraint.GetName(),
                          MessageTextFromValue( userUnits(), clearance ),
                          MessageTextFromValue( userUnits(), actual ) );

            drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + m_msg );
            drce->SetItems( pad, other );
            drce->SetViolatingRule( constraint.GetParentRule() );

            reportViolation( drce, (wxPoint) pos );
        }
    }

    return true;
}


void DRC_TEST_PROVIDER_COPPER_CLEARANCE::testPadClearances( )
{
    const int delta = 25;  // This is the number of tests between 2 calls to the progress bar

    size_t count = 0;

    for( MODULE* module : m_board->Modules() )
        count += module->Pads().size();

    reportAux( "Testing %d pads...", count );

    int ii = 0;

    for( MODULE* module : m_board->Modules() )
    {
        for( D_PAD* pad : module->Pads() )
        {
            if( !reportProgress( ii++, count, delta ) )
                break;

            for( PCB_LAYER_ID layer : pad->GetLayerSet().Seq() )
            {
                std::shared_ptr<SHAPE> padShape = getShape( pad, layer );

                m_copperTree.QueryColliding( pad, layer, layer,
                        // Filter:
                        [&]( BOARD_ITEM* other ) -> bool
                        {
                            if( other->HasFlag( SKIP_STRUCT ) )
                                return false;

                            return true;
                        },
                        // Visitor
                        [&]( BOARD_ITEM* other ) -> bool
                        {
                            return testPadAgainstItem( pad, padShape.get(), layer, other );
                        },
                        m_largestClearance );

                testItemAgainstZones( pad, layer );
            }

            pad->SetFlags( SKIP_STRUCT );
        }
    }
}


void DRC_TEST_PROVIDER_COPPER_CLEARANCE::testZones()
{
    const int delta = 50;  // This is the number of tests between 2 calls to the progress bar

    SHAPE_POLY_SET  buffer;
    SHAPE_POLY_SET* boardOutline = nullptr;

    if( m_board->GetBoardPolygonOutlines( buffer ) )
        boardOutline = &buffer;

    for( int layer_id = F_Cu; layer_id <= B_Cu; ++layer_id )
    {
        PCB_LAYER_ID layer = static_cast<PCB_LAYER_ID>( layer_id );
        std::vector<SHAPE_POLY_SET> smoothed_polys;
        smoothed_polys.resize( m_zones.size() );

        // Skip over layers not used on the current board
        if( !m_board->IsLayerEnabled( layer ) )
            continue;

        for( size_t ii = 0; ii < m_zones.size(); ii++ )
        {
            if( m_zones[ii]->IsOnLayer( layer ) )
                m_zones[ii]->BuildSmoothedPoly( smoothed_polys[ii], layer, boardOutline );
        }

        // iterate through all areas
        for( size_t ia = 0; ia < m_zones.size(); ia++ )
        {
            if( !reportProgress( layer_id * m_zones.size() + ia, B_Cu * m_zones.size(), delta ) )
                break;

            ZONE_CONTAINER* zoneRef = m_zones[ia];

            if( !zoneRef->IsOnLayer( layer ) )
                continue;

            // If we are testing a single zone, then iterate through all other zones
            // Otherwise, we have already tested the zone combination
            for( size_t ia2 = ia + 1; ia2 < m_zones.size(); ia2++ )
            {
                ZONE_CONTAINER* zoneToTest = m_zones[ia2];

                if( zoneRef == zoneToTest )
                    continue;

                // test for same layer
                if( !zoneToTest->IsOnLayer( layer ) )
                    continue;

                // Test for same net
                if( zoneRef->GetNetCode() == zoneToTest->GetNetCode() && zoneRef->GetNetCode() >= 0 )
                    continue;

                // test for different priorities
                if( zoneRef->GetPriority() != zoneToTest->GetPriority() )
                    continue;

                // test for different types
                if( zoneRef->GetIsRuleArea() != zoneToTest->GetIsRuleArea() )
                    continue;

                // Examine a candidate zone: compare zoneToTest to zoneRef

                // Get clearance used in zone to zone test.
                auto constraint = m_drcEngine->EvalRulesForItems( CLEARANCE_CONSTRAINT, zoneRef,
                                                                  zoneToTest );
                int  zone2zoneClearance = constraint.GetValue().Min();

                accountCheck( constraint );

                // Keepout areas have no clearance, so set zone2zoneClearance to 1
                // ( zone2zoneClearance = 0  can create problems in test functions)
                if( zoneRef->GetIsRuleArea() ) // fixme: really?
                    zone2zoneClearance = 1;

                // test for some corners of zoneRef inside zoneToTest
                for( auto iterator = smoothed_polys[ia].IterateWithHoles(); iterator; iterator++ )
                {
                    VECTOR2I currentVertex = *iterator;
                    wxPoint pt( currentVertex.x, currentVertex.y );

                    if( smoothed_polys[ia2].Contains( currentVertex ) )
                    {
                        std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_ZONES_INTERSECT );
                        drce->SetItems( zoneRef, zoneToTest );
                        drce->SetViolatingRule( constraint.GetParentRule() );

                        reportViolation( drce, pt );
                    }
                }

                // test for some corners of zoneToTest inside zoneRef
                for( auto iterator = smoothed_polys[ia2].IterateWithHoles(); iterator; iterator++ )
                {
                    VECTOR2I currentVertex = *iterator;
                    wxPoint pt( currentVertex.x, currentVertex.y );

                    if( smoothed_polys[ia].Contains( currentVertex ) )
                    {
                        std::shared_ptr<DRC_ITEM> drce = DRC_ITEM::Create( DRCE_ZONES_INTERSECT );
                        drce->SetItems( zoneToTest, zoneRef );
                        drce->SetViolatingRule( constraint.GetParentRule() );

                        reportViolation( drce, pt );
                    }
                }

                // Iterate through all the segments of refSmoothedPoly
                std::map<wxPoint, int> conflictPoints;

                for( auto refIt = smoothed_polys[ia].IterateSegmentsWithHoles(); refIt; refIt++ )
                {
                    // Build ref segment
                    SEG refSegment = *refIt;

                    // Iterate through all the segments in smoothed_polys[ia2]
                    for( auto testIt = smoothed_polys[ia2].IterateSegmentsWithHoles(); testIt; testIt++ )
                    {
                        // Build test segment
                        SEG testSegment = *testIt;
                        wxPoint pt;

                        int ax1, ay1, ax2, ay2;
                        ax1 = refSegment.A.x;
                        ay1 = refSegment.A.y;
                        ax2 = refSegment.B.x;
                        ay2 = refSegment.B.y;

                        int bx1, by1, bx2, by2;
                        bx1 = testSegment.A.x;
                        by1 = testSegment.A.y;
                        bx2 = testSegment.B.x;
                        by2 = testSegment.B.y;

                        int d = GetClearanceBetweenSegments( bx1, by1, bx2, by2,
                                                             0,
                                                             ax1, ay1, ax2, ay2,
                                                             0,
                                                             zone2zoneClearance,
                                                             &pt.x, &pt.y );

                        if( d < zone2zoneClearance )
                        {
                            if( conflictPoints.count( pt ) )
                                conflictPoints[ pt ] = std::min( conflictPoints[ pt ], d );
                            else
                                conflictPoints[ pt ] = d;
                        }
                    }
                }

                for( const std::pair<const wxPoint, int>& conflict : conflictPoints )
                {
                    int       actual = conflict.second;
                    std::shared_ptr<DRC_ITEM> drce;

                    if( actual <= 0 )
                    {
                        drce = DRC_ITEM::Create( DRCE_ZONES_INTERSECT );
                    }
                    else
                    {
                        drce = DRC_ITEM::Create( DRCE_CLEARANCE );

                        m_msg.Printf( _( "(%s clearance %s; actual %s)" ),
                                      constraint.GetName(),
                                      MessageTextFromValue( userUnits(), zone2zoneClearance ),
                                      MessageTextFromValue( userUnits(), conflict.second ) );

                        drce->SetErrorMessage( drce->GetErrorText() + wxS( " " ) + m_msg );
                    }

                    drce->SetItems( zoneRef, zoneToTest );
                    drce->SetViolatingRule( constraint.GetParentRule() );

                    reportViolation( drce, conflict.first );
                }
            }
        }
    }
}


int DRC_TEST_PROVIDER_COPPER_CLEARANCE::GetNumPhases() const
{
    return 5;
}


std::set<DRC_CONSTRAINT_TYPE_T> DRC_TEST_PROVIDER_COPPER_CLEARANCE::GetConstraintTypes() const
{
    return { CLEARANCE_CONSTRAINT };
}


namespace detail
{
    static DRC_REGISTER_TEST_PROVIDER<DRC_TEST_PROVIDER_COPPER_CLEARANCE> dummy;
}
