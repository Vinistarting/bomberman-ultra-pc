/* Bomberman ULTRA -- registro do job SPURS "crTaskRunJobList" (render/scene).
 *
 * O binário do job fica embutido no EBOOT em 0x00199A80 (42240 bytes) e é
 * carregado pelo job manager do SPURS no LS 0 (código PIC). Foi lifado com
 * tools/spu_lifter.py --code-end 0x9320: de 0x9320 até o fim são tabelas
 * (floats e máscaras de shuffle), não código. O fingerprint FNV-1a-64 é o
 * mesmo que o runtime calcula sobre os 42240 bytes em spu_workload_dispatch_job. */
#include "spu_workload.h"
#include "spu_context.h"

extern void spu_begin_image(int image_id);
extern void bmujob_render_spu_func_00000000(spu_context*);
extern void bmujob_render_spu_recomp_register(void);

#define BMU_JOB_RENDER_IMAGE_ID 0x1B0E0001
#define BMU_JOB_RENDER_FP       0x0D7FBC258406E228ULL

__attribute__((constructor)) static void bmu_register_jobs(void)
{
    spu_begin_image(BMU_JOB_RENDER_IMAGE_ID);
    bmujob_render_spu_recomp_register();
    spu_workload_register_img(BMU_JOB_RENDER_FP, bmujob_render_spu_func_00000000,
                              BMU_JOB_RENDER_IMAGE_ID, "bmujob_render (crTaskRunJobList)");
    spu_begin_image(0);
}
