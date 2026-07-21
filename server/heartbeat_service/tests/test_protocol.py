import json
import unittest

from server.heartbeat_service.src.protocol import (
    InvalidStatusPayload,
    build_ingest_ack,
)


class HeartbeatProtocolTest(unittest.TestCase):
    def test_builds_existing_ingest_ack_format(self):
        ack = build_ingest_ack(
            b'{"node_id":"node01","seq":42,"temperature":26.5}',
            "node01",
        )

        self.assertEqual(
            {"node_id": "node01", "seq": 42, "stored": True},
            json.loads(ack.to_json()),
        )

    def test_rejects_invalid_json(self):
        with self.assertRaisesRegex(InvalidStatusPayload, "UTF-8 JSON"):
            build_ingest_ack(b"not-json", "node01")

    def test_rejects_another_node(self):
        with self.assertRaisesRegex(InvalidStatusPayload, "unexpected node_id"):
            build_ingest_ack(b'{"node_id":"node02","seq":42}', "node01")

    def test_rejects_invalid_sequence(self):
        for seq in (-1, True, "42", None):
            with self.subTest(seq=seq):
                payload = json.dumps({"node_id": "node01", "seq": seq}).encode()
                with self.assertRaisesRegex(InvalidStatusPayload, "invalid seq"):
                    build_ingest_ack(payload, "node01")


if __name__ == "__main__":
    unittest.main()
