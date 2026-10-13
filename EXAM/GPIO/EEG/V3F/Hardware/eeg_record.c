#include "eeg_record.h"
#include "FATFS/ff.h"
#include "Serial.h"
#include <string.h>
#include <stdio.h>

#define REC_CHANNELS           8
#define REC_SAMPLE_RATE        250
#define REC_SAMPLES_PER_RECORD 250
#define REC_DATA_SIZE          (REC_CHANNELS * REC_SAMPLES_PER_RECORD * 3)
#define REC_HEADER_SIZE        (256 + REC_CHANNELS * 256)

static const char *s_ch_labels[REC_CHANNELS] = {
    "F4", "C4", "CP4", "OZ", "O1", "CP3", "C3", "F3"
};

static FIL      s_file;
static FATFS    s_fs;
static int      s_fs_mounted = 0;
static int      s_active = 0;
static uint8_t  s_buf[REC_DATA_SIZE];
static uint16_t s_sample_idx = 0;
static uint32_t s_record_count = 0;
static uint32_t s_sync_counter = 0;
static uint16_t s_file_seq = 0;
static char s_patient_id[12]   = "P000";
static char s_patient_name[40] = "EEG Patient";

static void fill_str(char *dst, const char *src, int len) {
    memset(dst, ' ', len);
    int n = (int)strlen(src);
    if (n > len) n = len;
    memcpy(dst, src, n);
}

static int write_header(uint32_t start_tick) {
    static uint8_t hdr[REC_HEADER_SIZE];
    memset(hdr, ' ', REC_HEADER_SIZE);
    char *p = (char *)hdr;
    char tmp[16];

    fill_str(p + 0, "255", 8);
    {
        char patient_field[88];
        snprintf(patient_field, sizeof(patient_field), "%s %s", s_patient_id, s_patient_name);
        fill_str(p + 8, patient_field, 80);
    }
    fill_str(p + 88, "WCH CH32H417", 80);

    uint32_t total_sec = start_tick / 1000u;
    uint32_t hh = (total_sec / 3600u) % 24u;
    uint32_t mm = (total_sec / 60u) % 60u;
    uint32_t ss = total_sec % 60u;
    snprintf(tmp, sizeof(tmp), "%02lu.%02lu.%02lu", (unsigned long)1, (unsigned long)1, (unsigned long)0);
    fill_str(p + 168, tmp, 8);
    snprintf(tmp, sizeof(tmp), "%02lu.%02lu.%02lu", (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
    fill_str(p + 176, tmp, 8);

    snprintf(tmp, sizeof(tmp), "%d", REC_HEADER_SIZE);
    fill_str(p + 184, tmp, 8);
    fill_str(p + 236, "-1", 8);
    fill_str(p + 244, "1.000000", 8);
    snprintf(tmp, sizeof(tmp), "%d", REC_CHANNELS);
    fill_str(p + 252, tmp, 4);

    for (int ch = 0; ch < REC_CHANNELS; ch++) {
        fill_str(p + 256  + ch * 16, s_ch_labels[ch], 16);
        fill_str(p + 384  + ch * 80, "Ag-Cl Electrode", 80);
        fill_str(p + 1024 + ch * 8,  "uV", 8);
        fill_str(p + 1088 + ch * 8,  "-187500.", 8);
        fill_str(p + 1152 + ch * 8,  "187500.", 8);
        fill_str(p + 1216 + ch * 8,  "-8388608", 8);
        fill_str(p + 1280 + ch * 8,  "8388607", 8);
        fill_str(p + 1344 + ch * 80, "HP:1Hz", 80);
        snprintf(tmp, sizeof(tmp), "%d", REC_SAMPLES_PER_RECORD);
        fill_str(p + 1984 + ch * 8,  tmp, 8);
    }

    UINT bw;
    if (f_write(&s_file, hdr, REC_HEADER_SIZE, &bw) != FR_OK || bw != REC_HEADER_SIZE) {
        return -1;
    }
    return 0;
}

int eeg_record_start(void) {
    if (s_active) return -1;

    if (!s_fs_mounted) {
        if (f_mount(&s_fs, "", 1) != FR_OK) {
            Serial_Printf(SERIAL_PORT_DEBUG, "[REC] mount FAIL\r\n");
            return -2;
        }
        s_fs_mounted = 1;
    }

    s_file_seq++;
    char filename[24];
    snprintf(filename, sizeof(filename), "%s_%04d.BDF", s_patient_id, s_file_seq);

    if (f_open(&s_file, filename, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        Serial_Printf(SERIAL_PORT_DEBUG, "[REC] open %s FAIL\r\n", filename);
        return -3;
    }

    extern volatile uint32_t g_icm42605_ms_tick;
    if (write_header(g_icm42605_ms_tick) != 0) {
        f_close(&s_file);
        Serial_Printf(SERIAL_PORT_DEBUG, "[REC] header FAIL\r\n");
        return -4;
    }

    s_sample_idx = 0;
    s_record_count = 0;
    s_sync_counter = 0;
    s_active = 1;

    Serial_Printf(SERIAL_PORT_DEBUG, "[REC] start %s\r\n", filename);
    return 0;
}

void eeg_record_stop(void) {
    if (!s_active) return;
    s_active = 0;

    if (s_record_count > 0) {
        char cnt_str[8];
        memset(cnt_str, ' ', 8);
        snprintf(cnt_str, sizeof(cnt_str), "%lu", (unsigned long)s_record_count);
        f_lseek(&s_file, 236);
        UINT bw;
        f_write(&s_file, cnt_str, 8, &bw);
    }

    f_close(&s_file);
    Serial_Printf(SERIAL_PORT_DEBUG, "[REC] stop, %lu records\r\n", (unsigned long)s_record_count);
}

void eeg_record_add_frame(const uint8_t *frame_27) {
    if (!s_active) return;

    const uint8_t *ch_data = frame_27 + 3;
    for (int ch = 0; ch < REC_CHANNELS; ch++) {
        uint32_t pos = (uint32_t)ch * REC_SAMPLES_PER_RECORD * 3 + (uint32_t)s_sample_idx * 3;
        s_buf[pos]     = ch_data[ch * 3 + 2];
        s_buf[pos + 1] = ch_data[ch * 3 + 1];
        s_buf[pos + 2] = ch_data[ch * 3];
    }
    s_sample_idx++;
}

void eeg_record_poll(void) {
    if (!s_active) return;
    if (s_sample_idx < REC_SAMPLES_PER_RECORD) return;

    UINT bw;
    if (f_write(&s_file, s_buf, REC_DATA_SIZE, &bw) != FR_OK || bw != REC_DATA_SIZE) {
        Serial_Printf(SERIAL_PORT_DEBUG, "[REC] write FAIL\r\n");
        eeg_record_stop();
        return;
    }

    s_record_count++;
    s_sample_idx = 0;

    s_sync_counter++;
    if (s_sync_counter >= 10) {
        f_sync(&s_file);
        s_sync_counter = 0;
    }
}

int eeg_record_is_active(void) {
    return s_active;
}

void eeg_record_set_patient(const char *id, const char *name) {
    if (id && id[0]) {
        strncpy(s_patient_id, id, sizeof(s_patient_id) - 1);
        s_patient_id[sizeof(s_patient_id) - 1] = '\0';
    }
    if (name && name[0]) {
        strncpy(s_patient_name, name, sizeof(s_patient_name) - 1);
        s_patient_name[sizeof(s_patient_name) - 1] = '\0';
    }
}