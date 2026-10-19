#ifndef DISKDUDE_SCANNER_H
#define DISKDUDE_SCANNER_H

#include "../Models/Models.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int64_t files_scanned;
    int64_t bytes_scanned;
    char current_path[512];
    bool is_running;
    bool is_finished;
    bool was_cancelled;
} DDScanProgress;

typedef struct DDDiskScanner DDDiskScanner;

DDDiskScanner *dd_scanner_create(void);
void dd_scanner_destroy(DDDiskScanner *scanner);

void dd_scanner_start(DDDiskScanner *scanner, const char *root_path);
void dd_scanner_cancel(DDDiskScanner *scanner);
void dd_scanner_get_progress(DDDiskScanner *scanner, DDScanProgress *out_progress);
DDFileTree *dd_scanner_take_result(DDDiskScanner *scanner);

int64_t dd_scanner_allocated_size(const char *path);

#endif
