/* In-game (battery) save persistence, from upstream elliotttate/DKC1Recomp
 * (c9946fa). Writes saves/save.srm only when cartridge RAM changed, through
 * a temporary file, so a crash or power loss never leaves a torn save.
 * Switch adaptations: Horizon's rename() refuses to replace an existing
 * file and newlib has no usable fsync/getpid there. */
#if !defined(_WIN32) && !defined(__SWITCH__)
#define _POSIX_C_SOURCE 200809L
#endif
#include "desktop_sram.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

static const char kSavePath[] = "saves/save.srm";

static bool Fail(char *error, size_t size, const char *operation) {
  if (error && size)
    snprintf(error, size,
             "Cannot %s %s. Check the save folder and available disk space. "
             "Your previous file has not been replaced.", operation, kSavePath);
  return false;
}

bool Dkc1SramLoad(Dkc1SramStore *store, uint8_t *ram, size_t size,
                  char *error, size_t error_size) {
  memset(store, 0, sizeof *store);
  if (!ram || size != kDkc1SramSize)
    return Fail(error, error_size, "load (unexpected cartridge RAM size)");
  FILE *file = fopen(kSavePath, "rb");
  if (!file) {
    if (errno != ENOENT)
      return Fail(error, error_size, "read");
    memcpy(store->persisted, ram, size);
    store->ready = true;
    return true;
  }
  uint8_t loaded[kDkc1SramSize];
  bool ok = fread(loaded, 1, size, file) == size;
  if (ok) ok = fgetc(file) == EOF && !ferror(file);
  if (fclose(file) != 0) ok = false;
  if (!ok)
    return Fail(error, error_size, "load (expected exactly 2048 bytes)");
  memcpy(ram, loaded, size);
  memcpy(store->persisted, loaded, size);
  store->ready = true;
  fprintf(stderr, "[sram] loaded %s (%zu bytes)\n", kSavePath, size);
  return true;
}

bool Dkc1SramFlush(Dkc1SramStore *store, const uint8_t *ram, size_t size,
                   bool force, char *error, size_t error_size) {
  if (!store->ready) return true;
  if (!ram || size != kDkc1SramSize)
    return Fail(error, error_size, "save (unexpected cartridge RAM size)");
  if (memcmp(store->persisted, ram, size) == 0) return true;
  if (!force && store->retry_frames) {
    store->retry_frames--;
    return true;
  }
  char temporary[96];
#if defined(_WIN32)
  int made = _mkdir("saves");
  snprintf(temporary, sizeof temporary, "saves/save.srm.%d.tmp", _getpid());
#elif defined(__SWITCH__)
  int made = mkdir("saves", 0755);
  snprintf(temporary, sizeof temporary, "saves/save.srm.tmp");
#else
  int made = mkdir("saves", 0755);
  snprintf(temporary, sizeof temporary, "saves/save.srm.%ld.tmp", (long)getpid());
#endif
  bool ok = made == 0 || errno == EEXIST;
  FILE *file = ok ? fopen(temporary, "wb") : NULL;
  ok = file != NULL;
  if (file) {
    ok = fwrite(ram, 1, size, file) == size;
    if (ok) ok = fflush(file) == 0;
    if (ok) {
#if defined(_WIN32)
      ok = _commit(_fileno(file)) == 0;
#elif defined(__SWITCH__)
      /* fclose() below commits the sdmc write. */
#else
      ok = fsync(fileno(file)) == 0;
#endif
    }
    if (fclose(file) != 0) ok = false;
  }
  if (ok) {
#if defined(_WIN32)
    ok = MoveFileExA(temporary, kSavePath,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#elif defined(__SWITCH__)
    /* Keep the previous save as save.srm.bak, then move the new one in;
     * if that fails, put the previous save back. */
    static const char kBackupPath[] = "saves/save.srm.bak";
    remove(kBackupPath);
    const bool had_previous = rename(kSavePath, kBackupPath) == 0;
    ok = rename(temporary, kSavePath) == 0;
    if (!ok && had_previous) (void)rename(kBackupPath, kSavePath);
#else
    ok = rename(temporary, kSavePath) == 0;
#endif
  }
  if (!ok) {
    if (file) remove(temporary);
    store->retry_frames = 60;
    fprintf(stderr, "[sram] unable to save %s; keeping previous file\n", kSavePath);
    return Fail(error, error_size, "write");
  }
  memcpy(store->persisted, ram, size);
  store->retry_frames = 0;
  fprintf(stderr, "[sram] saved %s (%zu bytes)\n", kSavePath, size);
  return true;
}
