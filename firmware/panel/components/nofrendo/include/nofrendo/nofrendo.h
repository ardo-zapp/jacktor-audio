#ifndef _NOFRENDO_H_
#define _NOFRENDO_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
   system_unknown,
   system_autodetect,
   system_nes,
   NUM_SUPPORTED_SYSTEMS
} system_t;

int nofrendo_main(int argc, char *argv[]);

extern volatile int nofrendo_ticks;
extern int main_loop(const char *filename, system_t type);

extern bool vid_preload_rom(const char *path);
extern void main_insert(const char *filename, system_t type);
extern void main_eject(void);
extern void main_quit(void);

#ifdef __cplusplus
}
#endif

#endif