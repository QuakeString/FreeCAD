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

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include <App/DocumentObject.h>

#include "ElementAppearance.h"
#include "PartFeature.h"
#include "TopoShape.h"

namespace Part
{

namespace
{

/// Where an element ultimately comes from: the id of the object whose shape first
/// produced it, and its name there. Two elements with the same root are pieces of
/// the same original face, edge or vertex.
using ElementRoot = std::pair<long, std::string>;

/// Root of @a name in @a owner's history, following same-type ancestors only. Works
/// for names that are no longer in the shape, since the history is encoded in the
/// name itself.
std::optional<ElementRoot> elementRoot(App::DocumentObject* owner, const char* name)
{
    const auto history = Feature::getElementHistory(owner, name, true, true);
    if (history.empty() || !history.back().element) {
        return std::nullopt;
    }
    return ElementRoot {history.back().tag, history.back().element.toString()};
}

/// 1-based index of @a element when it is a sub-shape of @a type within @a count, else 0.
int indexOfType(const Data::IndexedName& element, TopAbs_ShapeEnum type, int count)
{
    if (!element) {
        return 0;
    }
    const auto [elementType, index] = TopoShape::shapeTypeAndIndex(element);
    if (elementType != type || index < 1 || index > count) {
        return 0;
    }
    return index;
}

}  // namespace

ElementMaterialMap resolveElementMaterials(
    App::DocumentObject* owner,
    const TopoShape& shape,
    const std::vector<App::ElementNamePair>& references,
    const std::vector<App::Material>& materials,
    TopAbs_ShapeEnum type
)
{
    ElementMaterialMap result;
    if (shape.isNull()) {
        return result;
    }
    const auto count = static_cast<int>(shape.countSubShapes(type));
    const auto pairs = std::min(references.size(), materials.size());

    std::set<int> matchedExactly;
    std::vector<std::size_t> unresolved;

    for (std::size_t i = 0; i < pairs; ++i) {
        const auto& reference = references[i];
        if (!reference.newName.empty()) {
            // A mapped name. Look it up in the current element map.
            const auto element = shape.getElementName(reference.newName.c_str());
            if (const int index = indexOfType(element.index, type, count)) {
                result[index] = materials[i];
                matchedExactly.insert(index);
            }
            else {
                unresolved.push_back(i);
            }
            continue;
        }
        // No mapped name: the shape has no element map, or the reference was
        // never resolved. The indexed name is all there is, and a missing
        // marker says the element was lost.
        if (reference.oldName.empty() || Data::hasMissingElement(reference.oldName.c_str())) {
            continue;
        }
        const auto element = shape.getElementName(reference.oldName.c_str());
        if (const int index = indexOfType(element.index, type, count)) {
            result[index] = materials[i];
            matchedExactly.insert(index);
        }
    }

    if (!owner || unresolved.empty()) {
        return result;
    }

    // A reference whose name is gone still says where its element came from.
    // Every current element with the same root is a piece of that element.
    // Part::Feature::getRelatedElements() is not used here: it compares source
    // names without their tags, so with unmapped inputs a cylinder's "Face1"
    // matches a box's "Face1".
    std::map<ElementRoot, std::vector<int>> byRoot;
    const std::string& typeName = TopoShape::shapeName(type);
    for (int index = 1; index <= count; ++index) {
        const auto name = typeName + std::to_string(index);
        if (const auto root = elementRoot(owner, name.c_str())) {
            byRoot[*root].push_back(index);
        }
    }
    for (const auto i : unresolved) {
        const auto root = elementRoot(owner, references[i].newName.c_str());
        if (!root) {
            continue;
        }
        const auto related = byRoot.find(*root);
        if (related == byRoot.end()) {
            continue;
        }
        for (const int index : related->second) {
            if (matchedExactly.count(index) == 0) {
                result[index] = materials[i];
            }
        }
    }
    return result;
}

std::vector<App::Material> buildPositionalMaterials(
    std::size_t count,
    const App::Material& base,
    const ElementMaterialMap& overrides
)
{
    std::vector<App::Material> result(std::max<std::size_t>(count, 1), base);
    bool applied = false;
    for (const auto& [index, material] : overrides) {
        if (index >= 1 && static_cast<std::size_t>(index) <= count) {
            result[index - 1] = material;
            applied = true;
        }
    }
    if (!applied) {
        result.resize(1);
    }
    return result;
}

DerivedElementMaterials deriveElementMaterials(
    const std::vector<App::Material>& positional,
    TopAbs_ShapeEnum type
)
{
    DerivedElementMaterials derived;
    if (positional.empty()) {
        return derived;
    }

    // The base is the material most entries share. Count each distinct
    // material once, keeping the earliest index for tie breaking.
    std::vector<std::pair<std::size_t, std::size_t>> distinct;  // (first index, count)
    for (std::size_t i = 0; i < positional.size(); ++i) {
        auto it = std::find_if(distinct.begin(), distinct.end(), [&](const auto& entry) {
            return sameMaterial(positional[entry.first], positional[i]);
        });
        if (it == distinct.end()) {
            distinct.emplace_back(i, 1);
        }
        else {
            ++it->second;
        }
    }
    const auto best = std::max_element(
        distinct.begin(),
        distinct.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; }
    );
    derived.base = positional[best->first];

    const std::string& typeName = TopoShape::shapeName(type);
    for (std::size_t i = 0; i < positional.size(); ++i) {
        if (!sameMaterial(positional[i], derived.base)) {
            derived.overrides[typeName + std::to_string(i + 1)] = positional[i];
        }
    }
    return derived;
}

bool sameMaterial(const App::Material& lhs, const App::Material& rhs)
{
    // clang-format off
    return lhs.ambientColor == rhs.ambientColor
        && lhs.diffuseColor == rhs.diffuseColor
        && lhs.specularColor == rhs.specularColor
        && lhs.emissiveColor == rhs.emissiveColor
        && lhs.shininess == rhs.shininess
        && lhs.transparency == rhs.transparency
        && lhs.image == rhs.image
        && lhs.imagePath == rhs.imagePath
        && lhs.uuid == rhs.uuid;
    // clang-format on
}

}  // namespace Part
