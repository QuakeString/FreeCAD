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

#include "ThemeManager.h"

#include <algorithm>
#include <cctype>

#include <QEvent>

#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

#include <App/Application.h>
#include <Base/Console.h>

#include <FCConfig.h>

#include "Application.h"
#include "PreferencePackManager.h"
#include "Utilities.h"

#ifdef FC_OS_MACOSX
# include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef FC_USE_XDG_PORTAL
# include <optional>

# include <QDBusConnection>
# include <QDBusMessage>
# include <QDBusVariant>
# include <QVariant>
#endif

using namespace Gui;

namespace
{
constexpr const char* MainWindowGroup = "User parameter:BaseApp/Preferences/MainWindow";
constexpr const char* ModeKey = "ThemeMode";
constexpr const char* LightThemeKey = "ThemeLight";
constexpr const char* DarkThemeKey = "ThemeDark";
constexpr const char* ThemeKey = "Theme";

constexpr const char* DefaultLightTheme = "FreeCAD Light";
constexpr const char* DefaultDarkTheme = "FreeCAD Dark";

constexpr const char* ThemePackType = "Theme";

std::string toLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char chr) {
        return static_cast<char>(std::tolower(chr));
    });

    return text;
}

/// True if one of the tags contains @a word as a whole word, e.g. "dark" in "dark theme"
bool hasTag(const std::vector<std::string>& tags, const std::string& word)
{
    const auto isWord = [&word](const std::string& tag) {
        std::size_t pos = tag.find(word);

        while (pos != std::string::npos) {
            const bool startsWord = pos == 0
                || std::isalpha(static_cast<unsigned char>(tag[pos - 1])) == 0;
            const std::size_t end = pos + word.size();
            const bool endsWord = end == tag.size()
                || std::isalpha(static_cast<unsigned char>(tag[end])) == 0;

            if (startsWord && endsWord) {
                return true;
            }

            pos = tag.find(word, pos + 1);
        }

        return false;
    };

    return std::any_of(tags.begin(), tags.end(), isWord);
}

#ifdef FC_USE_XDG_PORTAL
constexpr const char* PortalService = "org.freedesktop.portal.Desktop";
constexpr const char* PortalPath = "/org/freedesktop/portal/desktop";
constexpr const char* PortalSettings = "org.freedesktop.portal.Settings";
constexpr const char* AppearanceNamespace = "org.freedesktop.appearance";
constexpr const char* ColorSchemeKey = "color-scheme";

// What the portal answers with. 0 stands for "no preference" and is left to the caller.
constexpr uint PreferDark = 1;
constexpr uint PreferLight = 2;

/// The portal hands the value over wrapped in one or two variants
QVariant unwrapVariant(QVariant value)
{
    while (value.canConvert<QDBusVariant>()) {
        value = value.value<QDBusVariant>().variant();
    }

    return value;
}

/// Asks the XDG desktop portal which color scheme the desktop prefers
std::optional<ColorScheme> readPortalColorScheme()
{
    auto bus = QDBusConnection::sessionBus();

    if (!bus.isConnected()) {
        return std::nullopt;
    }

    // Built by hand rather than through QDBusInterface, which would introspect the portal on
    // construction and so block startup for a round trip that is of no use here.
    const auto callPortal = [&bus](const char* method) {
        QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1(PortalService),
                                                          QString::fromLatin1(PortalPath),
                                                          QString::fromLatin1(PortalSettings),
                                                          QString::fromLatin1(method));
        call.setArguments({QString::fromLatin1(AppearanceNamespace),
                           QString::fromLatin1(ColorSchemeKey)});

        // A desktop that never answers must not hold up the whole application
        constexpr int timeoutMs = 1000;

        return bus.call(call, QDBus::Block, timeoutMs);
    };

    // ReadOne is the current call. Portals that predate it only know Read, which answers with
    // the value wrapped in one more variant.
    QDBusMessage reply = callPortal("ReadOne");

    if (reply.type() != QDBusMessage::ReplyMessage) {
        reply = callPortal("Read");
    }

    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return std::nullopt;
    }

    bool isNumber = false;
    const uint preference = unwrapVariant(reply.arguments().constFirst()).toUInt(&isNumber);

    if (!isNumber) {
        return std::nullopt;
    }

    if (preference == PreferDark) {
        return ColorScheme::Dark;
    }
    if (preference == PreferLight) {
        return ColorScheme::Light;
    }

    return std::nullopt;  // no preference, so let the palette decide
}

// The answer only changes when the portal says so, and asking blocks on a round trip, so it is
// kept until the SettingChanged signal arrives.
std::optional<ColorScheme> portalScheme;
bool portalSchemeKnown = false;

std::optional<ColorScheme> portalColorScheme()
{
    if (!portalSchemeKnown) {
        portalScheme = readPortalColorScheme();
        portalSchemeKnown = true;
    }

    return portalScheme;
}

void forgetPortalColorScheme()
{
    portalSchemeKnown = false;
}
#endif  // FC_USE_XDG_PORTAL
}  // namespace

ThemeManager* ThemeManager::_instance = nullptr;

ThemeManager::ThemeManager() = default;

ThemeManager::~ThemeManager() = default;

ThemeManager* ThemeManager::instance()
{
    if (!_instance) {
        _instance = new ThemeManager();
    }

    return _instance;
}

void ThemeManager::destruct()
{
    delete _instance;
    _instance = nullptr;
}

ParameterGrp::handle ThemeManager::parameters()
{
    return App::GetApplication().GetParameterGroupByPath(MainWindowGroup);
}

const char* ThemeManager::toString(ThemeMode mode)
{
    switch (mode) {
        case ThemeMode::Dark:
            return "Dark";
        case ThemeMode::System:
            return "System";
        case ThemeMode::Light:
        default:
            return "Light";
    }
}

ThemeMode ThemeManager::modeFromString(const std::string& mode)
{
    const std::string value = toLower(mode);

    if (value == "dark") {
        return ThemeMode::Dark;
    }
    if (value == "system") {
        return ThemeMode::System;
    }

    return ThemeMode::Light;
}

bool ThemeManager::isConfigured()
{
    return !parameters()->GetASCII(ModeKey, "").empty();
}

ThemeMode ThemeManager::mode() const
{
    const std::string stored = parameters()->GetASCII(ModeKey, "");

    if (stored.empty()) {
        // Nothing configured yet: stick to whatever theme is in use
        const ColorScheme scheme = colorSchemeOf(activeTheme());

        return scheme == ColorScheme::Dark ? ThemeMode::Dark : ThemeMode::Light;
    }

    return modeFromString(stored);
}

void ThemeManager::setMode(ThemeMode mode)
{
    parameters()->SetASCII(ModeKey, toString(mode));
}

std::string ThemeManager::theme(ColorScheme scheme) const
{
    const bool dark = scheme == ColorScheme::Dark;
    const std::string stored = parameters()->GetASCII(dark ? DarkThemeKey : LightThemeKey, "");

    if (!stored.empty()) {
        return stored;
    }

    // Nothing configured for this color scheme: fall back to the active theme if it matches,
    // otherwise to the built-in theme of that color scheme.
    const std::string active = activeTheme();
    if (!active.empty() && colorSchemeOf(active) == scheme) {
        return active;
    }

    const std::string fallback = dark ? DefaultDarkTheme : DefaultLightTheme;
    const std::vector<std::string> themes = availableThemes();

    if (std::find(themes.begin(), themes.end(), fallback) != themes.end()) {
        return fallback;
    }

    // The built-in theme is not installed, so use the first one of the matching color scheme
    for (const std::string& name : themes) {
        if (colorSchemeOf(name) == scheme) {
            return name;
        }
    }

    return active;
}

void ThemeManager::setTheme(ColorScheme scheme, const std::string& themeName)
{
    parameters()->SetASCII(scheme == ColorScheme::Dark ? DarkThemeKey : LightThemeKey, themeName);
}

ColorScheme ThemeManager::colorSchemeFromPalette(const QPalette& palette)
{
    // A dark scheme paints light text onto a dark window, a light one does the opposite
    const auto text = palette.color(QPalette::WindowText);
    const auto window = palette.color(QPalette::Window);

    return text.lightness() > window.lightness() ? ColorScheme::Dark : ColorScheme::Light;
}

ColorScheme ThemeManager::systemColorScheme()
{
    if (!qApp) {
        return ColorScheme::Light;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // https://www.qt.io/blog/dark-mode-on-windows-11-with-qt-6.5
    const auto scheme = QGuiApplication::styleHints()->colorScheme();

    if (scheme == Qt::ColorScheme::Dark) {
        return ColorScheme::Dark;
    }
    if (scheme == Qt::ColorScheme::Light) {
        return ColorScheme::Light;
    }
    // Qt reports Qt::ColorScheme::Unknown when no platform theme supplies a scheme, which is
    // what a Qt built without the GTK platform theme plugin does on GNOME. Fall through and
    // find the scheme elsewhere rather than call that light.
#endif

#ifdef FC_USE_XDG_PORTAL
    // Every desktop that implements the appearance portal answers this, whichever platform
    // theme Qt happens to have loaded.
    if (const auto preferred = portalColorScheme()) {
        return *preferred;
    }
#endif

#ifdef FC_OS_MACOSX
    auto key = CFSTR("AppleInterfaceStyle");
    if (auto value = CFPreferencesCopyAppValue(key, kCFPreferencesAnyApplication)) {
        bool dark = false;
        if (CFGetTypeID(value) == CFStringGetTypeID()) {
            dark = CFStringCompare((CFStringRef)value, CFSTR("Dark"), kCFCompareCaseInsensitive)
                == kCFCompareEqualTo;
        }
        CFRelease(value);
        if (dark) {
            return ColorScheme::Dark;
        }
    }
#endif  // FC_OS_MACOSX

    // Deduce the color scheme from the palette the platform theme provides
    return colorSchemeFromPalette(QPalette());
}

ColorScheme ThemeManager::colorScheme() const
{
    switch (mode()) {
        case ThemeMode::Dark:
            return ColorScheme::Dark;
        case ThemeMode::Light:
            return ColorScheme::Light;
        case ThemeMode::System:
        default:
            return systemColorScheme();
    }
}

std::string ThemeManager::activeTheme()
{
    return parameters()->GetASCII(ThemeKey, "");
}

std::string ThemeManager::effectiveTheme() const
{
    return theme(colorScheme());
}

std::vector<std::string> ThemeManager::availableThemes()
{
    std::vector<std::string> themes;

    if (!Application::Instance) {
        return themes;
    }

    for (const auto& pack : Application::Instance->prefPackManager()->preferencePacks()) {
        if (pack.second.metadata().type() == ThemePackType) {
            themes.push_back(pack.first);
        }
    }

    return themes;
}

ColorScheme ThemeManager::colorSchemeOf(const std::string& themeName)
{
    if (themeName.empty()) {
        return ColorScheme::Light;
    }

    if (Application::Instance) {
        const auto packs = Application::Instance->prefPackManager()->preferencePacks();
        const auto pack = packs.find(themeName);

        if (pack != packs.end()) {
            std::vector<std::string> tags;
            for (const auto& tag : pack->second.metadata().tag()) {
                tags.push_back(toLower(tag));
            }

            if (hasTag(tags, "dark")) {
                return ColorScheme::Dark;
            }
            if (hasTag(tags, "light")) {
                return ColorScheme::Light;
            }
        }
    }

    // No usable metadata, so fall back to the name of the theme
    return toLower(themeName).find("dark") != std::string::npos ? ColorScheme::Dark
                                                                : ColorScheme::Light;
}

bool ThemeManager::applyTheme(const std::string& themeName)
{
    if (themeName.empty() || !Application::Instance) {
        return false;
    }

    auto* packManager = Application::Instance->prefPackManager();
    packManager->rescan();

    if (!packManager->apply(themeName)) {
        Base::Console().warning("Could not apply the theme '%s'\n", themeName.c_str());

        return false;
    }

    // A theme pack sets the "Theme" key itself, but third-party ones may not, so make sure
    // that the applied theme is always recorded.
    parameters()->SetASCII(ThemeKey, themeName);

    Q_EMIT themeChanged(QString::fromStdString(themeName));

    return true;
}

bool ThemeManager::applyEffectiveTheme(bool force)
{
    const std::string wanted = effectiveTheme();

    if (wanted.empty() || (!force && wanted == activeTheme())) {
        return false;
    }

    return applyTheme(wanted);
}

void ThemeManager::selectTheme(const std::string& themeName)
{
    if (themeName.empty()) {
        return;
    }

    const ColorScheme scheme = colorSchemeOf(themeName);

    setTheme(scheme, themeName);
    setMode(scheme == ColorScheme::Dark ? ThemeMode::Dark : ThemeMode::Light);

    applyEffectiveTheme(true);
}

void ThemeManager::migrate()
{
    auto hGrp = parameters();

    if (!hGrp->GetASCII(ModeKey, "").empty()) {
        return;  // the mode has already been chosen
    }

    const std::string active = activeTheme();

    if (active.empty()) {
        return;  // no theme has been picked yet, leave the choice to the user
    }

    const ColorScheme scheme = colorSchemeOf(active);

    if (hGrp->GetASCII(LightThemeKey, "").empty()) {
        setTheme(ColorScheme::Light, scheme == ColorScheme::Light ? active : DefaultLightTheme);
    }
    if (hGrp->GetASCII(DarkThemeKey, "").empty()) {
        setTheme(ColorScheme::Dark, scheme == ColorScheme::Dark ? active : DefaultDarkTheme);
    }

    // Keep using the theme that is in use until the user opts into following the system
    setMode(scheme == ColorScheme::Dark ? ThemeMode::Dark : ThemeMode::Light);
}

bool ThemeManager::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qApp
        && (event->type() == QEvent::ApplicationPaletteChange
            || event->type() == QEvent::ThemeChange)) {
        onSystemColorSchemeChanged();
    }

    return QObject::eventFilter(watched, event);
}

void ThemeManager::onPortalSettingChanged(const QString& nameSpace, const QString& key)
{
#ifdef FC_USE_XDG_PORTAL
    if (nameSpace != QString::fromLatin1(AppearanceNamespace)
        || key != QString::fromLatin1(ColorSchemeKey)) {
        return;
    }

    forgetPortalColorScheme();
    onSystemColorSchemeChanged();
#else
    Q_UNUSED(nameSpace)
    Q_UNUSED(key)
#endif
}

void ThemeManager::onSystemColorSchemeChanged()
{
    if (mode() != ThemeMode::System) {
        return;
    }

    applyEffectiveTheme();
}

void ThemeManager::init()
{
    if (_initialized) {
        return;
    }

    _initialized = true;

    if (isInternalGuiTestRun()) {
        return;
    }

    migrate();

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(
        QGuiApplication::styleHints(),
        &QStyleHints::colorSchemeChanged,
        this,
        [this](Qt::ColorScheme) { onSystemColorSchemeChanged(); }
    );
#else
    // Without QStyleHints::colorSchemeChanged the only hint that the system switched between
    // light and dark is the palette the platform theme hands over to the application.
    if (qApp) {
        qApp->installEventFilter(this);
    }
#endif

#ifdef FC_USE_XDG_PORTAL
    // QStyleHints::colorSchemeChanged stays silent when Qt never learned the scheme to begin
    // with, so follow the portal directly as well. Its signal carries a third argument that
    // the slot leaves out, because the value is read back anyway.
    if (auto bus = QDBusConnection::sessionBus(); bus.isConnected()) {
        const bool subscribed = bus.connect(QString::fromLatin1(PortalService),
                                            QString::fromLatin1(PortalPath),
                                            QString::fromLatin1(PortalSettings),
                                            QStringLiteral("SettingChanged"),
                                            this,
                                            SLOT(onPortalSettingChanged(QString, QString)));

        if (!subscribed) {
            Base::Console().log("Theme: not following the color scheme of the desktop portal\n");
        }
    }
#endif

    if (isConfigured()) {
        applyEffectiveTheme();
    }
}

#include "moc_ThemeManager.cpp"
