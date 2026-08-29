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
#include <Gui/PrefWidgets.h>
#include <PreferencePages/DlgSettingsSelection.h>
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
        originalShowSelectionOnTop = Gui::ViewParams::instance()->getShowSelectionOnTop();
        originalMaxOnTopSelections = Gui::ViewParams::instance()->getMaxOnTopSelections();
    }

    void cleanup()
    {
        Gui::ViewParams::instance()->setTransparencyOnTop(originalTransparency);
        Gui::ViewParams::instance()->setShowSelectionOnTop(originalShowSelectionOnTop);
        Gui::ViewParams::instance()->setMaxOnTopSelections(originalMaxOnTopSelections);
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

    // --- the preferences page ---------------------------------------------------------
    //
    // The dialog carries its own copy of every default, written into the .ui file. Nothing
    // makes that copy follow ViewParams, so these check the two still agree: if they drift, a
    // fresh profile shows one value in the dialog while the viewer behaves like another.

    void testThePageExposesTheOnTopSettings()
    {
        Gui::Dialog::DlgSettingsSelection page;

        QVERIFY(page.findChild<Gui::PrefCheckBox*>("checkBoxSelectionOnTop") != nullptr);
        QVERIFY(page.findChild<Gui::PrefCheckBox*>("checkBoxPreSelectedFaceOnTop") != nullptr);
        QVERIFY(page.findChild<Gui::PrefDoubleSpinBox*>("spinBoxTransparencyOnTop") != nullptr);
        QVERIFY(page.findChild<Gui::PrefSpinBox*>("spinBoxMaxOnTopSelections") != nullptr);
    }

    void testTheWidgetsAreBoundToTheRightParameters()
    {
        Gui::Dialog::DlgSettingsSelection page;

        auto* onTop = page.findChild<Gui::PrefCheckBox*>("checkBoxSelectionOnTop");
        QCOMPARE(onTop->entryName(), QByteArray("ShowSelectionOnTop"));
        QCOMPARE(onTop->paramGrpPath(), QByteArray("View"));

        auto* preSel = page.findChild<Gui::PrefCheckBox*>("checkBoxPreSelectedFaceOnTop");
        QCOMPARE(preSel->entryName(), QByteArray("ShowPreSelectedFaceOnTop"));
        QCOMPARE(preSel->paramGrpPath(), QByteArray("View"));

        auto* transparency = page.findChild<Gui::PrefDoubleSpinBox*>("spinBoxTransparencyOnTop");
        QCOMPARE(transparency->entryName(), QByteArray("TransparencyOnTop"));
        QCOMPARE(transparency->paramGrpPath(), QByteArray("View"));

        auto* maxOnTop = page.findChild<Gui::PrefSpinBox*>("spinBoxMaxOnTopSelections");
        QCOMPARE(maxOnTop->entryName(), QByteArray("MaxOnTopSelections"));
        QCOMPARE(maxOnTop->paramGrpPath(), QByteArray("View"));
    }

    void testThePageDefaultsAgreeWithViewParams()
    {
        Gui::Dialog::DlgSettingsSelection page;
        Gui::ViewParams defaults;

        QCOMPARE(page.findChild<Gui::PrefCheckBox*>("checkBoxSelectionOnTop")->isChecked(),
                 defaults.getShowSelectionOnTop());
        QCOMPARE(page.findChild<Gui::PrefCheckBox*>("checkBoxPreSelectedFaceOnTop")->isChecked(),
                 defaults.getShowPreSelectedFaceOnTop());
        QCOMPARE(page.findChild<Gui::PrefDoubleSpinBox*>("spinBoxTransparencyOnTop")->value(),
                 defaults.getTransparencyOnTop());
        QCOMPARE(static_cast<long>(
                     page.findChild<Gui::PrefSpinBox*>("spinBoxMaxOnTopSelections")->value()),
                 defaults.getMaxOnTopSelections());
    }

    /// The spin box must be able to express the whole range the viewer understands
    void testTheTransparencySpinBoxCoversTheUsefulRange()
    {
        Gui::Dialog::DlgSettingsSelection page;
        auto* transparency = page.findChild<Gui::PrefDoubleSpinBox*>("spinBoxTransparencyOnTop");

        QCOMPARE(transparency->minimum(), 0.0);
        QCOMPARE(transparency->maximum(), 1.0);
        // Two decimals, or a user could not ask for anything between 0 and 1 in fine steps
        QVERIFY(transparency->decimals() >= 2);
    }

    /// Editing the page and saving has to be what the viewer then reads
    void testSavingThePageReachesTheViewer()
    {
        Gui::Dialog::DlgSettingsSelection page;
        page.loadSettings();

        page.findChild<Gui::PrefCheckBox*>("checkBoxSelectionOnTop")->setChecked(false);
        page.findChild<Gui::PrefDoubleSpinBox*>("spinBoxTransparencyOnTop")->setValue(0.65);
        page.findChild<Gui::PrefSpinBox*>("spinBoxMaxOnTopSelections")->setValue(5);
        page.saveSettings();

        QCOMPARE(Gui::ViewParams::instance()->getShowSelectionOnTop(), false);
        QCOMPARE(Gui::ViewParams::instance()->getTransparencyOnTop(), 0.65);
        QCOMPARE(Gui::ViewParams::instance()->getMaxOnTopSelections(), 5L);

        // and the viewer built afterwards uses it
        auto* root = new Gui::SoFCUnifiedSelection;
        root->ref();
        Gui::View3DInventorSelection selection(root);
        QCOMPARE(materialOnTop(root)->transparency[0], 0.65F);
        root->unref();
    }

private:
    double originalTransparency = 0.5;
    bool originalShowSelectionOnTop = true;
    long originalMaxOnTopSelections = 20;
};

QTEST_MAIN(SelectionOnTopTest)
#include "SelectionOnTop.moc"
