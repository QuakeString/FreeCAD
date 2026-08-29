// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <limits>
#include <set>
#include <string>

#include <App/Application.h>
#include <Gui/ViewParams.h>
#include <src/App/InitApplication.h>

using namespace Gui;

/** The parameters that decide whether a selection is drawn on top, and how transparent the
 * rest of the scene becomes while it is. Ported from the LinkStage3 branch, so the defaults
 * are asserted exactly: changing one silently changes what every user sees on first run.
 */
class ViewParamsTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        tests::initApplication();
        params = ViewParams::instance();

        // Remember what was there so one test cannot leak into the next
        showSelectionOnTop = params->getShowSelectionOnTop();
        showPreSelectedFaceOnTop = params->getShowPreSelectedFaceOnTop();
        transparencyOnTop = params->getTransparencyOnTop();
        maxOnTopSelections = params->getMaxOnTopSelections();
        defaultDrawStyle = params->getDefaultDrawStyle();
    }

    void TearDown() override
    {
        params->setShowSelectionOnTop(showSelectionOnTop);
        params->setShowPreSelectedFaceOnTop(showPreSelectedFaceOnTop);
        params->setTransparencyOnTop(transparencyOnTop);
        params->setMaxOnTopSelections(maxOnTopSelections);
        params->setDefaultDrawStyle(defaultDrawStyle);
    }

    ViewParams* params = nullptr;

private:
    long defaultDrawStyle = 0;
    bool showSelectionOnTop = true;
    bool showPreSelectedFaceOnTop = true;
    double transparencyOnTop = 0.5;
    long maxOnTopSelections = 20;
};

TEST_F(ViewParamsTest, DefaultsMatchTheBranchTheyCameFrom)  // NOLINT
{
    // A ViewParams reads the parameters of whoever is running the test, so the keys have to go
    // before the defaults can be observed. Without this the test passes or fails according to
    // whether the person running it has ever touched these settings.
    auto group =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");
    group->RemoveBool("ShowSelectionOnTop");
    group->RemoveBool("ShowPreSelectedFaceOnTop");
    group->RemoveFloat("TransparencyOnTop");
    group->RemoveInt("MaxOnTopSelections");
    group->RemoveInt("DefaultDrawStyle");

    ViewParams fresh;

    EXPECT_TRUE(fresh.getShowSelectionOnTop());
    EXPECT_TRUE(fresh.getShowPreSelectedFaceOnTop());
    EXPECT_DOUBLE_EQ(fresh.getTransparencyOnTop(), 0.5);
    EXPECT_EQ(fresh.getMaxOnTopSelections(), 20);
    // Zero is "As Is": a new document opens exactly as it does today unless asked otherwise
    EXPECT_EQ(fresh.getDefaultDrawStyle(), 0);
}

TEST_F(ViewParamsTest, BooleansRoundTripBothWays)  // NOLINT
{
    for (const bool value : {false, true, false}) {
        params->setShowSelectionOnTop(value);
        EXPECT_EQ(params->getShowSelectionOnTop(), value);

        params->setShowPreSelectedFaceOnTop(value);
        EXPECT_EQ(params->getShowPreSelectedFaceOnTop(), value);
    }
}

TEST_F(ViewParamsTest, TransparencyKeepsItsFraction)  // NOLINT
{
    // Stored as a double. Were it ever narrowed to an integer the whole range between fully
    // opaque and fully clear would collapse onto 0, which is the bug this guards.
    for (const double value : {0.0, 0.01, 0.25, 0.5, 0.75, 0.99, 1.0}) {
        params->setTransparencyOnTop(value);
        EXPECT_DOUBLE_EQ(params->getTransparencyOnTop(), value);
    }
}

TEST_F(ViewParamsTest, TransparencySurvivesValuesOutsideTheUsefulRange)  // NOLINT
{
    // Nothing clamps these, and a user editing the parameter by hand can write anything.
    // The contract is that the value is stored faithfully rather than mangled.
    for (const double value : {-1.5, 2.25, 1e6 + 0.5}) {
        params->setTransparencyOnTop(value);
        EXPECT_DOUBLE_EQ(params->getTransparencyOnTop(), value);
    }
}

TEST_F(ViewParamsTest, OnTopSelectionLimitTakesTheWholeRange)  // NOLINT
{
    for (const long value : {0L, 1L, 20L, 1000L, std::numeric_limits<long>::max()}) {
        params->setMaxOnTopSelections(value);
        EXPECT_EQ(params->getMaxOnTopSelections(), value);
    }
}

TEST_F(ViewParamsTest, ANegativeLimitIsKeptAsGiven)  // NOLINT
{
    // The viewer reads a negative limit as "no limit", so it must not be clamped to zero,
    // which would instead mean "never show anything on top".
    params->setMaxOnTopSelections(-1);
    EXPECT_EQ(params->getMaxOnTopSelections(), -1);
}

TEST_F(ViewParamsTest, EachParameterIsIndependent)  // NOLINT
{
    params->setShowSelectionOnTop(false);
    params->setShowPreSelectedFaceOnTop(true);
    params->setTransparencyOnTop(0.125);
    params->setMaxOnTopSelections(7);

    EXPECT_FALSE(params->getShowSelectionOnTop());
    EXPECT_TRUE(params->getShowPreSelectedFaceOnTop());
    EXPECT_DOUBLE_EQ(params->getTransparencyOnTop(), 0.125);
    EXPECT_EQ(params->getMaxOnTopSelections(), 7);

    // Writing one must not disturb the others
    params->setTransparencyOnTop(0.875);

    EXPECT_FALSE(params->getShowSelectionOnTop());
    EXPECT_TRUE(params->getShowPreSelectedFaceOnTop());
    EXPECT_EQ(params->getMaxOnTopSelections(), 7);
}

TEST_F(ViewParamsTest, ValuesReachTheParameterGroupSoTheyOutliveTheSession)  // NOLINT
{
    auto group =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");

    params->setShowSelectionOnTop(false);
    params->setTransparencyOnTop(0.375);
    params->setMaxOnTopSelections(11);

    // Read straight from the stored parameters rather than through the accessor, which proves
    // the value was actually written out and not merely cached in memory.
    EXPECT_FALSE(group->GetBool("ShowSelectionOnTop", true));
    EXPECT_DOUBLE_EQ(group->GetFloat("TransparencyOnTop", 0.0), 0.375);
    EXPECT_EQ(group->GetInt("MaxOnTopSelections", 0), 11);
}

TEST_F(ViewParamsTest, TheInstanceIsShared)  // NOLINT
{
    // Callers all over the Gui reach these through instance(); they must see one another's
    // writes, or a preference change would apply to only part of the application.
    ViewParams::instance()->setMaxOnTopSelections(42);
    EXPECT_EQ(ViewParams::instance()->getMaxOnTopSelections(), 42);
    EXPECT_EQ(params, ViewParams::instance());
}

TEST_F(ViewParamsTest, EveryDrawStyleIndexNamesTheStyleTheMenuOffers)  // NOLINT
{
    // These names are the override modes the viewer understands, and the order is the order of
    // the draw style commands in CommandView.cpp. If either side is reordered the preference
    // silently starts selecting a different style, which is what this pins down.
    EXPECT_STREQ(ViewParams::drawStyleName(0), "As Is");
    EXPECT_STREQ(ViewParams::drawStyleName(1), "Point");
    EXPECT_STREQ(ViewParams::drawStyleName(2), "Wireframe");
    EXPECT_STREQ(ViewParams::drawStyleName(3), "Hidden Line");
    EXPECT_STREQ(ViewParams::drawStyleName(4), "No Shading");
    EXPECT_STREQ(ViewParams::drawStyleName(5), "Shaded");
    EXPECT_STREQ(ViewParams::drawStyleName(6), "Flat Lines");
}

TEST_F(ViewParamsTest, AnUnknownDrawStyleIndexLeavesTheViewAlone)  // NOLINT
{
    // A number from a newer version, a hand edited parameter or a negative value must not put
    // the viewer into some arbitrary style.
    for (const long index : {-1L, 7L, 99L, std::numeric_limits<long>::max()}) {
        EXPECT_STREQ(ViewParams::drawStyleName(index), "As Is");
    }
}

TEST_F(ViewParamsTest, TheDrawStyleRoundTrips)  // NOLINT
{
    for (long index = 0; index <= 6; ++index) {
        params->setDefaultDrawStyle(index);
        EXPECT_EQ(params->getDefaultDrawStyle(), index);
    }
}

TEST_F(ViewParamsTest, EveryDrawStyleNameIsDistinct)  // NOLINT
{
    // Two indexes naming the same style would mean one of the menu entries is unreachable
    // through the preference.
    std::set<std::string> names;
    for (long index = 0; index <= 6; ++index) {
        names.insert(ViewParams::drawStyleName(index));
    }
    EXPECT_EQ(names.size(), 7U);
}
