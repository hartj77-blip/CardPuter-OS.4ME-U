// Safe file writes: a power cut or a full card never leaves a half-written
// or empty file in place of the old one.
#pragma once

#include <sys/types.h>

#include <string>

namespace zs {

// Writes data to path.tmp, flushes it to the card, then renames it over
// path. On any error the old file is untouched and this returns false.
// mode: permissions of the new file.
bool write_file_atomic(const std::string& path, const std::string& data, mode_t mode = 0644);

}  // namespace zs
