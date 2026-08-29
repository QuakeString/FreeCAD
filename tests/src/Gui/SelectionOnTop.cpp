// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QTest>

#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/nodekits/SoNodeKit.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoSeparator.h>

#include <App/Application.h>
#include <Gui/Application.h>
#include <Gui/Selection/SoFCUnifiedSelection.h>
#include <Gui/SoFCDB.h>
#include <Gui/View3DInventorSelection.h>
#include <Gui/ViewParams.h>
#include <src/App/InitApplication.h>

/** The scene graph that raises a selection above the rest of the model. These assertions are
 * about what the viewer actually builds, not about the parameters in isolation: the
 * transparency a user configures is only meaningful once it reaches the Coin material node.
 */
class SelectionOnTopTest: public QObject
{
    Q_OBJECT

private:
    static SoNode* childNamed(SoGroup* group, const char* name)
    {
        for (int i = 0; i < group->getNumChildren(); ++i) {
            SoNode* child = group->getChild(i);
            if (child->getName() == SbName(name)) {
                return child;
            }
        }
        return nullptr;
    }

    /// The group the viewer hangs everything drawn on top from
    static SoGroup* groupOnTop(Gui::SoFCUnifiedSelection* root)
    {
        return static_cast<SoGroup*>(childNamed(root, "GroupOnTop"));
    }

    static SoMaterial* materialOnTop(Gui::SoFCUnifiedSelection* root)
    {
        SoGroup* group = groupOnTop(root);
        return group ? static_cast<SoMaterial*>(childNamed(group, "GroupOnTopMaterial")) : nullptr;
    }

private Q_SLOTS:

    void initTestCase()
    {
        tests::initApplication();
        if (!Gui::Application::Instance) {
            new Gui::Application(false);
        }
        // The viewer's nodes are Coin types, so the database and FreeCAD's own node classes
        // have to be registered before any of them can be instantiated.
        SoDB::init();
        SoNodeKit::init();
        SoInteraction::init();
        Gui::SoFCDB::init();
        originalTransparency = Gui::ViewParams::instance()->getTransparencyOnTop();
    }

    void cleanup()
    {
        Gui::ViewParams::instance()->setTransparencyOnTop(originalTransparency);
    }

    /// Everything the rest of the tests reach for has to exist and be named as expected
    void testTheOnTopGroupIsBuilt()
    {
        auto* root = new Gui::SoFCUnifiedSelection;
        root->ref();
        Gui::View3DInventorSelection selection(root);

        SoGroup* group = groupOnTop(root);
        QVERIFY(group != nullptr);
        QVERIFY(childNamed(group, "GroupOnTopPickStyle") != nullptr);
        QVERIFY(childNamed(group, "GroupOnTopMaterial") != nullptr);
        QVERIFY(childNamed(group, "GroupOnTopSel") != nullptr);
        QVERIFY(childNamed(group, "GroupOnTopPreSel") != nullptr);

        root->unref();
    }

    /// What is drawn on top must not be pickable, or it would swallow clicks meant for the model
    void testTheOnTopGroupIsNotPickable()
    {
        auto* root = new Gui::SoFCUnifiedSelection;
        root->ref();
        Gui::View3DInventorSelection selection(root);

        auto* pickStyle = static_cast<SoPickStyle*>(childNamed(groupOnTop(root), "GroupOnTopPickStyle"));
        QVERIFY(pickStyle != nullptr);
        QCOMPARE(pickStyle->style.getValue(), static_cast<int>(SoPickStyle::UNPICKABLE));
        QVERIFY(pickStyle->isOverride());

        root->unref();
    }

    /// The configured transparency has to be the one the material carries, not a hardcoded value
    void testTransparencyComesFromTheParameter_data()
    {
        QTest::addColumn<double>("transparency");
        QTest::newRow("fully opaque") << 0.0;
        QTest::newRow("quarter") << 0.25;
        QTest::newRow("default") << 0.5;
        QTest::newRow("mostly clear") << 0.9;
        QTest::newRow("fully clear") << 1.0;
    }

    void testTransparencyComesFromTheParameter()
    {
        QFETCH(double, transparency);
        Gui::ViewParams::instance()->setTransparencyOnTop(transparency);

        auto* root = new Gui::SoFCUnifiedSelection;
        root->ref();
        Gui::View3DInventorSelection selection(root);

        SoMaterial* material = materialOnTop(root);
        QVERIFY(material != nullptr);
        QCOMPARE(material->transparency[0], static_cast<float>(transparency));

        root->unref();
    }

    /// Changing the preference has to reach an existing viewer, not only newly created ones
    void testChangingTheParameterReachesALiveViewer()
    {
        Gui::ViewParams::instance()->setTransparencyOnTop(0.25);

        auto* root = new Gui::SoFCUnifiedSelection;
        root->ref();
        Gui::View3DInventorSelection selection(root);
        QCOMPARE(materialOnTop(root)->transparency[0], 0.25F);

        // The viewer refreshes the material as selection changes are handled, so a preference
        // edited while a document is open takes effect without a restart.
        Gui::ViewParams::instance()->setTransparencyOnTop(0.8);
        Gui::SelectionChanges nothingSelected;
        nothingSelected.Type = Gui::SelectionChanges::ClrSelection;
        selection.checkGroupOnTop(nothingSelected);

        QCOMPARE(materialOnTop(root)->transparency[0], 0.8F);

        root->unref();
    }

    /// The material must override what the model sets, otherwise object colours win and the
    /// object on top stays opaque
    void testTheMaterialOverridesTheModel()
    {
        auto* root = new Gui::SoFCUnifiedSelection;
        root->ref();
        Gui::View3DInventorSelection selection(root);

        SoMaterial* material = materialOnTop(root);
        QVERIFY(material->isOverride());
        // Only transparency is imposed; the object keeps its own colour
        QVERIFY(material->diffuseColor.isIgnored());

        root->unref();
    }

private:
    double originalTransparency = 0.5;
};

QTEST_MAIN(SelectionOnTopTest)
#include "SelectionOnTop.moc"
