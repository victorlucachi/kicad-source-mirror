/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2020 Ian McInerney <Ian.S.McInerney at ieee.org>
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

#include <wx/string.h>

namespace KIPLATFORM
{
    namespace ENV
    {
        /**
         * Move the specified file/directory to the trash bin/recycle bin.
         *
         * @param aPath is the absolute path of the file/directory to move to the trash
         * @param aError is the error message saying why the operation failed
         *
         * @return true if the operation succeeds, false if it fails (see the contents of aError)
         */
        bool MoveToTrash( const wxString& aPath, wxString& aError );
    }
}
