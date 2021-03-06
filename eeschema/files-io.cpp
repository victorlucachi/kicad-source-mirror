/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2013 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2013 Wayne Stambaugh <stambaughw@gmail.com>
 * Copyright (C) 2013 CERN (www.cern.ch)
 * Copyright (C) 1992-2020 KiCad Developers, see AUTHORS.txt for contributors.
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

#include <advanced_config.h>
#include <class_library.h>
#include <confirm.h>
#include <connection_graph.h>
#include <dialog_migrate_buses.h>
#include <dialog_symbol_remap.h>
#include <eeschema_settings.h>
#include <gestfich.h>
#include <id.h>
#include <kiface_i.h>
#include <kiplatform/app.h>
#include <pgm_base.h>
#include <profile.h>
#include <project/project_file.h>
#include <project_rescue.h>
#include <reporter.h>
#include <richio.h>
#include <sch_component.h>
#include <sch_edit_frame.h>
#include <sch_plugins/legacy/sch_legacy_plugin.h>
#include <sch_sheet.h>
#include <sch_sheet_path.h>
#include <schematic.h>
#include <settings/common_settings.h>
#include <settings/settings_manager.h>
#include <symbol_lib_table.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/sch_editor_control.h>
#include <trace_helpers.h>
#include <widgets/infobar.h>
#include <wildcards_and_files_ext.h>
#include <page_layout/ws_data_model.h>
#include <wx/stdpaths.h>
#include <tools/ee_inspection_tool.h>

bool SCH_EDIT_FRAME::SaveEEFile( SCH_SHEET* aSheet, bool aSaveUnderNewName )
{
    wxString msg;
    wxFileName schematicFileName;
    bool success;

    if( aSheet == NULL )
        aSheet = GetCurrentSheet().Last();

    SCH_SCREEN* screen = aSheet->GetScreen();

    wxCHECK( screen, false );

    // If no name exists in the window yet - save as new.
    if( screen->GetFileName().IsEmpty() )
        aSaveUnderNewName = true;

    // Construct the name of the file to be saved
    schematicFileName = Prj().AbsolutePath( screen->GetFileName() );

    if( aSaveUnderNewName )
    {
        wxFileName savePath( Prj().GetProjectFullName() );

        if( !savePath.IsOk() || !savePath.IsDirWritable() )
        {
            savePath = GetMruPath();

            if( !savePath.IsOk() || !savePath.IsDirWritable() )
                savePath = wxStandardPaths::Get().GetDocumentsDir();
        }

        wxFileDialog dlg( this, _( "Schematic Files" ), savePath.GetPath(),
                          schematicFileName.GetFullName(), KiCadSchematicFileWildcard(),
                          wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

        if( dlg.ShowModal() == wxID_CANCEL )
            return false;

        schematicFileName = dlg.GetPath();

        if( schematicFileName.GetExt().IsEmpty() )
            schematicFileName.SetExt( KiCadSchematicFileExtension );
    }

    if( !IsWritable( schematicFileName ) )
        return false;

    wxFileName tempFile( schematicFileName );
    tempFile.SetName( wxT( "." ) + tempFile.GetName() );
    tempFile.SetExt( tempFile.GetExt() + wxT( "$" ) );

    // Save
    wxLogTrace( traceAutoSave,
                wxT( "Saving file <" ) + schematicFileName.GetFullPath() + wxT( ">" ) );

    SCH_IO_MGR::SCH_FILE_T pluginType = SCH_IO_MGR::GuessPluginTypeFromSchPath(
            schematicFileName.GetFullPath() );
    SCH_PLUGIN::SCH_PLUGIN_RELEASER pi( SCH_IO_MGR::FindPlugin( pluginType ) );

    try
    {
        pi->Save( tempFile.GetFullPath(), aSheet, &Schematic() );
        success = true;
    }
    catch( const IO_ERROR& ioe )
    {
        msg.Printf( _( "Error saving schematic file \"%s\".\n%s" ),
                    schematicFileName.GetFullPath(), ioe.What() );
        DisplayError( this, msg );

        msg.Printf( _( "Failed to create temporary file \"%s\"" ), tempFile.GetFullPath() );
        AppendMsgPanel( wxEmptyString, msg, CYAN );

        // In case we started a file but didn't fully write it, clean up
        wxRemoveFile( tempFile.GetFullPath() );

        success = false;
    }

    if( success )
    {
        // Replace the original with the temporary file we just wrote
        success = wxRenameFile( tempFile.GetFullPath(), schematicFileName.GetFullPath() );

        if( !success )
        {
            msg.Printf( _( "Error saving schematic file \"%s\".\n"
                           "Failed to rename temporary file %s" ),
                        schematicFileName.GetFullPath(), tempFile.GetFullPath() );
            DisplayError( this, msg );

            msg.Printf( _( "Failed to rename temporary file \"%s\"" ), tempFile.GetFullPath() );
            AppendMsgPanel( wxEmptyString, msg, CYAN );
        }
    }

    if( success )
    {
        // Delete auto save file.
        wxFileName autoSaveFileName = schematicFileName;
        autoSaveFileName.SetName( GetAutoSaveFilePrefix() + schematicFileName.GetName() );

        if( autoSaveFileName.FileExists() )
        {
            wxLogTrace( traceAutoSave,
                        wxT( "Removing auto save file <" ) + autoSaveFileName.GetFullPath() +
                        wxT( ">" ) );

            wxRemoveFile( autoSaveFileName.GetFullPath() );
        }

        // Update the screen and frame info and reset the lock file.
        if( aSaveUnderNewName )
        {
            screen->SetFileName( schematicFileName.GetFullPath() );
            LockFile( schematicFileName.GetFullPath() );
        }

        screen->ClrSave();
        screen->ClrModify();

        msg.Printf( _( "File %s saved" ),  screen->GetFileName() );
        SetStatusText( msg, 0 );
    }
    else
    {
        DisplayError( this, _( "File write operation failed." ) );
    }

    return success;
}


void SCH_EDIT_FRAME::Save_File( bool doSaveAs )
{
    if( doSaveAs )
    {
        if( SaveEEFile( NULL, true ) )
        {
            SCH_SCREEN* screen = GetScreen();

            wxCHECK( screen, /* void */ );

            wxFileName fn = screen->GetFileName();

            if( fn.GetExt() == LegacySchematicFileExtension )
                CreateArchiveLibraryCacheFile( true );

            // If we are saving under a new name, and don't have a real project yet, create one
            fn.SetExt( ProjectFileExtension );

            if( fn.IsDirWritable() && !fn.FileExists() )
            {
                Prj().SetReadOnly( false );
                GetSettingsManager()->SaveProjectAs( fn.GetFullPath() );
            }
        }
    }
    else
    {
        SaveEEFile( NULL );
    }

    UpdateTitle();
}


bool SCH_EDIT_FRAME::OpenProjectFiles( const std::vector<wxString>& aFileSet, int aCtl )
{
    // implement the pseudo code from KIWAY_PLAYER.h:
    wxString msg;

    auto cfg = dynamic_cast<EESCHEMA_SETTINGS*>( Kiface().KifaceSettings() );

    // This is for python:
    if( aFileSet.size() != 1 )
    {
        msg.Printf( "Eeschema:%s() takes only a single filename.", __WXFUNCTION__ );
        DisplayError( this, msg );
        return false;
    }

    wxString fullFileName( aFileSet[0] );

    // We insist on caller sending us an absolute path, if it does not, we say it's a bug.
    wxASSERT_MSG( wxFileName( fullFileName ).IsAbsolute(), wxT( "Path is not absolute!" ) );

    if( !LockFile( fullFileName ) )
    {
        msg.Printf( _( "Schematic file \"%s\" is already open." ), fullFileName );
        DisplayError( this, msg );
        return false;
    }

    if( !AskToSaveChanges() )
        return false;

    PROF_COUNTER openFiles( "OpenProjectFile" );

    wxFileName pro = fullFileName;
    pro.SetExt( ProjectFileExtension );

    bool is_new = !wxFileName::IsFileReadable( fullFileName );

    // If its a non-existent schematic and caller thinks it exists
    if( is_new && !( aCtl & KICTL_CREATE ) )
    {
        // notify user that fullFileName does not exist, ask if user wants to create it.
        msg.Printf( _( "Schematic \"%s\" does not exist.  Do you wish to create it?" ),
                    fullFileName );

        if( !IsOK( this, msg ) )
            return false;
    }

    // unload current project file before loading new
    {
        SetScreen( nullptr );
        m_toolManager->GetTool<EE_INSPECTION_TOOL>()->Reset( TOOL_BASE::MODEL_RELOAD );
        CreateScreens();
    }

    SetStatusText( wxEmptyString );
    ClearMsgPanel();

    SCH_IO_MGR::SCH_FILE_T schFileType = SCH_IO_MGR::GuessPluginTypeFromSchPath( fullFileName );

    // PROJECT::SetProjectFullName() is an impactful function.  It should only be
    // called under carefully considered circumstances.

    // The calling code should know not to ask me here to change projects unless
    // it knows what consequences that will have on other KIFACEs running and using
    // this same PROJECT.  It can be very harmful if that calling code is stupid.

    // NOTE: The calling code should never call this in hosted (non-standalone) mode with a
    // different project than what has been loaded by the manager frame.  This will crash.

    bool differentProject = pro.GetFullPath() != Prj().GetProjectFullName();

    if( differentProject )
    {
        if( !Prj().IsNullProject() )
            GetSettingsManager()->SaveProject();

        Schematic().SetProject( nullptr );
        GetSettingsManager()->UnloadProject( &Prj() );

        GetSettingsManager()->LoadProject( pro.GetFullPath() );

        // Do not allow saving a project if one doesn't exist.  This normally happens if we are
        // standalone and opening a schematic that has been moved from its project folder.
        if( !pro.Exists() && !( aCtl & KICTL_CREATE ) )
            Prj().SetReadOnly();

        CreateScreens();
    }

    if( schFileType == SCH_IO_MGR::SCH_LEGACY )
    {
        // Don't reload the symbol libraries if we are just launching Eeschema from KiCad again.
        // They are already saved in the kiface project object.
        if( differentProject || !Prj().GetElem( PROJECT::ELEM_SCH_PART_LIBS ) )
        {
            // load the libraries here, not in SCH_SCREEN::Draw() which is a context
            // that will not tolerate DisplayError() dialog since we're already in an
            // event handler in there.
            // And when a schematic file is loaded, we need these libs to initialize
            // some parameters (links to PART LIB, dangling ends ...)
            Prj().SetElem( PROJECT::ELEM_SCH_PART_LIBS, NULL );
            Prj().SchLibs();
        }
    }
    else
    {
        // No legacy symbol libraries including the cache are loaded with the new file format.
        Prj().SetElem( PROJECT::ELEM_SCH_PART_LIBS, NULL );
    }

    // Load the symbol library table, this will be used forever more.
    Prj().SetElem( PROJECT::ELEM_SYMBOL_LIB_TABLE, NULL );
    Prj().SchSymbolLibTable();

    // Load project settings after schematic has been set up with the project link, since this will
    // update some of the needed schematic settings such as drawing defaults
    LoadProjectSettings();

    wxFileName rfn( GetCurrentFileName() );
    rfn.MakeRelativeTo( Prj().GetProjectPath() );
    LoadWindowState( rfn.GetFullPath() );

    KIPLATFORM::APP::SetShutdownBlockReason( this, _( "Schematic file changes are unsaved" ) );

    if( Kiface().IsSingle() )
    {
        KIPLATFORM::APP::RegisterApplicationRestart( fullFileName );
    }

    if( is_new )
    {
        // mark new, unsaved file as modified.
        GetScreen()->SetModify();
        GetScreen()->SetFileName( fullFileName );
    }
    else
    {
        // This will rename the file if there is an autosave and the user want to recover.
		CheckForAutoSaveFile( fullFileName );

        SetScreen( nullptr );

        SCH_PLUGIN* plugin = SCH_IO_MGR::FindPlugin( schFileType );
        SCH_PLUGIN::SCH_PLUGIN_RELEASER pi( plugin );

        try
        {
            Schematic().SetRoot( pi->Load( fullFileName, &Schematic() ) );

            if( !pi->GetError().IsEmpty() )
            {
                DisplayErrorMessage( this,
                                     _( "The entire schematic could not be loaded.  Errors "
                                        "occurred attempting to load \nhierarchical sheet "
                                        "schematics." ),
                                     pi->GetError() );
            }
        }
        catch( const IO_ERROR& ioe )
        {
            // Do not leave g_RootSheet == NULL because it is expected to be
            // a valid sheet. Therefore create a dummy empty root sheet and screen.
            CreateScreens();
            m_toolManager->RunAction( ACTIONS::zoomFitScreen, true );

            msg.Printf( _( "Error loading schematic file \"%s\".\n%s" ),
                        fullFileName, ioe.What() );
            DisplayError( this, msg );

            msg.Printf( _( "Failed to load \"%s\"" ), fullFileName );
            AppendMsgPanel( wxEmptyString, msg, CYAN );

            return false;
        }

        // It's possible the schematic parser fixed errors due to bugs so warn the user
        // that the schematic has been fixed (modified).
        SCH_SHEET_LIST sheetList = Schematic().GetSheets();

        if( sheetList.IsModified() )
        {
            DisplayInfoMessage( this,
                                _( "An error was found when loading the schematic that has "
                                   "been automatically fixed.  Please save the schematic to "
                                   "repair the broken file or it may not be usable with other "
                                   "versions of KiCad." ) );
        }

        if( sheetList.AllSheetPageNumbersEmpty() )
            sheetList.SetInitialPageNumbers();

        UpdateFileHistory( fullFileName );

        SCH_SCREENS schematic( Schematic().Root() );

        // LIB_ID checks and symbol rescue only apply to the legacy file formats.
        if( schFileType == SCH_IO_MGR::SCH_LEGACY )
        {
            // Convert old projects over to use symbol library table.
            if( schematic.HasNoFullyDefinedLibIds() )
            {
                DIALOG_SYMBOL_REMAP dlgRemap( this );

                dlgRemap.ShowQuasiModal();
            }
            else
            {
                // Double check to ensure no legacy library list entries have been
                // added to the projec file symbol library list.
                wxString paths;
                wxArrayString libNames;

                PART_LIBS::LibNamesAndPaths( &Prj(), false, &paths, &libNames );

                if( !libNames.IsEmpty() )
                {
                    if( eeconfig()->m_Appearance.show_illegal_symbol_lib_dialog )
                    {
                        wxRichMessageDialog invalidLibDlg(
                                this,
                                _( "Illegal entry found in project file symbol library list." ),
                                _( "Project Load Warning" ),
                                wxOK | wxCENTER | wxICON_EXCLAMATION );
                        invalidLibDlg.ShowDetailedText(
                                _( "Symbol libraries defined in the project file symbol library "
                                   "list are no longer supported and will be removed.\n\nThis may "
                                   "cause broken symbol library links under certain conditions." ) );
                        invalidLibDlg.ShowCheckBox( _( "Do not show this dialog again." ) );
                        invalidLibDlg.ShowModal();
                        eeconfig()->m_Appearance.show_illegal_symbol_lib_dialog =
                                !invalidLibDlg.IsCheckBoxChecked();
                    }

                    libNames.Clear();
                    paths.Clear();
                    PART_LIBS::LibNamesAndPaths( &Prj(), true, &paths, &libNames );
                }

                if( !cfg || !cfg->m_RescueNeverShow )
                {
                    SCH_EDITOR_CONTROL* editor = m_toolManager->GetTool<SCH_EDITOR_CONTROL>();
                    editor->RescueSymbolLibTableProject( false );
                }
            }

            // Update all symbol library links for all sheets.
            schematic.UpdateSymbolLinks();

            if( !cfg || cfg->m_Appearance.show_sexpr_file_convert_warning )
            {
                wxRichMessageDialog newFileFormatDlg(
                        this,
                        _( "The schematic file will be converted to the new file format on save." ),
                        _( "Project Load Warning" ),
                        wxOK | wxCENTER | wxICON_EXCLAMATION );
                newFileFormatDlg.ShowDetailedText(
                        _( "This schematic was saved in the legacy file format which is no "
                           "longer supported and will be saved using the new file format.\n\nThe "
                           "new file format cannot be opened with previous versions of KiCad." ) );
                newFileFormatDlg.ShowCheckBox( _( "Do not show this dialog again." ) );
                newFileFormatDlg.ShowModal();

                if( cfg )
                    cfg->m_Appearance.show_sexpr_file_convert_warning =
                            !newFileFormatDlg.IsCheckBoxChecked();
            }

            // Legacy schematic can have duplicate time stamps so fix that before converting
            // to the s-expression format.
            schematic.ReplaceDuplicateTimeStamps();

            // Allow the schematic to be saved to new file format without making any edits.
            OnModify();
        }
        else  // S-expression schematic.
        {
            for( SCH_SCREEN* screen = schematic.GetFirst(); screen; screen = schematic.GetNext() )
                screen->UpdateLocalLibSymbolLinks();

            // Restore all of the loaded symbol and sheet instances from the root sheet.
            sheetList.UpdateSymbolInstances( Schematic().RootScreen()->GetSymbolInstances() );
            sheetList.UpdateSheetInstances( Schematic().RootScreen()->GetSheetInstances() );
        }

        Schematic().ConnectionGraph()->Reset();

        SetScreen( GetCurrentSheet().LastScreen() );

        // Migrate conflicting bus definitions
        // TODO(JE) This should only run once based on schematic file version
        if( Schematic().ConnectionGraph()->GetBusesNeedingMigration().size() > 0 )
        {
            DIALOG_MIGRATE_BUSES dlg( this );
            dlg.ShowQuasiModal();
            RecalculateConnections( NO_CLEANUP );
            OnModify();
        }

        GetScreen()->TestDanglingEnds();    // Only perform the dangling end test on root sheet.
        RecalculateConnections( GLOBAL_CLEANUP );
        ClearUndoRedoList();
        GetScreen()->m_Initialized = true;
    }

    m_toolManager->RunAction( ACTIONS::zoomFitScreen, true );
    SetSheetNumberAndCount();

    // re-create junctions if needed. Eeschema optimizes wires by merging
    // colinear segments. If a schematic is saved without a valid
    // cache library or missing installed libraries, this can cause connectivity errors
    // unless junctions are added.
    FixupJunctions();

    SyncView();
    GetScreen()->ClearDrawingState();

    UpdateTitle();

    wxFileName fn = Prj().AbsolutePath( GetScreen()->GetFileName() );
    m_infoBar->Dismiss();

    if( fn.FileExists() && !fn.IsFileWritable() )
    {
        m_infoBar->RemoveAllButtons();
        m_infoBar->AddCloseButton();
        m_infoBar->ShowMessage( "Schematic file is read only.", wxICON_WARNING );
    }

#ifdef PROFILE
    openFiles.Show();
#endif

    return true;
}


bool SCH_EDIT_FRAME::AppendSchematic()
{
    wxString    fullFileName;
    SCH_SCREEN* screen = GetScreen();

    if( !screen )
    {
        wxLogError( wxT( "Document not ready, cannot import" ) );
        return false;
    }

    // open file chooser dialog
    wxString path = wxPathOnly( Prj().GetProjectFullName() );

    wxFileDialog dlg( this, _( "Append Schematic" ), path, wxEmptyString,
                      KiCadSchematicFileWildcard(), wxFD_OPEN | wxFD_FILE_MUST_EXIST );

    if( dlg.ShowModal() == wxID_CANCEL )
        return false;

    fullFileName = dlg.GetPath();

    if( !LoadSheetFromFile( GetCurrentSheet().Last(), &GetCurrentSheet(), fullFileName ) )
        return false;

    SCH_SCREENS screens( GetCurrentSheet().Last() );
    screens.TestDanglingEnds();

    m_toolManager->RunAction( ACTIONS::zoomFitScreen, true );
    SetSheetNumberAndCount();

    SyncView();
    HardRedraw();   // Full reinit of the current screen and the display.
    OnModify();

    return true;
}


void SCH_EDIT_FRAME::OnAppendProject( wxCommandEvent& event )
{
    if( GetScreen() && GetScreen()->IsModified() )
    {
        wxString msg = _( "This operation cannot be undone.\n\n"
                          "Do you want to save the current document before proceeding?" );

        if( IsOK( this, msg ) )
            SaveProject();
    }

    AppendSchematic();
}


void SCH_EDIT_FRAME::OnImportProject( wxCommandEvent& aEvent )
{
    if( !AskToSaveChanges() )
        return;

    // Set the project location if none is set
    bool setProject = Prj().GetProjectFullName().IsEmpty();
    wxString path = wxPathOnly( Prj().GetProjectFullName() );

    // clang-format off
    std::list<std::pair<const wxString, const SCH_IO_MGR::SCH_FILE_T>> loaders;

    if( ADVANCED_CFG::GetCfg().m_PluginAltiumSch )
        loaders.emplace_back( AltiumSchematicFileWildcard(), SCH_IO_MGR::SCH_ALTIUM ); // Import Altium schematic files

    loaders.emplace_back( CadstarSchematicArchiveFileWildcard(), SCH_IO_MGR::SCH_CADSTAR_ARCHIVE ); //Import CADSTAR Schematic Archive files
    loaders.emplace_back( EagleSchematicFileWildcard(),  SCH_IO_MGR::SCH_EAGLE ); // Import Eagle schematic files
    // clang-format on

    wxString fileFilters;
    wxString allWildcards;

    for( auto& loader : loaders )
    {
        if( !fileFilters.IsEmpty() )
            fileFilters += wxChar( '|' );

        fileFilters += wxGetTranslation( loader.first );

        SCH_PLUGIN::SCH_PLUGIN_RELEASER plugin( SCH_IO_MGR::FindPlugin( loader.second ) );
        wxCHECK( plugin, /*void*/ );
        allWildcards += "*." + formatWildcardExt( plugin->GetFileExtension() ) + ";";
    }

    fileFilters = _( "All supported formats|" ) + allWildcards + "|" + fileFilters;

    wxFileDialog dlg( this, _( "Import Schematic" ), path, wxEmptyString, fileFilters,
            wxFD_OPEN | wxFD_FILE_MUST_EXIST ); // TODO

    if( dlg.ShowModal() == wxID_CANCEL )
        return;

    if( setProject )
    {
        if( !Prj().IsNullProject() )
            GetSettingsManager()->SaveProject();

        Schematic().SetProject( nullptr );
        GetSettingsManager()->UnloadProject( &Prj() );

        Schematic().Reset();

        wxFileName projectFn( dlg.GetPath() );
        projectFn.SetExt( ProjectFileExtension );
        GetSettingsManager()->LoadProject( projectFn.GetFullPath() );

        Schematic().SetProject( &Prj() );
    }

    wxFileName fn = dlg.GetPath();

    SCH_IO_MGR::SCH_FILE_T pluginType = SCH_IO_MGR::SCH_FILE_T::SCH_FILE_UNKNOWN;

    for( auto& loader : loaders )
    {
        if( fn.GetExt().CmpNoCase( SCH_IO_MGR::GetFileExtension( loader.second ) ) == 0 )
        {
            pluginType = loader.second;
            break;
        }
    }

    if( pluginType == SCH_IO_MGR::SCH_FILE_T::SCH_FILE_UNKNOWN )
    {
        wxLogError( wxString::Format( "unexpected file extension: %s", fn.GetExt() ) );
        return;
    }

    importFile( dlg.GetPath(), pluginType );
}


bool SCH_EDIT_FRAME::SaveProject()
{
    wxString msg;
    SCH_SCREEN* screen;
    SCH_SCREENS screens( Schematic().Root() );
    bool success = true;
    bool updateFileType = false;

    // I want to see it in the debugger, show me the string!  Can't do that with wxFileName.
    wxString    fileName = Prj().AbsolutePath( Schematic().Root().GetFileName() );
    wxFileName  fn = fileName;

    if( fn.IsOk() && !fn.IsDirWritable() )
    {
        msg = wxString::Format( _( "Directory \"%s\" is not writable." ), fn.GetPath() );
        DisplayError( this, msg );
        return false;
    }

    // Warn user on potential file overwrite.  This can happen on shared sheets.
    wxArrayString overwrittenFiles;

    for( size_t i = 0; i < screens.GetCount(); i++ )
    {
        screen = screens.GetScreen( i );

        wxCHECK2( screen, continue );

        // Convert legacy schematics file name extensions for the new format.
        wxFileName tmpFn = screen->GetFileName();

        if( !tmpFn.IsOk() )
            continue;

        if( tmpFn.GetExt() == KiCadSchematicFileExtension )
            continue;

        tmpFn.SetExt( KiCadSchematicFileExtension );

        if( tmpFn.FileExists() )
            overwrittenFiles.Add( tmpFn.GetFullPath() );
    }

    if( !overwrittenFiles.IsEmpty() )
    {
        for( auto overwrittenFile : overwrittenFiles )
        {
            if( msg.IsEmpty() )
                msg = overwrittenFile;
            else
                msg += "\n" + overwrittenFile;
        }

        wxRichMessageDialog dlg(
                this,
                _( "Saving the project to the new file format will overwrite existing files." ),
                _( "Project Save Warning" ),
                wxOK | wxCANCEL | wxCANCEL_DEFAULT | wxCENTER | wxICON_EXCLAMATION );
        dlg.ShowDetailedText( wxString::Format(
                              _( "The following files will be overwritten:\n\n%s" ), msg ) );
        dlg.SetOKCancelLabels( wxMessageDialog::ButtonLabel( _( "Overwrite Files" ) ),
                wxMessageDialog::ButtonLabel( _( "Abort Project Save" ) ) );

        if( dlg.ShowModal() == wxID_CANCEL )
            return false;
    }

    screens.BuildClientSheetPathList();

    for( size_t i = 0; i < screens.GetCount(); i++ )
    {
        screen = screens.GetScreen( i );

        wxCHECK2( screen, continue );

        // Convert legacy schematics file name extensions for the new format.
        wxFileName tmpFn = screen->GetFileName();

        if( tmpFn.IsOk() && tmpFn.GetExt() != KiCadSchematicFileExtension )
        {
            updateFileType = true;
            tmpFn.SetExt( KiCadSchematicFileExtension );

            for( auto item : screen->Items().OfType( SCH_SHEET_T ) )
            {
                SCH_SHEET* sheet = static_cast<SCH_SHEET*>( item );
                wxFileName sheetFileName = sheet->GetFileName();

                if( !sheetFileName.IsOk() || sheetFileName.GetExt() == KiCadSchematicFileExtension )
                    continue;

                sheetFileName.SetExt( KiCadSchematicFileExtension );
                sheet->SetFileName( sheetFileName.GetFullPath() );
                UpdateItem( sheet );
            }

            screen->SetFileName( tmpFn.GetFullPath() );
        }

        std::vector<SCH_SHEET_PATH>& sheets = screen->GetClientSheetPaths();

        if( sheets.size() == 1 )
            screen->SetVirtualPageNumber( 1 );
        else
            screen->SetVirtualPageNumber( 0 );  // multiple uses; no way to store the real sheet #

        success &= SaveEEFile( screens.GetSheet( i ) );
    }

    if( updateFileType )
        UpdateFileHistory( Schematic().RootScreen()->GetFileName() );

    // Save the sheet name map to the project file
    std::vector<FILE_INFO_PAIR>& sheets = Prj().GetProjectFile().GetSheets();
    sheets.clear();

    for( SCH_SHEET_PATH& sheetPath : Schematic().GetSheets() )
    {
        SCH_SHEET* sheet = sheetPath.Last();
        sheets.emplace_back( std::make_pair( sheet->m_Uuid, sheet->GetName() ) );
    }

    if( !Prj().IsNullProject() )
        Pgm().GetSettingsManager().SaveProject();

    if( !Kiface().IsSingle() )
    {
        WX_STRING_REPORTER backupReporter( &msg );

        if( !GetSettingsManager()->TriggerBackupIfNeeded( backupReporter ) )
            SetStatusText( msg, 0 );
    }

    UpdateTitle();

    return success;
}


bool SCH_EDIT_FRAME::doAutoSave()
{
    wxFileName  tmpFileName = Schematic().Root().GetFileName();
    wxFileName  fn = tmpFileName;
    wxFileName  tmp;
    SCH_SCREENS screens( Schematic().Root() );

    bool autoSaveOk = true;

    tmp.AssignDir( fn.GetPath() );

    if( !tmp.IsOk() )
        return false;

    if( !IsWritable( tmp ) )
        return false;

    for( size_t i = 0; i < screens.GetCount(); i++ )
    {
        // Only create auto save files for the schematics that have been modified.
        if( !screens.GetScreen( i )->IsSave() )
            continue;

        tmpFileName = fn = screens.GetScreen( i )->GetFileName();

        // Auto save file name is the normal file name prefixed with GetAutoSavePrefix().
        fn.SetName( GetAutoSaveFilePrefix() + fn.GetName() );

        screens.GetScreen( i )->SetFileName( fn.GetFullPath() );

        if( SaveEEFile( screens.GetSheet( i ), false ) )
            screens.GetScreen( i )->SetModify();
        else
            autoSaveOk = false;

        screens.GetScreen( i )->SetFileName( tmpFileName.GetFullPath() );
    }

    if( autoSaveOk )
    {
        m_autoSaveState = false;

        if( !Kiface().IsSingle() &&
            GetSettingsManager()->GetCommonSettings()->m_Backup.backup_on_autosave )
        {
            GetSettingsManager()->TriggerBackupIfNeeded( NULL_REPORTER::GetInstance() );
        }
    }

    return autoSaveOk;
}


bool SCH_EDIT_FRAME::importFile( const wxString& aFileName, int aFileType )
{
    wxFileName newfilename;
    SCH_SHEET_LIST sheetList = Schematic().GetSheets();

    switch( (SCH_IO_MGR::SCH_FILE_T) aFileType )
    {
    case SCH_IO_MGR::SCH_ALTIUM:
    case SCH_IO_MGR::SCH_CADSTAR_ARCHIVE:
    case SCH_IO_MGR::SCH_EAGLE:
        // We insist on caller sending us an absolute path, if it does not, we say it's a bug.
        wxASSERT_MSG( wxFileName( aFileName ).IsAbsolute(),
                      wxT( "Import eagle schematic caller didn't send full filename" ) );

        if( !LockFile( aFileName ) )
        {
            wxString msg = wxString::Format( _( "Schematic file \"%s\" is already open." ),
                                             aFileName );
            DisplayError( this, msg );
            return false;
        }

        try
        {
            SCH_PLUGIN::SCH_PLUGIN_RELEASER pi(
                    SCH_IO_MGR::FindPlugin( (SCH_IO_MGR::SCH_FILE_T) aFileType ) );
            Schematic().SetRoot( pi->Load( aFileName, &Schematic() ) );

            // Eagle sheets do not use a worksheet frame by default, so set it to an empty one
            WS_DATA_MODEL& pglayout = WS_DATA_MODEL::GetTheInstance();
            pglayout.SetEmptyLayout();

            BASE_SCREEN::m_PageLayoutDescrFileName = "empty.kicad_wks";
            wxFileName layoutfn( Prj().GetProjectPath(), BASE_SCREEN::m_PageLayoutDescrFileName );
            wxFile layoutfile;

            if( layoutfile.Create( layoutfn.GetFullPath() ) )
            {
                layoutfile.Write( WS_DATA_MODEL::EmptyLayout() );
                layoutfile.Close();
            }

            newfilename.SetPath( Prj().GetProjectPath() );
            newfilename.SetName( Prj().GetProjectName() );
            newfilename.SetExt( LegacySchematicFileExtension );

            SetScreen( GetCurrentSheet().LastScreen() );

            Schematic().Root().SetFileName( newfilename.GetFullPath() );
            GetScreen()->SetFileName( newfilename.GetFullPath() );
            GetScreen()->SetModify();

            SaveProjectSettings();

            UpdateFileHistory( aFileName );
            SCH_SCREENS schematic( Schematic().Root() );
            schematic.UpdateSymbolLinks();      // Update all symbol library links for all sheets.

            GetScreen()->m_Initialized = true;

            for( SCH_SCREEN* screen = schematic.GetFirst(); screen; screen = schematic.GetNext() )
            {
                for( auto item : screen->Items().OfType( SCH_COMPONENT_T ) )
                {
                    std::vector<wxPoint> pts;
                    SCH_COMPONENT*       cmp = static_cast<SCH_COMPONENT*>( item );

                    // Update footprint LIB_ID to point to the imported Eagle library
                    SCH_FIELD* fpField = cmp->GetField( FOOTPRINT );

                    if( !fpField->GetText().IsEmpty() )
                    {
                        LIB_ID fpId;
                        fpId.Parse( fpField->GetText(), LIB_ID::ID_SCH, true );
                        fpId.SetLibNickname( newfilename.GetName() );
                        fpField->SetText( fpId.Format() );
                    }
                }
            }

            // Only perform the dangling end test on root sheet.
            GetScreen()->TestDanglingEnds();

            ClearUndoRedoList();

            m_toolManager->RunAction( ACTIONS::zoomFitScreen, true );
            SetSheetNumberAndCount();
            SyncView();
            UpdateTitle();
        }
        catch( const IO_ERROR& ioe )
        {
            // Do not leave g_RootSheet == NULL because it is expected to be
            // a valid sheet. Therefore create a dummy empty root sheet and screen.
            CreateScreens();
            m_toolManager->RunAction( ACTIONS::zoomFitScreen, true );

            wxString msg;
            msg.Printf( _( "Error loading schematic \"%s\".\n%s" ), aFileName, ioe.What() );
            DisplayError( this, msg );

            msg.Printf( _( "Failed to load \"%s\"" ), aFileName );
            AppendMsgPanel( wxEmptyString, msg, CYAN );

            return false;
        }

        return true;

    default:
        return false;
    }
}


bool SCH_EDIT_FRAME::AskToSaveChanges()
{
    SCH_SCREENS screenList( Schematic().Root() );

    // Save any currently open and modified project files.
    for( SCH_SCREEN* screen = screenList.GetFirst(); screen; screen = screenList.GetNext() )
    {
        if( screen->IsModify() )
        {
            if( !HandleUnsavedChanges( this, _( "The current schematic has been modified.  "
                                                "Save changes?" ),
                                       [&]()->bool { return SaveProject(); } ) )
            {
                return false;
            }
        }
    }

    return true;
}
