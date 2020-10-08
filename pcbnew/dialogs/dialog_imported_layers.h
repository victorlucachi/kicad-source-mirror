/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2020 Roberto Fernandez Bautista <roberto.fer.bau@gmail.com>
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

#ifndef DIALOG_IMPORTED_LAYERS_H
#define DIALOG_IMPORTED_LAYERS_H

#include <dialog_imported_layers_base.h>
#include <plugins/cadstar/cadstar_pcb_archive_plugin.h> // INPUT_LAYER_DESC


class DIALOG_IMPORTED_LAYERS : public DIALOG_IMPORTED_LAYERS_BASE
{
private:
    const int selected = wxLIST_STATE_SELECTED;
    const int allitems = wxLIST_STATE_DONTCARE;

    std::vector<INPUT_LAYER_DESC> m_input_layers;
    std::vector<wxString>         m_unmatched_layer_names;
    LAYER_MAP                     m_matched_layers_map;

    //Helper functions
    PCB_LAYER_ID GetSelectedLayerID();
    PCB_LAYER_ID GetAutoMatchLayerID( wxString aInputLayerName );
    void         AddMappings();
    void         RemoveMappings( int aStatus );
    void         DeleteListItems( const wxArrayInt& aRowsToDelete, wxListCtrl* aListCtrl );

    //Event Handlers
    void OnAutoMatchLayersClicked( wxCommandEvent& event ) override;

    void OnUnMatchedDoubleClick( wxListEvent& event ) override  { AddMappings(); }
    void OnAddClicked( wxCommandEvent& event ) override         { AddMappings(); }
    void OnMatchedDoubleClick( wxListEvent& event ) override    { RemoveMappings( selected ); }
    void OnRemoveClicked( wxCommandEvent& event ) override      { RemoveMappings( selected ); }
    void OnRemoveAllClicked( wxCommandEvent& event ) override   { RemoveMappings( allitems ); }

public:
    DIALOG_IMPORTED_LAYERS( wxWindow* aParent, const std::vector<INPUT_LAYER_DESC>& aLayerDesc );

    /**
     * @brief Creates and shows a dialog (modal) and returns the data from it after completion.
     * If the dialog is closed or cancel is pressed, returns an empty LAYER_MAP
     * @param aParent Parent window for the invoked dialog.
     * @param aLayerDesc
     * @return Mapped layers
     */
    static LAYER_MAP GetMapModal( wxWindow* aParent,
                                  const std::vector<INPUT_LAYER_DESC>& aLayerDesc );
};

#endif // DIALOG_IMPORTED_LAYERS_H