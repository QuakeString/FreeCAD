// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <App/Application.h>
#include <App/Document.h>
#include <App/ElementNamingUtils.h>
#include <src/App/InitApplication.h>

#include "Mod/Part/App/ElementAppearance.h"
#include "Mod/Part/App/FeaturePartCut.h"
#include "Mod/Part/App/PrimitiveFeature.h"
#include "PartTestHelpers.h"

using namespace Part;

namespace
{

App::Material coloured(float r, float g, float b)
{
    App::Material material;
    material.diffuseColor = Base::Color(r, g, b);
    return material;
}

Base::Vector3d faceCentre(const TopoShape& shape, int index)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(shape.getSubShape(TopAbs_FACE, index), props);
    const auto centre = props.CentreOfMass();
    return Base::Vector3d(centre.X(), centre.Y(), centre.Z());
}

template<typename Predicate>
std::vector<int> facesWhere(const TopoShape& shape, Predicate predicate)
{
    std::vector<int> result;
    const int count = static_cast<int>(shape.countSubShapes(TopAbs_FACE));
    for (int i = 1; i <= count; ++i) {
        if (predicate(faceCentre(shape, i))) {
            result.push_back(i);
        }
    }
    return result;
}

template<typename Predicate>
int theFaceWhere(const TopoShape& shape, Predicate predicate)
{
    const auto faces = facesWhere(shape, predicate);
    EXPECT_EQ(faces.size(), 1U) << "expected exactly one matching face";
    return faces.empty() ? 0 : faces.front();
}

auto atZ(double z)
{
    return [z](const Base::Vector3d& centre) { return std::abs(centre.z - z) < 1e-6; };
}

// The drill hole's cylindrical wall, centred half way up the box.
bool inHole(const Base::Vector3d& centre)
{
    return std::abs(centre.x - 0.5) < 1e-6 && std::abs(centre.y - 1.0) < 1e-6
        && std::abs(centre.z - 1.5) < 1e-6;
}

/// "1:<mapped name> 2:<mapped name> ..." for every face, for failure messages.
std::string faceNames(const TopoShape& shape)
{
    std::string result;
    const int count = static_cast<int>(shape.countSubShapes(TopAbs_FACE));
    for (int i = 1; i <= count; ++i) {
        result += std::to_string(i) + ":"
            + shape.getMappedName(Data::IndexedName::fromConst("Face", i)).toString() + " ";
    }
    return result;
}

std::vector<int> keys(const ElementMaterialMap& map)
{
    std::vector<int> result;
    for (const auto& [index, material] : map) {
        result.push_back(index);
    }
    return result;
}

}  // namespace

/// A 1 x 2 x 3 box drilled top to bottom by a cylinder: 7 mapped faces.
class ElementAppearanceTest: public ::testing::Test, public PartTestHelpers::PartTestHelperClass
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        createTestDoc();
        _cylinder = _doc->addObject<Part::Cylinder>();
        _cylinder->Radius.setValue(0.25);
        _cylinder->Height.setValue(5);
        _cylinder->Placement.setValue(Base::Placement(drillPosition, Base::Rotation()));
        _cut = _doc->addObject<Part::Cut>();
        _cut->Base.setValue(_boxes[0]);
        _cut->Tool.setValue(_cylinder);
        _doc->recompute();
    }

    void TearDown() override
    {}

    static void paint(Part::Feature* feature, int faceIndex)
    {
        feature->ColoredElements.setValue(
            feature,
            std::vector<std::string> {"Face" + std::to_string(faceIndex)}
        );
    }

    static ElementMaterialMap resolve(Part::Feature* feature, const std::vector<App::Material>& materials)
    {
        return resolveElementMaterials(
            feature,
            feature->Shape.getShape(),
            feature->ColoredElements.getShadowSubs(),
            materials,
            TopAbs_FACE
        );
    }

    const Base::Vector3d drillPosition {0.5, 1.0, -1.0};
    const App::Material red = coloured(1, 0, 0);
    const App::Material blue = coloured(0, 0, 1);
    Part::Cylinder* _cylinder = nullptr;  // NOLINT
    Part::Cut* _cut = nullptr;            // NOLINT
};

TEST_F(ElementAppearanceTest, fixtureIsADrilledBoxWithAnElementMap)
{
    const auto& shape = _cut->Shape.getShape();
    EXPECT_EQ(shape.countSubShapes(TopAbs_FACE), 7U);
    EXPECT_GT(shape.getElementMapSize(), 0U);
    EXPECT_EQ(facesWhere(shape, atZ(3.0)).size(), 1U);
    EXPECT_EQ(facesWhere(shape, inHole).size(), 1U);
    EXPECT_EQ(_boxes[1]->Shape.getShape().getElementMapSize(), 0U) << "primitives are unmapped";
}

TEST_F(ElementAppearanceTest, referenceResolvesToAMappedNameWithoutTouchingTheFeature)
{
    const int top = theFaceWhere(_cut->Shape.getShape(), atZ(3.0));

    paint(_cut, top);

    const auto& shadows = _cut->ColoredElements.getShadowSubs();
    ASSERT_EQ(shadows.size(), 1U);
    EXPECT_EQ(_cut->ColoredElements.getValue(), _cut);
    EXPECT_NE(Data::isMappedElement(shadows[0].newName.c_str()), nullptr) << shadows[0].newName;
    EXPECT_NE(shadows[0].newName.find(Data::POSTFIX_TAG), std::string::npos) << shadows[0].newName;
    EXPECT_EQ(shadows[0].oldName, "Face" + std::to_string(top));
    EXPECT_FALSE(_cut->isTouched()) << "an output property must not schedule a recompute";
    const auto outList = _cut->getOutList();
    EXPECT_EQ(std::find(outList.begin(), outList.end(), _cut), outList.end())
        << "a hidden self link must not create a dependency";
}

TEST_F(ElementAppearanceTest, exactMatchPaintsTheReferencedFace)
{
    const int top = theFaceWhere(_cut->Shape.getShape(), atZ(3.0));
    paint(_cut, top);

    const auto resolved = resolve(_cut, {red});

    ASSERT_EQ(keys(resolved), std::vector<int> {top});
    EXPECT_TRUE(sameMaterial(resolved.at(top), red));
}

TEST_F(ElementAppearanceTest, exactMatchFollowsTheFaceThroughARecompute)
{
    paint(_cut, theFaceWhere(_cut->Shape.getShape(), atZ(3.0)));

    _boxes[0]->Height.setValue(3.5);
    _doc->recompute();

    const auto& shape = _cut->Shape.getShape();
    const int top = theFaceWhere(shape, atZ(3.5));
    EXPECT_EQ(keys(resolve(_cut, {red})), std::vector<int> {top});
    EXPECT_EQ(_cut->ColoredElements.getShadowSubs()[0].oldName, "Face" + std::to_string(top))
        << "the indexed shadow is rewritten to the current index";
}

TEST_F(ElementAppearanceTest, aFaceKeepsItsColourWhenItsHoleIsRemoved)
{
    paint(_cut, theFaceWhere(_cut->Shape.getShape(), atZ(3.0)));
    const std::string stored = _cut->ColoredElements.getShadowSubs()[0].newName;

    // Shorten the drill so it no longer reaches the top face: that face is no
    // longer "modified", so its name changes.
    _cylinder->Height.setValue(2);
    _doc->recompute();

    const auto& shape = _cut->Shape.getShape();
    const int top = theFaceWhere(shape, atZ(3.0));
    ASSERT_FALSE(static_cast<bool>(shape.getElementName(stored.c_str()).index))
        << "precondition: the drilled face's name must be gone: " << stored << "\n" << faceNames(shape);

    EXPECT_EQ(keys(resolve(_cut, {red})), std::vector<int> {top})
        << "shadow " << _cut->ColoredElements.getShadowSubs()[0].newName << "\n" << faceNames(shape);

    // The same through the resolver alone, with the stale name as stored on disk,
    // so the result does not depend on the App layer's own geometry search.
    const std::vector<App::ElementNamePair> stale = {{stored, "?Face3"}};
    EXPECT_EQ(keys(resolveElementMaterials(_cut, shape, stale, {red}, TopAbs_FACE)), std::vector<int> {top})
        << faceNames(shape);
}

TEST_F(ElementAppearanceTest, aSplitFaceKeepsTheColourOnTheFragmentThatKeptItsName)
{
    paint(_cut, theFaceWhere(_cut->Shape.getShape(), atZ(3.0)));
    const std::string stored = _cut->ColoredElements.getShadowSubs()[0].newName;

    // Swap the drill for a slot that splits the top face in two.
    auto slab = _doc->addObject<Part::Box>();
    slab->Length.setValue(0.2);
    slab->Width.setValue(4);
    slab->Height.setValue(2);
    slab->Placement.setValue(Base::Placement(Base::Vector3d(0.4, -1.0, 2.0), Base::Rotation()));
    _cut->Tool.setValue(slab);
    _doc->recompute();

    const auto& shape = _cut->Shape.getShape();
    ASSERT_EQ(facesWhere(shape, atZ(3.0)).size(), 2U);
    const auto kept = shape.getElementName(stored.c_str()).index;
    ASSERT_TRUE(static_cast<bool>(kept)) << "one fragment keeps the name\n" << faceNames(shape);
    const int keptIndex = TopoShape::shapeTypeAndIndex(kept).second;
    EXPECT_TRUE(atZ(3.0)(faceCentre(shape, keptIndex)));

    // An exact match takes precedence over origin tracing, as in LinkStage3: the
    // other fragment is not painted by this feature's own store.
    EXPECT_EQ(keys(resolve(_cut, {red})), std::vector<int> {keptIndex}) << faceNames(shape);
}

TEST_F(ElementAppearanceTest, aLostFaceIsSkippedAndComesBack)
{
    paint(_cut, theFaceWhere(_cut->Shape.getShape(), inHole));

    _cylinder->Placement.setValue(Base::Placement(Base::Vector3d(5, 5, -1), Base::Rotation()));
    _doc->recompute();

    EXPECT_EQ(_cut->Shape.getShape().countSubShapes(TopAbs_FACE), 6U);
    EXPECT_TRUE(resolve(_cut, {red}).empty())
        << "shadow " << _cut->ColoredElements.getShadowSubs()[0].newName << "\n"
        << faceNames(_cut->Shape.getShape());
    ASSERT_EQ(_cut->ColoredElements.getSubValues().size(), 1U) << "the reference is retained";
    const auto& shadows = _cut->ColoredElements.getShadowSubs();
    EXPECT_TRUE(Data::hasMissingElement(shadows[0].oldName.c_str())) << shadows[0].oldName;
    EXPECT_FALSE(shadows[0].newName.empty());

    _cylinder->Placement.setValue(Base::Placement(drillPosition, Base::Rotation()));
    _doc->recompute();

    const int hole = theFaceWhere(_cut->Shape.getShape(), inHole);
    EXPECT_EQ(keys(resolve(_cut, {red})), std::vector<int> {hole});
    EXPECT_FALSE(Data::hasMissingElement(_cut->ColoredElements.getShadowSubs()[0].oldName.c_str()));
}

TEST_F(ElementAppearanceTest, unmappedShapeFallsBackToTheIndex)
{
    auto box = _boxes[1];
    box->ColoredElements.setValue(box, std::vector<std::string> {"Face2"});

    EXPECT_TRUE(box->ColoredElements.getShadowSubs()[0].newName.empty());
    EXPECT_EQ(keys(resolve(box, {red})), std::vector<int> {2});

    box->Height.setValue(4);
    _doc->recompute();
    EXPECT_EQ(keys(resolve(box, {red})), std::vector<int> {2});
}

TEST_F(ElementAppearanceTest, referencesThatCannotMatchAreIgnored)
{
    auto box = _boxes[1];
    const std::vector<App::ElementNamePair> references = {
        {"", "?Face2"},  // lost element
        {"", "Face9"},   // out of range
        {"", ""},        // empty
        {"", "Edge2"},   // wrong type
    };

    const auto resolved = resolveElementMaterials(
        box,
        box->Shape.getShape(),
        references,
        {red, red, red, red},
        TopAbs_FACE
    );

    EXPECT_TRUE(resolved.empty());
}

TEST_F(ElementAppearanceTest, onlyPairedEntriesAreResolved)
{
    auto box = _boxes[1];
    const auto& shape = box->Shape.getShape();
    const std::vector<App::ElementNamePair> two = {{"", "Face1"}, {"", "Face2"}};
    const std::vector<App::ElementNamePair> one = {{"", "Face1"}};

    EXPECT_EQ(keys(resolveElementMaterials(box, shape, two, {red}, TopAbs_FACE)), std::vector<int> {1});
    EXPECT_EQ(keys(resolveElementMaterials(box, shape, one, {red, blue}, TopAbs_FACE)), std::vector<int> {1});
}

TEST_F(ElementAppearanceTest, selfLinkSurvivesSaveAndLoad)
{
    const int top = theFaceWhere(_cut->Shape.getShape(), atZ(3.0));
    paint(_cut, top);
    const std::string stored = _cut->ColoredElements.getShadowSubs()[0].newName;
    const std::string cutName = _cut->getNameInDocument();
    const auto path = (std::filesystem::temp_directory_path() / "ElementAppearanceTest.FCStd").string();

    ASSERT_TRUE(_doc->saveAs(path.c_str()));
    App::GetApplication().closeDocument(_docName.c_str());
    _doc = nullptr;
    auto doc = App::GetApplication().openDocument(path.c_str());
    ASSERT_NE(doc, nullptr);
    auto cut = dynamic_cast<Part::Cut*>(doc->getObject(cutName.c_str()));
    ASSERT_NE(cut, nullptr);

    EXPECT_EQ(cut->ColoredElements.getValue(), cut);
    ASSERT_EQ(cut->ColoredElements.getShadowSubs().size(), 1U);
    EXPECT_EQ(cut->ColoredElements.getShadowSubs()[0].newName, stored);
    doc->recompute();
    EXPECT_EQ(keys(resolve(cut, {red})), std::vector<int> {theFaceWhere(cut->Shape.getShape(), atZ(3.0))});

    App::GetApplication().closeDocument(doc->getName());
}

TEST(ElementAppearanceHelpers, positionalListCollapsesWhenNothingIsOverridden)
{
    const auto base = coloured(0.5F, 0.5F, 0.5F);
    const auto red = coloured(1, 0, 0);

    for (const auto& list : {
             buildPositionalMaterials(6, base, {}),
             buildPositionalMaterials(0, base, {{1, red}}),
             buildPositionalMaterials(6, base, {{7, red}}),
             buildPositionalMaterials(6, base, {{0, red}}),
         }) {
        ASSERT_EQ(list.size(), 1U);
        EXPECT_TRUE(sameMaterial(list[0], base));
    }
}

TEST(ElementAppearanceHelpers, positionalListAppliesOverridesAtTheirIndex)
{
    const auto base = coloured(0.5F, 0.5F, 0.5F);
    const auto red = coloured(1, 0, 0);
    const auto blue = coloured(0, 0, 1);

    const auto list = buildPositionalMaterials(6, base, {{2, red}, {5, blue}, {9, red}});

    ASSERT_EQ(list.size(), 6U);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& expected = i == 1 ? red : i == 4 ? blue : base;
        EXPECT_TRUE(sameMaterial(list[i], expected)) << "index " << i;
    }
}

TEST(ElementAppearanceHelpers, derivedBaseIsTheMostCommonMaterial)
{
    const auto grey = coloured(0.5F, 0.5F, 0.5F);
    const auto red = coloured(1, 0, 0);
    const auto blue = coloured(0, 0, 1);

    auto derived = deriveElementMaterials({grey, red, grey, grey, blue, grey}, TopAbs_FACE);
    EXPECT_TRUE(sameMaterial(derived.base, grey));
    ASSERT_EQ(derived.overrides.size(), 2U);
    EXPECT_TRUE(sameMaterial(derived.overrides.at("Face2"), red));
    EXPECT_TRUE(sameMaterial(derived.overrides.at("Face5"), blue));

    derived = deriveElementMaterials({red, grey, grey, grey, grey, grey}, TopAbs_FACE);
    EXPECT_TRUE(sameMaterial(derived.base, grey)) << "a painted first face is not the base";
    ASSERT_EQ(derived.overrides.size(), 1U);
    EXPECT_TRUE(sameMaterial(derived.overrides.at("Face1"), red));

    derived = deriveElementMaterials({red, grey}, TopAbs_EDGE);
    EXPECT_TRUE(sameMaterial(derived.base, red)) << "ties go to the earliest entry";
    ASSERT_EQ(derived.overrides.size(), 1U);
    EXPECT_TRUE(sameMaterial(derived.overrides.at("Edge2"), grey));

    derived = deriveElementMaterials({red}, TopAbs_FACE);
    EXPECT_TRUE(sameMaterial(derived.base, red));
    EXPECT_TRUE(derived.overrides.empty());

    EXPECT_TRUE(deriveElementMaterials({}, TopAbs_FACE).overrides.empty());
}

TEST(ElementAppearanceHelpers, sameMaterialLooksPastTheUuid)
{
    auto lhs = coloured(1, 0, 0);
    auto rhs = coloured(0, 0, 1);
    lhs.uuid = rhs.uuid = "same-library-material";

    EXPECT_TRUE(lhs == rhs) << "App::Material::operator== short-circuits on the uuid";
    EXPECT_FALSE(sameMaterial(lhs, rhs));

    auto copy = lhs;
    EXPECT_TRUE(sameMaterial(lhs, copy));
    copy.transparency = 0.5F;
    EXPECT_FALSE(sameMaterial(lhs, copy));
    copy = lhs;
    copy.uuid = "another";
    EXPECT_FALSE(sameMaterial(lhs, copy)) << "the uuid itself is part of the identity";
}
