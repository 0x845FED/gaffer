//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2016, Image Engine Design Inc. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//      * Redistributions of source code must retain the above
//        copyright notice, this list of conditions and the following
//        disclaimer.
//
//      * Redistributions in binary form must reproduce the above
//        copyright notice, this list of conditions and the following
//        disclaimer in the documentation and/or other materials provided with
//        the distribution.
//
//      * Neither the name of John Haddon nor the names of
//        any other contributors to this software may be used to endorse or
//        promote products derived from this software without specific prior
//        written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#include "boost/python.hpp"

#include "Gaffer/MetadataAlgo.h"
#include "Gaffer/GraphComponent.h"
#include "Gaffer/Plug.h"
#include "Gaffer/Node.h"

#include "GafferBindings/MetadataAlgoBinding.h"

using namespace boost::python;
using namespace Gaffer;
using namespace Gaffer::MetadataAlgo;

namespace
{

static boost::python::list bookmarksWrapper( const Node *node )
{
	std::vector<NodePtr> bookmarks;
	MetadataAlgo::bookmarks( node, bookmarks );

	boost::python::list result;
	for( std::vector<NodePtr>::const_iterator it = bookmarks.begin(), endIt = bookmarks.end(); it != endIt; ++it )
	{
		result.append( *it );
	}

	return result;
}

}

namespace GafferBindings
{

void bindMetadataAlgo()
{
	object module( borrowed( PyImport_AddModule( "Gaffer.MetadataAlgo" ) ) );
	scope().attr( "MetadataAlgo" ) = module;
	scope moduleScope( module );

	def( "setReadOnly", &setReadOnly, ( arg( "graphComponent" ), arg( "readOnly"), arg( "persistent" ) = true ) );
	def( "getReadOnly", &getReadOnly );
	def( "readOnly", &readOnly );
	def( "setBookmarked", &setBookmarked, ( arg( "graphComponent" ), arg( "bookmarked"), arg( "persistent" ) = true ) );
	def( "getBookmarked", &getBookmarked );
	def( "bookmarks", &bookmarksWrapper );
	def(
		"affectedByChange",
		(bool (*)( const Plug *, IECore::TypeId, const MatchPattern &, const Plug * ))&affectedByChange,
		( arg( "plug" ), arg( "changedNodeTypeId"), arg( "changedPlugPath" ), arg( "changedPlug" ) )
	);
	def(
		"affectedByChange",
		(bool (*)( const Node *node, IECore::TypeId changedNodeTypeId, const Node *changedNode ))&affectedByChange,
		( arg( "node" ), arg( "changedNodeTypeId"), arg( "changedNode" ) )
	);

	def(
		"childAffectedByChange",
		(bool (*)( const GraphComponent *, IECore::TypeId, const StringAlgo::MatchPattern &, const Gaffer::Plug * ))&childAffectedByChange,
		( arg( "parent" ), arg( "changedNodeTypeId"), arg( "changedPlugPath" ), arg( "changedPlug" ) )
	);
	def(
		"childAffectedByChange",
		(bool (*)( const GraphComponent *, IECore::TypeId, const Gaffer::Node * ))&childAffectedByChange,
		( arg( "parent" ), arg( "changedNodeTypeId"), arg( "changedNode" ) )
	);

	def( "ancestorAffectedByChange", &ancestorAffectedByChange, ( arg( "plug" ), arg( "changedNodeTypeId"), arg( "changedPlugPath" ), arg( "changedPlug" ) ) );

	def( "copy", &copy, ( arg( "from" ), arg( "to" ), arg( "exclude" ) = "", arg( "persistentOnly" ) = true, arg( "persistent" ) = true ) );

	def( "copyColors", &copyColors,  (arg( "srcPlug" ), arg( "dstPlug" ), arg( "overwrite") ));

}

} // namespace GafferBindings
