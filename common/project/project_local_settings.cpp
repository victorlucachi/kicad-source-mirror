/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2020 CERN
 * @author Jon Evans <jon@craftyjon.com>
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

#include <project.h>
#include <project/project_local_settings.h>
#include <settings/parameters.h>

const int projectLocalSettingsVersion = 2;


PROJECT_LOCAL_SETTINGS::PROJECT_LOCAL_SETTINGS( PROJECT* aProject, const wxString& aFilename ) :
        JSON_SETTINGS( aFilename, SETTINGS_LOC::PROJECT, projectLocalSettingsVersion,
                       /* aCreateIfMissing = */ true, /* aCreateIfDefault = */ false,
                       /* aWriteFile = */ true ),
        m_project( aProject ),
        m_SelectionFilter()
{
    m_params.emplace_back( new PARAM_LAMBDA<std::string>( "board.visible_layers",
            [&]() -> std::string
            {
                return m_VisibleLayers.FmtHex();
            },
            [&]( const std::string& aString )
            {
                m_VisibleLayers.ParseHex( aString.c_str(), aString.size() );
            },
            LSET::AllLayersMask().FmtHex() ) );

    m_params.emplace_back( new PARAM_LAMBDA<nlohmann::json>( "board.visible_items",
            [&]() -> nlohmann::json
            {
                nlohmann::json ret = nlohmann::json::array();

                for( size_t i = 0; i < m_VisibleItems.size(); i++ )
                    if( m_VisibleItems.test( i ) )
                        ret.push_back( i );

                return ret;
            },
            [&]( const nlohmann::json& aVal )
            {
                if( !aVal.is_array() || aVal.empty() )
                {
                    m_VisibleItems = GAL_SET::DefaultVisible();
                    return;
                }

                m_VisibleItems.reset();

                for( const nlohmann::json& entry : aVal )
                {
                    try
                    {
                        int i = entry.get<int>();
                        m_VisibleItems.set( i );
                    }
                    catch( ... )
                    {
                        // Non-integer or out of range entry in the array; ignore
                    }
                }
            },
            {} ) );

    m_params.emplace_back( new PARAM_LAMBDA<nlohmann::json>( "board.selection_filter",
            [&]() -> nlohmann::json
            {
                nlohmann::json ret;

                ret["lockedItems"] = m_SelectionFilter.lockedItems;
                ret["footprints"]  = m_SelectionFilter.footprints;
                ret["text"]        = m_SelectionFilter.text;
                ret["tracks"]      = m_SelectionFilter.tracks;
                ret["vias"]        = m_SelectionFilter.vias;
                ret["pads"]        = m_SelectionFilter.pads;
                ret["graphics"]    = m_SelectionFilter.graphics;
                ret["zones"]       = m_SelectionFilter.zones;
                ret["keepouts"]    = m_SelectionFilter.keepouts;
                ret["dimensions"]  = m_SelectionFilter.dimensions;
                ret["otherItems"]  = m_SelectionFilter.otherItems;

                return ret;
            },
            [&]( const nlohmann::json& aVal )
            {
                if( aVal.empty() || !aVal.is_object() )
                    return;

                SetIfPresent( aVal, "lockedItems", m_SelectionFilter.lockedItems );
                SetIfPresent( aVal, "footprints", m_SelectionFilter.footprints );
                SetIfPresent( aVal, "text", m_SelectionFilter.text );
                SetIfPresent( aVal, "tracks", m_SelectionFilter.tracks );
                SetIfPresent( aVal, "vias", m_SelectionFilter.vias );
                SetIfPresent( aVal, "pads", m_SelectionFilter.pads );
                SetIfPresent( aVal, "graphics", m_SelectionFilter.graphics );
                SetIfPresent( aVal, "zones", m_SelectionFilter.zones );
                SetIfPresent( aVal, "keepouts", m_SelectionFilter.keepouts );
                SetIfPresent( aVal, "dimensions", m_SelectionFilter.dimensions );
                SetIfPresent( aVal, "otherItems", m_SelectionFilter.otherItems );
            },
            {
                { "lockedItems", true },
                { "footprints", true },
                { "text", true },
                { "tracks", true },
                { "vias", true },
                { "pads", true },
                { "graphics", true },
                { "zones", true },
                { "keepouts", true },
                { "dimensions", true },
                { "otherItems", true }
            } ) );

    m_params.emplace_back( new PARAM_ENUM<PCB_LAYER_ID>(
            "board.active_layer", &m_ActiveLayer, F_Cu, PCBNEW_LAYER_ID_START, F_Fab ) );

    m_params.emplace_back( new PARAM<wxString>( "board.active_layer_preset",
            &m_ActiveLayerPreset, "" ) );

    m_params.emplace_back( new PARAM_ENUM<HIGH_CONTRAST_MODE>( "board.high_contrast_mode",
            &m_ContrastModeDisplay, HIGH_CONTRAST_MODE::NORMAL, HIGH_CONTRAST_MODE::NORMAL,
            HIGH_CONTRAST_MODE::HIDDEN ) );

    m_params.emplace_back( new PARAM<double>( "board.opacity.tracks", &m_TrackOpacity, 1.0 ) );
    m_params.emplace_back( new PARAM<double>( "board.opacity.vias", &m_ViaOpacity, 1.0 ) );
    m_params.emplace_back( new PARAM<double>( "board.opacity.pads", &m_PadOpacity, 1.0 ) );
    m_params.emplace_back( new PARAM<double>( "board.opacity.zones", &m_ZoneOpacity, 0.6 ) );

    m_params.emplace_back( new PARAM_LIST<wxString>( "board.hidden_nets", &m_HiddenNets, {} ) );

    m_params.emplace_back( new PARAM_ENUM<NET_COLOR_MODE>( "board.net_color_mode",
                           &m_NetColorMode, NET_COLOR_MODE::RATSNEST, NET_COLOR_MODE::OFF,
                           NET_COLOR_MODE::ALL ) );

    m_params.emplace_back( new PARAM_ENUM<RATSNEST_MODE>( "board.ratsnest_display_mode",
                           &m_RatsnestMode, RATSNEST_MODE::ALL, RATSNEST_MODE::ALL,
                           RATSNEST_MODE::VISIBLE ) );

    // TODO: move the rest of PCB_DISPLAY_OPTIONS that are project-specific in here
#if 0
    m_params.emplace_back( new PARAM_ENUM<ZONE_DISPLAY_MODE>( "board.zone_display_mode",
            &m_ZoneDisplayMode, ZONE_DISPLAY_MODE::SHOW_FILLED, ZONE_DISPLAY_MODE::SHOW_OUTLINED,
            ZONE_DISPLAY_MODE::SHOW_FILLED ) );
#endif

    m_params.emplace_back( new PARAM_LAMBDA<nlohmann::json>( "project.files",
            [&]() -> nlohmann::json
            {
                nlohmann::json ret = nlohmann::json::array();

                for( PROJECT_FILE_STATE& fileState : m_files )
                {
                    nlohmann::json file;
                    file["name"] = fileState.fileName;
                    file["open"] = fileState.open;

                    nlohmann::json window;
                    window["maximized"] = fileState.window.maximized;
                    window["size_x"]    = fileState.window.size_x;
                    window["size_y"]    = fileState.window.size_y;
                    window["pos_x"]     = fileState.window.pos_x;
                    window["pos_y"]     = fileState.window.pos_y;
                    window["display"]   = fileState.window.display;

                    file["window"] = window;

                    ret.push_back( file );
                }

                return ret;
            },
            [&]( const nlohmann::json& aVal )
            {
                if( !aVal.is_array() || aVal.empty() )
                {
                    return;
                }

                for( const nlohmann::json& file : aVal )
                {
                    PROJECT_FILE_STATE fileState;
                    try
                    {
                        SetIfPresent( file, "name", fileState.fileName );
                        SetIfPresent( file, "open", fileState.open );
                        SetIfPresent( file, "window.size_x", fileState.window.size_x );
                        SetIfPresent( file, "window.size_y", fileState.window.size_y );
                        SetIfPresent( file, "window.pos_x", fileState.window.pos_x );
                        SetIfPresent( file, "window.pos_y", fileState.window.pos_y );
                        SetIfPresent( file, "window.maximized", fileState.window.maximized );
                        SetIfPresent( file, "window.display", fileState.window.display );

                        m_files.push_back( fileState );
                    }
                    catch( ... )
                    {
                        // Non-integer or out of range entry in the array; ignore
                    }
                }

            },
            {
            } ) );

    registerMigration( 1, 2,
            [&]()
            {
                /**
                 * Schema version 1 to 2:
                 * LAYER_PADS and LAYER_ZONES added to visibility controls
                 */

                nlohmann::json::json_pointer ptr( "/board/visible_items"_json_pointer );

                if( contains( ptr ) )
                {
                    if( ( *this )[ptr].is_array() )
                    {
                        ( *this )[ptr].push_back( LAYER_PADS );
                        ( *this )[ptr].push_back( LAYER_ZONES );
                    }
                    else
                    {
                        at( "board" ).erase( "visible_items" );
                    }
                }

                return true;
            } );
}


bool PROJECT_LOCAL_SETTINGS::MigrateFromLegacy( wxConfigBase* aLegacyConfig )
{
    /**
     * The normal legacy migration code won't be used for this because the only legacy
     * information stored here was stored in board files, so we do that migration when loading
     * the board.
     */
    return true;
}


bool PROJECT_LOCAL_SETTINGS::SaveToFile( const wxString& aDirectory, bool aForce )
{
    wxASSERT( m_project );

    ( *this )[PointerFromString( "meta.filename" )] =
            m_project->GetProjectName() + "." + ProjectLocalSettingsFileExtension;

    return JSON_SETTINGS::SaveToFile( aDirectory, aForce );
}


const PROJECT_FILE_STATE* PROJECT_LOCAL_SETTINGS::GetFileState( const wxString& aFileName )
{
    auto it = std::find_if( m_files.begin(), m_files.end(), 
                            [&aFileName]( const PROJECT_FILE_STATE &a )
                            {
                                return a.fileName == aFileName;
                            } );

    if( it != m_files.end() )
    {
        return &( *it );
    }

    return nullptr;
}


void PROJECT_LOCAL_SETTINGS::SaveFileState( const wxString& aFileName,
                                            const WINDOW_SETTINGS* aWindowCfg, bool aOpen )
{
    auto it = std::find_if( m_files.begin(), m_files.end(),
                            [&aFileName]( const PROJECT_FILE_STATE& a ) 
                            { 
                                return a.fileName == aFileName; 
                            } );

    if( it == m_files.end() )
    {
        PROJECT_FILE_STATE fileState;
        fileState.fileName = aFileName;

        m_files.push_back( fileState );

        it = m_files.end() - 1;
    }

    ( *it ).window = aWindowCfg->state;
    ( *it ).open   = aOpen;
}


void PROJECT_LOCAL_SETTINGS::ClearFileState()
{
    m_files.clear();
}
