// Controlled crash harness for DIAG-02 closure (Phase 2).
//
// PURPOSE
//   Prove that the diagnostics crash-capture path writes its self-contained
//   FALLBACK artifact (and that the final log is flushed) when a REAL process
//   crash drives the installed top-level SEH filter — not by calling
//   writeFinalArtifact() directly (the existing diagnostics_tests.cpp already
//   covers that graceful path), but by deliberately faulting.
//
//   CrashCapture::install() registers handleUnhandledException via
//   SetUnhandledExceptionFilter (process-wide, last-chance). The only faithful
//   way to drive that handler is for THIS process to take an unhandled SEH
//   exception (access violation), which then terminates the process. The parent
//   CTest driver inspects what this process left behind.
//
// HONESTY
//   This proves the FALLBACK self-contained JSON artifact path
//   (artifact_type == "fallback_crash_report"). A real OS minidump is deferred
//   to B7. The SEH path intentionally omits recent log records to stay
//   async-safe mid-crash; the "final log flush" leg is exercised by the
//   --graceful mode below (writeFinalArtifact embeds the flushed recent records).
//
// MODES (argv)
//   <artifact_dir> <session_id> --crash     : install + seed identity + fault
//   <artifact_dir> <session_id> --graceful  : install + writeFinalArtifact (flush)
//
// All scratch lands under the dir the driver passes (a TEMP/build dir) — never
// the repo tree.

#include "diagnostics/crash/crash_capture.h"
#include "diagnostics/logging/diagnostic_logger.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace {

// A volatile null write that the optimizer cannot elide; raises
// EXCEPTION_ACCESS_VIOLATION (0xC0000005), the canonical unhandled SEH
// exception that reaches SetUnhandledExceptionFilter.
[[noreturn]] void forceAccessViolation() {
  volatile int* null_pointer = nullptr;
  *null_pointer = 0x5150;  // crash here
  // Unreachable, but keep the compiler from proving [[noreturn]] is violated.
  while (true) {
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    return 2;  // misuse -> driver treats as a setup failure
  }

  const std::filesystem::path artifact_dir = argv[1];
  const std::string session_id = argv[2];
  const std::string mode = argv[3];

  // (2) Start a logger so a recent log record exists; point it at the same root
  // so the driver can confirm the final log flush on the graceful path.
  vrclient::diagnostics::SessionMetadata session_metadata;
  session_metadata.session_id = session_id;
  session_metadata.game_id = "crash-probe-game";
  session_metadata.build_id = "crash-probe-build";
  session_metadata.adapter_id = "crash-probe-adapter";
  session_metadata.launch_path = "C:/VRClient/vr_crash_harness.exe";

  vrclient::diagnostics::LoggerConfig log_config;
  log_config.log_directory = artifact_dir / "logs";

  vrclient::diagnostics::AsyncLogger logger;
  if (!logger.start(log_config, session_metadata)) {
    return 3;
  }
  logger.log(vrclient::diagnostics::Severity::Error, "crash_probe_recent_event");

  // (3) Install the crash capture (registers the SEH top-level filter on Windows).
  vrclient::diagnostics::CrashArtifactConfig crash_config;
  crash_config.artifact_directory = artifact_dir / "crashes";
  crash_config.session_id = session_id;

  vrclient::diagnostics::CrashCapture crash_capture;
  if (!crash_capture.install(crash_config, &logger)) {
    return 4;
  }

  // (4) Seed game/build/adapter/launch identity that the handler will copy into
  // the artifact (proves the captured artifact carries the live process identity).
  crash_capture.updateMetadata({
      "runtime_snapshot",
      0,
      VR_RUNTIME_STATE_RUNNING,
      session_metadata.game_id,
      session_metadata.build_id,
      session_metadata.adapter_id,
      session_metadata.launch_path,
  });

  if (mode == "--graceful") {
    // Final-log-flush leg: writeFinalArtifact embeds the flushed recent records
    // and updates lastArtifactPath(). Clean shutdown, exit 0.
    vrclient::diagnostics::CrashArtifactMetadata graceful_metadata;
    graceful_metadata.reason = "graceful_shutdown_flush";
    graceful_metadata.runtime_state = VR_RUNTIME_STATE_STOPPED;
    crash_capture.writeFinalArtifact(graceful_metadata);
    logger.stop();  // explicit final flush
    return 0;
  }

  if (mode == "--crash") {
    // (5) Deliberately fault. The installed SEH filter runs
    // writeUnhandledExceptionArtifact -> writeArtifact (fallback JSON), then the
    // process terminates with the access-violation code. We do NOT call
    // logger.stop() here on purpose: the artifact must be written BY the handler
    // during the crash, not by graceful teardown.
    forceAccessViolation();
  }

  return 5;  // unknown mode
}
