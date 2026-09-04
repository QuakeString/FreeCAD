// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QTest>

#include <App/Application.h>
#include <Base/Interpreter.h>
#include <Gui/Application.h>
#include <Gui/ApplicationPy.h>
#include <Gui/Workbench.h>
#include <Gui/WorkbenchManager.h>
#include <src/App/InitApplication.h>

/** Loading a workbench without activating it.
 *
 * The workbenches a user asks to have ready at startup are loaded after the window is up.
 * Loading has to run Initialize(), which is what imports the workbench's modules and creates
 * its commands, and it has to leave everything visible alone: no Activated hook, no change of
 * active workbench. A spy workbench written in Python records which hooks run.
 */
class WorkbenchLoadingTest: public QObject
{
    Q_OBJECT

private:
    static void run(const char* code)
    {
        try {
            Base::Interpreter().runString(code);
        }
        catch (const Base::Exception& e) {
            QFAIL(qPrintable(QStringLiteral("Python failed: %1").arg(QString::fromUtf8(e.what()))));
        }
    }

    /// Reads back one attribute of the spy as an integer
    static int spyCount(const char* attribute)
    {
        Base::PyGILStateLocker lock;
        const std::string code = std::string("import FreeCADGui\n"
                                             "__spy_value = FreeCADGui.__spy_wb.")
            + attribute;
        Base::Interpreter().runString(code.c_str());
        PyObject* module = PyImport_AddModule("__main__");
        PyObject* value = PyDict_GetItemString(PyModule_GetDict(module), "__spy_value");
        return value ? static_cast<int>(PyLong_AsLong(value)) : -1;
    }

private Q_SLOTS:

    void initTestCase()
    {
        tests::initApplication();
        // The workbench classes are registered by the GUI's type initialization, without
        // which "Gui::PythonWorkbench" is not a known type and no workbench can be created
        Gui::Application::initTypes();
        if (!Gui::Application::Instance) {
            new Gui::Application(false);
        }

        // A workbench that does nothing but count the hooks the application calls on it
        // The FreeCADGui module and its addWorkbench are only set up by a GUI-enabled
        // application, which needs a QApplication these tests do not have. The spy is a plain
        // object with the methods the application looks for, kept on the FreeCADGui module so
        // that the tests can read its counters back, and registered through the same C++
        // entry point addWorkbench reaches.
        // addWorkbench looks for a Workbench base class in __main__, which FreeCADGuiInit.py
        // defines on a real start; a bare one stands in for it here.
        run("import FreeCADGui\n"
            "class Workbench:\n"
            "    pass\n"
            "class SpyWorkbench(Workbench):\n"
            "    MenuText = 'Spy'\n"
            "    ToolTip = 'Counts hooks'\n"
            "    def __init__(self):\n"
            "        self.initialized = 0\n"
            "        self.activated = 0\n"
            "        self.deactivated = 0\n"
            "    def Initialize(self):\n"
            "        self.initialized += 1\n"
            "    def Activated(self):\n"
            "        self.activated += 1\n"
            "    def Deactivated(self):\n"
            "        self.deactivated += 1\n"
            "    def GetClassName(self):\n"
            "        return 'Gui::PythonWorkbench'\n"
            "FreeCADGui.__spy_wb = SpyWorkbench()\n");

        Base::PyGILStateLocker lock;
        PyObject* gui = PyImport_AddModule("FreeCADGui");
        PyObject* spy = PyObject_GetAttrString(gui, "__spy_wb");
        QVERIFY(spy != nullptr);
        PyObject* args = Py_BuildValue("(O)", spy);
        PyObject* result = Gui::ApplicationPy::sAddWorkbench(nullptr, args);
        Py_DECREF(args);
        Py_DECREF(spy);
        QVERIFY2(result != nullptr, "addWorkbench rejected the spy");
        Py_DECREF(result);
    }

    void testLoadingRunsInitializeButNotActivated()
    {
        QVERIFY(Gui::Application::Instance->loadWorkbench("SpyWorkbench"));

        QCOMPARE(spyCount("initialized"), 1);
        QCOMPARE(spyCount("activated"), 0);
        QCOMPARE(spyCount("deactivated"), 0);
    }

    void testLoadingLeavesTheActiveWorkbenchAlone()
    {
        Gui::Workbench* before = Gui::WorkbenchManager::instance()->active();
        QVERIFY(Gui::Application::Instance->loadWorkbench("SpyWorkbench"));
        QCOMPARE(Gui::WorkbenchManager::instance()->active(), before);
    }

    void testLoadingTwiceInitializesOnce()
    {
        // Initialize() imports modules and creates commands; running it again would duplicate
        // the commands and re-import everything.
        QVERIFY(Gui::Application::Instance->loadWorkbench("SpyWorkbench"));
        QVERIFY(Gui::Application::Instance->loadWorkbench("SpyWorkbench"));
        QCOMPARE(spyCount("initialized"), 1);
    }

    void testLoadingAnUnknownWorkbenchFailsQuietly()
    {
        // A name from a removed addon must not stop the rest of the list from loading
        QVERIFY(!Gui::Application::Instance->loadWorkbench("NoSuchWorkbench"));
    }

    // Activating a loaded workbench afterwards is not covered here: activation builds the
    // toolbars through the main window, which these tests do not have. That loading is a
    // prefix of activation is guaranteed by loadWorkbench running the same steps
    // activateWorkbench does before it activates, and by nothing else.
};

QTEST_MAIN(WorkbenchLoadingTest)
#include "WorkbenchLoading.moc"
