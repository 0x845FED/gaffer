//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2023, Cinesite VFX Ltd. All rights reserved.
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

#pragma once

#include "GafferSceneUI/Export.h"
#include "GafferSceneUI/TransformTool.h"
#include "GafferSceneUI/TypeIds.h"

#include "GafferUI/Handle.h"
#include "GafferUI/RotateHandle.h"

#include "Gaffer/ScriptNode.h"

#include <unordered_map>

namespace GafferSceneUI
{

IE_CORE_FORWARDDECLARE( SceneView );

class GAFFERSCENEUI_API LightPositionTool : public GafferSceneUI::TransformTool
{

	public :

		LightPositionTool( SceneView *view, const std::string &name = defaultName<LightPositionTool>() );
		~LightPositionTool() override;

		GAFFER_NODE_DECLARE_TYPE( GafferSceneUI::LightPositionTool, LightPositionToolTypeId, TransformTool );

		// Positions the current selection to cast a shadow from `shadowPivot` to `shadowTarget`,
		// with the light `targetDistance` from the pivot. All coordinates are in world space.
		void position( const Imath::V3f &shadowPivot, const Imath::V3f &shadowTarget, const float targetDistance );

	protected :

		bool affectsHandles( const Gaffer::Plug *input ) const override;
		void updateHandles( float rasterScale ) override;

	private :

		struct TranslationRotation
		{

			TranslationRotation( const Selection &selection, Orientation orientation );

			bool canApplyTranslation() const;
			bool canApplyRotation( const Imath::V3i &axisMask ) const;
			void applyTranslation( const Imath::V3f &translation );
			void applyRotation( const Imath::Eulerf &rotation );

			private :

				Imath::V3f updatedRotateValue( const Gaffer::V3fPlug *rotatePlug, const Imath::Eulerf &rotation, Imath::V3f *currentValue = nullptr ) const;

				const Selection &m_selection;
				Imath::M44f m_gadgetToTranslationXform;
				Imath::M44f m_gadgetToRotationXform;

				mutable std::optional<Imath::V3f> m_originalTranslation;
				mutable std::optional<Imath::Eulerf> m_originalRotation;  // Radians

		};

		IECore::RunTimeTypedPtr handleDragBegin( GafferUI::Gadget *gadget );
		bool handleDragMove( GafferUI::Gadget *gadget, const GafferUI::DragDropEvent &event );
		bool handleDragEnd();

		IECore::RunTimeTypedPtr sceneGadgetDragBegin( GafferUI::Gadget *gadget, const GafferUI::DragDropEvent &event );
		bool sceneGadgetDragEnter( GafferUI::Gadget *gadget, const GafferUI::DragDropEvent &event );
		bool sceneGadgetDragMove( const GafferUI::DragDropEvent &event );
		bool sceneGadgetDragEnd();

		bool keyPress( const GafferUI::KeyEvent &event );
		bool keyRelease( const GafferUI::KeyEvent &event );
		void viewportGadgetLeave( const GafferUI::ButtonEvent &event );
		void visibilityChanged( GafferUI::Gadget *gadget );

		void plugSet( Gaffer::Plug *plug );

		bool buttonPress( const GafferUI::ButtonEvent &event );
		bool buttonRelease( const GafferUI::ButtonEvent &event );

		bool placeTarget( const IECore::LineSegment3f &eventLine );

		enum class TargetMode
		{
			None,
			ShadowPivot,
			ShadowTarget
		};

		void setTargetMode( TargetMode mode );
		TargetMode getTargetMode() const { return m_targetMode; }

		void setShadowPivot( const Imath::V3f &p, Gaffer::ScriptNodePtr scriptNode );
		std::optional<Imath::V3f> getShadowPivot() const;
		void setShadowTarget( const Imath::V3f &p, Gaffer::ScriptNodePtr scriptNode );
		std::optional<Imath::V3f> getShadowTarget() const;
		void setShadowPivotDistance( const float d );
		std::optional<float> getShadowPivotDistance() const;

		TargetMode m_targetMode;

		std::optional<TranslationRotation> m_drag;
		float m_startShadowPivotDistance;

		GafferUI::HandlePtr m_shadowHandle;
		GafferUI::RotateHandlePtr m_rotateHandle;

		Gaffer::Signals::ScopedConnection m_contextChangedConnection;

		// Pivots and targets are stored in transform space - the world space transform
		// of the scene in which the transform will be applied.
		// See `TransformTool::transformSpace()` for details.
		std::unordered_map<std::string, std::optional<Imath::V3f>> m_shadowPivotMap;
		std::unordered_map<std::string, std::optional<Imath::V3f>> m_shadowTargetMap;

		std::unordered_map<std::string, std::optional<float>> m_shadowPivotDistanceMap;

		bool m_draggingTarget;

		static ToolDescription<LightPositionTool, SceneView> g_toolDescription;
		static size_t g_firstPlugIndex;

};

IE_CORE_DECLAREPTR( LightPositionTool )

}  // namespace GafferSceneUI