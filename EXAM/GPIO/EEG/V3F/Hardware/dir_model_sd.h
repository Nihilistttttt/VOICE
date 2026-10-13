#ifndef DIR_MODEL_SD_H
#define DIR_MODEL_SD_H

#include <stdint.h>

#define DIR_MODEL_ID_LEN       8u
#define DIR_MODEL_NAME_LEN     32u
#define DIR_MODEL_PAYLOAD_LEN  292u
#define DIR_MODEL_FILE_LEN     (DIR_MODEL_ID_LEN + DIR_MODEL_NAME_LEN + 4u + DIR_MODEL_PAYLOAD_LEN)
#define DIR_MODEL_MAX_LIST     5u
#define DIR_MODEL_LIST_ENTRY   (DIR_MODEL_ID_LEN + DIR_MODEL_NAME_LEN + 4u)

typedef struct {
    char    patient_id[DIR_MODEL_ID_LEN];
    char    patient_name[DIR_MODEL_NAME_LEN];
    float   balanced_accuracy;
    uint8_t model_data[DIR_MODEL_PAYLOAD_LEN];
} DirModelFile_t;

typedef struct {
    char  patient_id[DIR_MODEL_ID_LEN];
    char  patient_name[DIR_MODEL_NAME_LEN];
    float balanced_accuracy;
} DirModelEntry_t;

int  DirModelSD_Init(void);
int  DirModelSD_Save(const DirModelFile_t *model);
int  DirModelSD_Scan(DirModelEntry_t *list, uint8_t max_count, uint8_t *out_count);
int  DirModelSD_Load(const char *patient_id, DirModelFile_t *out_model);
void DirModelSD_SendList(void);

#endif