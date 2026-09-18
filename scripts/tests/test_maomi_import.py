import io
import json
import unittest
from scripts import maomi_import as importer


class ImportTest(unittest.TestCase):
    def test_csv_preserves_quoted_meanings_and_normalizes_words(self):
        words = importer.read_csv(io.StringIO('word,meaning\n Apple ,"苹果,水果"\ncat,猫\n'))
        self.assertEqual(words, [("apple", "苹果,水果"), ("cat", "猫")])

    def test_rejects_duplicates_long_words_invalid_utf8_controls(self):
        for csv in ['word,meaning\ncat,猫\nCAT,猫\n', 'word,meaning\na b,词\n',
                    'word,meaning\ncat,\n', 'word,meaning\ncat,a\x00b\n',
                    'word,meaning\n' + 'a' * 33 + ',词\n']:
            with self.subTest(csv=csv), self.assertRaises(ValueError):
                importer.read_csv(io.StringIO(csv))

    def test_crc_is_stable_and_changes_with_meaning(self):
        # CRC32 of UTF-8 bytes: 63 61 74 00 e7 8c ab 00.
        self.assertEqual(importer.book_crc([("cat", "猫")]), 1749061360)
        self.assertNotEqual(importer.book_crc([("cat", "猫")]), importer.book_crc([("cat", "猫咪")]))

    def test_protocol_ignores_logs_and_unrelated_responses(self):
        class Serial:
            def write(self, data):
                request = json.loads(data.decode()[5:])["request"]
                self.lines = iter([b"I (12) boot: hello\n", b'@ML1 {"request":0,"ok":true}\n',
                                   (f'@ML1 {{"request":{request},"ok":true,"revision":3}}\n').encode()])
            def readline(self, size):
                return next(self.lines, b"")
        self.assertEqual(importer.Device(Serial()).call("status")["revision"], 3)

    def test_device_failure_is_reported(self):
        class Serial:
            def write(self, data):
                self.request = json.loads(data.decode()[5:])["request"]
            def readline(self, size):
                return (f'@ML1 {{"request":{self.request},"ok":false,"error":"study_active"}}\n').encode()
        with self.assertRaisesRegex(ValueError, "study_active"):
            importer.Device(Serial()).call("import_begin")

    def test_lost_response_retries_exact_same_request(self):
        class Serial:
            writes = []
            def write(self, data):
                self.writes.append(data)
            def readline(self, size):
                if len(self.writes) == 1:
                    return b""
                request = json.loads(self.writes[-1].decode()[5:])["request"]
                return (f'@ML1 {{"request":{request},"ok":true}}\n').encode()
        serial = Serial()
        self.assertTrue(importer.Device(serial, timeout=0.001).call("status")["ok"])
        self.assertEqual(len(serial.writes), 2)
        self.assertEqual(serial.writes[0], serial.writes[1])
