// SPDX-License-Identifier: LGPL-2.1-or-later
/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#ifndef PART_ELEMENTAPPEARANCE_H
#define PART_ELEMENTAPPEARANCE_H

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include <TopAbs_ShapeEnum.hxx>

#include <App/ElementNamingUtils.h>
#include <App/Material.h>
#include <Mod/Part/PartGlobal.h>

namespace App
{
class DocumentObject;
}

namespace Part
{
class TopoShape;

/// Appearance overrides resolved to 1-based sub-shape indices of one shape type.
using ElementMaterialMap = std::map<int, App::Material>;

/** Resolve stored element references to the sub-shapes they denote in @a shape.
 *
 * @a references and @a materials are index aligned: the shadow subs of a feature's
 * ColoredElements property and the material list a view provider keeps beside it.
 * Each reference is tried in order. An element that still exists under its mapped
 * name (or, for shapes without an element map, its indexed name) is matched exactly.
 * A mapped name that no longer exists is traced back to the object and element it
 * originally came from, and every current element of the same type with that same
 * origin takes the material, unless another reference matched it exactly. A face
 * whose hole was removed, or that was split and renamed, is found this way.
 * References that match neither way contribute nothing; the caller leaves them in
 * place so they take effect again as soon as their element reappears.
 *
 * Only the first min(references, materials) pairs are considered, so a desync
 * between the two lists can never assign a material to the wrong element.
 */
PartExport ElementMaterialMap resolveElementMaterials(
    App::DocumentObject* owner,
    const TopoShape& shape,
    const std::vector<App::ElementNamePair>& references,
    const std::vector<App::Material>& materials,
    TopAbs_ShapeEnum type
);

/** Build the positional material list a view provider renders from.
 *
 * Returns a single @a base entry when no override applies, which the renderer
 * treats as one material for the whole shape. Otherwise returns @a count entries
 * in sub-shape order with the overrides applied on top of @a base.
 */
PartExport std::vector<App::Material> buildPositionalMaterials(
    std::size_t count,
    const App::Material& base,
    const ElementMaterialMap& overrides
);

/// What deriveElementMaterials() recovers from a positional list.
struct PartExport DerivedElementMaterials
{
    /// The material most entries share; ties go to the earliest entry.
    App::Material base;
    /// Every entry that differs from @a base, keyed by indexed element name ("Face2").
    std::map<std::string, App::Material> overrides;
};

/** Recover element overrides from a positional list written by older code or scripts.
 *
 * The base is the most common material in the list. That is the only reading that
 * survives a list where the first face itself carries an override.
 */
PartExport DerivedElementMaterials deriveElementMaterials(
    const std::vector<App::Material>& positional,
    TopAbs_ShapeEnum type
);

/** Field-wise material comparison.
 *
 * App::Material::operator== reports two materials with the same non-empty uuid as
 * equal whatever their colours are, which would hide a colour edit made on a
 * library material. This compares every field.
 */
PartExport bool sameMaterial(const App::Material& lhs, const App::Material& rhs);

}  // namespace Part

#endif  // PART_ELEMENTAPPEARANCE_H
