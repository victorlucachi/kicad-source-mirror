/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2016-2017 KiCad Developers, see AUTHORS.txt for contributors.
 * Copyright (C) 2017 Chris Pavlina <pavlina.chris@gmail.com>
 * Copyright (C) 2016 Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
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

#ifndef __FOOTPRINT_PREVIEW_PANEL_H
#define __FOOTPRINT_PREVIEW_PANEL_H

#include <wx/wx.h>

#include <map>
#include <deque>
#include <functional>

#include <pcb_draw_panel_gal.h>
#include <gal/color4d.h>
#include <gal/gal_display_options.h>
#include <lib_id.h>
#include <kiway_player.h>
#include <core/optional.h>

#include <widgets/footprint_preview_widget.h>

class MODULE;
class KIWAY;
class IO_MGR;
class BOARD;
class FP_LOADER_THREAD;
class FP_THREAD_IFACE;


/**
 * Panel that renders a single footprint via Cairo GAL, meant to be exported
 * through Kiface.
 */
class FOOTPRINT_PREVIEW_PANEL :
    public PCB_DRAW_PANEL_GAL, public KIWAY_HOLDER, public FOOTPRINT_PREVIEW_PANEL_BASE
{
    friend class FP_THREAD_IFACE;
    friend class FP_LOADER_THREAD;

public:

    virtual ~FOOTPRINT_PREVIEW_PANEL( );

    virtual void CacheFootprint( const LIB_ID& aFPID ) override;

    virtual void DisplayFootprint( const LIB_ID& aFPID ) override;

    virtual void SetStatusHandler( FOOTPRINT_STATUS_HANDLER aHandler ) override;

    virtual const KIGFX::COLOR4D& GetBackgroundColor() override;
    virtual const KIGFX::COLOR4D& GetForegroundColor() override;

    virtual wxWindow* GetWindow() override;
    BOARD* GetBoard() { return m_dummyBoard.get(); }

    static FOOTPRINT_PREVIEW_PANEL* New( KIWAY* aKiway, wxWindow* aParent );

private:
    struct CACHE_ENTRY
    {
        LIB_ID                  fpid;
        std::shared_ptr<MODULE> footprint;
        FOOTPRINT_STATUS        status;
    };

    /**
     * Create a new panel
     *
     * @param aKiway the connected KIWAY
     * @param aParent the owning WX window
     * @param aOpts the GAL options (ownership is assumed)
     * @param aGalType the displayed GAL type
     */
    FOOTPRINT_PREVIEW_PANEL( KIWAY* aKiway, wxWindow* aParent,
                             std::unique_ptr<KIGFX::GAL_DISPLAY_OPTIONS> aOpts,
                             GAL_TYPE aGalType );


    virtual CACHE_ENTRY CacheAndReturn( const LIB_ID& aFPID );

    void OnLoaderThreadUpdate( wxCommandEvent& aEvent );

    void renderFootprint( std::shared_ptr<MODULE> aFootprint );

private:
    FP_LOADER_THREAD*                m_loader;
    std::shared_ptr<FP_THREAD_IFACE> m_iface;
    FOOTPRINT_STATUS_HANDLER         m_handler;

    std::unique_ptr<BOARD>                      m_dummyBoard;
    std::unique_ptr<KIGFX::GAL_DISPLAY_OPTIONS> m_displayOptions;

    std::shared_ptr<MODULE> m_currentFootprint;
    LIB_ID                  m_currentFPID;
    bool                    m_footprintDisplayed;
};

#endif
