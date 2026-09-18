import unittest
from scripts.tests import test_maomi_host_cpp as host


class LearningTest(unittest.TestCase):
    setUpClass = classmethod(host.MaomiHostCppTest.setUpClass.__func__)
    compile_and_run = host.MaomiHostCppTest.compile_and_run
    def test_pet_life_contract(self):
        self.compile_and_run("maomi-pet-life", [
            host.ROOT / "scripts/tests/maomi_pet_life_test.cc",
            host.BOARD / "maomi_learning.cc",
        ])

    def test_listening_hint_matches_capture_readiness(self):
        self.compile_and_run("maomi-learning-presentation", [
            host.ROOT / "scripts/tests/maomi_learning_presentation_test.cc",
        ])

    def test_learning_contract(self):
        self.compile_and_run("maomi-learning", [
            host.ROOT / "scripts/tests/maomi_learning_test.cc",
            host.BOARD / "maomi_learning.cc",
        ])
