##########################################################################
#
#  Copyright (c) 2016, John Haddon. All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are
#  met:
#
#      * Redistributions of source code must retain the above
#        copyright notice, this list of conditions and the following
#        disclaimer.
#
#      * Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following
#        disclaimer in the documentation and/or other materials provided with
#        the distribution.
#
#      * Neither the name of John Haddon nor the names of
#        any other contributors to this software may be used to endorse or
#        promote products derived from this software without specific prior
#        written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
#  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
#  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
#  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
#  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
#  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
#  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
#  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
#  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
##########################################################################

import IECore

import Gaffer
import GafferUITest
import GafferScene
import GafferSceneUI

class TranslateToolTest( GafferUITest.TestCase ) :

	def testSelection( self ) :

		script = Gaffer.ScriptNode()

		script["plane"] = GafferScene.Plane()

		script["group"] = GafferScene.Group()
		script["group"]["in"][0].setInput( script["plane"]["out"] )

		script["transformFilter"] = GafferScene.PathFilter()

		script["transform"] = GafferScene.Transform()
		script["transform"]["in"].setInput( script["group"]["out"] )
		script["transform"]["filter"].setInput( script["transformFilter"]["out"] )

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["transform"]["out"] )

		tool = GafferSceneUI.TranslateTool( view )
		tool["active"].setValue( True )

		self.assertTrue( tool.selection().transformPlug is None )

		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), GafferScene.PathMatcher( [ "/group/plane" ] ) )
		self.assertEqual( tool.selection().path, "/group/plane" )
		self.assertEqual( tool.selection().context, view.getContext() )
		self.assertTrue( tool.selection().upstreamScene.isSame( script["plane"]["out"] ) )
		self.assertEqual( tool.selection().upstreamPath, "/plane" )
		self.assertTrue( tool.selection().transformPlug.isSame( script["plane"]["transform"] ) )
		self.assertEqual( tool.selection().transformSpace, IECore.M44f() )

		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), GafferScene.PathMatcher( [ "/group" ] ) )
		self.assertEqual( tool.selection().path, "/group" )
		self.assertEqual( tool.selection().context, view.getContext() )
		self.assertTrue( tool.selection().upstreamScene.isSame( script["group"]["out"] ) )
		self.assertEqual( tool.selection().upstreamPath, "/group" )
		self.assertTrue( tool.selection().transformPlug.isSame( script["group"]["transform"] ) )
		self.assertEqual( tool.selection().transformSpace, IECore.M44f() )

		script["transformFilter"]["paths"].setValue( IECore.StringVectorData( [ "/group" ] ) )
		self.assertTrue( tool.selection().transformPlug.isSame( script["transform"]["transform"] ) )

		script["transformFilter"]["enabled"].setValue( False )
		self.assertTrue( tool.selection().transformPlug.isSame( script["group"]["transform"] ) )

		script["transformFilter"]["enabled"].setValue( True )
		self.assertEqual( tool.selection().path, "/group" )
		self.assertEqual( tool.selection().context, view.getContext() )
		self.assertTrue( tool.selection().upstreamScene.isSame( script["transform"]["out"] ) )
		self.assertEqual( tool.selection().upstreamPath, "/group" )
		self.assertTrue( tool.selection().transformPlug.isSame( script["transform"]["transform"] ) )
		self.assertEqual( tool.selection().transformSpace, IECore.M44f() )

		script["transform"]["enabled"].setValue( False )
		self.assertTrue( tool.selection().transformPlug.isSame( script["group"]["transform"] ) )

	def testTranslate( self ) :

		script = Gaffer.ScriptNode()

		script["plane"] = GafferScene.Plane()

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["plane"]["out"] )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), GafferScene.PathMatcher( [ "/plane" ] ) )

		tool = GafferSceneUI.TranslateTool( view )
		tool["active"].setValue( True )

		tool.translate( IECore.V3f( 1, 0, 0 ) )

		self.assertEqual(
			script["plane"]["out"].fullTransform( "/plane" ).translation(),
			IECore.V3f( 1, 0, 0 ),
		)

	def testInteractionWithRotation( self ) :

		script = Gaffer.ScriptNode()

		script["plane"] = GafferScene.Plane()

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["plane"]["out"] )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), GafferScene.PathMatcher( [ "/plane" ] ) )

		tool = GafferSceneUI.TranslateTool( view )
		tool["active"].setValue( True )
		tool["orientation"].setValue( tool.Orientation.Local )

		with Gaffer.UndoScope( script ) :
			tool.translate( IECore.V3f( 1, 0, 0 ) )

		self.assertTrue(
			IECore.V3f( 1, 0, 0 ).equalWithAbsError(
				script["plane"]["out"].fullTransform( "/plane" ).translation(),
				0.0000001
			)
		)
		script.undo()

		script["plane"]["transform"]["rotate"]["y"].setValue( 90 )

		with Gaffer.UndoScope( script ) :
			tool.translate( IECore.V3f( 1, 0, 0 ) )

		self.assertTrue(
			IECore.V3f( 0, 0, -1 ).equalWithAbsError(
				script["plane"]["out"].fullTransform( "/plane" ).translation(),
				0.0000001
			)
		)
		script.undo()

	def testInteractionWithGroupRotation( self ) :

		script = Gaffer.ScriptNode()

		script["plane"] = GafferScene.Plane()
		script["group"] = GafferScene.Group()
		script["group"]["in"][0].setInput( script["plane"]["out"] )
		script["group"]["transform"]["rotate"]["y"].setValue( 90 )

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["group"]["out"] )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), GafferScene.PathMatcher( [ "/group/plane" ] ) )

		tool = GafferSceneUI.TranslateTool( view )
		tool["active"].setValue( True )

		tool.translate( IECore.V3f( 1, 0, 0 ) )

		self.assertTrue(
			IECore.V3f( 0, 0, -1 ).equalWithAbsError(
				script["group"]["out"].fullTransform( "/group/plane" ).translation(),
				0.0000001
			)
		)

	def testInteractionWithGroupTranslation( self ) :

		script = Gaffer.ScriptNode()

		script["plane"] = GafferScene.Plane()
		script["group"] = GafferScene.Group()
		script["group"]["in"][0].setInput( script["plane"]["out"] )
		script["group"]["transform"]["translate"].setValue( IECore.V3f( 1, 2, 3 ) )

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["group"]["out"] )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), GafferScene.PathMatcher( [ "/group/plane" ] ) )

		tool = GafferSceneUI.TranslateTool( view )
		tool["active"].setValue( True )

		tool.translate( IECore.V3f( -1, 0, 0 ) )

		self.assertEqual(
			script["group"]["out"].fullTransform( "/group/plane" ).translation(),
			IECore.V3f( 0, 2, 3 ),
		)

	def testOrientation( self ) :

		script = Gaffer.ScriptNode()

		script["plane"] = GafferScene.Plane()
		script["plane"]["transform"]["rotate"]["y"].setValue( 90 )

		script["group"] = GafferScene.Group()
		script["group"]["in"][0].setInput( script["plane"]["out"] )
		script["group"]["transform"]["rotate"]["y"].setValue( 90 )

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["group"]["out"] )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), GafferScene.PathMatcher( [ "/group/plane" ] ) )

		tool = GafferSceneUI.TranslateTool( view )
		tool["active"].setValue( True )

		# Local

		tool["orientation"].setValue( tool.Orientation.Local )

		with Gaffer.UndoScope( script ) :
			tool.translate( IECore.V3f( 1, 0, 0 ) )

		self.assertTrue(
			IECore.V3f( -1, 0, 0 ).equalWithAbsError(
				script["group"]["out"].fullTransform( "/group/plane" ).translation(),
				0.000001
			)
		)
		script.undo()

		# Parent

		tool["orientation"].setValue( tool.Orientation.Parent )

		with Gaffer.UndoScope( script ) :
			tool.translate( IECore.V3f( 1, 0, 0 ) )

		self.assertTrue(
			IECore.V3f( 0, 0, -1 ).equalWithAbsError(
				script["group"]["out"].fullTransform( "/group/plane" ).translation(),
				0.0000001
			)
		)
		script.undo()

		# World

		tool["orientation"].setValue( tool.Orientation.World )

		with Gaffer.UndoScope( script ) :
			tool.translate( IECore.V3f( 1, 0, 0 ) )

		self.assertTrue(
			IECore.V3f( 1, 0, 0 ).equalWithAbsError(
				script["group"]["out"].fullTransform( "/group/plane" ).translation(),
				0.0000001
			)
		)

	def testScale( self ) :

		script = Gaffer.ScriptNode()

		script["plane"] = GafferScene.Plane()
		script["plane"]["transform"]["scale"].setValue( IECore.V3f( 10 ) )

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["plane"]["out"] )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), GafferScene.PathMatcher( [ "/plane" ] ) )

		tool = GafferSceneUI.TranslateTool( view )
		tool["active"].setValue( True )

		with Gaffer.UndoScope( script ) :
			tool.translate( IECore.V3f( 1, 0, 0 ) )

		self.assertTrue(
			IECore.V3f( 1, 0, 0 ).equalWithAbsError(
				script["plane"]["out"].fullTransform( "/plane" ).translation(),
				0.0000001
			)
		)

		script.undo()

		tool["orientation"].setValue( tool.Orientation.Local )

		with Gaffer.UndoScope( script ) :
			tool.translate( IECore.V3f( 1, 0, 0 ) )

		self.assertTrue(
			IECore.V3f( 1, 0, 0 ).equalWithAbsError(
				script["plane"]["out"].fullTransform( "/plane" ).translation(),
				0.0000001
			)
		)

	def testGroup( self ) :

		script = Gaffer.ScriptNode()

		script["group"] = GafferScene.Group()

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["group"]["out"] )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), GafferScene.PathMatcher( [ "/group" ] ) )

		tool = GafferSceneUI.TranslateTool( view )
		tool["active"].setValue( True )

		tool.translate( IECore.V3f( 1, 0, 0 ) )

		self.assertEqual(
			script["group"]["out"].fullTransform( "/group" ).translation(),
			IECore.V3f( 1, 0, 0 ),
		)

	def testTransform( self ) :

		script = Gaffer.ScriptNode()

		script["plane"] = GafferScene.Plane()
		script["plane"]["transform"]["rotate"]["y"].setValue( 90 )

		script["transformFilter"] = GafferScene.PathFilter()
		script["transformFilter"]["paths"].setValue( IECore.StringVectorData( [ "/plane" ] ) )

		script["transform"] = GafferScene.Transform()
		script["transform"]["in"].setInput( script["plane"]["out"] )
		script["transform"]["filter"].setInput( script["transformFilter"]["out"] )

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["transform"]["out"] )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), GafferScene.PathMatcher( [ "/plane" ] ) )

		tool = GafferSceneUI.TranslateTool( view )
		tool["active"].setValue( True )
		tool["orientation"].setValue( tool.Orientation.Local )

		tool.translate( IECore.V3f( 1, 0, 0 ) )

		self.assertTrue(
			IECore.V3f( 0, 0, -1 ).equalWithAbsError(
				script["transform"]["out"].fullTransform( "/plane" ).translation(),
				0.0000001
			)
		)

	def testTransformWithRotation( self ) :

		script = Gaffer.ScriptNode()

		script["plane"] = GafferScene.Plane()

		script["transformFilter"] = GafferScene.PathFilter()
		script["transformFilter"]["paths"].setValue( IECore.StringVectorData( [ "/plane" ] ) )

		script["transform"] = GafferScene.Transform()
		script["transform"]["in"].setInput( script["plane"]["out"] )
		script["transform"]["filter"].setInput( script["transformFilter"]["out"] )
		script["transform"]["transform"]["rotate"]["y"].setValue( 90 )

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["transform"]["out"] )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), GafferScene.PathMatcher( [ "/plane" ] ) )

		tool = GafferSceneUI.TranslateTool( view )
		tool["active"].setValue( True )
		tool["orientation"].setValue( tool.Orientation.Local )

		tool.translate( IECore.V3f( 1, 0, 0 ) )

		self.assertTrue(
			IECore.V3f( 0, 0, -1 ).equalWithAbsError(
				script["transform"]["out"].fullTransform( "/plane" ).translation(),
				0.0000001
			)
		)

if __name__ == "__main__":
	unittest.main()
