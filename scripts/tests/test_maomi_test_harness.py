import unittest
from unittest import mock

from scripts.tests import test_maomi_host_cpp


class RequiredCompilerGateTest(unittest.TestCase):
    def test_missing_host_compiler_fails_instead_of_skipping_required_cpp_tests(self):
        with mock.patch.object(test_maomi_host_cpp, "find_host_compiler", return_value=None):
            try:
                test_maomi_host_cpp.MaomiHostCppTest.setUpClass()
            except BaseException as error:
                self.assertNotIsInstance(error, unittest.SkipTest)
                self.assertIsInstance(error, RuntimeError)
            else:
                self.fail("missing compiler must fail the required C++ test gate")


if __name__ == "__main__":
    unittest.main()
