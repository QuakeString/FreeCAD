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

#pragma once

#include <string>
#include <vector>

#include <QObject>

#include <FCGlobal.h>
#include <Base/Parameter.h>

namespace Gui
{

/// Appearance of the user interface.
enum class ColorScheme
{
    Light,
    Dark
};

/// Determines how the active theme is picked.
enum class ThemeMode
{
    /// Always use the theme configured for the light color scheme
    Light,
    /// Always use the theme configured for the dark color scheme
    Dark,
    /// Follow the color scheme of the operating system
    System
};

/**
 * The ThemeManager stores which theme the user has picked for the light and for the dark
 * color scheme and applies the one that matches the currently selected mode.
 *
 * In ThemeMode::System the color scheme reported by the operating system decides which of
 * the two themes is used, and the manager keeps watching it so that the theme is switched
 * on the fly whenever the system changes between light and dark.
 *
 * The selection is stored in "BaseApp/Preferences/MainWindow" using the keys "ThemeMode",
 * "ThemeLight" and "ThemeDark". The theme that is actually applied is - as before - kept
 * in the "Theme" key.
 */
class GuiExport ThemeManager: public QObject
{
    Q_OBJECT

public:
    static ThemeManager* instance();
    static void destruct();

    /** Migrates a pre-existing theme selection, applies the theme matching the current mode
     * and starts watching the color scheme of the operating system.
     * Subsequent calls do nothing.
     */
    void init();

    ThemeMode mode() const;
    void setMode(ThemeMode mode);

    /// Name of the theme configured for the given color scheme
    std::string theme(ColorScheme scheme) const;
    void setTheme(ColorScheme scheme, const std::string& themeName);

    /// Color scheme currently reported by the operating system
    static ColorScheme systemColorScheme();

    /// Color scheme resulting from the current mode
    ColorScheme colorScheme() const;

    /// Name of the theme that matches the current mode
    std::string effectiveTheme() const;

    /// Name of the theme that is currently applied
    static std::string activeTheme();

    /// True once the user has picked a theme mode
    static bool isConfigured();

    /** Applies the theme matching the current mode.
     * @param force apply the theme even if it is the one already in use
     * @return true if a theme has been applied
     */
    bool applyEffectiveTheme(bool force = false);

    /// Applies the given theme preference pack. Returns true if it has been applied.
    bool applyTheme(const std::string& themeName);

    /** Makes @a themeName the theme of the color scheme it belongs to, switches to the
     * matching fixed mode and applies it.
     */
    void selectTheme(const std::string& themeName);

    /// Names of all installed theme preference packs
    static std::vector<std::string> availableThemes();

    /// Color scheme a theme preference pack belongs to, deduced from its metadata
    static ColorScheme colorSchemeOf(const std::string& themeName);

    static const char* toString(ThemeMode mode);
    static ThemeMode modeFromString(const std::string& mode);

Q_SIGNALS:
    /// Emitted after a different theme has been applied
    void themeChanged(const QString& themeName);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    ThemeManager();
    ~ThemeManager() override;

    void onSystemColorSchemeChanged();
    void migrate();

    static ParameterGrp::handle parameters();

    static ThemeManager* _instance;

    bool _initialized {false};
};

}  // namespace Gui
