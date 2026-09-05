// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QAction>
#include <QObject>
#include <QTest>

#include <Gui/Action.h>
#include <Gui/Command.h>

namespace
{

/// The smallest command an action can belong to. Gui::Action reads its name.
class TestCommand: public Gui::Command
{
public:
    TestCommand()
        : Gui::Command("Test_ActionGroup")
    {}

protected:
    void activated(int) override
    {}
    void languageChange() override
    {}
    void updateAction(int) override
    {}
    const char* className() const override
    {
        return "TestCommand";
    }
};

}  // namespace

/** Checking an action of a group by index.
 *
 * The index comes from saved settings or from a command whose actions may not have
 * been built yet, so it routinely names an action that does not exist. That must
 * leave the group alone rather than take the application down.
 */
class ActionGroupTest: public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void checksTheActionAtTheIndex()
    {
        TestCommand command;
        Gui::ActionGroup group(&command);
        QAction* first = group.addAction(QStringLiteral("first"));
        QAction* second = group.addAction(QStringLiteral("second"));
        first->setCheckable(true);
        second->setCheckable(true);

        group.setCheckedAction(1);

        QVERIFY(!first->isChecked());
        QVERIFY(second->isChecked());
        QCOMPARE(group.checkedAction(), 1);
        QCOMPARE(group.property("defaultAction").toInt(), 1);
    }

    void ignoresAnIndexPastTheEnd()
    {
        TestCommand command;
        Gui::ActionGroup group(&command);
        QAction* only = group.addAction(QStringLiteral("only"));
        only->setCheckable(true);
        group.setCheckedAction(0);

        group.setCheckedAction(7);

        QVERIFY(only->isChecked());
        QCOMPARE(group.property("defaultAction").toInt(), 0);
    }

    void ignoresANegativeIndex()
    {
        TestCommand command;
        Gui::ActionGroup group(&command);
        group.addAction(QStringLiteral("only"))->setCheckable(true);

        group.setCheckedAction(-1);

        QCOMPARE(group.checkedAction(), -1);
    }

    void ignoresAnIndexOnAnEmptyGroup()
    {
        TestCommand command;
        Gui::ActionGroup group(&command);

        group.setCheckedAction(0);

        QVERIFY(group.actions().isEmpty());
    }
};

QTEST_MAIN(ActionGroupTest)

#include "ActionGroupTest.moc"
