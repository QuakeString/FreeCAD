// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2025 FreeCAD Project Association                         *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include <gtest/gtest.h>

#include <App/Application.h>
#include <Gui/ThemeManager.h>
#include <src/App/InitApplication.h>

using namespace Gui;

class ThemeManagerTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        tests::initApplication();

        parameters = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/MainWindow"
        );

        removeThemeParameters();
    }

    void TearDown() override
    {
        removeThemeParameters();
    }

    void removeThemeParameters()
    {
        parameters->RemoveASCII("Theme");
        parameters->RemoveASCII("ThemeMode");
        parameters->RemoveASCII("ThemeLight");
        parameters->RemoveASCII("ThemeDark");
    }

    static ThemeManager& manager()
    {
        return *ThemeManager::instance();
    }

    ParameterGrp::handle parameters;
};

TEST_F(ThemeManagerTest, ModeIsConvertedToAndFromText)
{
    EXPECT_STREQ(ThemeManager::toString(ThemeMode::Light), "Light");
    EXPECT_STREQ(ThemeManager::toString(ThemeMode::Dark), "Dark");
    EXPECT_STREQ(ThemeManager::toString(ThemeMode::System), "System");

    EXPECT_EQ(ThemeManager::modeFromString("Light"), ThemeMode::Light);
    EXPECT_EQ(ThemeManager::modeFromString("dark"), ThemeMode::Dark);
    EXPECT_EQ(ThemeManager::modeFromString("SYSTEM"), ThemeMode::System);

    // Anything unexpected falls back to the light mode
    EXPECT_EQ(ThemeManager::modeFromString("nonsense"), ThemeMode::Light);
}

TEST_F(ThemeManagerTest, ModeIsStoredInTheParameters)
{
    EXPECT_FALSE(ThemeManager::isConfigured());

    manager().setMode(ThemeMode::System);

    EXPECT_TRUE(ThemeManager::isConfigured());
    EXPECT_EQ(parameters->GetASCII("ThemeMode", ""), "System");
    EXPECT_EQ(manager().mode(), ThemeMode::System);
}

TEST_F(ThemeManagerTest, ModeDefaultsToTheColorSchemeOfTheActiveTheme)
{
    // Users that have not picked a mode yet keep the theme they are using
    parameters->SetASCII("Theme", "FreeCAD Dark");
    EXPECT_EQ(manager().mode(), ThemeMode::Dark);

    parameters->SetASCII("Theme", "FreeCAD Light");
    EXPECT_EQ(manager().mode(), ThemeMode::Light);
}

TEST_F(ThemeManagerTest, ThemesAreStoredPerColorScheme)
{
    manager().setTheme(ColorScheme::Light, "Some Light Theme");
    manager().setTheme(ColorScheme::Dark, "Some Dark Theme");

    EXPECT_EQ(parameters->GetASCII("ThemeLight", ""), "Some Light Theme");
    EXPECT_EQ(parameters->GetASCII("ThemeDark", ""), "Some Dark Theme");

    EXPECT_EQ(manager().theme(ColorScheme::Light), "Some Light Theme");
    EXPECT_EQ(manager().theme(ColorScheme::Dark), "Some Dark Theme");
}

TEST_F(ThemeManagerTest, TheModeSelectsTheEffectiveTheme)
{
    manager().setTheme(ColorScheme::Light, "Some Light Theme");
    manager().setTheme(ColorScheme::Dark, "Some Dark Theme");

    manager().setMode(ThemeMode::Light);
    EXPECT_EQ(manager().colorScheme(), ColorScheme::Light);
    EXPECT_EQ(manager().effectiveTheme(), "Some Light Theme");

    manager().setMode(ThemeMode::Dark);
    EXPECT_EQ(manager().colorScheme(), ColorScheme::Dark);
    EXPECT_EQ(manager().effectiveTheme(), "Some Dark Theme");
}

TEST_F(ThemeManagerTest, ThemeOfAColorSchemeFallsBackToTheActiveTheme)
{
    parameters->SetASCII("Theme", "Some Dark Theme");

    EXPECT_EQ(manager().theme(ColorScheme::Dark), "Some Dark Theme");
}

TEST_F(ThemeManagerTest, ThemesWithoutMetadataAreClassifiedByTheirName)
{
    EXPECT_EQ(ThemeManager::colorSchemeOf("FreeCAD Dark"), ColorScheme::Dark);
    EXPECT_EQ(ThemeManager::colorSchemeOf("FreeCAD Light"), ColorScheme::Light);
    EXPECT_EQ(ThemeManager::colorSchemeOf("FreeCAD Classic"), ColorScheme::Light);
    EXPECT_EQ(ThemeManager::colorSchemeOf(""), ColorScheme::Light);
}
