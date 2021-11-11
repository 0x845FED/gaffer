##########################################################################
#
#  Copyright (c) 2021, Cinesite VFX Ltd. All rights reserved.
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

import Gaffer
import IECore

# Patch to allow loading of gaffer scenes and ensure forward compatibility of code
# written with Animation.Type renamed to Animation.Interpolation (0.61 onwards)
Gaffer.Animation.Interpolation = Gaffer.Animation.Type
Gaffer.Animation.Key.setInterpolation = Gaffer.Animation.Key.setType
Gaffer.Animation.Key.getInterpolation = Gaffer.Animation.Key.getType

# Patch to allow loading of gaffer scenes and ensure forward compatibility of code
# written with Animation.Interpolation.Step renamed to Animation.Interpolation.Constant (0.61 onwards)
Gaffer.Animation.Interpolation.Constant = Gaffer.Animation.Interpolation.Step

# Patch to allow loading of gaffer scenes and ensure forward compatibility of code
# written with Animation.Key representation that includes in/out tangent slope, scale and tieMode
Gaffer.Animation.Interpolation.ConstantNext = Gaffer.Animation.Interpolation.Constant
Gaffer.Animation.Interpolation.Cubic = Gaffer.Animation.Interpolation.Linear
Gaffer.Animation.Interpolation.Bezier = Gaffer.Animation.Interpolation.Linear
Gaffer.Animation.TieMode = IECore.Enum.create( "Manual", "Slope", "Scale" )
Gaffer.Animation.Key = type( "KeyCompatibility_0_61", tuple( [ Gaffer.Animation.Key ] ),
{ "__init__" : lambda self, time = 0.0, value = 0.0, type = Gaffer.Animation.Type.Linear,
	inSlope = None, inScale = None, outSlope = None, outScale = None, tieMode = None :
		super( Gaffer.Animation.Key, self ).__init__( time, value, type ) } )
