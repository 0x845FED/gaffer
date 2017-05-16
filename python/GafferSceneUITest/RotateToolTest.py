##########################################################################
#
#  Copyright (c) 2017, John Haddon. All rights reserved.
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

class RotateToolTest( GafferUITest.TestCase ) :

	def testRotate( self ) :

		script = Gaffer.ScriptNode()
		script["cube"] = GafferScene.Cube()

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["cube"]["out"] )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), GafferScene.PathMatcher( [ "/cube" ] ) )

		tool = GafferSceneUI.RotateTool( view )
		tool["active"].setValue( True )

		for i in range( 0, 6 ) :
			tool.rotate( 1, 90 )
			self.assertEqual( script["cube"]["transform"]["rotate"]["y"].getValue(), (i + 1) * 90 )

	def testInteractionWithGroupRotation( self ) :

		script = Gaffer.ScriptNode()

		script["cube"] = GafferScene.Cube()
		script["group"] = GafferScene.Group()
		script["group"]["in"][0].setInput( script["cube"]["out"] )

		# Rotates the X axis onto the negative Z axis
		script["group"]["transform"]["rotate"]["y"].setValue( 90 )

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["group"]["out"] )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), GafferScene.PathMatcher( [ "/group/cube" ] ) )

		tool = GafferSceneUI.RotateTool( view )
		tool["active"].setValue( True )

		# Rotates 90 degrees using the Z handle. This will
		# rotate about the X axis in world space, because the
		# handle orientation has been affected by the group
		# transform (because default orientation is Parent).
		tool.rotate( 2, 90 )

		# We expect this to have aligned the cube's local X axis onto
		# the Y axis in world space, and the local Y axis onto the world
		# Z axis.
		self.assertTrue(
			IECore.V3f( 0, 1, 0 ).equalWithAbsError(
				IECore.V3f( 1, 0, 0 ) * script["group"]["out"].fullTransform( "/group/cube" ),
				0.000001
			)
		)
		self.assertTrue(
			IECore.V3f( 0, 0, 1 ).equalWithAbsError(
				IECore.V3f( 0, 1, 0 ) * script["group"]["out"].fullTransform( "/group/cube" ),
				0.000001
			)
		)

	def testOrientation( self ) :

		script = Gaffer.ScriptNode()

		script["cube"] = GafferScene.Cube()
		script["cube"]["transform"]["rotate"]["y"].setValue( 90 )

		script["group"] = GafferScene.Group()
		script["group"]["in"][0].setInput( script["cube"]["out"] )
		script["group"]["transform"]["rotate"]["y"].setValue( 90 )

		view = GafferSceneUI.SceneView()
		view["in"].setInput( script["group"]["out"] )
		GafferSceneUI.ContextAlgo.setSelectedPaths( view.getContext(), GafferScene.PathMatcher( [ "/group/cube" ] ) )

		tool = GafferSceneUI.RotateTool( view )
		tool["active"].setValue( True )

		# Local

		tool["orientation"].setValue( tool.Orientation.Local )

		with Gaffer.UndoScope( script ) :
			tool.rotate( 2, 90 )

		self.assertTrue(
			IECore.V3f( 0, 1, 0 ).equalWithAbsError(
				IECore.V3f( 1, 0, 0 ) * script["group"]["out"].fullTransform( "/group/cube" ),
				0.000001
			)
		)
		script.undo()

		# Parent

		tool["orientation"].setValue( tool.Orientation.Parent )

		with Gaffer.UndoScope( script ) :
			tool.rotate( 0, 90 )

		self.assertTrue(
			IECore.V3f( 0, 1, 0 ).equalWithAbsError(
				IECore.V3f( 1, 0, 0 ) * script["group"]["out"].fullTransform( "/group/cube" ),
				0.000001
			)
		)
		script.undo()

		# World

		tool["orientation"].setValue( tool.Orientation.World )

		with Gaffer.UndoScope( script ) :
			tool.rotate( 2, 90 )

		self.assertTrue(
			IECore.V3f( 0, -1, 0 ).equalWithAbsError(
				IECore.V3f( 1, 0, 0 ) * script["group"]["out"].fullTransform( "/group/cube" ),
				0.000001
			)
		)

if __name__ == "__main__":
	unittest.main()
