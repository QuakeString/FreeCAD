# SPDX-License-Identifier: LGPL-2.1-or-later

"""Per-element appearance across a PartDesign body.

A face painted on one feature keeps its appearance on the features built after it,
and on the body's tip, without being pinned to a face index.
"""

import unittest

import FreeCAD
import FreeCADGui
import TestSketcherApp

from parttests.ElementAppearanceTest import face_rendering, rgb

RED = (1.0, 0.0, 0.0, 1.0)


def plus_x_face(shape):
    """1-based index of the single face whose centre lies furthest along +X."""
    centres = [f.CenterOfMass.x for f in shape.Faces]
    best = max(centres)
    found = [i + 1 for i, x in enumerate(centres) if abs(x - best) < 1e-6]
    assert len(found) == 1, found
    return found[0]


class TestElementAppearance(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignElementAppearance")
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        sketch = self.Doc.addObject("Sketcher::SketchObject", "SketchPad")
        self.Body.addObject(sketch)
        TestSketcherApp.CreateRectangleSketch(sketch, (0, 0), (10, 10))
        self.Doc.recompute()
        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Body.addObject(self.Pad)
        self.Pad.Profile = sketch
        self.Pad.Length = 10
        self.Doc.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def paintPadRight(self):
        index = plus_x_face(self.Pad.Shape)
        self.Pad.ViewObject.setElementColors({"Face%d" % index: RED})
        return index

    def assertPaintedRight(self, feature, message=""):
        """Only the +X face of this feature is red, and the faces are drawn per part."""
        shape = feature.Shape
        index = plus_x_face(shape)
        view = feature.ViewObject
        appearance = view.ShapeAppearance
        self.assertEqual(len(appearance), len(shape.Faces), message)
        self.assertEqual(rgb(appearance[index - 1].DiffuseColor), rgb(RED), message)
        others = {rgb(appearance[i].DiffuseColor) for i in range(len(appearance)) if i != index - 1}
        self.assertEqual(others, {rgb(view.BaseAppearance.DiffuseColor)}, "only that face: " + message)
        self.assertNotEqual(rgb(view.BaseAppearance.DiffuseColor), rgb(RED), message)
        self.assertEqual(face_rendering(view), ("PER_PART", len(shape.Faces)), message)

    def assertUnpainted(self, feature, message=""):
        """No face carries an appearance of its own.

        The scene graph is only inspected for a visible feature: a hidden one's
        material nodes sit under an inactive switch and are not reachable.
        """
        self.assertEqual(len(feature.ViewObject.ShapeAppearance), 1, message)
        if feature.ViewObject.Visibility:
            self.assertEqual(face_rendering(feature.ViewObject), ("OVERALL", 1), message)

    def addPocket(self):
        sketch = self.Doc.addObject("Sketcher::SketchObject", "SketchPocket")
        self.Body.addObject(sketch)
        TestSketcherApp.CreateRectangleSketch(sketch, (2, 2), (3, 3))
        self.Doc.recompute()
        pocket = self.Doc.addObject("PartDesign::Pocket", "Pocket")
        self.Body.addObject(pocket)
        pocket.Profile = sketch
        pocket.Length = 4
        pocket.Reversed = True  # the sketch sits on the pad's bottom face
        self.Doc.recompute()
        return pocket

    def testPaintingAnAncestorReachesFeaturesThatAlreadyExist(self):
        pocket = self.addPocket()
        self.assertUnpainted(pocket, "nothing is painted yet")

        self.paintPadRight()

        self.assertPaintedRight(pocket, "painting the pad must reach the pocket at once")

    def testUnpaintingAnAncestorClearsWhatItGave(self):
        self.paintPadRight()
        pocket = self.addPocket()
        self.assertPaintedRight(pocket)

        self.Pad.ViewObject.setElementColors({})

        self.assertUnpainted(pocket, "the pocket must not keep a colour nothing backs")
        self.assertUnpainted(self.Pad)

        self.Pad.Length = 12
        self.Doc.recompute()
        self.assertUnpainted(pocket, "and must not paint a face after the shape changes")

    def testColourSurvivesOnTheFeatureItself(self):
        self.paintPadRight()

        self.assertPaintedRight(self.Pad)
        self.Pad.Length = 15
        self.Doc.recompute()
        self.assertPaintedRight(self.Pad, "after a dimension change")

    def testNextFeatureInheritsTheColour(self):
        self.paintPadRight()

        pocket = self.addPocket()

        self.assertEqual(self.Body.Tip, pocket)
        self.assertGreater(len(pocket.Shape.Faces), len(self.Pad.Shape.Faces))
        self.assertPaintedRight(pocket, "the pocket must inherit the pad's painted face")
        self.assertEqual(pocket.ColoredElements, None, "inheritance stores nothing of its own")

    def testInheritanceStopsWhenSwitchedOff(self):
        self.paintPadRight()
        pocket = self.addPocket()
        self.assertPaintedRight(pocket)

        pocket.ViewObject.MapFaceColor = False

        self.assertEqual(len(pocket.ViewObject.ShapeAppearance), 1)
        self.assertEqual(face_rendering(pocket.ViewObject), ("OVERALL", 1))

    def testBodyReportsTheColourOfItsTip(self):
        self.paintPadRight()
        pocket = self.addPocket()
        self.assertEqual(self.Body.Tip, pocket)

        colors = self.Body.ViewObject.getElementColors()

        # The body delegates to its tip, which inherited the colour rather than
        # storing it, so the tip's inherited faces have to be reported too.
        painted = [key for key, value in colors.items() if rgb(value) == rgb(RED)]
        self.assertEqual(len(painted), 1, "one painted face, got %s" % colors)
        self.assertEqual(painted[0], "Face%d" % plus_x_face(pocket.Shape))

    def testFilletOnThePaintedFaceInheritsTheColour(self):
        index = self.paintPadRight()
        # An edge of the painted face itself, so the fillet reshapes that face.
        painted = self.Pad.Shape.Faces[index - 1].Edges[0]
        matches = [
            i + 1 for i, edge in enumerate(self.Pad.Shape.Edges) if edge.isSame(painted)
        ]
        self.assertEqual(len(matches), 1)
        name = "Edge%d" % matches[0]
        fillet = self.Doc.addObject("PartDesign::Fillet", "Fillet")
        self.Body.addObject(fillet)
        fillet.Base = (self.Pad, [name])
        fillet.Radius = 1.0
        self.Doc.recompute()
        self.assertNotIn("Invalid", fillet.State)

        self.assertPaintedRight(fillet, "the fillet inherits the pad's painted face")
        self.assertGreater(len(fillet.Shape.Faces), len(self.Pad.Shape.Faces))
