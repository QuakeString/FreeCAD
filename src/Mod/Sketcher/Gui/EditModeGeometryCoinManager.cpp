// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2021 Abdullah Tahiri <abdullah.tahiri.yo@gmail.com>     *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include <FCConfig.h>

#include <memory>

#include <Inventor/SbVec3f.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoMarkerSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>

#include <Gui/Inventor/MarkerBitmaps.h>
#include <Gui/Inventor/SmSwitchboard.h>
#include <Mod/Sketcher/App/Constraint.h>
#include <Mod/Sketcher/App/GeoEnum.h>
#include <Mod/Sketcher/App/GeoList.h>
#include <Mod/Sketcher/App/GeometryFacade.h>
#include <Mod/Sketcher/App/SolverGeometryExtension.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include "EditModeGeometryCoinConverter.h"
#include "EditModeGeometryCoinManager.h"
#include "ViewProviderSketchCoinAttorney.h"
#include "Mod/Sketcher/App/ExternalGeometryFacade.h"


using namespace SketcherGui;
using namespace Sketcher;

//**************************** EditModeGeometryCoinManager class ******************************

EditModeGeometryCoinManager::EditModeGeometryCoinManager(
    ViewProviderSketch& vp,
    DrawingParameters& drawingParams,
    GeometryLayerParameters& geometryLayerParams,
    AnalysisResults& analysisResultStruct,
    EditModeScenegraphNodes& editModeScenegraph,
    CoinMapping& coinMap
)
    : viewProvider(vp)
    , drawingParameters(drawingParams)
    , geometryLayerParameters(geometryLayerParams)
    , analysisResults(analysisResultStruct)
    , editModeScenegraphNodes(editModeScenegraph)
    , coinMapping(coinMap)
{}

EditModeGeometryCoinManager::~EditModeGeometryCoinManager()
{}

void EditModeGeometryCoinManager::processGeometry(const GeoListFacade& geolistfacade)
{
    // enable all layers
    editModeScenegraphNodes.PointsGroup->enable.setNum(geometryLayerParameters.getCoinLayerCount());
    editModeScenegraphNodes.CurvesGroup->enable.setNum(
        geometryLayerParameters.getCoinLayerCount() * geometryLayerParameters.getSubLayerCount()
    );
    editModeScenegraphNodes.PointsHaloGroup->enable.setNum(
        geometryLayerParameters.getCoinLayerCount()
    );
    editModeScenegraphNodes.CurvesHaloGroup->enable.setNum(
        geometryLayerParameters.getCoinLayerCount() * geometryLayerParameters.getSubLayerCount()
    );
    SbBool* swsp = editModeScenegraphNodes.PointsGroup->enable.startEditing();
    SbBool* swsc = editModeScenegraphNodes.CurvesGroup->enable.startEditing();
    SbBool* swsph = editModeScenegraphNodes.PointsHaloGroup->enable.startEditing();
    SbBool* swsch = editModeScenegraphNodes.CurvesHaloGroup->enable.startEditing();

    auto layersconfigurations = viewProvider.VisualLayerList.getValues();

    for (auto l = 0; l < geometryLayerParameters.getCoinLayerCount(); l++) {
        auto enabled = layersconfigurations[l].isVisible();

        // Without the halo its pass is switched off altogether, rather than left drawing fully
        // transparent geometry every frame.
        auto haloEnabled = enabled && drawingParameters.SelectHalo;

        swsp[l] = enabled;
        swsph[l] = haloEnabled;
        int slCount = geometryLayerParameters.getSubLayerCount();
        for (int t = 0; t < slCount; t++) {
            swsc[l * slCount + t] = enabled;
            swsch[l * slCount + t] = haloEnabled;
        }
    }

    editModeScenegraphNodes.PointsGroup->enable.finishEditing();
    editModeScenegraphNodes.CurvesGroup->enable.finishEditing();
    editModeScenegraphNodes.PointsHaloGroup->enable.finishEditing();
    editModeScenegraphNodes.CurvesHaloGroup->enable.finishEditing();

    // Define the coin nodes that will be filled in with the geometry layers
    GeometryLayerNodes geometrylayernodes {
        editModeScenegraphNodes.PointsMaterials,
        editModeScenegraphNodes.PointsCoordinate,
        editModeScenegraphNodes.CurvesMaterials,
        editModeScenegraphNodes.CurvesCoordinate,
        editModeScenegraphNodes.CurveSet
    };

    // process geometry layers
    EditModeGeometryCoinConverter gcconv(
        viewProvider,
        geometrylayernodes,
        drawingParameters,
        geometryLayerParameters,
        coinMapping
    );

    gcconv.convert(geolistfacade);

    // set cross coordinates
    editModeScenegraphNodes.RootCrossHSet->numVertices.set1Value(0, 2);
    editModeScenegraphNodes.RootCrossVSet->numVertices.set1Value(0, 2);

    analysisResults.combRepresentationScale = gcconv.getCombRepresentationScale();
    analysisResults.boundingBoxMagnitudeOrder = exp(
        ceil(log(std::abs(gcconv.getBoundingBoxMaxMagnitude())))
    );
    analysisResults.bsplineGeoIds = gcconv.getBSplineGeoIds();
    analysisResults.arcGeoIds = gcconv.getArcGeoIds();
}

void EditModeGeometryCoinManager::updateGeometryColor(
    const GeoListFacade& geolistfacade,
    bool issketchinvalid
)
{
    // Lambdas for convenience retrieval of geometry information
    auto isDefinedGeomPoint = [&geolistfacade](int GeoId, Sketcher::PointPos PosId) {
        auto geom = geolistfacade.getGeometryFacadeFromGeoId(GeoId);
        if (geom) {
            bool isStartOrEnd = PosId == Sketcher::PointPos::start
                || PosId == Sketcher::PointPos::end;
            return isStartOrEnd && !geom->getConstruction();
        }
        return false;
    };

    auto isExternalDefiningGeomPoint = [&geolistfacade](int GeoId) {
        auto geom = geolistfacade.getGeometryFacadeFromGeoId(GeoId);
        if (geom) {
            std::unique_ptr<Part::Geometry> geomCopy(geom->clone());
            // The ExternalGeometryFacade is not the owner of the geometry
            auto egf = ExternalGeometryFacade::getFacade(geomCopy.get());
            auto ref = egf->getRef();
            return egf->testFlag(ExternalGeometryExtension::Defining);
        }
        return false;
    };

    auto isCoincident = [&](int GeoId, Sketcher::PointPos PosId) {
        const std::vector<Sketcher::Constraint*>& constraints
            = ViewProviderSketchCoinAttorney::getConstraints(viewProvider);
        for (auto& constr : constraints) {
            if (constr->Type == Coincident
                || (constr->Type == Tangent && constr->FirstPos != Sketcher::PointPos::none)
                || (constr->Type == Perpendicular && constr->FirstPos != Sketcher::PointPos::none
                    && constr->SecondPos != Sketcher::PointPos::none)) {
                if ((constr->First == GeoId && constr->FirstPos == PosId)
                    || (constr->Second == GeoId && constr->SecondPos == PosId)) {
                    return true;
                }
            }
        }
        return false;
    };

    auto isInternalAlignedGeom = [&geolistfacade](int GeoId) {
        auto geom = geolistfacade.getGeometryFacadeFromGeoId(GeoId);
        if (geom) {
            return geom->isInternalAligned();
        }
        return false;
    };

    auto isFullyConstraintElement = [&geolistfacade](int GeoId) {
        auto geom = geolistfacade.getGeometryFacadeFromGeoId(GeoId);

        if (geom) {
            if (geom->hasExtension(Sketcher::SolverGeometryExtension::getClassTypeId())) {

                auto solvext = std::static_pointer_cast<const Sketcher::SolverGeometryExtension>(
                    geom->getExtension(Sketcher::SolverGeometryExtension::getClassTypeId()).lock()
                );

                return (solvext->getGeometry() == Sketcher::SolverGeometryExtension::FullyConstraint);
            }
        }
        return false;
    };

    bool sketchFullyConstrained = ViewProviderSketchCoinAttorney::isSketchFullyConstrained(
        viewProvider
    );

    // Update Colors

    SbColor* crosscolorH = editModeScenegraphNodes.RootCrossHMaterials->diffuseColor.startEditing();
    SbColor* crosscolorV = editModeScenegraphNodes.RootCrossVMaterials->diffuseColor.startEditing();
    auto viewOrientationFactor = ViewProviderSketchCoinAttorney::getViewOrientationFactor(viewProvider);

    // Origin point
    auto preselectcross = ViewProviderSketchCoinAttorney::getPreselectCross(viewProvider);
    if (preselectcross == 0) {  // 0 means the RootPoint is preselected
        editModeScenegraphNodes.OriginPointMaterial->diffuseColor = drawingParameters.PreselectColor;
    }
    else {
        editModeScenegraphNodes.OriginPointMaterial->diffuseColor
            = drawingParameters.FullyConstraintElementColor;
    }
    editModeScenegraphNodes.OriginPointCoordinate->point.set1Value(
        0,
        SbVec3f(0, 0, viewOrientationFactor * drawingParameters.zRootPoint)
    );
    editModeScenegraphNodes.OriginPointCoordinateOccluded->point.set1Value(
        0,
        SbVec3f(0, 0, viewOrientationFactor * drawingParameters.zRootPoint)
    );

    for (auto l = 0; l < geometryLayerParameters.getCoinLayerCount(); l++) {
        float x, y, z;
        int PtNum = editModeScenegraphNodes.PointsMaterials[l]->diffuseColor.getNum();
        SbColor* pcolor = editModeScenegraphNodes.PointsMaterials[l]->diffuseColor.startEditing();
        SbVec3f* pverts = editModeScenegraphNodes.PointsCoordinate[l]->point.startEditing();

        // The halo pass mirrors the point set one to one. Every point gets an entry and starts out
        // fully transparent, so that only the points marked below end up surrounded by a ring.
        SoMaterial* pointsHaloMaterial = editModeScenegraphNodes.PointsHaloMaterials[l];
        pointsHaloMaterial->diffuseColor.setNum(PtNum);
        pointsHaloMaterial->transparency.setNum(PtNum);
        SbColor* phcolor = pointsHaloMaterial->diffuseColor.startEditing();
        float* phtransparency = pointsHaloMaterial->transparency.startEditing();
        for (int i = 0; i < PtNum; i++) {
            phcolor[i] = drawingParameters.SelectHaloColor;
            phtransparency[i] = 1.0f;
        }

        // The halo shares the coordinates of the geometry, so it is moved behind it as a whole.
        editModeScenegraphNodes.PointsHaloTranslation[l]->translation.setValue(
            0,
            0,
            -viewOrientationFactor * drawingParameters.zHaloOffset
        );

        // colors of the point set
        for (int i = 0; i < PtNum; i++) {
            if (!coinMapping.isValidPointId(i, l)) {
                continue;
            }

            int GeoId = coinMapping.getPointGeoId(i, l);
            Sketcher::PointPos PosId = coinMapping.getPointPosId(i, l);
            bool isExternal = GeoId < -1;

            if (isExternal) {
                if (isCoincident(GeoId, PosId) && !issketchinvalid) {
                    pcolor[i] = drawingParameters.ConstrIcoColor;
                }
                else {
                    pcolor[i] = isExternalDefiningGeomPoint(GeoId)
                        ? drawingParameters.CurveExternalDefiningColor
                        : drawingParameters.CurveExternalColor;
                }
            }
            else if (issketchinvalid) {
                pcolor[i] = drawingParameters.InvalidSketchColor;
            }
            else if (sketchFullyConstrained) {
                // root point is not coloured nor external
                pcolor[i] = drawingParameters.FullyConstrainedColor;
            }
            else {
                bool constrainedElement = isFullyConstraintElement(GeoId);

                if (isInternalAlignedGeom(GeoId)) {
                    if (constrainedElement) {
                        pcolor[i] = drawingParameters.FullyConstraintInternalAlignmentColor;
                    }
                    else {
                        if (isCoincident(GeoId, PosId)) {
                            pcolor[i] = drawingParameters.ConstrIcoColor;
                        }
                        else {
                            pcolor[i] = drawingParameters.InternalAlignedGeoColor;
                        }
                    }
                }
                else {
                    if (!isDefinedGeomPoint(GeoId, PosId)) {
                        if (constrainedElement) {
                            pcolor[i] = drawingParameters.FullyConstraintConstructionElementColor;
                        }
                        else {
                            if (isCoincident(GeoId, PosId)) {
                                pcolor[i] = drawingParameters.ConstrIcoColor;
                            }
                            else {
                                pcolor[i] = drawingParameters.CurveDraftColor;
                            }
                        }
                    }
                    else {  // this is a defined GeomPoint
                        if (constrainedElement) {
                            pcolor[i] = drawingParameters.FullyConstraintElementColor;
                        }
                        else {
                            if (isCoincident(GeoId, PosId)) {
                                pcolor[i] = drawingParameters.ConstrIcoColor;
                            }
                            else {
                                pcolor[i] = drawingParameters.CurveColor;
                            }
                        }
                    }
                }
            }
        }

        // update rendering height of points

        auto getRenderHeight = [this](
                                   DrawingParameters::GeometryRendering renderingtype,
                                   float toprendering,
                                   float midrendering,
                                   float lowrendering
                               ) {
            if (drawingParameters.topRenderingGeometry == renderingtype) {
                return toprendering;
            }
            else if (drawingParameters.midRenderingGeometry == renderingtype) {
                return midrendering;
            }
            else {
                return lowrendering;
            }
        };

        float zNormPoint = getRenderHeight(
            DrawingParameters::GeometryRendering::NormalGeometry,
            drawingParameters.zHighPoints,
            drawingParameters.zMidPoints,
            drawingParameters.zMidPoints
        );

        float zConstrPoint = getRenderHeight(
            DrawingParameters::GeometryRendering::Construction,
            drawingParameters.zHighPoints,
            drawingParameters.zMidPoints,
            drawingParameters.zMidPoints
        );

        for (int i = 0; i < PtNum; i++) {
            if (!coinMapping.isValidPointId(i, l)) {
                continue;
            }

            int GeoId = coinMapping.getPointGeoId(i, l);
            Sketcher::PointPos PosId = coinMapping.getPointPosId(i, l);
            pverts[i].getValue(x, y, z);
            auto geom = geolistfacade.getGeometryFacadeFromGeoId(GeoId);
            bool isExternal = GeoId < -1;

            if (geom) {
                z = viewOrientationFactor * zNormPoint;

                if (isCoincident(GeoId, PosId)) {
                    z = viewOrientationFactor * drawingParameters.zLowPoints;
                }
                else {
                    if (isExternal || isInternalAlignedGeom(GeoId)) {
                        z = viewOrientationFactor * drawingParameters.zRootPoint;
                    }
                    else if (geom->getConstruction()) {
                        z = viewOrientationFactor * zConstrPoint;
                    }
                }
                pverts[i].setValue(x, y, z);
            }
        }

        auto preselectpoint = ViewProviderSketchCoinAttorney::getPreselectPoint(viewProvider);
        auto preselectcross = ViewProviderSketchCoinAttorney::getPreselectCross(viewProvider);
        auto preselectcurve = ViewProviderSketchCoinAttorney::getPreselectCurve(viewProvider);

        auto raisePoint = [](SbVec3f& point, float height) {
            float x, y, z;
            point.getValue(x, y, z);
            point.setValue(x, y, height);
        };

        MultiFieldId preselectpointmfid;

        if (preselectcross == 0) {
            editModeScenegraphNodes.OriginPointMaterial->diffuseColor = drawingParameters.PreselectColor;
        }
        else if (preselectpoint != -1) {
            preselectpointmfid = coinMapping.getIndexLayer(preselectpoint);
            if (MultiFieldId::Invalid != preselectpointmfid && preselectpointmfid.layerId == l
                && preselectpointmfid.fieldIndex < PtNum) {

                pcolor[preselectpointmfid.fieldIndex] = drawingParameters.PreselectColor;

                if (drawingParameters.SelectHalo) {
                    phtransparency[preselectpointmfid.fieldIndex]
                        = drawingParameters.SelectHaloTransparency;
                }

                raisePoint(
                    pverts[preselectpointmfid.fieldIndex],
                    viewOrientationFactor * drawingParameters.zHighlight
                );
            }
        }

        ViewProviderSketchCoinAttorney::executeOnSelectionPointSet(
            viewProvider,
            [this,
             pcolor,
             pverts,
             phtransparency,
             PtNum,
             preselectpointmfid,
             layerId = l,
             &coinMapping = coinMapping,
             drawingParameters = this->drawingParameters,
             raisePoint,
             viewOrientationFactor](const int i) {
                auto pointindex = coinMapping.getIndexLayer(i);

                if (pointindex.fieldIndex == -1) {  // It's the origin
                    editModeScenegraphNodes.OriginPointMaterial->diffuseColor
                        = drawingParameters.SelectColor;
                    return;
                }

                if (layerId == pointindex.layerId && pointindex.fieldIndex >= 0
                    && pointindex.fieldIndex < PtNum) {
                    pcolor[pointindex.fieldIndex] = (preselectpointmfid == pointindex)
                        ? drawingParameters.PreselectSelectedColor
                        : drawingParameters.SelectColor;

                    if (drawingParameters.SelectHalo) {
                        phtransparency[pointindex.fieldIndex]
                            = drawingParameters.SelectHaloTransparency;
                    }

                    raisePoint(
                        pverts[pointindex.fieldIndex],
                        viewOrientationFactor * drawingParameters.zHighlight
                    );
                }
            }
        );

        // update colors and rendering height of the curves

        float zNormLine = getRenderHeight(
            DrawingParameters::GeometryRendering::NormalGeometry,
            drawingParameters.zHighLines,
            drawingParameters.zMidLines,
            drawingParameters.zLowLines
        );

        float zConstrLine = getRenderHeight(
            DrawingParameters::GeometryRendering::Construction,
            drawingParameters.zHighLines,
            drawingParameters.zMidLines,
            drawingParameters.zLowLines
        );

        float zExtLine = getRenderHeight(
            DrawingParameters::GeometryRendering::ExternalGeometry,
            drawingParameters.zHighLines,
            drawingParameters.zMidLines,
            drawingParameters.zLowLines
        );

        for (auto t = 0; t < geometryLayerParameters.getSubLayerCount(); t++) {
            int CurvNum = editModeScenegraphNodes.CurvesMaterials[l][t]->diffuseColor.getNum();
            SbColor* color = editModeScenegraphNodes.CurvesMaterials[l][t]->diffuseColor.startEditing();
            SbVec3f* verts = editModeScenegraphNodes.CurvesCoordinate[l][t]->point.startEditing();

            // As for the points, the halo pass mirrors the curves and only the ones marked below
            // are made visible.
            SoMaterial* curvesHaloMaterial = editModeScenegraphNodes.CurvesHaloMaterials[l][t];
            curvesHaloMaterial->diffuseColor.setNum(CurvNum);
            curvesHaloMaterial->transparency.setNum(CurvNum);
            SbColor* hcolor = curvesHaloMaterial->diffuseColor.startEditing();
            float* htransparency = curvesHaloMaterial->transparency.startEditing();
            for (int i = 0; i < CurvNum; i++) {
                hcolor[i] = drawingParameters.SelectHaloColor;
                htransparency[i] = 1.0f;
            }

            editModeScenegraphNodes.CurvesHaloTranslation[l][t]->translation.setValue(
                0,
                0,
                -viewOrientationFactor * drawingParameters.zHaloOffset
            );

            int j = 0;  // vertexindex
            for (int i = 0; i < CurvNum; i++) {
                if (!coinMapping.isValidCurveId(i, l, t)) {
                    continue;
                }

                int GeoId = coinMapping.getCurveGeoId(i, l, t);
                // CurvId has several vertices associated to 1 material
                // edit->CurveSet->numVertices => [i] indicates number of vertex for line i.
                int indexes = (editModeScenegraphNodes.CurveSet[l][t]->numVertices[i]);

                bool preselected = (preselectcurve == GeoId);

                auto* obj = viewProvider.getSketchObject();
                bool isGroupMember = GeoId >= 0 && obj->isInGroup(GeoId, false);
                if (isGroupMember) {
                    // We use the same color as group handle.
                    GeoId = obj->getGroupHandleIfInGroup(GeoId);
                }

                bool selected = ViewProviderSketchCoinAttorney::isCurveSelected(viewProvider, GeoId);
                // if a grouped edge is preselected we still want it to be shown
                preselected = preselected ? true : (preselectcurve == GeoId);
                bool constrainedElement = isFullyConstraintElement(GeoId);
                bool isExternal = GeoId < -1;

                if (selected || preselected) {
                    color[i] = selected ? (preselected ? drawingParameters.PreselectSelectedColor
                                                       : drawingParameters.SelectColor)
                                        : drawingParameters.PreselectColor;

                    if (drawingParameters.SelectHalo) {
                        htransparency[i] = drawingParameters.SelectHaloTransparency;
                    }

                    for (int k = j; j < k + indexes; j++) {
                        verts[j].getValue(x, y, z);
                        verts[j] = SbVec3f(x, y, viewOrientationFactor * drawingParameters.zHighLine);
                    }
                }
                else if (isExternal) {
                    auto geom = geolistfacade.getGeometryFacadeFromGeoId(GeoId);
                    std::unique_ptr<Part::Geometry> geomCopy(geom->clone());
                    // The ExternalGeometryFacade is not the owner of the geometry
                    auto egf = ExternalGeometryFacade::getFacade(geomCopy.get());
                    auto ref = egf->getRef();
                    if (egf->testFlag(ExternalGeometryExtension::Missing)) {
                        color[i] = drawingParameters.InvalidSketchColor;
                    }
                    else {
                        color[i] = egf->testFlag(ExternalGeometryExtension::Defining)
                            ? drawingParameters.CurveExternalDefiningColor
                            : drawingParameters.CurveExternalColor;
                    }
                    for (int k = j; j < k + indexes; j++) {
                        verts[j].getValue(x, y, z);
                        verts[j] = SbVec3f(x, y, viewOrientationFactor * zExtLine);
                    }
                }
                else {
                    if (issketchinvalid) {
                        color[i] = drawingParameters.InvalidSketchColor;

                        for (int k = j; j < k + indexes; j++) {
                            verts[j].getValue(x, y, z);
                            verts[j] = SbVec3f(x, y, viewOrientationFactor * zNormLine);
                        }
                    }
                    else if (geometryLayerParameters.isConstructionSubLayer(t)) {
                        if (constrainedElement) {
                            color[i] = drawingParameters.FullyConstraintConstructionElementColor;
                        }
                        else {
                            color[i] = drawingParameters.CurveDraftColor;
                        }

                        for (int k = j; j < k + indexes; j++) {
                            verts[j].getValue(x, y, z);
                            verts[j] = SbVec3f(x, y, viewOrientationFactor * zConstrLine);
                        }
                    }
                    else if (geometryLayerParameters.isInternalSubLayer(t)) {
                        if (constrainedElement) {
                            color[i] = drawingParameters.FullyConstraintInternalAlignmentColor;
                        }
                        else {
                            color[i] = drawingParameters.InternalAlignedGeoColor;
                        }

                        for (int k = j; j < k + indexes; j++) {
                            verts[j].getValue(x, y, z);
                            verts[j] = SbVec3f(x, y, viewOrientationFactor * zConstrLine);
                        }
                    }
                    else {
                        if (sketchFullyConstrained) {
                            color[i] = drawingParameters.FullyConstrainedColor;
                        }
                        else if (constrainedElement) {
                            color[i] = drawingParameters.FullyConstraintElementColor;
                        }
                        else {
                            color[i] = drawingParameters.CurveColor;
                        }

                        for (int k = j; j < k + indexes; j++) {
                            verts[j].getValue(x, y, z);
                            verts[j] = SbVec3f(x, y, viewOrientationFactor * zNormLine);
                        }
                    }
                }
            }

            editModeScenegraphNodes.CurvesMaterials[l][t]->diffuseColor.finishEditing();
            editModeScenegraphNodes.CurvesCoordinate[l][t]->point.finishEditing();
            editModeScenegraphNodes.CurveSet[l][t]->numVertices.finishEditing();
            curvesHaloMaterial->diffuseColor.finishEditing();
            curvesHaloMaterial->transparency.finishEditing();
        }

        // colors of the cross
        if (l == 0) {  // only in layer 0
            if (ViewProviderSketchCoinAttorney::isCurveSelected(viewProvider, Sketcher::GeoEnum::HAxis)) {
                crosscolorH[0] = drawingParameters.SelectColor;
            }
            else if (preselectcross == 1) {  // cross only in layer 0
                crosscolorH[0] = drawingParameters.PreselectColor;
            }
            else {
                crosscolorH[0] = drawingParameters.CrossColorH;
            }

            if (ViewProviderSketchCoinAttorney::isCurveSelected(viewProvider, Sketcher::GeoEnum::VAxis)) {
                crosscolorV[0] = drawingParameters.SelectColor;
            }
            else if (preselectcross == 2) {
                crosscolorV[0] = drawingParameters.PreselectColor;
            }
            else {
                crosscolorV[0] = drawingParameters.CrossColorV;
            }
        }

        editModeScenegraphNodes.PointsMaterials[l]->diffuseColor.finishEditing();
        editModeScenegraphNodes.PointsCoordinate[l]->point.finishEditing();
        pointsHaloMaterial->diffuseColor.finishEditing();
        pointsHaloMaterial->transparency.finishEditing();
    }

    editModeScenegraphNodes.RootCrossHMaterials->diffuseColor.finishEditing();
    editModeScenegraphNodes.RootCrossVMaterials->diffuseColor.finishEditing();

    // set color in the hidden pass
    editModeScenegraphNodes.RootCrossMaterialsOccludedH->diffuseColor.setValue(
        editModeScenegraphNodes.RootCrossHMaterials->diffuseColor[0]
    );
    editModeScenegraphNodes.RootCrossMaterialsOccludedV->diffuseColor.setValue(
        editModeScenegraphNodes.RootCrossVMaterials->diffuseColor[0]
    );
    editModeScenegraphNodes.OriginPointMaterialOccluded->diffuseColor.setValue(
        editModeScenegraphNodes.OriginPointMaterial->diffuseColor[0]
    );
}

void EditModeGeometryCoinManager::updateGeometryLayersConfiguration()
{
    // Several cases:
    // 1) The number of layers have changed
    // 2) The number of layers is the same, but the configuration needs to be updated

    // TODO: Quite some room for improvement here:
    geometryLayerParameters.setCoinLayerCount(viewProvider.VisualLayerList.getSize());

    emptyGeometryRootNodes();
    createEditModePointInventorNodes();
    createEditModeCurveInventorNodes();
}

auto concat(std::string string, int i)
{
    return string + std::to_string(i);
};


void EditModeGeometryCoinManager::createEditModeInventorNodes()
{
    createGeometryRootNodes();

    geometryLayerParameters.setCoinLayerCount(viewProvider.VisualLayerList.getSize());

    createEditModePointInventorNodes();

    createEditModeCurveInventorNodes();
}

void EditModeGeometryCoinManager::createGeometryRootNodes()
{
    // The halo passes come first, so that the geometry is drawn over its own halo.
    editModeScenegraphNodes.CurvesHaloGroup = new SmSwitchboard;
    editModeScenegraphNodes.EditRoot->addChild(editModeScenegraphNodes.CurvesHaloGroup);

    editModeScenegraphNodes.PointsHaloGroup = new SmSwitchboard;
    editModeScenegraphNodes.EditRoot->addChild(editModeScenegraphNodes.PointsHaloGroup);

    // stuff for the points ++++++++++++++++++++++++++++++++++++++
    editModeScenegraphNodes.PointsGroup = new SmSwitchboard;
    editModeScenegraphNodes.EditRoot->addChild(editModeScenegraphNodes.PointsGroup);

    // stuff for the Curves +++++++++++++++++++++++++++++++++++++++
    editModeScenegraphNodes.CurvesGroup = new SmSwitchboard;
    editModeScenegraphNodes.EditRoot->addChild(editModeScenegraphNodes.CurvesGroup);
}

void EditModeGeometryCoinManager::emptyGeometryRootNodes()
{
    Gui::coinRemoveAllChildren(editModeScenegraphNodes.PointsHaloGroup);
    Gui::coinRemoveAllChildren(editModeScenegraphNodes.CurvesHaloGroup);
    Gui::coinRemoveAllChildren(editModeScenegraphNodes.PointsGroup);
    Gui::coinRemoveAllChildren(editModeScenegraphNodes.CurvesGroup);

    // Removing the separators deletes the nodes they held, so the pointers to them have to go as
    // well. The creation of the layers appends to these vectors, and would otherwise both leave
    // dangling pointers behind and shift every layer index.
    editModeScenegraphNodes.PointsMaterials.clear();
    editModeScenegraphNodes.PointsCoordinate.clear();
    editModeScenegraphNodes.PointsDrawStyle.clear();
    editModeScenegraphNodes.PointSet.clear();
    editModeScenegraphNodes.PointsHaloMaterials.clear();
    editModeScenegraphNodes.PointsHaloTranslation.clear();
    editModeScenegraphNodes.PointHaloSet.clear();

    editModeScenegraphNodes.CurvesMaterials.clear();
    editModeScenegraphNodes.CurvesCoordinate.clear();
    editModeScenegraphNodes.CurveSet.clear();
    editModeScenegraphNodes.CurvesHaloMaterials.clear();
    editModeScenegraphNodes.CurvesHaloTranslation.clear();
}

void EditModeGeometryCoinManager::createEditModePointInventorNodes()
{
    for (int i = 0; i < geometryLayerParameters.getCoinLayerCount(); i++) {
        SoSeparator* sep = new SoSeparator;
        sep->ref();

        auto somaterial = new SoMaterial;
        editModeScenegraphNodes.PointsMaterials.push_back(somaterial);
        editModeScenegraphNodes.PointsMaterials[i]->setName(concat("PointsMaterials_", i).c_str());
        sep->addChild(editModeScenegraphNodes.PointsMaterials[i]);

        SoMaterialBinding* MtlBind = new SoMaterialBinding;
        MtlBind->setName(concat("PointsMaterialBinding", i).c_str());
        MtlBind->value = SoMaterialBinding::PER_VERTEX;
        sep->addChild(MtlBind);

        auto coords = new SoCoordinate3;
        editModeScenegraphNodes.PointsCoordinate.push_back(coords);
        editModeScenegraphNodes.PointsCoordinate[i]->setName(concat("PointsCoordinate", i).c_str());
        sep->addChild(editModeScenegraphNodes.PointsCoordinate[i]);

        auto drawstyle = new SoDrawStyle;
        editModeScenegraphNodes.PointsDrawStyle.push_back(drawstyle);
        editModeScenegraphNodes.PointsDrawStyle[i]->setName(concat("PointsDrawStyle", i).c_str());
        editModeScenegraphNodes.PointsDrawStyle[i]->pointSize = 8
            * drawingParameters.pixelScalingFactor;
        sep->addChild(editModeScenegraphNodes.PointsDrawStyle[i]);

        auto pointset = new SoMarkerSet;
        editModeScenegraphNodes.PointSet.push_back(pointset);
        editModeScenegraphNodes.PointSet[i]->setName(concat("PointSet", i).c_str());
        editModeScenegraphNodes.PointSet[i]->markerIndex = Gui::Inventor::MarkerBitmaps::getMarkerIndex(
            "CIRCLE_FILLED",
            drawingParameters.markerSize
        );
        sep->addChild(editModeScenegraphNodes.PointSet[i]);

        editModeScenegraphNodes.PointsGroup->addChild(sep);
        sep->unref();

        createEditModePointHaloInventorNodes(i);
    }
}

void EditModeGeometryCoinManager::createEditModePointHaloInventorNodes(int layer)
{
    SoSeparator* sep = new SoSeparator;
    sep->ref();

    // The halo is decoration only: picking must keep reaching the geometry below it.
    auto pickstyle = new SoPickStyle;
    pickstyle->style = SoPickStyle::UNPICKABLE;
    sep->addChild(pickstyle);

    auto translation = new SoTranslation;
    translation->setName(concat("PointsHaloTranslation", layer).c_str());
    editModeScenegraphNodes.PointsHaloTranslation.push_back(translation);
    sep->addChild(translation);

    auto somaterial = new SoMaterial;
    somaterial->setName(concat("PointsHaloMaterials_", layer).c_str());
    editModeScenegraphNodes.PointsHaloMaterials.push_back(somaterial);
    sep->addChild(somaterial);

    auto mtlBind = new SoMaterialBinding;
    mtlBind->setName(concat("PointsHaloMaterialBinding", layer).c_str());
    mtlBind->value = SoMaterialBinding::PER_VERTEX;
    sep->addChild(mtlBind);

    // The very same coordinates as the point set, so that the halo needs no bookkeeping of its own
    sep->addChild(editModeScenegraphNodes.PointsCoordinate[layer]);

    auto pointset = new SoMarkerSet;
    pointset->setName(concat("PointHaloSet", layer).c_str());
    pointset->markerIndex = getHaloMarkerIndex();
    editModeScenegraphNodes.PointHaloSet.push_back(pointset);
    sep->addChild(pointset);

    editModeScenegraphNodes.PointsHaloGroup->addChild(sep);
    sep->unref();
}

int EditModeGeometryCoinManager::getHaloMarkerIndex() const
{
    // Marker bitmaps only exist in a handful of sizes, and an unknown size silently falls back to
    // the smallest one, so the next size up that is actually available has to be picked.
    int wanted = drawingParameters.markerSize + 2 * drawingParameters.SelectHaloWidth;

    auto sizes = Gui::Inventor::MarkerBitmaps::getSupportedSizes("CIRCLE_FILLED");
    sizes.sort();

    int size = drawingParameters.markerSize;
    for (int supported : sizes) {
        if (supported > drawingParameters.markerSize) {
            size = supported;
            if (supported >= wanted) {
                break;
            }
        }
    }

    return Gui::Inventor::MarkerBitmaps::getMarkerIndex("CIRCLE_FILLED", size);
}

void EditModeGeometryCoinManager::createEditModeCurveInventorNodes()
{
    editModeScenegraphNodes.CurvesDrawStyle = new SoDrawStyle;
    editModeScenegraphNodes.CurvesDrawStyle->setName("CurvesDrawStyle");
    editModeScenegraphNodes.CurvesDrawStyle->lineWidth = drawingParameters.CurveWidth
        * drawingParameters.pixelScalingFactor;
    editModeScenegraphNodes.CurvesDrawStyle->linePattern = drawingParameters.CurvePattern;
    editModeScenegraphNodes.CurvesDrawStyle->linePatternScaleFactor = 2;

    editModeScenegraphNodes.CurvesConstructionDrawStyle = new SoDrawStyle;
    editModeScenegraphNodes.CurvesConstructionDrawStyle->setName("CurvesConstructionDrawStyle");
    editModeScenegraphNodes.CurvesConstructionDrawStyle->lineWidth = drawingParameters.ConstructionWidth
        * drawingParameters.pixelScalingFactor;
    editModeScenegraphNodes.CurvesConstructionDrawStyle->linePattern
        = drawingParameters.ConstructionPattern;
    editModeScenegraphNodes.CurvesConstructionDrawStyle->linePatternScaleFactor = 2;

    editModeScenegraphNodes.CurvesInternalDrawStyle = new SoDrawStyle;
    editModeScenegraphNodes.CurvesInternalDrawStyle->setName("CurvesInternalDrawStyle");
    editModeScenegraphNodes.CurvesInternalDrawStyle->lineWidth = drawingParameters.InternalWidth
        * drawingParameters.pixelScalingFactor;
    editModeScenegraphNodes.CurvesInternalDrawStyle->linePattern = drawingParameters.InternalPattern;
    editModeScenegraphNodes.CurvesInternalDrawStyle->linePatternScaleFactor = 2;

    editModeScenegraphNodes.CurvesExternalDrawStyle = new SoDrawStyle;
    editModeScenegraphNodes.CurvesExternalDrawStyle->setName("CurvesExternalDrawStyle");
    editModeScenegraphNodes.CurvesExternalDrawStyle->lineWidth = drawingParameters.ExternalWidth
        * drawingParameters.pixelScalingFactor;
    editModeScenegraphNodes.CurvesExternalDrawStyle->linePattern = drawingParameters.ExternalPattern;
    editModeScenegraphNodes.CurvesExternalDrawStyle->linePatternScaleFactor = 2;

    editModeScenegraphNodes.CurvesExternalDefiningDrawStyle = new SoDrawStyle;
    editModeScenegraphNodes.CurvesExternalDefiningDrawStyle->setName("CurvesExternalDefiningDrawStyle");
    editModeScenegraphNodes.CurvesExternalDefiningDrawStyle->lineWidth
        = drawingParameters.ExternalDefiningWidth * drawingParameters.pixelScalingFactor;
    editModeScenegraphNodes.CurvesExternalDefiningDrawStyle->linePattern
        = drawingParameters.ExternalDefiningPattern;
    editModeScenegraphNodes.CurvesExternalDefiningDrawStyle->linePatternScaleFactor = 2;

    createEditModeCurveHaloDrawStyles();

    for (int i = 0; i < geometryLayerParameters.getCoinLayerCount(); i++) {
        editModeScenegraphNodes.CurvesMaterials.emplace_back();
        editModeScenegraphNodes.CurvesCoordinate.emplace_back();
        editModeScenegraphNodes.CurveSet.emplace_back();
        editModeScenegraphNodes.CurvesHaloMaterials.emplace_back();
        editModeScenegraphNodes.CurvesHaloTranslation.emplace_back();
        for (int t = 0; t < geometryLayerParameters.getSubLayerCount(); t++) {
            SoSeparator* sep = new SoSeparator;
            sep->ref();

            auto somaterial = new SoMaterial;
            somaterial->setName(concat("CurvesMaterials", i * 10 + t).c_str());
            editModeScenegraphNodes.CurvesMaterials[i].push_back(somaterial);
            sep->addChild(editModeScenegraphNodes.CurvesMaterials[i][t]);

            auto MtlBind = new SoMaterialBinding;
            MtlBind->setName(concat("CurvesMaterialsBinding", i * 10 + t).c_str());
            MtlBind->value = SoMaterialBinding::PER_FACE;
            sep->addChild(MtlBind);

            auto coords = new SoCoordinate3;
            coords->setName(concat("CurvesCoordinate", i * 10 + t).c_str());
            editModeScenegraphNodes.CurvesCoordinate[i].push_back(coords);
            sep->addChild(editModeScenegraphNodes.CurvesCoordinate[i][t]);

            if (geometryLayerParameters.isConstructionSubLayer(t)) {
                sep->addChild(editModeScenegraphNodes.CurvesConstructionDrawStyle);
            }
            else if (geometryLayerParameters.isInternalSubLayer(t)) {
                sep->addChild(editModeScenegraphNodes.CurvesInternalDrawStyle);
            }
            else if (geometryLayerParameters.isExternalSubLayer(t)) {
                sep->addChild(editModeScenegraphNodes.CurvesExternalDrawStyle);
            }
            else if (geometryLayerParameters.isExternalDefiningSubLayer(t)) {
                sep->addChild(editModeScenegraphNodes.CurvesExternalDefiningDrawStyle);
            }
            else {
                sep->addChild(editModeScenegraphNodes.CurvesDrawStyle);
            }

            auto solineset = new SoLineSet;
            solineset->setName(concat("CurvesLineSet", i * 10 + t).c_str());
            editModeScenegraphNodes.CurveSet[i].push_back(solineset);
            sep->addChild(editModeScenegraphNodes.CurveSet[i][t]);

            editModeScenegraphNodes.CurvesGroup->addChild(sep);
            sep->unref();

            createEditModeCurveHaloInventorNodes(i, t);
        }
    }
}

void EditModeGeometryCoinManager::createEditModeCurveHaloDrawStyles()
{
    const auto haloDrawStyle = [](SoDrawStyle* style, const char* name) {
        style->setName(name);
        // A solid halo, so that a dashed curve is still surrounded by an unbroken outline
        style->linePattern = 0b1111111111111111;
    };

    editModeScenegraphNodes.CurvesHaloDrawStyle = new SoDrawStyle;
    haloDrawStyle(editModeScenegraphNodes.CurvesHaloDrawStyle, "CurvesHaloDrawStyle");

    editModeScenegraphNodes.CurvesHaloConstructionDrawStyle = new SoDrawStyle;
    haloDrawStyle(
        editModeScenegraphNodes.CurvesHaloConstructionDrawStyle,
        "CurvesHaloConstructionDrawStyle"
    );

    editModeScenegraphNodes.CurvesHaloInternalDrawStyle = new SoDrawStyle;
    haloDrawStyle(
        editModeScenegraphNodes.CurvesHaloInternalDrawStyle,
        "CurvesHaloInternalDrawStyle"
    );

    editModeScenegraphNodes.CurvesHaloExternalDrawStyle = new SoDrawStyle;
    haloDrawStyle(
        editModeScenegraphNodes.CurvesHaloExternalDrawStyle,
        "CurvesHaloExternalDrawStyle"
    );

    editModeScenegraphNodes.CurvesHaloExternalDefiningDrawStyle = new SoDrawStyle;
    haloDrawStyle(
        editModeScenegraphNodes.CurvesHaloExternalDefiningDrawStyle,
        "CurvesHaloExternalDefiningDrawStyle"
    );

    updateHaloNodeSizes();
}

void EditModeGeometryCoinManager::updateHaloNodeSizes()
{
    const auto haloWidth = [this](int width) {
        return (width + 2 * drawingParameters.SelectHaloWidth)
            * drawingParameters.pixelScalingFactor;
    };

    editModeScenegraphNodes.CurvesHaloDrawStyle->lineWidth = haloWidth(
        drawingParameters.CurveWidth
    );
    editModeScenegraphNodes.CurvesHaloConstructionDrawStyle->lineWidth = haloWidth(
        drawingParameters.ConstructionWidth
    );
    editModeScenegraphNodes.CurvesHaloInternalDrawStyle->lineWidth = haloWidth(
        drawingParameters.InternalWidth
    );
    editModeScenegraphNodes.CurvesHaloExternalDrawStyle->lineWidth = haloWidth(
        drawingParameters.ExternalWidth
    );
    editModeScenegraphNodes.CurvesHaloExternalDefiningDrawStyle->lineWidth = haloWidth(
        drawingParameters.ExternalDefiningWidth
    );

    int markerIndex = getHaloMarkerIndex();
    for (auto* pointHaloSet : editModeScenegraphNodes.PointHaloSet) {
        pointHaloSet->markerIndex = markerIndex;
    }
}

void EditModeGeometryCoinManager::createEditModeCurveHaloInventorNodes(int layer, int sublayer)
{
    SoSeparator* sep = new SoSeparator;
    sep->ref();

    // The halo is decoration only: picking must keep reaching the geometry below it.
    auto pickstyle = new SoPickStyle;
    pickstyle->style = SoPickStyle::UNPICKABLE;
    sep->addChild(pickstyle);

    auto translation = new SoTranslation;
    translation->setName(concat("CurvesHaloTranslation", layer * 10 + sublayer).c_str());
    editModeScenegraphNodes.CurvesHaloTranslation[layer].push_back(translation);
    sep->addChild(translation);

    auto somaterial = new SoMaterial;
    somaterial->setName(concat("CurvesHaloMaterials", layer * 10 + sublayer).c_str());
    editModeScenegraphNodes.CurvesHaloMaterials[layer].push_back(somaterial);
    sep->addChild(somaterial);

    auto mtlBind = new SoMaterialBinding;
    mtlBind->setName(concat("CurvesHaloMaterialsBinding", layer * 10 + sublayer).c_str());
    mtlBind->value = SoMaterialBinding::PER_FACE;
    sep->addChild(mtlBind);

    // The very same coordinates and line set as the geometry, so that the halo follows it without
    // any bookkeeping of its own
    sep->addChild(editModeScenegraphNodes.CurvesCoordinate[layer][sublayer]);

    if (geometryLayerParameters.isConstructionSubLayer(sublayer)) {
        sep->addChild(editModeScenegraphNodes.CurvesHaloConstructionDrawStyle);
    }
    else if (geometryLayerParameters.isInternalSubLayer(sublayer)) {
        sep->addChild(editModeScenegraphNodes.CurvesHaloInternalDrawStyle);
    }
    else if (geometryLayerParameters.isExternalSubLayer(sublayer)) {
        sep->addChild(editModeScenegraphNodes.CurvesHaloExternalDrawStyle);
    }
    else if (geometryLayerParameters.isExternalDefiningSubLayer(sublayer)) {
        sep->addChild(editModeScenegraphNodes.CurvesHaloExternalDefiningDrawStyle);
    }
    else {
        sep->addChild(editModeScenegraphNodes.CurvesHaloDrawStyle);
    }

    sep->addChild(editModeScenegraphNodes.CurveSet[layer][sublayer]);

    editModeScenegraphNodes.CurvesHaloGroup->addChild(sep);
    sep->unref();
}
