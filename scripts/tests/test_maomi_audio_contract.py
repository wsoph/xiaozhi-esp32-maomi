import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
AUDIO_HEADER = ROOT / "main" / "audio" / "audio_service.h"
AUDIO_SOURCE = ROOT / "main" / "audio" / "audio_service.cc"
APPLICATION_SOURCE = ROOT / "main" / "application.cc"
APPLICATION_HEADER = ROOT / "main" / "application.h"
PROTOCOL_HEADER = ROOT / "main" / "protocols" / "protocol.h"
BOARD_SOURCE = (
    ROOT
    / "main"
    / "boards"
    / "zhengchen"
    / "1.54tft-wifi-maomi"
    / "zhengchen-1.54tft-wifi-maomi.cc"
)
ORIGINAL_BOARD_SOURCE = (
    ROOT
    / "main"
    / "boards"
    / "zhengchen"
    / "1.54tft-wifi"
    / "zhengchen-1.54tft-wifi.cc"
)


class MaomiAudioContractTest(unittest.TestCase):
    def test_wake_event_wiring_precedes_same_cycle_audio_send(self):
        source = APPLICATION_SOURCE.read_text(encoding="utf-8")
        run_loop = re.search(
            r"void\s+Application::Run\(\)(?P<body>.*?)\n}", source, re.DOTALL
        )
        self.assertIsNotNone(run_loop)
        body = run_loop.group("body")
        wake_branch = body.index("if (bits & MAIN_EVENT_WAKE_WORD_DETECTED)")
        send_branch = body.index("if (bits & MAIN_EVENT_SEND_AUDIO)")
        self.assertLess(wake_branch, send_branch)

    def test_audio_service_exposes_additive_safe_playback_and_upload_cleanup(self):
        header = AUDIO_HEADER.read_text(encoding="utf-8")

        self.assertRegex(header, r"bool\s+TryPlaySound\s*\(")
        self.assertRegex(header, r"void\s+DiscardVoiceUploadBacklog\s*\(")
        self.assertIn("on_playback_finished", header)
        self.assertIn("VoiceUploadGate voice_upload_gate_", header)
        self.assertIn("AcquireVoiceUploadLease", header)

        protocol = PROTOCOL_HEADER.read_text(encoding="utf-8")
        self.assertRegex(protocol, r"bool\s+playback_end\s*=\s*false")
        self.assertRegex(protocol, r"uint32_t\s+voice_upload_generation\s*=\s*0")

        application_header = APPLICATION_HEADER.read_text(encoding="utf-8")
        self.assertIn("SetPlaybackFinishedObserver", application_header)

    def test_safe_playback_validates_the_complete_ogg_before_queueing(self):
        source = AUDIO_SOURCE.read_text(encoding="utf-8")

        validation = re.search(
            r"bool\s+AudioService::TryPlaySound\b(?P<body>.*?)\n}",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(validation)
        body = validation.group("body")
        self.assertIn("Finish()", body)
        self.assertIn("PushPacketToDecodeQueue", body)
        self.assertLess(body.index("Finish()"), body.index("PushPacketToDecodeQueue"))

    def test_maomi_path_is_nonblocking_and_uses_exact_playback_ticket(self):
        source = BOARD_SOURCE.read_text(encoding="utf-8")

        self.assertIn("DiscardVoiceUploadBacklog()", source)
        self.assertRegex(source, r"TryPlaySound\s*\([^;]*false\s*,\s*playback_id\s*\)")
        self.assertIn("SetPlaybackFinishedObserver", source)
        self.assertIn("HandlePlaybackFinished", source)
        self.assertNotIn("GetPlaybackGeneration()", source)

    def test_maomi_official_invoke_runs_directly_on_the_main_task(self):
        application_header = APPLICATION_HEADER.read_text(encoding="utf-8")
        board_source = BOARD_SOURCE.read_text(encoding="utf-8")

        self.assertIn("TryWakeWordInvokeFromMainTask", application_header)
        self.assertIn("TryWakeWordInvokeFromMainTask(wake_word)", board_source)

    def test_busy_wake_sequence_blocks_chat_button_before_dialog_start(self):
        source = BOARD_SOURCE.read_text(encoding="utf-8")
        click = re.search(
            r"boot_button_\.OnClick\(\[this\]\(\) \{(?P<body>.*?)\n\s*\}\);",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(click)
        body = click.group("body")
        self.assertIn("maomi_wake_.IsBusy()", body)
        self.assertLess(body.index("maomi_wake_.IsBusy()"), body.index("app.ToggleChatState()"))

    def test_listening_wake_wiring_resumes_the_upload_gate(self):
        source = APPLICATION_SOURCE.read_text(encoding="utf-8")
        handler_start = source.index("void Application::HandleWakeWordDetectedEvent()")
        handler_end = source.index("bool Application::BeginWakeWordInvoke", handler_start)
        handler = source[handler_start:handler_end]
        listening_branch = re.search(
            r"if \(state == kDeviceStateListening\) \{(?P<body>.*?)\n\s*\} else \{",
            handler,
            re.DOTALL,
        )
        self.assertIsNotNone(listening_branch)
        self.assertIn("ResumeVoiceUpload()", listening_branch.group("body"))

    def test_wake_upload_discard_is_opt_in_for_the_maomi_board(self):
        audio_source = AUDIO_SOURCE.read_text(encoding="utf-8")
        wake_callback_start = audio_source.index("audio_engine_->OnWakeWordDetected")
        wake_callback_end = audio_source.index("});", wake_callback_start)
        wake_callback = audio_source[wake_callback_start:wake_callback_end]
        maomi_board = BOARD_SOURCE.read_text(encoding="utf-8")
        original_board = ORIGINAL_BOARD_SOURCE.read_text(encoding="utf-8")

        self.assertIn("discard_voice_upload_on_wake_", wake_callback)
        self.assertIn("SetDiscardVoiceUploadOnWake(true)", maomi_board)
        self.assertNotIn("SetDiscardVoiceUploadOnWake", original_board)

    def test_official_fallback_keeps_the_streamable_synchronous_path(self):
        source = BOARD_SOURCE.read_text(encoding="utf-8")
        start = source.index("maomi::WakePlaybackStart StartMaomiLocalResponse()")
        end = source.index("void RestoreMaomiWakeDetection()", start)
        body = source[start:end]

        self.assertEqual(body.count("TryPlaySound("), 1)
        self.assertIn("if (!audio_service.PlaySound(Lang::Sounds::OGG_POPUP))", body)
        self.assertIn("kFailed", body)
        self.assertIn("kFallbackCompleted", body)

    def test_official_play_sound_is_single_pass_streaming(self):
        source = AUDIO_SOURCE.read_text(encoding="utf-8")
        start = source.index("AudioService::PlaySound")
        end = source.index("bool AudioService::TryPlaySound", start)
        body = source[start:end]

        self.assertNotIn("TryPlaySound(", body)
        self.assertEqual(body.count("OggDemuxer"), 1)
        self.assertIn("PushPacketToDecodeQueue", body)
        self.assertIn("Finish()", body)

    def test_completion_and_poll_delivery_do_not_allocate_per_event(self):
        application = APPLICATION_SOURCE.read_text(encoding="utf-8")
        board = BOARD_SOURCE.read_text(encoding="utf-8")

        completion_start = application.index("callbacks.on_playback_finished")
        completion_end = application.index("audio_service_.SetCallbacks", completion_start)
        completion_body = application[completion_start:completion_end]
        self.assertNotIn("Schedule(", completion_body)
        self.assertIn("MAIN_EVENT_PLAYBACK_FINISHED", completion_body)

        poll_start = board.index("void PollMaomiWake()")
        poll_end = board.index("bool InitializeMaomiWake()", poll_start)
        poll_body = board[poll_start:poll_end]
        self.assertNotIn("Schedule(", poll_body)
        self.assertIn("RequestBoardPoll()", poll_body)


if __name__ == "__main__":
    unittest.main()
