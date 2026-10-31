#pragma once

namespace gt3r {
// Defer periodic rendering diagnostics only. Other messages remain immediate.
void log_defer_diagnostics(bool enabled);
void log_flush_diagnostics();
}
