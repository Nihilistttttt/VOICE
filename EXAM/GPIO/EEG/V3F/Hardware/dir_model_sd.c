#include "dir_model_sd.h"
#include "FATFS/ff.h"
#include "Serial.h"
#include "eeg_protocol.h"
#include "Message_Parser.h"
#include <string.h>
#include <stdio.h>

#define DIR_MODEL_DIR   "DIRMODEL"

static FATFS s_fs;
static int   s_fs_mounted = 0;

static int ensure_mounted(void)
{
    if (s_fs_mounted) return 0;
    if (f_mount(&s_fs, "", 1) != FR_OK) return -1;
    s_fs_mounted = 1;
    f_mkdir(DIR_MODEL_DIR);
    return 0;
}

int DirModelSD_Init(void)
{
    return ensure_mounted();
}

int DirModelSD_Save(const DirModelFile_t *model)
{
    if (model == 0) return -1;
    if (ensure_mounted() != 0) return -2;

    char path[32];
    snprintf(path, sizeof(path), "%s/%s.BIN", DIR_MODEL_DIR, model->patient_id);

    FIL f;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return -3;

    UINT bw;
    UINT total = 0;
    if (f_write(&f, model->patient_id, DIR_MODEL_ID_LEN, &bw) != FR_OK) { f_close(&f); return -4; }
    total += bw;
    if (f_write(&f, model->patient_name, DIR_MODEL_NAME_LEN, &bw) != FR_OK) { f_close(&f); return -4; }
    total += bw;
    if (f_write(&f, &model->balanced_accuracy, 4, &bw) != FR_OK) { f_close(&f); return -4; }
    total += bw;
    if (f_write(&f, model->model_data, DIR_MODEL_PAYLOAD_LEN, &bw) != FR_OK) { f_close(&f); return -4; }
    total += bw;

    f_sync(&f);
    f_close(&f);

    if (total != DIR_MODEL_FILE_LEN) return -5;

    Serial_Printf(SERIAL_PORT_DEBUG, "[DIRMOD] saved %s/%s (%uB)\r\n",
                  DIR_MODEL_DIR, model->patient_id, (unsigned)total);
    return 0;
}

int DirModelSD_Scan(DirModelEntry_t *list, uint8_t max_count, uint8_t *out_count)
{
    if (list == 0 || out_count == 0) return -1;
    *out_count = 0;
    if (ensure_mounted() != 0) return -2;

    char path[32];
    snprintf(path, sizeof(path), "%s", DIR_MODEL_DIR);

    DIR dir;
    FILINFO fno;
    if (f_opendir(&dir, path) != FR_OK) return -3;

    uint8_t cnt = 0;
    while (cnt < max_count && f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
        if (fno.fattrib & AM_DIR) continue;
        const char *dot = strrchr(fno.fname, '.');
        if (dot == 0 || strcmp(dot, ".BIN") != 0) continue;

        char fpath[48];
        snprintf(fpath, sizeof(fpath), "%s/%s", DIR_MODEL_DIR, fno.fname);

        FIL f;
        if (f_open(&f, fpath, FA_READ) != FR_OK) continue;

        UINT br;
        DirModelEntry_t *e = &list[cnt];
        memset(e, 0, sizeof(*e));
        f_read(&f, e->patient_id, DIR_MODEL_ID_LEN, &br);
        f_read(&f, e->patient_name, DIR_MODEL_NAME_LEN, &br);
        f_read(&f, &e->balanced_accuracy, 4, &br);
        f_close(&f);

        e->patient_id[DIR_MODEL_ID_LEN - 1] = '\0';
        e->patient_name[DIR_MODEL_NAME_LEN - 1] = '\0';
        cnt++;
    }
    f_closedir(&dir);
    *out_count = cnt;

    Serial_Printf(SERIAL_PORT_DEBUG, "[DIRMOD] scan found %u models\r\n", (unsigned)cnt);
    return 0;
}

int DirModelSD_Load(const char *patient_id, DirModelFile_t *out_model)
{
    if (patient_id == 0 || out_model == 0) return -1;
    if (ensure_mounted() != 0) return -2;

    char path[32];
    snprintf(path, sizeof(path), "%s/%s.BIN", DIR_MODEL_DIR, patient_id);

    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return -3;

    UINT br;
    UINT total = 0;
    f_read(&f, out_model->patient_id, DIR_MODEL_ID_LEN, &br); total += br;
    f_read(&f, out_model->patient_name, DIR_MODEL_NAME_LEN, &br); total += br;
    f_read(&f, &out_model->balanced_accuracy, 4, &br); total += br;
    f_read(&f, out_model->model_data, DIR_MODEL_PAYLOAD_LEN, &br); total += br;
    f_close(&f);

    if (total != DIR_MODEL_FILE_LEN) return -4;
    return 0;
}

void DirModelSD_SendList(void)
{
    static DirModelEntry_t s_list[DIR_MODEL_MAX_LIST];
    uint8_t count = 0;

    int ret = DirModelSD_Scan(s_list, DIR_MODEL_MAX_LIST, &count);
    if (ret != 0) {
        Serial_Printf(SERIAL_PORT_DEBUG, "[DIRMOD] scan failed: %d\r\n", ret);
        count = 0;
    }

    uint8_t buf[1 + DIR_MODEL_MAX_LIST * DIR_MODEL_LIST_ENTRY];
    uint16_t idx = 0;
    buf[idx++] = count;
    for (uint8_t i = 0; i < count; i++) {
        memcpy(&buf[idx], s_list[i].patient_id, DIR_MODEL_ID_LEN);
        idx += DIR_MODEL_ID_LEN;
        memcpy(&buf[idx], s_list[i].patient_name, DIR_MODEL_NAME_LEN);
        idx += DIR_MODEL_NAME_LEN;
        memcpy(&buf[idx], &s_list[i].balanced_accuracy, 4);
        idx += 4;
    }

    Pack_Frame(SERIAL_PORT_DEBUG, CMD_DIR_MODEL_LIST, buf, idx);
    Pack_Frame(SERIAL_PORT_WIFI, CMD_DIR_MODEL_LIST, buf, idx);
    Serial_Printf(SERIAL_PORT_DEBUG, "[DIRMOD] sent list %u models\r\n", (unsigned)count);
}