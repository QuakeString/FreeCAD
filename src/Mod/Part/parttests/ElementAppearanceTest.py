# SPDX-License-Identifier: LGPL-2.1-or-later

"""Per-element appearance overrides on a Part::Feature view provider.

A painted face must stay painted through recomputes that add, remove or renumber
faces, and what the scene graph draws must agree with the properties.
"""

import os
import re
import tempfile
import unittest
import zipfile

import FreeCAD as App
import FreeCADGui as Gui
import Part
from pivy import coin

RED = (1.0, 0.0, 0.0, 1.0)
BLUE = (0.0, 0.0, 1.0, 1.0)


def material(rgba):
    m = App.Material()
    m.DiffuseColor = rgba
    return m


def rgb(color):
    return tuple(round(c, 3) for c in color[:3])


def face_index(shape, predicate):
    """1-based index of the single face whose centre of mass satisfies predicate."""
    found = [i + 1 for i, f in enumerate(shape.Faces) if predicate(f.CenterOfMass)]
    assert len(found) == 1, found
    return found[0]


def on_top(z):
    return lambda c: abs(c.z - z) < 1e-6


def in_hole(c):
    return abs(c.x - 0.5) < 1e-6 and abs(c.y - 1.0) < 1e-6 and abs(c.z - 1.5) < 1e-6


def face_rendering(vo):
    """(binding, colour count) of the material the faces are drawn with."""
    search = coin.SoSearchAction()
    search.setName("FlatRoot")
    search.setInterest(coin.SoSearchAction.FIRST)
    search.apply(vo.RootNode)
    flat = search.getPath().getTail()
    names = {
        coin.SoMaterialBinding.OVERALL: "OVERALL",
        coin.SoMaterialBinding.PER_PART: "PER_PART",
        coin.SoMaterialBinding.PER_FACE: "PER_FACE",
    }
    binding = None
    count = None
    for i in range(flat.getNumChildren()):
        child = flat.getChild(i)
        if child.isOfType(coin.SoMaterialBinding.getClassTypeId()):
            binding = names.get(child.value.getValue(), "?")
    search = coin.SoSearchAction()
    search.setType(coin.SoMaterial.getClassTypeId())
    search.setInterest(coin.SoSearchAction.FIRST)
    search.apply(flat)
    if search.getPath():
        count = search.getPath().getTail().diffuseColor.getNum()
    return binding, count


class ElementAppearanceGuiTest(unittest.TestCase):
    """A 1 x 2 x 3 box drilled top to bottom by a cylinder: 7 mapped faces."""

    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.doc = App.newDocument("ElementAppearance")
        self.box = self.doc.addObject("Part::Box", "Box")
        self.box.Length, self.box.Width, self.box.Height = 1, 2, 3
        self.cylinder = self.doc.addObject("Part::Cylinder", "Cylinder")
        self.cylinder.Radius, self.cylinder.Height = 0.25, 5
        self.cylinder.Placement.Base = App.Vector(0.5, 1.0, -1.0)
        self.cut = self.doc.addObject("Part::Cut", "Cut")
        self.cut.Base, self.cut.Tool = self.box, self.cylinder
        self.doc.recompute()
        self.vo = self.cut.ViewObject
        self.base = rgb(self.vo.ShapeAppearance[0].DiffuseColor)
        self.assertEqual(len(self.cut.Shape.Faces), 7)

    def tearDown(self):
        for doc in list(App.listDocuments().values()):
            App.closeDocument(doc.Name)

    # -- helpers --------------------------------------------------------------------

    def top(self, z=3.0):
        return face_index(self.cut.Shape, on_top(z))

    def stored(self):
        """Element names in the store; an empty link reads back as None."""
        value = self.cut.ColoredElements
        return [] if value is None else value[1]

    def paint(self, index, rgba=RED):
        self.vo.setElementColors({"Face%d" % index: rgba})

    def assertDrawn(self, index, rgba=RED, faces=None):
        """The face at index has the colour in the property and the faces are drawn per part."""
        faces = faces or len(self.cut.Shape.Faces)
        appearance = self.vo.ShapeAppearance
        self.assertEqual(len(appearance), faces)
        self.assertEqual(rgb(appearance[index - 1].DiffuseColor), rgb(rgba))
        others = {rgb(appearance[i].DiffuseColor) for i in range(faces) if i != index - 1}
        self.assertEqual(others, {rgb(self.vo.BaseAppearance.DiffuseColor)})
        self.assertEqual(face_rendering(self.vo), ("PER_PART", faces))

    def assertUniform(self):
        self.assertEqual(len(self.vo.ShapeAppearance), 1)
        self.assertEqual(face_rendering(self.vo), ("OVERALL", 1))

    # -- tests ----------------------------------------------------------------------

    def testPaintingAFaceStoresAnElementReference(self):
        top = self.top()
        self.paint(top)

        self.assertEqual(self.cut.ColoredElements, (self.cut, ["Face%d" % top]))
        self.assertEqual(len(self.vo.MappedAppearance), 1)
        self.assertEqual(rgb(self.vo.MappedAppearance[0].DiffuseColor), rgb(RED))
        self.assertEqual(rgb(self.vo.BaseAppearance.DiffuseColor), self.base)
        self.assertDrawn(top)
        self.assertEqual(rgb(self.vo.getElementColors("Face%d" % top)["Face%d" % top]), rgb(RED))
        self.assertNotIn("Touched", self.cut.State, "an appearance change must not need a recompute")

    def testWholeObjectQueryListsBaseAndOverrides(self):
        top = self.top()
        self.paint(top)

        colors = self.vo.getElementColors()

        self.assertEqual(set(colors), {"Face", "Edge", "Vertex", "Face%d" % top})
        self.assertEqual(rgb(colors["Face"]), self.base)
        self.assertEqual(rgb(colors["Face%d" % top]), rgb(RED))

    def testColourFollowsTheFaceThroughRecomputes(self):
        self.paint(self.top())

        # A dimension change keeps the face's name.
        self.box.Height = 3.5
        self.doc.recompute()
        self.assertDrawn(self.top(3.5))

        # A drill that no longer reaches the top face changes the face's name;
        # the colour follows the face through its origin.
        self.cylinder.Height = 2
        self.doc.recompute()
        self.assertDrawn(self.top(3.5))
        self.assertEqual(self.stored(), ["Face%d" % self.top(3.5)])

    def testColourReturnsWhenTheFaceComesBack(self):
        self.paint(face_index(self.cut.Shape, in_hole))

        self.cylinder.Placement.Base = App.Vector(5, 5, -1)
        self.doc.recompute()
        self.assertEqual(len(self.cut.Shape.Faces), 6)
        self.assertUniform()
        self.assertEqual(len(self.stored()), 1, "the reference is kept")

        self.cylinder.Placement.Base = App.Vector(0.5, 1.0, -1.0)
        self.doc.recompute()
        self.assertDrawn(face_index(self.cut.Shape, in_hole))

    def testBaseChangeKeepsOverrides(self):
        top = self.top()
        self.paint(top)

        self.vo.ShapeAppearance = [material(BLUE)]

        self.assertEqual(rgb(self.vo.BaseAppearance.DiffuseColor), rgb(BLUE))
        self.assertDrawn(top)
        self.assertEqual(self.stored(), ["Face%d" % top])

    def testTransparencyAppliesToEveryFace(self):
        top = self.top()
        self.paint(top)

        self.vo.Transparency = 50

        self.assertDrawn(top)
        self.assertEqual({round(m.Transparency, 2) for m in self.vo.ShapeAppearance}, {0.5})
        self.assertEqual(round(self.vo.MappedAppearance[0].Transparency, 2), 0.5)
        self.assertEqual(round(self.vo.BaseAppearance.Transparency, 2), 0.5)

    def testPositionalWriteIsAdopted(self):
        top = self.top()
        materials = [material(self.base + (1.0,)) for _ in self.cut.Shape.Faces]
        materials[top - 1] = material(RED)

        self.vo.ShapeAppearance = materials

        self.assertEqual(self.cut.ColoredElements, (self.cut, ["Face%d" % top]))
        self.assertEqual(rgb(self.vo.BaseAppearance.DiffuseColor), self.base)
        self.assertDrawn(top)
        self.box.Height = 3.5
        self.doc.recompute()
        self.assertDrawn(self.top(3.5))

    def testLegacyDiffuseColorWriteIsAdopted(self):
        top = self.top()
        colors = [self.base + (1.0,)] * len(self.cut.Shape.Faces)
        colors[top - 1] = RED

        self.vo.DiffuseColor = colors

        self.assertEqual(self.cut.ColoredElements, (self.cut, ["Face%d" % top]))
        self.assertDrawn(top)

    def testUniformListKeepsItsLength(self):
        # A boolean writes one material per face when its inputs share a colour.
        # Nothing is overridden, so the list must survive untouched.
        uniform = [material(BLUE) for _ in self.cut.Shape.Faces]

        self.vo.ShapeAppearance = uniform

        self.assertEqual(len(self.vo.ShapeAppearance), len(self.cut.Shape.Faces))
        self.assertEqual(self.stored(), [])
        self.assertEqual(rgb(self.vo.BaseAppearance.DiffuseColor), rgb(BLUE))
        self.assertEqual(face_rendering(self.vo), ("PER_PART", len(self.cut.Shape.Faces)))

    def testUniformListClearsExistingOverrides(self):
        self.paint(self.top())

        self.vo.ShapeAppearance = [material(BLUE) for _ in self.cut.Shape.Faces]

        self.assertEqual(self.stored(), [], "a uniform repaint drops the overrides")
        self.assertEqual(rgb(self.vo.BaseAppearance.DiffuseColor), rgb(BLUE))
        self.assertUniform()

    def testStaleListIsLeftAlone(self):
        # Fewer entries than faces says nothing about which face is which.
        self.vo.ShapeAppearance = [material(RED), material(BLUE)]

        self.assertEqual(self.stored(), [])
        self.assertEqual(len(self.vo.ShapeAppearance), 2)
        self.assertEqual(rgb(self.vo.BaseAppearance.DiffuseColor), self.base, "base untouched")

    def testShortMaterialListIsNotPairedPositionally(self):
        # The element names and their materials are saved separately, so a file can
        # arrive with the names alone. Painting them with a default material would
        # put a wrong colour on a real face.
        top = self.top()
        self.paint(top)
        self.cut.ColoredElements = (self.cut, ["Face%d" % top, "Face1"])

        self.assertEqual(self.vo.getElementColors(), self.vo.getElementColors())
        self.assertNotIn("Face1", self.vo.getElementColors())
        self.assertEqual(len(self.vo.ShapeAppearance), 1)
        self.assertUniform()

    def testEdgeAndVertexKeysSetLineAndPointColour(self):
        self.vo.setElementColors({"Edge": (0.0, 1.0, 0.0, 1.0), "Vertex": BLUE})

        self.assertEqual(rgb(self.vo.LineColor), (0.0, 1.0, 0.0))
        self.assertEqual(rgb(self.vo.PointColor), rgb(BLUE))
        self.assertEqual(self.stored(), [])

    def testAnUnpaintedObjectStoresNothing(self):
        # An empty store must stay empty: a material list that holds one default
        # material is written into every saved file for no reason.
        self.assertEqual(self.stored(), [])
        self.assertEqual(len(self.vo.MappedAppearance), 0)

        self._save_and_reopen()

        self.assertEqual(self.stored(), [])
        self.assertEqual(len(self.vo.MappedAppearance), 0)

    def testClearingOverrides(self):
        self.paint(self.top())

        self.vo.setElementColors({})

        # The element list is the authority. A material list cannot be empty
        # (App::PropertyMaterialList keeps one entry), so what matters is that
        # no override is in effect.
        self.assertEqual(self.stored(), [])
        self.assertEqual(set(self.vo.getElementColors()), {"Face", "Edge", "Vertex"})
        self.assertUniform()

    def testUndoAndRedo(self):
        top = self.top()
        self.doc.openTransaction("paint")
        self.paint(top)
        self.doc.commitTransaction()
        self.assertDrawn(top)

        self.doc.undo()
        self.assertEqual(self.stored(), [])
        self.assertUniform()

        self.doc.redo()
        self.assertEqual(self.stored(), ["Face%d" % top])
        self.assertDrawn(top)

    def _save_and_reopen(self, rewrite=None):
        path = os.path.join(self.tempdir.name, "ElementAppearanceTest.FCStd")
        self.doc.saveAs(path)
        App.closeDocument(self.doc.Name)
        if rewrite:
            rewrite(path)
        self.doc = App.openDocument(path)
        self.box = self.doc.Box
        self.cylinder = self.doc.Cylinder
        self.cut = self.doc.Cut
        self.vo = self.cut.ViewObject

    def testSaveAndReload(self):
        top = self.top()
        self.paint(top)

        self._save_and_reopen()

        self.assertEqual(self.cut.ColoredElements, (self.cut, ["Face%d" % top]))
        self.assertEqual(len(self.vo.MappedAppearance), 1)
        self.assertDrawn(top)
        self.cylinder.Height = 2
        self.doc.recompute()
        self.assertDrawn(self.top())

    def testLegacyFileIsMigratedOnLoad(self):
        top = self.top()
        self.paint(top)

        def drop_properties(xml, names):
            """Remove property elements, fixing the Count each Properties block declares."""

            def fix_block(match):
                block = match.group(0)
                removed = 0
                for name in names:
                    block, n = re.subn(
                        r'<Property name="%s"[^>]*?(?:/>|>.*?</Property>)\s*' % name,
                        "",
                        block,
                        flags=re.S,
                    )
                    removed += n
                if removed:
                    block = re.sub(
                        r'(<Properties[^>]*\bCount=")(\d+)',
                        lambda m: m.group(1) + str(int(m.group(2)) - removed),
                        block,
                        count=1,
                    )
                return block

            return re.sub(r"<Properties\b.*?</Properties>", fix_block, xml, flags=re.S)

        def strip_overrides(path):
            """Rewrite the file as a build without element overrides would have saved it."""
            with zipfile.ZipFile(path) as source:
                entries = {name: source.read(name) for name in source.namelist()}
            entries["Document.xml"] = drop_properties(
                entries["Document.xml"].decode(), ["ColoredElements"]
            ).encode()
            entries["GuiDocument.xml"] = drop_properties(
                entries["GuiDocument.xml"].decode(), ["BaseAppearance", "MappedAppearance"]
            ).encode()
            for name in list(entries):
                if name.startswith("MappedAppearance"):
                    del entries[name]
            with zipfile.ZipFile(path, "w") as target:
                for name, data in entries.items():
                    target.writestr(name, data)

        self._save_and_reopen(strip_overrides)

        self.assertEqual(self.cut.ColoredElements, (self.cut, ["Face%d" % top]))
        self.assertEqual(rgb(self.vo.BaseAppearance.DiffuseColor), self.base)
        self.assertDrawn(top)
        self.cylinder.Height = 2
        self.doc.recompute()
        self.assertDrawn(self.top())
