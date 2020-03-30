/*
 * KiRouter - a push-and-(sometimes-)shove PCB router
 *
 * Copyright (C) 2013-2020 CERN
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

#include <memory>

#include "pns_line.h"
#include "pns_solid.h"
#include "pns_via.h"
#include "pns_router.h"

#include "pns_component_dragger.h"
#include "pns_debug_decorator.h"

namespace PNS
{

COMPONENT_DRAGGER::COMPONENT_DRAGGER( ROUTER* aRouter ) : DRAG_ALGO( aRouter )
{
    // ensure all variables are initialized
    m_dragStatus = false;
    m_currentNode = nullptr;
}


COMPONENT_DRAGGER::~COMPONENT_DRAGGER()
{
}


bool COMPONENT_DRAGGER::Start( const VECTOR2I& aP, ITEM_SET& aPrimitives )
{
    m_currentNode         = nullptr;
    m_initialDraggedItems = aPrimitives;
    m_p0                  = aP;

    for( auto item : aPrimitives.Items() )
    {
        if( item.item->Kind() != ITEM::SOLID_T )
            continue;

        auto solid = static_cast<SOLID*>( item.item );
        auto jt    = m_world->FindJoint( solid->Pos(), solid );

        m_solids.insert( solid );

        for( auto link : jt->LinkList() )
        {
            if( link.item->OfKind( ITEM::SEGMENT_T | ITEM::ARC_T ) )
            {
                LINKED_ITEM* li = static_cast<LINKED_ITEM*>( link.item );
                int          segIndex;

                auto l0 = m_world->AssembleLine( li, &segIndex );

//                printf( "solid %p jt %p fanout %d segs %d\n", solid, jt, jt->LinkCount(),
///                      l0.SegmentCount() );

                DRAGGED_CONNECTION cn;

                cn.origLine    = l0;
                cn.attachedPad = solid;
                m_conns.push_back( cn );
            }
        }
    }

//    printf( "Total: %d conns to drag\n", m_conns.size() );

    return true;
}

bool COMPONENT_DRAGGER::Drag( const VECTOR2I& aP )
{
    m_world->KillChildren();
    m_currentNode = m_world->Branch();

    for( auto item : m_initialDraggedItems.Items() )
        m_currentNode->Remove( item );

    m_draggedItems.Clear();

    for( auto item : m_solids )
    {
        SOLID*                 s      = static_cast<SOLID*>( item );
        auto                   p_next = aP - m_p0 + s->Pos();
        std::unique_ptr<SOLID> snew( static_cast<SOLID*>( s->Clone() ) );
        snew->SetPos( p_next );

        m_draggedItems.Add( snew.get() );
        m_currentNode->Add( std::move( snew ) );

        for( auto& l : m_conns )
        {
            if( l.attachedPad == s )
            {
                l.p_orig = s->Pos();
                l.p_next = p_next;
            }
        }
    }

    for( auto& cn : m_conns )
    {
        auto l_new( cn.origLine );
        l_new.Unmark();
        l_new.ClearSegmentLinks();
        l_new.DragCorner( cn.p_next, cn.origLine.CLine().Find( cn.p_orig ) );

        Dbg()->AddLine( l_new.CLine(), 4, 100000 );
        m_draggedItems.Add( l_new );

        auto l_orig( cn.origLine );
        m_currentNode->Remove( l_orig );
        m_currentNode->Add( l_new );
    }

    return true;
}

bool COMPONENT_DRAGGER::FixRoute()
{
    NODE* node = CurrentNode();

    if( node )
    {
        bool ok;
        if( Settings().CanViolateDRC() )
            ok = true;
        else
            ok = !node->CheckColliding( m_draggedItems );

        if( !ok )
            return false;

        Router()->CommitRouting( node );
        return true;
    }

    return false;
}

NODE* COMPONENT_DRAGGER::CurrentNode() const
{
    return m_currentNode ? m_currentNode : m_world;
}

const ITEM_SET COMPONENT_DRAGGER::Traces()
{
    return m_draggedItems;
}

}; // namespace PNS
