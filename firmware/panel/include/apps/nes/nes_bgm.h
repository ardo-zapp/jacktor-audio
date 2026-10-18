#ifndef NES_BGM_H
#define NES_BGM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void nes_bgm_init(void);
void nes_bgm_start(void);
void nes_bgm_pause(void);
void nes_bgm_resume(void);
void nes_bgm_stop(void);
bool nes_bgm_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif // NES_BGM_H
